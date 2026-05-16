#pragma once

#include "types.hpp"
#include "database.hpp"
#include <string>
#include <vector>

// ─────────────────────────────────────────────
//  SweepConfig
//  Parameter vectors for a sweep run
// ─────────────────────────────────────────────

struct SweepConfig {
    std::vector<int>    lookbacks;
    std::vector<double> divisors;
    std::vector<double> tp_pips;
    std::vector<double> sl_pips;
    double              lots = 0.1;
};

// ─────────────────────────────────────────────
//  AppConfig
//  Everything loaded from config.toml
// ─────────────────────────────────────────────

struct AppConfig {
    // Database
    DatabaseConfig db;

    // Data paths and date range
    std::string parquet_30;
    std::string parquet_1min;
    std::string start_date;
    std::string end_date;

    // Account
    double initial_equity = 10000.0;

    // Single run
    BacktestConfig single;
    std::string    single_label;

    // Sweep
    SweepConfig sweep;
};

// ─────────────────────────────────────────────
//  load_config
//  Reads config.toml and returns AppConfig.
//  Throws std::runtime_error if file not found
//  or required keys are missing.
// ─────────────────────────────────────────────

AppConfig load_config(const std::string& path = "config.toml");