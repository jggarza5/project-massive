#pragma once

#include "types.hpp"
#include <vector>
#include <optional>

// ─────────────────────────────────────────────
//  Signal detection
//  Body breakout: fires when current bar body
//  exceeds the recent max body by a margin
//  defined by divisor.
//
//  trigger = max_body * (1 + 1/divisor)
//  e.g. divisor=4 → body must be 25% larger
//  than the recent maximum to trigger.
//
//  Signal is read on bid bars only.
//  Entry uses ask (long) or bid (short) prices.
// ─────────────────────────────────────────────

std::optional<Direction> detect_signal(
    const std::vector<Bar>& bars,
    int                     i,
    int                     lookback,
    double                  divisor);

// Returns true if spread is within acceptable range
bool spread_ok(
    const Bar&        bar,
    const Instrument& instrument);

// Compute entry, TP, and SL levels from a signal
TradeSetup make_trade_setup(
    const Instrument&        instrument,
    Direction                direction,
    const Bar&               entry_bar,
    int                      signal_bar_idx,
    int                      entry_bar_idx,
    double                   tp_pips,
    double                   sl_pips,
    double                   lots);