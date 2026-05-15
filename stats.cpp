#include "stats.hpp"
#include "simulation.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <stdexcept>


// ─────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────

static double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

static double stddev(const std::vector<double>& v, double avg) {
    if (v.size() < 2) return 0.0;
    double sq_sum = 0.0;
    for (double x : v) sq_sum += (x - avg) * (x - avg);
    return std::sqrt(sq_sum / (v.size() - 1));
}

static double downside_dev(const std::vector<double>& v, double avg) {
    if (v.size() < 2) return 0.0;
    double sq_sum = 0.0;
    int    count  = 0;
    for (double x : v) {
        if (x < avg) {
            sq_sum += (x - avg) * (x - avg);
            ++count;
        }
    }
    return (count < 2) ? 0.0 : std::sqrt(sq_sum / (count - 1));
}

// ─────────────────────────────────────────────
//  compute_stats
// ─────────────────────────────────────────────

StatsResult compute_stats(const SimulationResult& result) {
    StatsResult stats;
    const auto& trades = result.trades;

    if (trades.empty()) return stats;

    stats.total_trades = static_cast<int>(trades.size());

    // ── Per-trade P&L accumulation ─────────────────
    std::vector<double> all_pnl;
    all_pnl.reserve(trades.size());

    std::unordered_map<std::string, StatsResult::SymbolStats> sym_map;

    for (const auto& t : trades) {
        all_pnl.push_back(t.pnl_usd);

        if (t.pnl_usd > 0.0) {
            ++stats.winning_trades;
            stats.gross_profit += t.pnl_usd;
            stats.avg_win      += t.pnl_usd;
        } else if (t.pnl_usd < 0.0) {
            ++stats.losing_trades;
            stats.gross_loss   += t.pnl_usd;
            stats.avg_loss     += t.pnl_usd;
        } else {
            ++stats.breakeven_trades;
        }

        stats.avg_duration_bars += t.duration_bars();
        stats.max_duration_bars  = std::max(
            stats.max_duration_bars, t.duration_bars());

        // Per-symbol accumulation
        auto& ss   = sym_map[t.symbol];
        ss.symbol  = t.symbol;
        ss.net_pnl += t.pnl_usd;
        ++ss.trades;
        if (t.pnl_usd > 0.0) ss.win_rate += 1.0;
    }

    // ── Aggregate ratios ───────────────────────────
    stats.net_pnl    = stats.gross_profit + stats.gross_loss;
    stats.expectancy = stats.net_pnl / stats.total_trades;

    stats.win_rate = (stats.total_trades > 0)
        ? static_cast<double>(stats.winning_trades) / stats.total_trades
        : 0.0;

    stats.avg_win = (stats.winning_trades > 0)
        ? stats.avg_win / stats.winning_trades : 0.0;

    stats.avg_loss = (stats.losing_trades > 0)
        ? stats.avg_loss / stats.losing_trades : 0.0;

    stats.avg_rr = (stats.avg_loss != 0.0)
        ? std::abs(stats.avg_win / stats.avg_loss) : 0.0;

    stats.profit_factor = (stats.gross_loss != 0.0)
        ? stats.gross_profit / std::abs(stats.gross_loss) : 0.0;

    stats.avg_duration_bars /= stats.total_trades;

    // ── Max drawdown ───────────────────────────────
    {
        double peak    = result.initial_equity;
        double equity  = result.initial_equity;
        double max_dd  = 0.0;
        double max_dd_pct = 0.0;

        for (const auto& ep : result.equity_curve) {
            equity = ep.equity;
            if (equity > peak) peak = equity;
            double dd     = peak - equity;
            double dd_pct = (peak > 0.0) ? dd / peak : 0.0;
            if (dd     > max_dd)     max_dd     = dd;
            if (dd_pct > max_dd_pct) max_dd_pct = dd_pct;
        }

        stats.max_drawdown     = max_dd;
        stats.max_drawdown_pct = max_dd_pct * 100.0;
    }

    // ── Sharpe & Sortino ───────────────────────────
    // Using bar-by-bar equity returns.
    // Annualization factor: 30min bars per year.
    // Forex trades ~245 days * 24hrs * 2 bars/hr = 11,760 bars/year
    {
        const double BARS_PER_YEAR = 11760.0;
        const auto&  curve         = result.equity_curve;

        std::vector<double> returns;
        returns.reserve(curve.size());

        for (size_t k = 1; k < curve.size(); ++k) {
            double prev = curve[k-1].equity;
            if (prev > 0.0)
                returns.push_back((curve[k].equity - prev) / prev);
        }

        if (!returns.empty()) {
            double avg_ret = mean(returns);
            double sd      = stddev(returns, avg_ret);
            double dsd     = downside_dev(returns, avg_ret);

            stats.sharpe_ratio  = (sd  > 0.0)
                ? (avg_ret / sd)  * std::sqrt(BARS_PER_YEAR) : 0.0;
            stats.sortino_ratio = (dsd > 0.0)
                ? (avg_ret / dsd) * std::sqrt(BARS_PER_YEAR) : 0.0;
        }
    }

    // ── Per-symbol finalization ────────────────────
    for (auto& [sym, ss] : sym_map) {
        if (ss.trades > 0)
            ss.win_rate /= ss.trades;
        stats.by_symbol.push_back(ss);
    }

    // Sort by net P&L descending
    std::sort(stats.by_symbol.begin(), stats.by_symbol.end(),
        [](const auto& a, const auto& b) {
            return a.net_pnl > b.net_pnl;
        });

    return stats;
}

