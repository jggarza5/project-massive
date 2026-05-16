#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include "database.hpp"
#include <string>
#include <vector>

struct BacktestRun {
    std::string      run_id;
    BacktestConfig   config;
    SimulationResult result;
    StatsResult      stats;
};

class Backtest {
public:
    Backtest(
        const DatabaseConfig& db_config,
        const std::string&    parquet_daily,
        const std::string&    parquet_sub,
        const std::string&    start_date,
        const std::string&    end_date,
        double                initial_equity = 10000.0);

    BacktestRun run(
        const BacktestConfig& config,
        const std::string&    label = "");

    std::vector<BacktestRun> sweep(
        StrategyMode               strategy_mode,
        const std::vector<int>&    entry_periods,
        const std::vector<double>& trigger_atr_mults,
        const std::vector<double>& tp_atr_mults,
        const std::vector<double>& sl_atr_mults,
        int                        atr_period         = 14,
        int                        max_trades_per_day = 3,
        double                     lots               = 0.1);

    static void print_leaderboard(
        const std::vector<BacktestRun>& runs,
        int                             top_n = 10);

private:
    DatabaseConfig db_config_;
    std::string    parquet_daily_, parquet_sub_;
    std::string    start_date_, end_date_;
    double         initial_equity_;

    std::vector<SymbolData> load_symbols();
    static std::vector<SymbolData> fresh_copy(
        const std::vector<SymbolData>& source);
};
