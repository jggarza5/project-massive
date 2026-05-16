#pragma once

#include "types.hpp"
#include <vector>
#include <optional>

// ─────────────────────────────────────────────
//  DonchianPoint
//  Channel values at a single bar.
//  Computed exclusive of bar i (no lookahead).
// ─────────────────────────────────────────────

struct DonchianPoint {
    double upper = 0.0;
    double lower = 0.0;
    bool   valid = false;
};

// ─────────────────────────────────────────────
//  compute_donchian
//  Window [i-upper_period, i-1] for upper,
//  [i-lower_period, i-1] for lower.
//  Returns vector same length as bars.
// ─────────────────────────────────────────────

std::vector<DonchianPoint> compute_donchian(
    const std::vector<Bar>& bars,
    int                     upper_period,
    int                     lower_period);

// ─────────────────────────────────────────────
//  donchian_signal  — RAW BREAKOUT DIRECTION
//  Returns the direction of the breakout.
//  Strategy mode in simulation decides whether
//  to trade WITH or AGAINST the breakout.
//
//  Long:  bid_close breaks ABOVE upper channel
//  Short: bid_close breaks BELOW lower channel
// ─────────────────────────────────────────────

std::optional<Direction> donchian_signal(
    const std::vector<Bar>&           bars,
    const std::vector<DonchianPoint>& channel,
    int                               i);

// ─────────────────────────────────────────────
//  compute_atr
//  Wilder's Average True Range on bar series.
//  Uses daily bars (bid_high, bid_low, bid_close).
//
//  TR[i] = max(high-low,
//              |high - prev_close|,
//              |low  - prev_close|)
//
//  ATR seeded as SMA(TR, period) at bar[period],
//  then Wilder's smoothing:
//    ATR[i] = (ATR[i-1] * (period-1) + TR[i]) / period
//
//  Returns vector same length as bars.
//  Values at i < period are 0.0 (invalid).
// ─────────────────────────────────────────────

std::vector<double> compute_atr(
    const std::vector<Bar>& bars,
    int                     period);