// ─────────────────────────────────────────────
//  print_stats
// ─────────────────────────────────────────────

void print_stats(const StatsResult& stats) {
    auto pct = [](double v) {
        return std::fixed << std::setprecision(1) << v * 100.0 << "%";
    };
    auto usd = [](double v) {
        return std::fixed << std::setprecision(2) << v;
    };
    auto f2 = [](double v) {
        return std::fixed << std::setprecision(2) << v;
    };

    std::cout << "\n";
    std::cout << "══════════════════════════════════════\n";
    std::cout << "  BACKTEST RESULTS\n";
    std::cout << "══════════════════════════════════════\n";
    std::cout << "  Total trades      : " << stats.total_trades    << "\n";
    std::cout << "  Winners           : " << stats.winning_trades  << "\n";
    std::cout << "  Losers            : " << stats.losing_trades   << "\n";
    std::cout << "  Win rate          : " << pct(stats.win_rate)   << "\n";
    std::cout << "──────────────────────────────────────\n";
    std::cout << "  Net P&L           : $" << usd(stats.net_pnl)     << "\n";
    std::cout << "  Gross profit      : $" << usd(stats.gross_profit) << "\n";
    std::cout << "  Gross loss        : $" << usd(stats.gross_loss)   << "\n";
    std::cout << "  Profit factor     : "  << f2(stats.profit_factor) << "\n";
    std::cout << "  Expectancy        : $" << usd(stats.expectancy)   << "\n";
    std::cout << "──────────────────────────────────────\n";
    std::cout << "  Avg win           : $" << usd(stats.avg_win)      << "\n";
    std::cout << "  Avg loss          : $" << usd(stats.avg_loss)     << "\n";
    std::cout << "  Avg R:R           : "  << f2(stats.avg_rr)        << "\n";
    std::cout << "──────────────────────────────────────\n";
    std::cout << "  Max drawdown      : $" << usd(stats.max_drawdown)     << "\n";
    std::cout << "  Max drawdown %    : "  << std::fixed << std::setprecision(1)
              << stats.max_drawdown_pct << "%\n";
    std::cout << "  Sharpe ratio      : "  << f2(stats.sharpe_ratio)  << "\n";
    std::cout << "  Sortino ratio     : "  << f2(stats.sortino_ratio) << "\n";
    std::cout << "──────────────────────────────────────\n";
    std::cout << "  Avg duration      : "  << f2(stats.avg_duration_bars) << " bars\n";
    std::cout << "  Max duration      : "  << stats.max_duration_bars    << " bars\n";
    std::cout << "──────────────────────────────────────\n";
    std::cout << "  BY SYMBOL\n";
    std::cout << "──────────────────────────────────────\n";
    for (const auto& ss : stats.by_symbol) {
        std::cout << "  " << std::left << std::setw(8) << ss.symbol
                  << "  trades: " << std::setw(4) << ss.trades
                  << "  P&L: $"   << std::setw(10) << std::fixed
                  << std::setprecision(2) << ss.net_pnl
                  << "  WR: "     << std::fixed << std::setprecision(1)
                  << ss.win_rate * 100.0 << "%\n";
    }
    std::cout << "══════════════════════════════════════\n\n";
}