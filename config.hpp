#pragma once

#include "types.hpp"
#include "database.hpp"
#include "wfo.hpp"
#include <string>
#include <vector>

struct SweepConfig {
    StrategyMode        strategy_mode = StrategyMode::MeanReversion;
    std::vector<int>    entry_periods;
    std::vector<double> trigger_atr_mults;
    std::vector<double> tp_atr_mults;
    std::vector<double> sl_atr_mults;
    int                 atr_period         = 14;
    int                 max_trades_per_day = 3;
    double              lots               = 0.1;
};

struct AppConfig {
    DatabaseConfig db;

    std::string parquet_daily;
    std::string parquet_sub;
    std::string start_date;
    std::string end_date;
    int         sub_bar_minutes = 30;

    double initial_equity = 10000.0;

    BacktestConfig single;
    std::string    single_label;

    SweepConfig sweep;
    WFOConfig   wfo;
};

AppConfig load_config(const std::string& path = "config.toml");
