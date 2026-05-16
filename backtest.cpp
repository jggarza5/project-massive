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
    const std::string&    parquet_30,
    const std::string&    parquet_1,
    const std::string&    start_date,
    const std::string&    end_date,
    double                initial_equity)
    : db_config_      (db_config)
    , parquet_30_     (parquet_30)
    , parquet_1_      (parquet_1)
    , start_date_     (start_date)
    , end_date_       (end_date)
    , initial_equity_ (initial_equity)
{}

// ─────────────────────────────────────────────
//  load_symbols
// ─────────────────────────────────────────────

std::vector<SymbolData> Backtest::load_symbols() {
    BarLoader loader(parquet_30_, parquet_1_);
    auto symbols = loader.load_all_symbols(start_date_, end_date_);

    if (symbols.empty())
        throw std::runtime_error(
            "No bar data loaded — check Parquet paths and date range");

    std::cout << "[backtest] Loaded " << symbols.size()
              << " symbols\n"
              << "[backtest] 30-min bars: " << symbols[0].bars_30.size() << "\n"
              << "[backtest]  1-min bars: " << symbols[0].bars_1.size()  << "\n";

    return symbols;
}

// ─────────────────────────────────────────────
//  fresh_copy
//  Deep copy of SymbolData — bars shared,
//  open positions reset for each sweep run
// ─────────────────────────────────────────────

std::vector<SymbolData> Backtest::fresh_copy(
    const std::vector<SymbolData>& source)
{
    std::vector<SymbolData> copy;
    copy.reserve(source.size());

    for (const auto& s : source) {
        SymbolData fresh;
        fresh.instrument    = s.instrument;
        fresh.bars_30       = s.bars_30;    // shared read-only data
        fresh.bars_1        = s.bars_1;
        fresh.open_position = std::nullopt;
        copy.push_back(std::move(fresh));
    }

    return copy;
}

// ─────────────────────────────────────────────
//  run
// ─────────────────────────────────────────────

BacktestRun Backtest::run(
    const BacktestConfig& config,
    const std::string&    label)
{
    std::cout << "\n[backtest] Starting run: " << label << "\n"
              << "[backtest] lookback=" << config.lookback
              << " divisor="           << config.divisor
              << " tp="                << config.take_profit_pips
              << " sl="                << config.stop_loss_pips
              << " lots="              << config.lots << "\n";

    auto symbols = load_symbols();
    auto result  = run_simulation(symbols, config, initial_equity_);
    auto stats   = compute_stats(result);

    print_stats(stats);

    ResultWriter writer(db_config_);
    writer.create_tables();
    std::string run_id = writer.save(result, stats, config, label);

    return BacktestRun{ run_id, config, std::move(result), std::move(stats) };
}

// ─────────────────────────────────────────────
//  sweep
// ─────────────────────────────────────────────

std::vector<BacktestRun> Backtest::sweep(
    const std::vector<int>&    lookbacks,
    const std::vector<double>& divisors,
    const std::vector<double>& tp_pips_vec,
    const std::vector<double>& sl_pips_vec,
    double                     lots)
{
    // Load both resolutions once
    auto base_symbols = load_symbols();

    size_t total = lookbacks.size()   *
                   divisors.size()    *
                   tp_pips_vec.size() *
                   sl_pips_vec.size();

    std::cout << "\n[backtest] Sweep: " << total << " combinations\n";

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
                    config.lookback         = lb;
                    config.divisor          = div;
                    config.take_profit_pips = tp;
                    config.stop_loss_pips   = sl;
                    config.lots             = lots;

                    std::string label =
                        "lb"  + std::to_string(lb) +
                        "_d"  + std::to_string(static_cast<int>(div * 10)) +
                        "_tp" + std::to_string(static_cast<int>(tp)) +
                        "_sl" + std::to_string(static_cast<int>(sl));

                    std::cout << "[backtest] Run " << run_num
                              << "/" << total
                              << " — " << label << "\n";

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