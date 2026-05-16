#include "simulation.hpp"
#include "indicators.hpp"
#include <cmath>
#include <stdexcept>
#include <limits>
#include <algorithm>

// ─────────────────────────────────────────────
//  compute_pnl
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                           pos,
    const Instrument&                             instrument,
    double                                        exit_price,
    const std::unordered_map<std::string,double>& rates)
{
    double mult    = (pos.direction == Direction::Long) ? 1.0 : -1.0;
    double raw_pnl = (exit_price - pos.entry_price)
                   * mult * instrument.lot_size * pos.lots;

    double pnl_usd;
    if (instrument.profit_ccy == "USD") {
        pnl_usd = raw_pnl;
    } else {
        auto it = rates.find("USD" + instrument.profit_ccy);
        if (it == rates.end() || it->second == 0.0)
            throw std::runtime_error(
                "Missing FX rate: USD" + instrument.profit_ccy);
        pnl_usd = raw_pnl / it->second;
    }
    return { raw_pnl, pnl_usd };
}

// ─────────────────────────────────────────────
//  build_rates_map
// ─────────────────────────────────────────────

static std::unordered_map<std::string,double> build_rates_map(
    const std::vector<SymbolData>& symbols,
    int                            sub_idx)
{
    std::unordered_map<std::string,double> rates;
    for (const auto& s : symbols)
        if (sub_idx < static_cast<int>(s.bars_sub.size()))
            rates[s.instrument.symbol] = s.bars_sub[sub_idx].mid_close();
    return rates;
}

// ─────────────────────────────────────────────
//  find_daily_idx_for_sub
// ─────────────────────────────────────────────

static int find_daily_idx_for_sub(
    const std::vector<Bar>& bars_daily,
    long long               sub_ts,
    int                     hint)
{
    int n = static_cast<int>(bars_daily.size());
    int i = std::max(0, hint);
    while (i + 1 < n && bars_daily[i + 1].timestamp <= sub_ts)
        ++i;
    return i;
}

// ─────────────────────────────────────────────
//  close_position
// ─────────────────────────────────────────────

static Trade close_position(
    SymbolData&                                   sym,
    double                                        exit_price,
    ExitReason                                    reason,
    int                                           exit_sub_idx,
    const std::unordered_map<std::string,double>& rates)
{
    auto& pos = *sym.open_position;
    auto [raw, usd] = compute_pnl(pos, sym.instrument, exit_price, rates);

    Trade t;
    t.symbol        = sym.instrument.symbol;
    t.direction     = pos.direction;
    t.mode          = pos.mode;
    t.lots          = pos.lots;
    t.entry_price   = pos.entry_price;
    t.exit_price    = exit_price;
    t.exit_reason   = reason;
    t.entry_time    = pos.entry_time;
    t.exit_time     = sym.bars_sub[exit_sub_idx].timestamp;
    t.entry_day_idx = pos.entry_day_idx;
    t.entry_sub_idx = pos.entry_sub_idx;
    t.exit_sub_idx  = exit_sub_idx;
    t.raw_pnl       = raw;
    t.pnl_usd       = usd;

    sym.open_position.reset();
    return t;
}

// ─────────────────────────────────────────────
//  compute_tp_sl
// ─────────────────────────────────────────────

static std::pair<double,double> compute_tp_sl(
    Direction direction,
    double    entry,
    double    atr,
    double    tp_mult,
    double    sl_mult)
{
    if (direction == Direction::Long)
        return { entry + tp_mult * atr, entry - sl_mult * atr };
    else
        return { entry - tp_mult * atr, entry + sl_mult * atr };
}

// ─────────────────────────────────────────────
//  arm_pending
//  Computes threshold and TP/SL for a pending
//  entry and sets it active.
// ─────────────────────────────────────────────

