#include "simulation.hpp"
#include <stdexcept>
#include <cmath>

// ─────────────────────────────────────────────
//  check_exit
// ─────────────────────────────────────────────

ExitReason check_exit(
    const Bar&          bar,
    const OpenPosition& pos)
{
    double tp = pos.setup.take_profit;
    double sl = pos.setup.stop_loss;

    if (pos.setup.direction == Direction::Long) {
        // Long exits monitored on bid
        if (bar.bid_low  <= sl) return ExitReason::StopLoss;
        if (bar.bid_high >= tp) return ExitReason::TakeProfit;
    } else {
        // Short exits monitored on ask
        if (bar.ask_high >= sl) return ExitReason::StopLoss;
        if (bar.ask_low  <= tp) return ExitReason::TakeProfit;
    }

    return ExitReason::EndOfData;
}

// ─────────────────────────────────────────────
//  exit_price_for
//  Returns the actual fill price on exit.
//  TP/SL levels are used as the fill price
//  since we can't see intrabar tick movement.
// ─────────────────────────────────────────────

static double exit_price_for(
    ExitReason          reason,
    const OpenPosition& pos,
    const Bar&          bar)
{
    if (reason == ExitReason::TakeProfit) return pos.setup.take_profit;
    if (reason == ExitReason::StopLoss)   return pos.setup.stop_loss;

    // EndOfData — exit at bar close
    return (pos.setup.direction == Direction::Long)
        ? bar.bid_close
        : bar.ask_close;
}

// ─────────────────────────────────────────────
//  compute_pnl
//
//  raw_pnl is in quote currency (e.g. USD for
//  EURUSD, JPY for USDJPY and crosses).
//
//  Conversion to USD:
//    quote == USD  → no conversion
//    quote == JPY  → divide by USDJPY rate
//    quote == CHF  → divide by USDCHF rate
//    quote == CAD  → divide by USDCAD rate
// ─────────────────────────────────────────────

std::pair<double, double> compute_pnl(
    const OpenPosition&                           pos,
    const Instrument&                             instrument,
    double                                        exit_price,
    const std::unordered_map<std::string,double>& rates)
{
    double direction_mult = (pos.setup.direction == Direction::Long) ? 1.0 : -1.0;
    double price_diff     = (exit_price - pos.setup.entry) * direction_mult;
    double raw_pnl        = price_diff * instrument.lot_size * pos.setup.lots;

    double pnl_usd;
    const std::string& profit_ccy = instrument.profit_ccy;

    if (profit_ccy == "USD") {
        pnl_usd = raw_pnl;
    } else {
        // Look up USD/profit_ccy rate to convert
        std::string rate_key = "USD" + profit_ccy;
        auto it = rates.find(rate_key);
        if (it == rates.end() || it->second == 0.0)
            throw std::runtime_error("Missing FX rate for conversion: " + rate_key);
        pnl_usd = raw_pnl / it->second;
    }

    return { raw_pnl, pnl_usd };
}

// ─────────────────────────────────────────────
//  build_rates_map
//  Snapshot of mid prices at bar i for all
//  symbols — used for P&L currency conversion.
// ─────────────────────────────────────────────

static std::unordered_map<std::string, double> build_rates_map(
    const std::vector<SymbolState>& symbols,
    int                             i)
{
    std::unordered_map<std::string, double> rates;
    for (const auto& s : symbols) {
        if (i < static_cast<int>(s.bars.size()))
            rates[s.instrument.symbol] = s.bars[i].mid_close();
    }
    return rates;
}

// ─────────────────────────────────────────────
//  run_simulation
// ─────────────────────────────────────────────

