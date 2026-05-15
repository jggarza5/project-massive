#pragma once

#include "types.hpp"
#include "signal.hpp"
#include <vector>
#include <optional>
#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────
//  OpenPosition
//  An in-flight trade not yet closed
// ─────────────────────────────────────────────

struct OpenPosition {
    TradeSetup setup;
    long long  entry_time;
};

// ─────────────────────────────────────────────
//  SymbolState
//  All data and runtime state for one instrument
// ─────────────────────────────────────────────

struct SymbolState {
    Instrument          instrument;
    std::vector<Bar>    bars;
    std::optional<OpenPosition> open_position; // nullopt = flat
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
//  check_exit
//  Tests whether a bar closes an open position.
//  Conservative: SL is checked before TP.
//  Longs exit on bid bars, shorts on ask bars.
// ─────────────────────────────────────────────

ExitReason check_exit(
    const Bar&        bar,
    const OpenPosition& pos);

// ─────────────────────────────────────────────
//  compute_pnl
//  Calculates raw P&L in profit currency and
//  converts to USD using rates at exit time.
//
//  rates: map of "USDJPY" -> mid price etc.
//  Used to convert non-USD profit currencies.
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                          pos,
    const Instrument&                            instrument,
    double                                       exit_price,
    const std::unordered_map<std::string,double>& rates);

// ─────────────────────────────────────────────
//  run_simulation
//  Main bar-by-bar loop across all symbols.
//  All symbols must have the same bar count
//  and aligned timestamps (30min grid).
// ─────────────────────────────────────────────

SimulationResult run_simulation(
    std::vector<SymbolState>& symbols,
    const BacktestConfig&     config,
    double                    initial_equity = 10000.0);