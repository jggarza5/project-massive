#include "backtest.hpp"
#include <iostream>
#include <stdexcept>

// ─────────────────────────────────────────────
//  Database configuration
//  Edit these to match your PostgreSQL setup
// ─────────────────────────────────────────────

static DatabaseConfig make_db_config() {
    DatabaseConfig cfg;
    cfg.host     = "localhost";
    cfg.port     = "5432";
    cfg.dbname   = "massive";
    cfg.user     = "postgres";
    cfg.password = "";          // set if needed
    return cfg;
}

// ─────────────────────────────────────────────
//  Single run
//  Quick test with one parameter set
// ─────────────────────────────────────────────

static void single_run(Backtest& bt) {
    BacktestConfig config;
    config.lookback         = 10;
    config.divisor          = 4.0;
    config.take_profit_pips = 50.0;
    config.stop_loss_pips   = 25.0;
    config.lots             = 0.1;

    auto run = bt.run(config, "initial_test");

    std::cout << "\nRun ID: " << run.run_id << "\n";
    std::cout << "Net P&L: $" << run.stats.net_pnl << "\n";
    std::cout << "Sharpe:  "  << run.stats.sharpe_ratio << "\n";
}

// ─────────────────────────────────────────────
//  Parameter sweep
//  Explores combinations of lookback/divisor
//  and TP/SL ratios
// ─────────────────────────────────────────────

static void parameter_sweep(Backtest& bt) {
    std::vector<int>    lookbacks = { 5, 10, 20 };
    std::vector<double> divisors  = { 2.0, 4.0, 8.0 };
    std::vector<double> tp_pips   = { 30.0, 50.0, 80.0 };
    std::vector<double> sl_pips   = { 15.0, 25.0, 40.0 };

    auto runs = bt.sweep(lookbacks, divisors, tp_pips, sl_pips, 0.1);

    Backtest::print_leaderboard(runs, 10);
}

// ─────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        auto db_config = make_db_config();

        Backtest bt(
            db_config,
            "C:/code/python/forex-data/bars_2025.parquet",
            "C:/code/python/forex-data/bars_2025_1min.parquet",
            "2025-01-01",   // start date (inclusive)
            "2026-01-01",   // end date   (exclusive)
            10000.0         // initial equity (USD)
        );

        // Choose mode via command line argument:
        //   ./backtest run    — single run
        //   ./backtest sweep  — parameter sweep
        //   (default)         — single run

        std::string mode = (argc > 1) ? argv[1] : "run";

        if (mode == "sweep") {
            parameter_sweep(bt);
        } else {
            single_run(bt);
        }

    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}