SimulationResult run_simulation(
    std::vector<SymbolState>& symbols,
    const BacktestConfig&     config,
    double                    initial_equity)
{
    if (symbols.empty())
        throw std::invalid_argument("No symbols provided");

    // All symbols must have the same bar count
    int num_bars = static_cast<int>(symbols[0].bars.size());
    for (const auto& s : symbols) {
        if (static_cast<int>(s.bars.size()) != num_bars)
            throw std::invalid_argument(
                "Bar count mismatch for symbol: " + s.instrument.symbol);
    }

    SimulationResult result;
    result.initial_equity = initial_equity;
    double equity         = initial_equity;

    for (int i = 0; i < num_bars; ++i) {
        int open_count = 0;

        // Build rates snapshot once per bar for P&L conversion
        auto rates = build_rates_map(symbols, i);

        for (auto& sym : symbols) {
            const Bar& bar = sym.bars[i];

            // ── 1. Check exits on open positions ──────────────
            if (sym.open_position.has_value()) {
                ++open_count;
                ExitReason reason = check_exit(bar, *sym.open_position);

                if (reason != ExitReason::EndOfData) {
                    auto& pos        = *sym.open_position;
                    double exit_px   = exit_price_for(reason, pos, bar);
                    auto [raw, usd]  = compute_pnl(pos, sym.instrument, exit_px, rates);

                    Trade trade;
                    trade.symbol       = sym.instrument.symbol;
                    trade.direction    = pos.setup.direction;
                    trade.lots         = pos.setup.lots;
                    trade.entry_price  = pos.setup.entry;
                    trade.exit_price   = exit_px;
                    trade.exit_reason  = reason;
                    trade.entry_time   = pos.entry_time;
                    trade.exit_time    = bar.timestamp;
                    trade.entry_bar    = pos.setup.entry_bar;
                    trade.exit_bar     = i;
                    trade.raw_pnl      = raw;
                    trade.pnl_usd      = usd;

                    equity += usd;
                    result.trades.push_back(trade);
                    sym.open_position.reset();
                }
            }

            // ── 2. Check for new signals ───────────────────────
            // Only enter if flat on this symbol and not last bar
            if (!sym.open_position.has_value() && i + 1 < num_bars) {
                auto sig = detect_signal(
                    sym.bars, i,
                    config.lookback,
                    config.divisor);

                if (sig.has_value()) {
                    const Bar& next_bar = sym.bars[i + 1];

                    // Spread filter on entry bar
                    if (spread_ok(next_bar, sym.instrument)) {
                        auto setup = make_trade_setup(
                            sym.instrument,
                            *sig,
                            next_bar,
                            i,
                            i + 1,
                            config.take_profit_pips,
                            config.stop_loss_pips,
                            config.lots);

                        sym.open_position = OpenPosition{
                            setup,
                            next_bar.timestamp
                        };
                    }
                }
            }
        } // end symbol loop

        // ── 3. Snapshot equity ────────────────────────────────
        result.equity_curve.push_back(EquityPoint{
            symbols[0].bars[i].timestamp,
            equity,
            open_count
        });
    }

    // Close any positions still open at end of data
    auto rates = build_rates_map(symbols, num_bars - 1);
    for (auto& sym : symbols) {
        if (sym.open_position.has_value()) {
            auto& pos       = *sym.open_position;
            const Bar& last = sym.bars[num_bars - 1];
            double exit_px  = exit_price_for(ExitReason::EndOfData, pos, last);
            auto [raw, usd] = compute_pnl(pos, sym.instrument, exit_px, rates);

            Trade trade;
            trade.symbol      = sym.instrument.symbol;
            trade.direction   = pos.setup.direction;
            trade.lots        = pos.setup.lots;
            trade.entry_price = pos.setup.entry;
            trade.exit_price  = exit_px;
            trade.exit_reason = ExitReason::EndOfData;
            trade.entry_time  = pos.entry_time;
            trade.exit_time   = last.timestamp;
            trade.entry_bar   = pos.setup.entry_bar;
            trade.exit_bar    = num_bars - 1;
            trade.raw_pnl     = raw;
            trade.pnl_usd     = usd;

            equity += usd;
            result.trades.push_back(trade);
            sym.open_position.reset();
        }
    }

    result.final_equity = equity;
    return result;
}