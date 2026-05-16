#pragma once

#include "types.hpp"
#include "indicators.hpp"
#include <vector>
#include <optional>
#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────
//  OpenPosition
// ─────────────────────────────────────────────

struct OpenPosition {
    std::string  symbol;
    Direction    direction;
    StrategyMode mode;
    double       lots;
    double       entry_price;
    double       take_profit;
    double       stop_loss;
    long long    entry_time;
    int          entry_day_idx;
    int          entry_sub_idx;
};

// ─────────────────────────────────────────────
//  PendingEntry
//  One armed trigger waiting to fire on sub-bars
// ─────────────────────────────────────────────

struct PendingEntry {
    bool         active    = false;
    Direction    direction = Direction::Long;
    StrategyMode mode      = StrategyMode::MeanReversion;
    double       tp        = 0.0;
    double       sl        = 0.0;
    double       threshold = 0.0;
};

// ─────────────────────────────────────────────
//  SymbolData
//  In "Both" mode, pending_mr and pending_trend
//  can both be armed simultaneously. Whichever
//  sub-bar hits first executes; the other cancels.
// ─────────────────────────────────────────────

struct SymbolData {
    Instrument           instrument;
    std::vector<Bar>     bars_daily;
    std::vector<Bar>     bars_sub;
    std::optional<OpenPosition> open_position;

    int          trades_today          = 0;
    bool         signal_consumed_today = false;  // prevents re-arming
                                                 // the same daily signal
                                                 // after a trade closes
    PendingEntry pending_mr;     // mean reversion pending
    PendingEntry pending_trend;  // trend continuation pending
};

// ─────────────────────────────────────────────
//  SimulationResult
// ─────────────────────────────────────────────

struct SimulationResult {
    std::vector<Trade>       trades;
    std::vector<EquityPoint> equity_curve;
    double                   initial_equity;
    double                   final_equity;
};

// ─────────────────────────────────────────────
//  compute_pnl
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                           pos,
    const Instrument&                             instrument,
    double                                        exit_price,
    const std::unordered_map<std::string,double>& rates);

// ─────────────────────────────────────────────
//  run_simulation
// ─────────────────────────────────────────────

SimulationResult run_simulation(
    std::vector<SymbolData>& symbols,
    const BacktestConfig&    config,
    double                   initial_equity = 10000.0);