static void arm_pending(
    PendingEntry& p,
    StrategyMode  mode,
    Direction     breakout_dir,   // raw breakout direction
    double        channel_level,  // upper or lower channel boundary
    double        atr,
    double        trigger_mult,
    double        tp_mult,
    double        sl_mult)
{
    Direction trade_dir;
    double    threshold;

    if (mode == StrategyMode::MeanReversion) {
        // Fade the breakout
        // Long breakout → SHORT trade → trigger BELOW the high
        // Short breakout → LONG trade → trigger ABOVE the low
        trade_dir = (breakout_dir == Direction::Long)
            ? Direction::Short : Direction::Long;
        threshold = (breakout_dir == Direction::Long)
            ? channel_level - trigger_mult * atr   // below high
            : channel_level + trigger_mult * atr;  // above low
    } else {
        // Follow the breakout (TrendContinuation)
        // Long breakout → LONG trade → trigger ABOVE the high
        // Short breakout → SHORT trade → trigger BELOW the low
        trade_dir = breakout_dir;
        threshold = (breakout_dir == Direction::Long)
            ? channel_level + trigger_mult * atr   // above high
            : channel_level - trigger_mult * atr;  // below low
    }

    auto [tp, sl] = compute_tp_sl(trade_dir, threshold, atr, tp_mult, sl_mult);

    p.active    = true;
    p.direction = trade_dir;
    p.mode      = mode;
    p.tp        = tp;
    p.sl        = sl;
    p.threshold = threshold;
}

// ─────────────────────────────────────────────
//  try_execute
//  Checks if bar crosses pending threshold and
//  executes if so. Returns true if executed.
// ─────────────────────────────────────────────

