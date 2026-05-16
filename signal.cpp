#include "signal.hpp"
#include <cmath>

// ─────────────────────────────────────────────
//  detect_signal
// ─────────────────────────────────────────────

std::optional<Direction> detect_signal(
    const std::vector<Bar>& bars,
    int                     i,
    int                     lookback,
    double                  divisor)
{
    if (i < lookback) return std::nullopt;

    double max_body = 0.0;
    for (int j = i - lookback; j < i; ++j) {
        double body = bars[j].bid_body();
        if (body > max_body) max_body = body;
    }

    if (max_body == 0.0) return std::nullopt;

    double trigger = max_body + (max_body / divisor);
    double body    = bars[i].bid_body();

    if (body > trigger) {
        if (bars[i].bid_close > bars[i].bid_open) return Direction::Long;
        if (bars[i].bid_close < bars[i].bid_open) return Direction::Short;
    }

    return std::nullopt;
}

// ─────────────────────────────────────────────
//  spread_ok
// ─────────────────────────────────────────────

bool spread_ok(
    const Bar&        bar,
    const Instrument& instrument)
{
    double spread_pips = bar.spread() / instrument.pip_size;
    return spread_pips <= instrument.spread_pips;
}