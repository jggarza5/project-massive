#include "backtest.hpp"
#include "config.hpp"
#include <iostream>
#include <stdexcept>
#include <windows.h>

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        // Load config from file — defaults to config.toml in working directory.
        // Override with: backtest.exe run my_config.toml
        std::string config_path = "config.toml";
        std::string mode        = "run";

        if (argc > 1) mode        = argv[1];
        if (argc > 2) config_path = argv[2];

        auto cfg = load_config(config_path);

        Backtest bt(
            cfg.db,
            cfg.parquet_30,
            cfg.parquet_1min,
            cfg.start_date,
            cfg.end_date,
            cfg.initial_equity
        );

        if (mode == "sweep") {
            auto runs = bt.sweep(
                cfg.sweep.lookbacks,
                cfg.sweep.divisors,
                cfg.sweep.tp_pips,
                cfg.sweep.sl_pips,
                cfg.sweep.lots
            );
            Backtest::print_leaderboard(runs, 10);

        } else {
            auto run = bt.run(cfg.single, cfg.single_label);
            std::cout << "\nRun ID: " << run.run_id   << "\n"
                      << "Net P&L: $" << run.stats.net_pnl      << "\n"
                      << "Sharpe:  "  << run.stats.sharpe_ratio  << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}