#include "backtest.hpp"
#include "stats.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

Backtest::Backtest(
    const DatabaseConfig& db_config,
    const std::string&    parquet_daily,
    const std::string&    parquet_sub,
    const std::string&    start_date,
    const std::string&    end_date,
    double                initial_equity)
    : db_config_     (db_config)
    , parquet_daily_ (parquet_daily)
    , parquet_sub_   (parquet_sub)
    , start_date_    (start_date)
    , end_date_      (end_date)
    , initial_equity_(initial_equity)
{}

std::vector<SymbolData> Backtest::load_symbols() {
    BarLoader loader(parquet_daily_, parquet_sub_);
    auto syms = loader.load_all_symbols(start_date_, end_date_);
    if (syms.empty())
        throw std::runtime_error("No bar data loaded");
    std::cout << "[backtest] " << syms.size() << " symbols | "
              << "daily=" << syms[0].bars_daily.size()
              << " sub=" << syms[0].bars_sub.size() << "\n";
    return syms;
}

std::vector<SymbolData> Backtest::fresh_copy(
    const std::vector<SymbolData>& source)
{
    std::vector<SymbolData> copy;
    copy.reserve(source.size());
    for (const auto& s : source) {
        SymbolData f;
        f.instrument    = s.instrument;
        f.bars_daily    = s.bars_daily;
        f.bars_sub      = s.bars_sub;
        f.open_position = std::nullopt;
        f.trades_today  = 0;
        f.pending_mr    = {};
        f.pending_trend = {};
        copy.push_back(std::move(f));
    }
    return copy;
}

BacktestRun Backtest::run(
    const BacktestConfig& config,
    const std::string&    label)
{
    std::cout << "\n[backtest] Run: " << label << "\n"
              << "  entry=" << config.entry_donchian_period
              << "  atr="   << config.atr_period
              << "  tp="    << config.tp_atr_mult << "x"
              << "  sl="    << config.sl_atr_mult << "x"
              << "  max_trades=" << config.max_trades_per_day << "\n";

    auto symbols = load_symbols();
    auto result  = run_simulation(symbols, config, initial_equity_);
    auto stats   = compute_stats(result);
    print_stats(stats);

    ResultWriter writer(db_config_);
    writer.create_tables();
    auto run_id = writer.save(result, stats, config, label);

    return BacktestRun{ run_id, config, std::move(result), std::move(stats) };
}

std::vector<BacktestRun> Backtest::sweep(
    StrategyMode               cfg_sweep_mode,
    const std::vector<int>&    entry_periods,
    const std::vector<double>& trigger_atr_mults,
    const std::vector<double>& tp_atr_mults,
    const std::vector<double>& sl_atr_mults,
    int                        atr_period,
    int                        max_trades_per_day,
    double                     lots)
{
    auto base = load_symbols();
    size_t total = entry_periods.size()
                 * trigger_atr_mults.size()
                 * tp_atr_mults.size()
                 * sl_atr_mults.size();

    std::cout << "\n[backtest] Sweep: " << total << " combinations"
              << " (results not persisted)\n";

    std::vector<BacktestRun> runs;
    runs.reserve(total);

    int n = 0;
    for (int ep : entry_periods) {
        for (double trig_m : trigger_atr_mults) {
            for (double tp_m : tp_atr_mults) {
                for (double sl_m : sl_atr_mults) {
                    ++n;
                    BacktestConfig cfg;
                    cfg.strategy_mode         = cfg_sweep_mode;
                    cfg.entry_donchian_period = ep;
                    cfg.atr_period            = atr_period;
                    cfg.trigger_atr_mult      = trig_m;
                    cfg.tp_atr_mult           = tp_m;
                    cfg.sl_atr_mult           = sl_m;
                    cfg.lots                  = lots;
                    cfg.max_trades_per_day    = max_trades_per_day;

                    std::string label =
                        "ep"   + std::to_string(ep) +
                        "_tr"  + std::to_string(static_cast<int>(trig_m * 100)) +
                        "_tp"  + std::to_string(static_cast<int>(tp_m  * 10)) +
                        "_sl"  + std::to_string(static_cast<int>(sl_m  * 10));

                    std::cout << "[backtest] " << n << "/" << total
                              << " " << label << "\n";

                    auto syms   = fresh_copy(base);
                    auto result = run_simulation(syms, cfg, initial_equity_);
                    auto stats  = compute_stats(result);

                    runs.push_back(BacktestRun{
                        label, cfg, std::move(result), std::move(stats) });
                }
            }
        }
    }

    std::sort(runs.begin(), runs.end(),
        [](const auto& a, const auto& b) {
            return a.stats.sharpe_ratio > b.stats.sharpe_ratio;
        });

    return runs;
}

void Backtest::print_leaderboard(
    const std::vector<BacktestRun>& runs,
    int                             top_n)
{
    int n = std::min(top_n, static_cast<int>(runs.size()));
    std::cout << "\n" << std::string(72, '=') << "\n";
    std::cout << "  SWEEP LEADERBOARD  (top " << n << " by Sharpe)\n";
    std::cout << std::string(72, '=') << "\n";
    std::cout << std::left
        << std::setw(6)  << "Rank"
        << std::setw(10) << "Sharpe"
        << std::setw(12) << "Net P&L"
        << std::setw(8)  << "WinRate"
        << std::setw(8)  << "PF"
        << std::setw(8)  << "Entry"
        << std::setw(8)  << "Trig"
        << std::setw(8)  << "TP"
        << std::setw(8)  << "SL"
        << "\n";
    std::cout << std::string(76, '-') << "\n";

    for (int i = 0; i < n; ++i) {
        const auto& r = runs[i];
        std::cout << std::left << std::fixed
            << std::setw(6)  << (i + 1)
            << std::setw(10) << std::setprecision(3) << r.stats.sharpe_ratio
            << std::setw(12) << std::setprecision(2) << r.stats.net_pnl
            << std::setw(8)  << std::setprecision(1) << r.stats.win_rate * 100
            << std::setw(8)  << std::setprecision(2) << r.stats.profit_factor
            << std::setw(8)  << r.config.entry_donchian_period
            << std::setw(8)  << std::setprecision(2) << r.config.trigger_atr_mult
            << std::setw(8)  << std::setprecision(1) << r.config.tp_atr_mult
            << std::setw(8)  << std::setprecision(1) << r.config.sl_atr_mult
            << "\n";
    }
    std::cout << std::string(72, '=') << "\n\n";
}
