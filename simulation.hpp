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
    std::string symbol;
    Direction   direction;
    double      lots;
    double      entry_price;    // exact threshold price where signal crossed
    double      take_profit;
    double      stop_loss;
    long long   entry_time;     // Unix epoch of entry 1-min bar
    int         entry_bar_30;   // 30-min bar index where signal fired
    int         entry_bar_1;    // 1-min bar index of actual entry
};

// ─────────────────────────────────────────────
//  SymbolData
//  All bar data for one instrument.
//  1-min bars are indexed by their bar_time
//  for fast lookup from 30-min bar timestamps.
// ─────────────────────────────────────────────

struct SymbolData {
    Instrument           instrument;
    std::vector<Bar>     bars_30;       // 30-min bars — signal generation
    std::vector<Bar>     bars_1;        // 1-min bars  — entry/exit simulation
    std::optional<OpenPosition> open_position;
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
//  compute_entry_threshold
//  Returns the exact price level that must be
//  crossed by a 1-min bar to trigger entry.
//
//  Long:  ask_open_of_signal_bar + trigger
//  Short: bid_open_of_signal_bar - trigger
//
//  trigger = max_body + max_body / divisor
// ─────────────────────────────────────────────

double compute_entry_threshold(
    const Bar&  signal_bar,
    Direction   direction,
    double      trigger);   // in price units (not pips)

// ─────────────────────────────────────────────
//  find_entry_bar
//  Scans 1-min bars within the signal bar's
//  30-min window for the first bar that crosses
//  the entry threshold.
//
//  Long:  first 1-min bar where ask_high >= threshold
//  Short: first 1-min bar where bid_low  <= threshold
//
//  Returns index into bars_1 if found, else -1.
//  Entry fills at exactly the threshold price.
// ─────────────────────────────────────────────

int find_entry_bar(
    const std::vector<Bar>& bars_1,
    int                     start_idx,  // first 1-min bar of signal 30-min period
    int                     end_idx,    // one past last 1-min bar of that period
    Direction               direction,
    double                  threshold);

// ─────────────────────────────────────────────
//  find_exit_bar
//  Scans 1-min bars from after entry for the
//  first bar that hits TP or SL.
//
//  Long exits on bid:
//    bid_low  <= SL → StopLoss
//    bid_high >= TP → TakeProfit
//  Short exits on ask:
//    ask_high >= SL → StopLoss
//    ask_low  <= TP → TakeProfit
//
//  SL checked before TP within same bar.
//  Returns index into bars_1 if found, else -1
//  (position remains open past end of data).
// ─────────────────────────────────────────────

struct ExitResult {
    int        bar_idx;     // -1 if not found
    ExitReason reason;
    double     price;       // TP or SL level, or bar close if EndOfData
};

ExitResult find_exit_bar(
    const std::vector<Bar>& bars_1,
    int                     start_idx,  // first 1-min bar after entry
    Direction               direction,
    double                  take_profit,
    double                  stop_loss);

// ─────────────────────────────────────────────
//  compute_pnl
//  P&L in profit currency and USD.
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                           pos,
    const Instrument&                             instrument,
    double                                        exit_price,
    const std::unordered_map<std::string,double>& rates);

// ─────────────────────────────────────────────
//  run_simulation
//  Main simulation loop.
//
//  For each 30-min bar:
//    1. Check if open position exits in any
//       1-min bar within this 30-min window
//    2. If flat, check for signal on 30-min bar
//    3. If signal, scan 1-min bars in same window
//       for entry threshold cross
//    4. Snapshot equity
//
//  All SymbolData must cover the same date range.
// ─────────────────────────────────────────────

SimulationResult run_simulation(
    std::vector<SymbolData>& symbols,
    const BacktestConfig&    config,
    double                   initial_equity = 10000.0);