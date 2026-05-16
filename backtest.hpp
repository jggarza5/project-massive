#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include "database.hpp"
#include "save_results.hpp"
#include <string>
#include <vector>

// ─────────────────────────────────────────────
//  BacktestRun
//  Everything produced by a single run
// ─────────────────────────────────────────────

struct BacktestRun {
    std::string      run_id;
    BacktestConfig   config;
    SimulationResult result;
    StatsResult      stats;
};

// ─────────────────────────────────────────────
//  Backtest
//  Orchestrates the full pipeline:
//    load bars → simulate → compute stats → save
//
//  Supports:
//    - single run with one config
//    - parameter sweep over lookback/divisor/
//      tp_pips/sl_pips combinations
// ─────────────────────────────────────────────

class Backtest {
public:
    Backtest(
        const DatabaseConfig& db_config,
        const std::string&    parquet_30,     // 30-min bars
        const std::string&    parquet_1,      // 1-min bars
        const std::string&    start_date,
        const std::string&    end_date,
        double                initial_equity = 10000.0);

    // Run a single backtest with the given config
    BacktestRun run(
        const BacktestConfig& config,
        const std::string&    label = "");

    // Sweep over all combinations of parameter vectors.
    // Returns all runs sorted by Sharpe descending.
    std::vector<BacktestRun> sweep(
        const std::vector<int>&    lookbacks,
        const std::vector<double>& divisors,
        const std::vector<double>& tp_pips,
        const std::vector<double>& sl_pips,
        double                     lots = 0.1);

    // Print leaderboard of sweep results
    static void print_leaderboard(
        const std::vector<BacktestRun>& runs,
        int                             top_n = 10);

private:
    DatabaseConfig db_config_;
    std::string    parquet_30_;
    std::string    parquet_1_;
    std::string    start_date_;
    std::string    end_date_;
    double         initial_equity_;

    std::vector<SymbolData> load_symbols();

    // Deep copy with open positions cleared
    static std::vector<SymbolData> fresh_copy(
        const std::vector<SymbolData>& source);
};