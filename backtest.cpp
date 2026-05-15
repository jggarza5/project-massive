#include "backtest.hpp"
#include "signal.hpp"
#include "stats.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────

Backtest::Backtest(
    const DatabaseConfig& db_config,
    const std::string&    start_date,
    const std::string&    end_date,
    double                initial_equity)
    : db_config_      (db_config)
    , start_date_     (start_date)
    , end_date_       (end_date)
    , initial_equity_ (initial_equity)
{}

// ─────────────────────────────────────────────
//  load_symbols
//  Loads bar data from DB once and caches it.
//  Subsequent calls return the cached data.
// ─────────────────────────────────────────────

std::vector<SymbolState> Backtest::load_symbols() {
    Database db(db_config_);
    auto symbols = db.load_all_symbols(start_date_, end_date_);

    if (symbols.empty())
        throw std::runtime_error("No bar data loaded — check date range and DB");

    std::cout << "[backtest] Loaded " << symbols.size()
              << " symbols, " << symbols[0].bars.size()
              << " bars each\n";

    return symbols;
}

// ─────────────────────────────────────────────
//  fresh_copy
//  Returns a deep copy of SymbolStates with
//  all open positions cleared so each run
//  starts from a clean slate without
//  reloading bar data from the database.
// ─────────────────────────────────────────────

std::vector<SymbolState> Backtest::fresh_copy(
    const std::vector<SymbolState>& source)
{
    std::vector<SymbolState> copy;
    copy.reserve(source.size());

    for (const auto& s : source) {
        SymbolState fresh;
        fresh.instrument    = s.instrument;
        fresh.bars          = s.bars;        // shared read-only data
        fresh.open_position = std::nullopt;  // always start flat
        copy.push_back(std::move(fresh));
    }

    return copy;
}

// ─────────────────────────────────────────────
//  run
//  Single backtest run:
//    load → simulate → stats → save → return
// ─────────────────────────────────────────────

BacktestRun Backtest::run(
    const BacktestConfig& config,
    const std::string&    label)
{
    std::cout << "\n[backtest] Starting run: " << label << "\n";
    std::cout << "[backtest] lookback=" << config.lookback
              << " divisor="           << config.divisor
              << " tp="                << config.take_profit_pips
              << " sl="                << config.stop_loss_pips
              << " lots="              << config.lots << "\n";

    // Load bars
    auto symbols = load_symbols();

    // Simulate
    auto result = run_simulation(symbols, config, initial_equity_);

    // Stats
    auto stats = compute_stats(result);

    // Print summary
    print_stats(stats);

    // Save to DB
    ResultWriter writer(db_config_);
    writer.create_tables();
    std::string run_id = writer.save(result, stats, config, label);

    return BacktestRun{ run_id, config, std::move(result), std::move(stats) };
}

// ─────────────────────────────────────────────
//  sweep
//  Runs all combinations of the provided
//  parameter vectors. Bar data is loaded once
//  and reused across all runs.
// ─────────────────────────────────────────────

std::vector<BacktestRun> Backtest::sweep(
    const std::vector<int>&    lookbacks,
    const std::vector<double>& divisors,
    const std::vector<double>& tp_pips_vec,
    const std::vector<double>& sl_pips_vec,
    double                     lots)
{
    // Load bars once
    auto base_symbols = load_symbols();

    // Count total combinations
    size_t total = lookbacks.size()  *
                   divisors.size()   *
                   tp_pips_vec.size()*
                   sl_pips_vec.size();

    std::cout << "\n[backtest] Sweep: " << total << " combinations\n";

    // Set up result writer once
    ResultWriter writer(db_config_);
    writer.create_tables();

    std::vector<BacktestRun> runs;
    runs.reserve(total);

    int run_num = 0;
    for (int lb : lookbacks) {
        for (double div : divisors) {
            for (double tp : tp_pips_vec) {
                for (double sl : sl_pips_vec) {
                    ++run_num;

                    BacktestConfig config;
                    config.lookback          = lb;
                    config.divisor           = div;
                    config.take_profit_pips  = tp;
                    config.stop_loss_pips    = sl;
                    config.lots              = lots;

                    // Label encodes the parameters
                    std::string label =
                        "lb"  + std::to_string(lb)  +
                        "_d"  + std::to_string(static_cast<int>(div * 10)) +
                        "_tp" + std::to_string(static_cast<int>(tp)) +
                        "_sl" + std::to_string(static_cast<int>(sl));

                    std::cout << "[backtest] Run " << run_num
                              << "/" << total
                              << " — " << label << "\n";

                    // Fresh copy of bars — no DB reload needed
                    auto symbols = fresh_copy(base_symbols);
                    auto result  = run_simulation(symbols, config, initial_equity_);
                    auto stats   = compute_stats(result);
                    auto run_id  = writer.save(result, stats, config, label);

                    runs.push_back(BacktestRun{
                        run_id, config,
                        std::move(result),
                        std::move(stats)
                    });
                }
            }
        }
    }

    // Sort by Sharpe descending
    std::sort(runs.begin(), runs.end(),
        [](const BacktestRun& a, const BacktestRun& b) {
            return a.stats.sharpe_ratio > b.stats.sharpe_ratio;
        });

    return runs;
}

// ─────────────────────────────────────────────
//  print_leaderboard
// ─────────────────────────────────────────────

void Backtest::print_leaderboard(
    const std::vector<BacktestRun>& runs,
    int                             top_n)
{
    int n = std::min(top_n, static_cast<int>(runs.size()));

    std::cout << "\n";
    std::cout << "══════════════════════════════════════════════════════════════\n";
    std::cout << "  SWEEP LEADERBOARD  (top " << n << " by Sharpe)\n";
    std::cout << "══════════════════════════════════════════════════════════════\n";
    std::cout << std::left
              << std::setw(6)  << "Rank"
              << std::setw(12) << "Sharpe"
              << std::setw(12) << "Net P&L"
              << std::setw(8)  << "WinRate"
              << std::setw(8)  << "PF"
              << std::setw(8)  << "LB"
              << std::setw(8)  << "Div"
              << std::setw(8)  << "TP"
              << std::setw(8)  << "SL"
              << "\n";
    std::cout << "──────────────────────────────────────────────────────────────\n";

    for (int i = 0; i < n; ++i) {
        const auto& r = runs[i];
        const auto& s = r.stats;
        const auto& c = r.config;

        std::cout << std::left  << std::fixed
                  << std::setw(6)  << (i + 1)
                  << std::setw(12) << std::setprecision(3) << s.sharpe_ratio
                  << std::setw(12) << std::setprecision(2) << s.net_pnl
                  << std::setw(8)  << std::setprecision(1) << s.win_rate * 100.0
                  << std::setw(8)  << std::setprecision(2) << s.profit_factor
                  << std::setw(8)  << c.lookback
                  << std::setw(8)  << std::setprecision(1) << c.divisor
                  << std::setw(8)  << std::setprecision(0) << c.take_profit_pips
                  << std::setw(8)  << std::setprecision(0) << c.stop_loss_pips
                  << "\n";
    }

    std::cout << "══════════════════════════════════════════════════════════════\n\n";
}