#pragma once

#include <string>
#include <cmath>

// ─────────────────────────────────────────────
//  Direction
// ─────────────────────────────────────────────

enum class Direction { Long, Short };

// ─────────────────────────────────────────────
//  ExitReason
// ─────────────────────────────────────────────

enum class ExitReason { TakeProfit, StopLoss, EndOfData };

// ─────────────────────────────────────────────
//  Bar
//  30-minute OHLC bar built from bid/ask ticks
// ─────────────────────────────────────────────

struct Bar {
    long long timestamp;        // Unix epoch seconds (bar open time)

    // Bid side
    double bid_open;
    double bid_high;
    double bid_low;
    double bid_close;

    // Ask side
    double ask_open;
    double ask_high;
    double ask_low;
    double ask_close;

    int    tick_count;          // number of ticks in this bar

    // Derived
    double spread()    const { return ask_close - bid_close; }
    double bid_body()  const { return std::abs(bid_close - bid_open); }
    double mid_close() const { return (bid_close + ask_close) / 2.0; }
};

// ─────────────────────────────────────────────
//  Instrument
//  Static metadata per symbol
// ─────────────────────────────────────────────

struct Instrument {
    std::string symbol;         // e.g. "EURUSD"
    std::string base_ccy;       // e.g. "EUR"
    std::string quote_ccy;      // e.g. "USD"
    std::string profit_ccy;     // currency P&L is denominated in
    double      pip_size;       // 0.0001 or 0.01 for JPY pairs
    double      lot_size;       // usually 100000 (standard lot)
    double      max_spread_pips;// skip entry if spread exceeds this
};

// ─────────────────────────────────────────────
//  Trade
//  Completed round-trip trade
// ─────────────────────────────────────────────

struct Trade {
    std::string symbol;
    Direction   direction;
    double      lots;

    double      entry_price;
    double      exit_price;
    ExitReason  exit_reason;

    long long   entry_time;     // Unix epoch seconds
    long long   exit_time;

    int         entry_bar;
    int         exit_bar;

    double      raw_pnl;        // P&L in profit currency (pips * pip_value)
    double      pnl_usd;        // P&L converted to account currency (USD)

    // Convenience
    int  duration_bars() const { return exit_bar - entry_bar; }
    bool is_winner()     const { return pnl_usd > 0.0; }
};

// ─────────────────────────────────────────────
//  BacktestConfig
//  All tunable parameters in one place
// ─────────────────────────────────────────────

struct BacktestConfig {
    // Signal parameters
    int    lookback = 10;       // bars to look back for max body
    double divisor  = 4.0;      // trigger margin: max_body * (1 + 1/divisor)

    // Exit parameters (in pips)
    double take_profit_pips = 50.0;
    double stop_loss_pips   = 25.0;

    // Position sizing
    double lots = 0.1;          // fixed lot size per trade

    // Filters
    double max_spread_pips = 3.0; // majors; crosses may need higher

    // Date range
    long long start_time = 0;   // Unix epoch; 0 = no filter
    long long end_time   = 0;   // Unix epoch; 0 = no filter

    // Account
    std::string account_currency = "USD";
};

// ─────────────────────────────────────────────
//  EquityPoint
//  Snapshot of account equity at each bar
// ─────────────────────────────────────────────

struct EquityPoint {
    long long timestamp;
    double    equity;
    int       open_positions;
};