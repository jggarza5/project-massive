#include "indicators.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

// ─────────────────────────────────────────────
//  compute_donchian
// ─────────────────────────────────────────────

std::vector<DonchianPoint> compute_donchian(
    const std::vector<Bar>& bars,
    int                     upper_period,
    int                     lower_period)
{
    if (upper_period < 1 || lower_period < 1)
        throw std::invalid_argument("Donchian periods must be >= 1");

    int n        = static_cast<int>(bars.size());
    int min_bars = std::max(upper_period, lower_period);

    std::vector<DonchianPoint> ch(n);

    for (int i = 0; i < n; ++i) {
        if (i < min_bars) { ch[i].valid = false; continue; }

        double upper = bars[i - upper_period].bid_high;
        for (int j = i - upper_period + 1; j < i; ++j)
            upper = std::max(upper, bars[j].bid_high);

        double lower = bars[i - lower_period].bid_low;
        for (int j = i - lower_period + 1; j < i; ++j)
            lower = std::min(lower, bars[j].bid_low);

        ch[i] = { upper, lower, true };
    }

    return ch;
}

// ─────────────────────────────────────────────
//  donchian_signal  — RAW BREAKOUT DIRECTION
//
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
    int                               i)
{
    if (i < 0 || i >= static_cast<int>(bars.size())) return std::nullopt;
    if (!channel[i].valid)                           return std::nullopt;

    double close = bars[i].bid_close;

    // Raw breakout direction — NOT flipped
    if (close > channel[i].upper) return Direction::Long;
    if (close < channel[i].lower) return Direction::Short;

    return std::nullopt;
}

// ─────────────────────────────────────────────
//  compute_atr  — Wilder's ATR
// ─────────────────────────────────────────────

std::vector<double> compute_atr(
    const std::vector<Bar>& bars,
    int                     period)
{
    if (period < 1)
        throw std::invalid_argument("ATR period must be >= 1");

    int n = static_cast<int>(bars.size());
    std::vector<double> atr(n, 0.0);

    if (n < period + 1) return atr;  // not enough bars

    // ── True Range ────────────────────────────
    std::vector<double> tr(n, 0.0);
    for (int i = 1; i < n; ++i) {
        double hl   = bars[i].bid_high - bars[i].bid_low;
        double hpc  = std::abs(bars[i].bid_high - bars[i-1].bid_close);
        double lpc  = std::abs(bars[i].bid_low  - bars[i-1].bid_close);
        tr[i] = std::max({ hl, hpc, lpc });
    }

    // ── Seed: SMA of first `period` TR values ──
    // Use bars [1..period] (tr[0] is undefined)
    double sum = 0.0;
    for (int i = 1; i <= period; ++i)
        sum += tr[i];
    atr[period] = sum / period;

    // ── Wilder's smoothing ────────────────────
    for (int i = period + 1; i < n; ++i)
        atr[i] = (atr[i-1] * (period - 1) + tr[i]) / period;

    return atr;
}
