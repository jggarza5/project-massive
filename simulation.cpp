#include "simulation.hpp"
#include <cmath>
#include <stdexcept>
#include <iostream>

// ─────────────────────────────────────────────
//  compute_entry_threshold
// ─────────────────────────────────────────────

double compute_entry_threshold(
    const Bar&  signal_bar,
    Direction   direction,
    double      trigger)
{
    if (direction == Direction::Long)
        // Long: ask must cross ask_open + trigger
        return signal_bar.ask_open + trigger;
    else
        // Short: bid must cross bid_open - trigger
        return signal_bar.bid_open - trigger;
}

// ─────────────────────────────────────────────
//  find_entry_bar
// ─────────────────────────────────────────────

int find_entry_bar(
    const std::vector<Bar>& bars_1,
    int                     start_idx,
    int                     end_idx,
    Direction               direction,
    double                  threshold)
{
    for (int i = start_idx; i < end_idx; ++i) {
        const Bar& b = bars_1[i];
        if (direction == Direction::Long) {
            if (b.ask_high >= threshold) return i;
        } else {
            if (b.bid_low  <= threshold) return i;
        }
    }
    return -1; // threshold not reached within this 30-min window
}

// ─────────────────────────────────────────────
//  find_exit_bar
// ─────────────────────────────────────────────

ExitResult find_exit_bar(
    const std::vector<Bar>& bars_1,
    int                     start_idx,
    Direction               direction,
    double                  take_profit,
    double                  stop_loss)
{
    int n = static_cast<int>(bars_1.size());

    for (int i = start_idx; i < n; ++i) {
        const Bar& b = bars_1[i];

        if (direction == Direction::Long) {
            // SL checked before TP — conservative
            if (b.bid_low  <= stop_loss)   return { i, ExitReason::StopLoss,   stop_loss   };
            if (b.bid_high >= take_profit) return { i, ExitReason::TakeProfit, take_profit };
        } else {
            if (b.ask_high >= stop_loss)   return { i, ExitReason::StopLoss,   stop_loss   };
            if (b.ask_low  <= take_profit) return { i, ExitReason::TakeProfit, take_profit };
        }
    }

    // No exit found — close at last bar's close price
    int last = n - 1;
    double close_px = (direction == Direction::Long)
        ? bars_1[last].bid_close
        : bars_1[last].ask_close;

    return { last, ExitReason::EndOfData, close_px };
}

// ─────────────────────────────────────────────
//  compute_pnl
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                           pos,
    const Instrument&                             instrument,
    double                                        exit_price,
    const std::unordered_map<std::string,double>& rates)
{
    double direction_mult = (pos.direction == Direction::Long) ? 1.0 : -1.0;
    double price_diff     = (exit_price - pos.entry_price) * direction_mult;
    double raw_pnl        = price_diff * instrument.lot_size * pos.lots;

    double pnl_usd;
    const std::string& profit_ccy = instrument.profit_ccy;

    if (profit_ccy == "USD") {
        pnl_usd = raw_pnl;
    } else {
        std::string rate_key = "USD" + profit_ccy;
        auto it = rates.find(rate_key);
        if (it == rates.end() || it->second == 0.0)
            throw std::runtime_error("Missing FX rate: " + rate_key);
        pnl_usd = raw_pnl / it->second;
    }

    return { raw_pnl, pnl_usd };
}

// ─────────────────────────────────────────────
//  find_1min_range
//  Returns [start_idx, end_idx) of 1-min bars
//  that fall within a given 30-min bar's window.
//
//  30-min bar_time is the window open (inclusive).
//  Window closes 30 minutes later (exclusive).
//
//  Uses binary search for efficiency.
// ─────────────────────────────────────────────

