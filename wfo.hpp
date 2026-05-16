#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include "database.hpp"
#include <string>
#include <vector>

struct WFOConfig {
    StrategyMode        strategy_mode = StrategyMode::MeanReversion;
    int is_days  = 252;
    int oos_days = 63;

    std::vector<int>    entry_periods;
    std::vector<double> trigger_atr_mults;
    std::vector<double> tp_atr_mults;
    std::vector<double> sl_atr_mults;
    int                 atr_period         = 14;
    int                 max_trades_per_day = 3;
    double              lots               = 0.1;
};

struct SymbolFoldStats {
    std::string symbol;
    int    trades        = 0;
    double win_rate      = 0.0;
    double net_pnl       = 0.0;
    double profit_factor = 0.0;
};

struct FoldResult {
    int fold_num;
    std::string is_start, is_end;
    std::string oos_start, oos_end;

    BacktestConfig best_config;

    int    is_trades        = 0;
    double is_win_rate      = 0.0;
    double is_net_pnl       = 0.0;
    double is_profit_factor = 0.0;

    int    oos_trades        = 0;
    double oos_win_rate      = 0.0;
    double oos_net_pnl       = 0.0;
    double oos_profit_factor = 0.0;

    std::vector<SymbolFoldStats> is_by_symbol;
    std::vector<SymbolFoldStats> oos_by_symbol;
};

struct WFOResult {
    std::vector<FoldResult> folds;
    int    total_oos_trades      = 0;
    double total_oos_net_pnl     = 0.0;
    double avg_oos_profit_factor = 0.0;
    double avg_oos_win_rate      = 0.0;
};

WFOResult run_wfo(
    const std::vector<SymbolData>& symbols,
    const WFOConfig&               config,
    const std::string&             start_date,
    const std::string&             end_date,
    double                         initial_equity = 10000.0);

void print_wfo_results(const WFOResult& result);
