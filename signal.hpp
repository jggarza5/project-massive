#pragma once

#include "types.hpp"
#include <vector>
#include <optional>

// ─────────────────────────────────────────────
//  detect_signal
//  Body breakout signal on 30-min bid bars.
//
//  Fires when current bar body exceeds:
//    trigger = max_body + max_body / divisor
//
//  Returns Long, Short, or nullopt.
// ─────────────────────────────────────────────

std::optional<Direction> detect_signal(
    const std::vector<Bar>& bars,
    int                     i,
    int                     lookback,
    double                  divisor);

// ─────────────────────────────────────────────
//  spread_ok
//  Returns true if spread is within acceptable
//  range for the given instrument.
// ─────────────────────────────────────────────

bool spread_ok(
    const Bar&        bar,
    const Instrument& instrument);