static std::pair<int,int> find_1min_range(
    const std::vector<Bar>& bars_1,
    long long               bar_30_open_epoch,  // Unix seconds
    int                     hint_start = 0)     // search from here
{
    long long window_end = bar_30_open_epoch + 30 * 60;

    int n = static_cast<int>(bars_1.size());
    int start = hint_start;

    // Advance start to first 1-min bar >= bar_30_open_epoch
    while (start < n && bars_1[start].timestamp < bar_30_open_epoch)
        ++start;

    // Advance end to first 1-min bar >= window_end
    int end = start;
    while (end < n && bars_1[end].timestamp < window_end)
        ++end;

    return { start, end };
}

// ─────────────────────────────────────────────
//  build_rates_map
// ─────────────────────────────────────────────

static std::unordered_map<std::string,double> build_rates_map(
    const std::vector<SymbolData>& symbols,
    int                            bar_30_idx)
{
    std::unordered_map<std::string,double> rates;
    for (const auto& s : symbols) {
        if (bar_30_idx < static_cast<int>(s.bars_30.size()))
            rates[s.instrument.symbol] = s.bars_30[bar_30_idx].mid_close();
    }
    return rates;
}

// ─────────────────────────────────────────────
//  run_simulation
// ─────────────────────────────────────────────

