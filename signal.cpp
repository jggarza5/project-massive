#include "signal.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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

    // Find max body over [i-lookback, i-1]
    double max_body = 0.0;
    for (int j = i - lookback; j < i; ++j) {
        double body = bars[j].bid_body();
        if (body > max_body) max_body = body;
    }

    // No meaningful history — skip
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
    return spread_pips <= instrument.max_spread_pips;
}

// ─────────────────────────────────────────────
//  make_trade_setup
//  Entry price uses ask for longs, bid for shorts.
//  TP/SL distances are measured in pips from entry.
// ─────────────────────────────────────────────

TradeSetup make_trade_setup(
    const Instrument& instrument,
    Direction         direction,
    const Bar&        entry_bar,
    int               signal_bar_idx,
    int               entry_bar_idx,
    double            tp_pips,
    double            sl_pips,
    double            lots)
{
    if (tp_pips <= 0.0 || sl_pips <= 0.0)
        throw std::invalid_argument("tp_pips and sl_pips must be positive");

    double pip       = instrument.pip_size;
    double tp_dist   = tp_pips * pip;
    double sl_dist   = sl_pips * pip;

    double entry = (direction == Direction::Long)
        ? entry_bar.ask_open
        : entry_bar.bid_open;

    double tp, sl;
    if (direction == Direction::Long) {
        tp = entry + tp_dist;
        sl = entry - sl_dist;
    } else {
        tp = entry - tp_dist;
        sl = entry + sl_dist;
    }

    return TradeSetup{
        instrument.symbol,
        direction,
        lots,
        entry,
        tp,
        sl,
        signal_bar_idx,
        entry_bar_idx
    };
}