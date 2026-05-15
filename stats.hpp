#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────
//  StatsResult
//  All performance metrics for one backtest run
// ─────────────────────────────────────────────

struct StatsResult {
    // Trade counts
    int    total_trades    = 0;
    int    winning_trades  = 0;
    int    losing_trades   = 0;
    int    breakeven_trades= 0;

    // P&L
    double gross_profit    = 0.0;   // sum of winning trade P&L (USD)
    double gross_loss      = 0.0;   // sum of losing trade P&L (USD)
    double net_pnl         = 0.0;   // gross_profit + gross_loss
    double profit_factor   = 0.0;   // gross_profit / abs(gross_loss)

    // Ratios
    double win_rate        = 0.0;   // winning_trades / total_trades
    double avg_win         = 0.0;   // avg P&L of winning trades (USD)
    double avg_loss        = 0.0;   // avg P&L of losing trades (USD)
    double avg_rr          = 0.0;   // abs(avg_win / avg_loss)
    double expectancy      = 0.0;   // avg P&L per trade (USD)

    // Drawdown
    double max_drawdown    = 0.0;   // peak-to-trough in USD
    double max_drawdown_pct= 0.0;   // peak-to-trough as % of peak equity

    // Risk-adjusted
    double sharpe_ratio    = 0.0;   // annualized, assumes 30min bars
    double sortino_ratio   = 0.0;   // downside deviation only

    // Duration
    double avg_duration_bars = 0.0; // average trade length in bars
    int    max_duration_bars = 0;

    // Per symbol breakdown
    struct SymbolStats {
        std::string symbol;
        int    trades      = 0;
        double net_pnl     = 0.0;
        double win_rate    = 0.0;
    };
    std::vector<SymbolStats> by_symbol;
};

// ─────────────────────────────────────────────
//  compute_stats
//  Derives all metrics from a completed
//  simulation result.
// ─────────────────────────────────────────────

StatsResult compute_stats(const SimulationResult& result);

// ─────────────────────────────────────────────
//  print_stats
//  Prints a formatted summary to stdout
// ─────────────────────────────────────────────

void print_stats(const StatsResult& stats);