SimulationResult run_simulation(
    std::vector<SymbolData>& symbols,
    const BacktestConfig&    config,
    double                   initial_equity)
{
    if (symbols.empty())
        throw std::invalid_argument("No symbols provided");

    int num_bars_30 = static_cast<int>(symbols[0].bars_30.size());
    for (const auto& s : symbols) {
        if (static_cast<int>(s.bars_30.size()) != num_bars_30)
            throw std::invalid_argument(
                "30-min bar count mismatch: " + s.instrument.symbol);
    }

    SimulationResult result;
    result.initial_equity = initial_equity;
    double equity = initial_equity;

    // Per-symbol cursor into 1-min bars — avoids rescanning from 0 each bar
    std::vector<int> cursor_1(symbols.size(), 0);

    for (int i = 0; i < num_bars_30; ++i) {
        int open_count = 0;
        auto rates     = build_rates_map(symbols, i);

        for (size_t s_idx = 0; s_idx < symbols.size(); ++s_idx) {
            auto& sym          = symbols[s_idx];
            const Bar& bar_30  = sym.bars_30[i];
            long long  bar_ts  = bar_30.timestamp;

            // Find 1-min bars for this 30-min window
            auto [m1_start, m1_end] = find_1min_range(
                sym.bars_1, bar_ts, cursor_1[s_idx]);
            cursor_1[s_idx] = m1_start; // advance cursor

            // ── 1. Check exits on open position ───────────
            if (sym.open_position.has_value()) {
                ++open_count;
                auto& pos = *sym.open_position;

                // Scan 1-min bars in this window for TP/SL
                ExitResult ex = find_exit_bar(
                    sym.bars_1,
                    m1_start,
                    pos.direction,
                    pos.take_profit,
                    pos.stop_loss);

                // Only close if exit found within this window
                // (EndOfData exit handled at end of simulation)
                bool in_window = (ex.bar_idx >= m1_start &&
                                  ex.bar_idx <  m1_end);
                bool end_reached = (ex.reason == ExitReason::EndOfData);

                if (in_window || end_reached) {
                    // Recompute rates at exit bar if possible
                    auto exit_rates = rates;

                    auto [raw, usd] = compute_pnl(
                        pos, sym.instrument, ex.price, exit_rates);

                    Trade trade;
                    trade.symbol       = sym.instrument.symbol;
                    trade.direction    = pos.direction;
                    trade.lots         = pos.lots;
                    trade.entry_price  = pos.entry_price;
                    trade.exit_price   = ex.price;
                    trade.exit_reason  = ex.reason;
                    trade.entry_time   = pos.entry_time;
                    trade.exit_time    = (ex.bar_idx >= 0 && ex.bar_idx < static_cast<int>(sym.bars_1.size()))
                                         ? sym.bars_1[ex.bar_idx].timestamp
                                         : bar_ts;
                    trade.entry_bar    = pos.entry_bar_30;
                    trade.exit_bar     = i;
                    trade.raw_pnl      = raw;
                    trade.pnl_usd      = usd;

                    equity += usd;
                    result.trades.push_back(trade);
                    sym.open_position.reset();
                }
            }

            // ── 2. Check for new signal ────────────────────
            if (!sym.open_position.has_value() &&
                i >= config.lookback &&
                i + 1 < num_bars_30)
            {
                // Spread filter on current bar
                if (!spread_ok(bar_30, sym.instrument))
                    continue;

                auto sig = detect_signal(
                    sym.bars_30, i,
                    config.lookback,
                    config.divisor);

                if (!sig.has_value()) continue;

                // Compute trigger distance and threshold price
                double max_body = 0.0;
                for (int j = i - config.lookback; j < i; ++j)
                    max_body = std::max(max_body, sym.bars_30[j].bid_body());

                double trigger   = max_body + (max_body / config.divisor);
                double threshold = compute_entry_threshold(
                    bar_30, *sig, trigger);

                // ── Lookahead fix ──────────────────────────────
                // Signal fires on bar i CLOSE.
                // Scan 1-min bars in bar i+1's window — not bar i.
                // This ensures we only use information available
                // after bar i has fully closed.
                long long next_bar_ts = sym.bars_30[i + 1].timestamp;
                auto [next_start, next_end] = find_1min_range(
                    sym.bars_1, next_bar_ts, m1_end);

                int entry_idx = find_entry_bar(
                    sym.bars_1, next_start, next_end, *sig, threshold);

                if (entry_idx < 0) continue; // threshold not reached in i+1

                // Entry at CLOSE of the crossing 1-min bar.
                // Long fills at ask_close, short fills at bid_close.
                double entry_price = (*sig == Direction::Long)
                    ? sym.bars_1[entry_idx].ask_close
                    : sym.bars_1[entry_idx].bid_close;

                // Compute TP/SL from entry
                double pip     = sym.instrument.pip_size;
                double tp_dist = config.take_profit_pips * pip;
                double sl_dist = config.stop_loss_pips   * pip;

                double tp, sl;
                if (*sig == Direction::Long) {
                    tp = entry_price + tp_dist;
                    sl = entry_price - sl_dist;
                } else {
                    tp = entry_price - tp_dist;
                    sl = entry_price + sl_dist;
                }

                sym.open_position = OpenPosition{
                    sym.instrument.symbol,
                    *sig,
                    config.lots,
                    entry_price,
                    tp,
                    sl,
                    sym.bars_1[entry_idx].timestamp,
                    i,
                    entry_idx
                };
            }
        } // end symbol loop

        // ── 3. Snapshot equity ────────────────────────────
        result.equity_curve.push_back(EquityPoint{
            symbols[0].bars_30[i].timestamp,
            equity,
            open_count
        });
    }

    // ─────────────────────────────────────────
    //  Force-close any positions still open
    //  at end of data
    // ─────────────────────────────────────────
    auto rates = build_rates_map(symbols, num_bars_30 - 1);

    for (auto& sym : symbols) {
        if (!sym.open_position.has_value()) continue;

        auto& pos       = *sym.open_position;
        const Bar& last = sym.bars_1.back();

        double close_px = (pos.direction == Direction::Long)
            ? last.bid_close
            : last.ask_close;

        auto [raw, usd] = compute_pnl(
            pos, sym.instrument, close_px, rates);

        Trade trade;
        trade.symbol      = sym.instrument.symbol;
        trade.direction   = pos.direction;
        trade.lots        = pos.lots;
        trade.entry_price = pos.entry_price;
        trade.exit_price  = close_px;
        trade.exit_reason = ExitReason::EndOfData;
        trade.entry_time  = pos.entry_time;
        trade.exit_time   = last.timestamp;
        trade.entry_bar   = pos.entry_bar_30;
        trade.exit_bar    = num_bars_30 - 1;
        trade.raw_pnl     = raw;
        trade.pnl_usd     = usd;

        equity += usd;
        result.trades.push_back(trade);
        sym.open_position.reset();
    }

    result.final_equity = equity;
    return result;
}