#include "backtest.hpp"
#include "config.hpp"
#include "wfo.hpp"
#include "database.hpp"
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    try {
        std::string mode        = (argc > 1) ? argv[1] : "run";
        std::string config_path = (argc > 2) ? argv[2] : "config.toml";

        auto cfg = load_config(config_path);

        if (mode == "wfo") {
            BarLoader loader(cfg.parquet_daily, cfg.parquet_sub);
            auto symbols = loader.load_all_symbols(cfg.start_date, cfg.end_date);

            auto result = run_wfo(
                symbols, cfg.wfo,
                cfg.start_date, cfg.end_date,
                cfg.initial_equity);

            print_wfo_results(result);

        } else if (mode == "sweep") {
            Backtest bt(
                cfg.db,
                cfg.parquet_daily, cfg.parquet_sub,
                cfg.start_date, cfg.end_date,
                cfg.initial_equity);

            auto runs = bt.sweep(
                cfg.sweep.strategy_mode,
                cfg.sweep.entry_periods,
                cfg.sweep.trigger_atr_mults,
                cfg.sweep.tp_atr_mults,
                cfg.sweep.sl_atr_mults,
                cfg.sweep.atr_period,
                cfg.sweep.max_trades_per_day,
                cfg.sweep.lots);

            Backtest::print_leaderboard(runs, 10);

        } else {
            Backtest bt(
                cfg.db,
                cfg.parquet_daily, cfg.parquet_sub,
                cfg.start_date, cfg.end_date,
                cfg.initial_equity);

            auto run = bt.run(cfg.single, cfg.single_label);
            std::cout << "\nRun ID:  " << run.run_id            << "\n"
                      << "Net P&L: $" << run.stats.net_pnl      << "\n"
                      << "Sharpe:  "  << run.stats.sharpe_ratio << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
