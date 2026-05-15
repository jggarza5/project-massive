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
        const std::string&    start_date,
        const std::string&    end_date,
        double                initial_equity = 10000.0);

    // Run a single backtest with the given config
    BacktestRun run(
        const BacktestConfig& config,
        const std::string&    label = "");

    // Sweep over all combinations of the provided
    // parameter vectors and run each one.
    // Returns all runs sorted by Sharpe descending.
    std::vector<BacktestRun> sweep(
        const std::vector<int>&    lookbacks,
        const std::vector<double>& divisors,
        const std::vector<double>& tp_pips,
        const std::vector<double>& sl_pips,
        double                     lots = 0.1);

    // Print a leaderboard of sweep results to stdout
    static void print_leaderboard(
        const std::vector<BacktestRun>& runs,
        int                             top_n = 10);

private:
    DatabaseConfig db_config_;
    std::string    start_date_;
    std::string    end_date_;
    double         initial_equity_;

    // Cached bar data — loaded once, reused across sweep runs
    std::vector<SymbolState> load_symbols();
    bool                     bars_loaded_ = false;
    std::vector<Bar>         cached_bars_; // raw cache per symbol handled below

    // Deep copy of SymbolStates with open_position reset
    // so each run starts clean without reloading from DB
    static std::vector<SymbolState> fresh_copy(
        const std::vector<SymbolState>& source);
};