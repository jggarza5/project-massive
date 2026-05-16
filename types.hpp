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
//  StrategyMode
//
//  MeanReversion:
//    New N-day high → SHORT trigger BELOW high
//    New N-day low  → LONG  trigger ABOVE low
//    Entry when price pulls back to trigger
//
//  TrendContinuation:
//    New N-day high → LONG  trigger ABOVE high
//    New N-day low  → SHORT trigger BELOW low
//    Entry when price pushes through trigger
//
//  Both:
//    Sets both triggers on every breakout.
//    Whichever sub-bar hits first fires.
//    Only one position per symbol at a time.
// ─────────────────────────────────────────────

enum class StrategyMode {
    MeanReversion,
    TrendContinuation,
    Both
};

// Parse from string ("Mean-rev", "Trend", "Both")
StrategyMode parse_strategy_mode(const std::string& s);
std::string  strategy_mode_str  (StrategyMode mode);

// ─────────────────────────────────────────────
//  Bar
// ─────────────────────────────────────────────

struct Bar {
    long long timestamp;

    double bid_open, bid_high, bid_low, bid_close;
    double ask_open, ask_high, ask_low, ask_close;
    int    tick_count;

    double mid_close() const { return (bid_close + ask_close) / 2.0; }
    double bid_body()  const { return std::abs(bid_close - bid_open); }
    double spread()    const { return ask_close - bid_close; }
};

// ─────────────────────────────────────────────
//  Instrument
// ─────────────────────────────────────────────

struct Instrument {
    std::string symbol;
    std::string base_ccy;
    std::string quote_ccy;
    std::string profit_ccy;
    double      pip_size;
    double      lot_size;
    double      spread_pips;
};

// ─────────────────────────────────────────────
//  Trade
// ─────────────────────────────────────────────

struct Trade {
    std::string  symbol;
    Direction    direction;
    StrategyMode mode;        // which strategy generated this trade
    double       lots;

    double     entry_price;
    double     exit_price;
    ExitReason exit_reason;

    long long entry_time;
    long long exit_time;

    int entry_day_idx;
    int entry_sub_idx;
    int exit_sub_idx;

    double raw_pnl;
    double pnl_usd;

    bool is_winner() const { return pnl_usd > 0.0; }
};

// ─────────────────────────────────────────────
//  BacktestConfig
// ─────────────────────────────────────────────

struct BacktestConfig {
    StrategyMode strategy_mode = StrategyMode::MeanReversion;

    int    entry_donchian_period = 20;
    int    atr_period            = 14;
    double trigger_atr_mult      = 0.25;
    double tp_atr_mult           = 1.0;
    double sl_atr_mult           = 2.0;

    double lots               = 0.1;
    int    max_trades_per_day = 3;
};

// ─────────────────────────────────────────────
//  EquityPoint
// ─────────────────────────────────────────────

struct EquityPoint {
    long long timestamp;
    double    equity;
    int       open_positions;
};

// ─────────────────────────────────────────────
//  StrategyMode helpers (implemented in types.cpp)
// ─────────────────────────────────────────────