static bool try_execute(
    PendingEntry&         p,
    const Bar&            bar,
    const Instrument&     instr,
    const BacktestConfig& cfg,
    long long             ts,
    int                   today_idx,
    int                   b,
    SymbolData&           sym)
{
    if (!p.active) return false;

    // Long: price rose to/through threshold (bar high >= threshold)
    // Short: price fell to/through threshold (bar low <= threshold)
    bool triggered = (p.direction == Direction::Long)
        ? (bar.bid_high >= p.threshold)
        : (bar.bid_low  <= p.threshold);

    if (!triggered) return false;

    // Fill at threshold ± spread
    double spread   = instr.spread_pips * instr.pip_size;
    double entry_px = (p.direction == Direction::Long)
        ? p.threshold + spread
        : p.threshold - spread;

    // Shift TP/SL by spread offset
    double offset = entry_px - p.threshold;

    sym.open_position = OpenPosition{
        instr.symbol,
        p.direction,
        p.mode,
        cfg.lots,
        entry_px,
        p.tp + offset,
        p.sl + offset,
        ts,
        today_idx,
        b
    };

    p.active = false;
    return true;
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

    int num_sub = std::numeric_limits<int>::max();
    for (const auto& s : symbols)
        num_sub = std::min(num_sub, static_cast<int>(s.bars_sub.size()));
    if (num_sub == 0)
        throw std::invalid_argument("No sub-bar data");

    int n_sym = static_cast<int>(symbols.size());

    // Precompute Donchian channels on daily bars
    std::vector<std::vector<DonchianPoint>> entry_ch(n_sym);
    for (int si = 0; si < n_sym; ++si)
        entry_ch[si] = compute_donchian(
            symbols[si].bars_daily,
            config.entry_donchian_period,
            config.entry_donchian_period);

    // Precompute ATR on daily bars
    std::vector<std::vector<double>> atr_series(n_sym);
    for (int si = 0; si < n_sym; ++si)
        atr_series[si] = compute_atr(
            symbols[si].bars_daily,
            config.atr_period);

    // Reset runtime state
    for (auto& s : symbols) {
        s.open_position = std::nullopt;
        s.trades_today  = 0;
        s.pending_mr    = {};
        s.pending_trend = {};
    }

    SimulationResult result;
    result.initial_equity = initial_equity;
    double equity = initial_equity;

    std::vector<int> day_cursor(n_sym, 0);
    std::vector<int> last_day_idx(n_sym, -1);

    for (int b = 0; b < num_sub; ++b) {
        int open_count = 0;
        auto rates = build_rates_map(symbols, b);

        for (int si = 0; si < n_sym; ++si) {
            auto&      sym  = symbols[si];
            const Bar& bar  = sym.bars_sub[b];
            long long  ts   = bar.timestamp;

            // ── Advance daily cursor ───────────────────
            day_cursor[si] = find_daily_idx_for_sub(
                sym.bars_daily, ts, day_cursor[si]);
            int today_idx = day_cursor[si];

            // Reset on new day
            if (today_idx != last_day_idx[si]) {
                sym.trades_today  = 0;
                sym.pending_mr    = {};
                sym.pending_trend = {};
                last_day_idx[si]  = today_idx;
            }

            // ── Execute pending entries ────────────────
            // Both can be armed; first to trigger fires.
            // After one fires, cancel the other.
            if (!sym.open_position.has_value()
                && sym.trades_today < config.max_trades_per_day)
            {
                bool fired = false;

                // Check MR pending first
                if (try_execute(sym.pending_mr, bar, sym.instrument,
                                config, ts, today_idx, b, sym))
                {
                    ++sym.trades_today;
                    sym.pending_trend = {};  // cancel other
                    fired = true;
                }

                // Check Trend pending if MR didn't fire
                if (!fired && try_execute(sym.pending_trend, bar,
                                sym.instrument, config, ts, today_idx, b, sym))
                {
                    ++sym.trades_today;
                    sym.pending_mr = {};     // cancel other
                }
            }

            // ── Check exits on open position ──────────
            if (sym.open_position.has_value()) {
                ++open_count;
                auto& pos = *sym.open_position;
                double tp = pos.take_profit;
                double sl = pos.stop_loss;

                ExitReason reason;
                double     exit_px = 0.0;
                bool       do_exit = false;

                if (pos.direction == Direction::Long) {
                    if (bar.bid_low  <= sl) {
                        reason = ExitReason::StopLoss;
                        exit_px = sl; do_exit = true;
                    } else if (bar.bid_high >= tp) {
                        reason = ExitReason::TakeProfit;
                        exit_px = tp; do_exit = true;
                    }
                } else {
                    if (bar.bid_high >= sl) {
                        reason = ExitReason::StopLoss;
                        exit_px = sl; do_exit = true;
                    } else if (bar.bid_low <= tp) {
                        reason = ExitReason::TakeProfit;
                        exit_px = tp; do_exit = true;
                    }
                }

                if (do_exit) {
                    auto trade = close_position(sym, exit_px, reason, b, rates);
                    equity += trade.pnl_usd;
                    result.trades.push_back(std::move(trade));
                }
            }

            // ── Arm new pending entries on signal ─────
            if (!sym.open_position.has_value()
                && sym.trades_today < config.max_trades_per_day
                && b + 1 < num_sub)
            {
                int prev_day = today_idx - 1;
                if (prev_day >= 0
                    && entry_ch[si][prev_day].valid
                    && atr_series[si][prev_day] > 0.0)
                {
                    auto sig = donchian_signal(
                        sym.bars_daily,
                        entry_ch[si],
                        prev_day);

                    if (sig.has_value()) {
                        double atr = atr_series[si][prev_day];

                        // Channel level that was broken
                        double level = (*sig == Direction::Long)
                            ? entry_ch[si][prev_day].upper
                            : entry_ch[si][prev_day].lower;

                        bool do_mr    = (config.strategy_mode == StrategyMode::MeanReversion
                                      || config.strategy_mode == StrategyMode::Both);
                        bool do_trend = (config.strategy_mode == StrategyMode::TrendContinuation
                                      || config.strategy_mode == StrategyMode::Both);

                        if (do_mr && !sym.pending_mr.active)
                            arm_pending(sym.pending_mr,
                                StrategyMode::MeanReversion,
                                *sig, level, atr,
                                config.trigger_atr_mult,
                                config.tp_atr_mult,
                                config.sl_atr_mult);

                        if (do_trend && !sym.pending_trend.active)
                            arm_pending(sym.pending_trend,
                                StrategyMode::TrendContinuation,
                                *sig, level, atr,
                                config.trigger_atr_mult,
                                config.tp_atr_mult,
                                config.sl_atr_mult);
                    }
                }
            }
        } // end symbol loop

        result.equity_curve.push_back(EquityPoint{
            symbols[0].bars_sub[b].timestamp,
            equity,
            open_count
        });
    }

    // Force-close at end of data
    auto rates = build_rates_map(symbols, num_sub - 1);
    for (auto& sym : symbols) {
        if (!sym.open_position.has_value()) continue;
        const Bar& last = sym.bars_sub.back();
        double close_px = (sym.open_position->direction == Direction::Long)
            ? last.bid_close : last.ask_close;
        auto trade = close_position(
            sym, close_px, ExitReason::EndOfData, num_sub - 1, rates);
        equity += trade.pnl_usd;
        result.trades.push_back(std::move(trade));
    }

    result.final_equity = equity;
    return result;
}
