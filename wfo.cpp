#include "wfo.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <unordered_map>
#include <stdexcept>

// ─────────────────────────────────────────────
//  Date utilities
// ─────────────────────────────────────────────

static long long date_to_epoch(const std::string& date) {
    std::tm tm = {};
    std::istringstream ss(date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
#ifdef _WIN32
    return static_cast<long long>(_mkgmtime(&tm));
#else
    return static_cast<long long>(timegm(&tm));
#endif
}

static std::string epoch_to_date(long long epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// ─────────────────────────────────────────────
//  slice_symbol_data
//  warmup_days: extra calendar days before
//  start_epoch so indicators are fully seeded.
// ─────────────────────────────────────────────

static SymbolData slice_symbol_data(
    const SymbolData& src,
    long long         start_epoch,
    long long         end_epoch,
    int               warmup_days = 0)
{
    long long data_start = start_epoch -
        static_cast<long long>(warmup_days * 2) * 86400LL;

    SymbolData out;
    out.instrument    = src.instrument;
    out.open_position = std::nullopt;
    out.trades_today  = 0;
    out.pending_mr    = {};
    out.pending_trend = {};

    for (const auto& b : src.bars_daily)
        if (b.timestamp >= data_start && b.timestamp < end_epoch)
            out.bars_daily.push_back(b);

    for (const auto& b : src.bars_sub)
        if (b.timestamp >= data_start && b.timestamp < end_epoch)
            out.bars_sub.push_back(b);

    return out;
}

// ─────────────────────────────────────────────
//  extract_symbol_stats
// ─────────────────────────────────────────────

static std::vector<SymbolFoldStats> extract_symbol_stats(
    const SimulationResult& result)
{
    std::unordered_map<std::string, SymbolFoldStats> map;
    for (const auto& t : result.trades) {
        auto& s    = map[t.symbol];
        s.symbol   = t.symbol;
        s.net_pnl += t.pnl_usd;
        ++s.trades;
        if (t.pnl_usd > 0) s.win_rate += 1.0;
    }
    for (auto& [sym, s] : map) {
        if (s.trades > 0) {
            s.win_rate /= s.trades;
            double gp = 0, gl = 0;
            for (const auto& t : result.trades) {
                if (t.symbol != sym) continue;
                if (t.pnl_usd > 0) gp += t.pnl_usd;
                else                gl += t.pnl_usd;
            }
            s.profit_factor = gl != 0 ? gp / std::abs(gl) : 0.0;
        }
    }
    std::vector<SymbolFoldStats> out;
    for (auto& [sym, s] : map) out.push_back(s);
    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b) { return a.symbol < b.symbol; });
    return out;
}

// ─────────────────────────────────────────────
//  run_sweep_on_window
// ─────────────────────────────────────────────

static std::pair<BacktestConfig, SimulationResult> run_sweep_on_window(
    const std::vector<SymbolData>& symbols,
    const WFOConfig&               wfo,
    double                         initial_equity)
{
    BacktestConfig   best_cfg;
    SimulationResult best_result;
    double           best_pf = -1.0;

    for (int ep : wfo.entry_periods) {
        for (double trig_m : wfo.trigger_atr_mults) {
            for (double tp_m : wfo.tp_atr_mults) {
                for (double sl_m : wfo.sl_atr_mults) {
                    BacktestConfig cfg;
                    cfg.strategy_mode         = wfo.strategy_mode;
                    cfg.entry_donchian_period = ep;
                    cfg.atr_period            = wfo.atr_period;
                    cfg.trigger_atr_mult      = trig_m;
                    cfg.tp_atr_mult           = tp_m;
                    cfg.sl_atr_mult           = sl_m;
                    cfg.lots                  = wfo.lots;
                    cfg.max_trades_per_day    = wfo.max_trades_per_day;

                auto sym_copy = symbols;
                for (auto& s : sym_copy) {
                    s.open_position = std::nullopt;
                    s.trades_today  = 0;
                    s.pending_mr    = {};
                    s.pending_trend = {};
                }

                bool has_bars = false;
                for (const auto& s : sym_copy)
                    if (!s.bars_daily.empty() && !s.bars_sub.empty())
                        { has_bars = true; break; }
                if (!has_bars) continue;

                SimulationResult result;
                try {
                    result = run_simulation(sym_copy, cfg, initial_equity);
                } catch (...) { continue; }

                if (result.trades.empty()) continue;

                double gp = 0, gl = 0;
                for (const auto& t : result.trades) {
                    if (t.pnl_usd > 0) gp += t.pnl_usd;
                    else                gl += t.pnl_usd;
                }
                double pf = gl != 0 ? gp / std::abs(gl) : 0.0;

                if (pf > best_pf) {
                    best_pf     = pf;
                    best_cfg    = cfg;
                    best_result = std::move(result);
                }
                }  // sl_m
            }  // tp_m
        }  // trig_m
    }  // ep

    return { best_cfg, std::move(best_result) };
}

// ─────────────────────────────────────────────
//  run_wfo
// ─────────────────────────────────────────────

WFOResult run_wfo(
    const std::vector<SymbolData>& symbols,
    const WFOConfig&               config,
    const std::string&             start_date,
    const std::string&             end_date,
    double                         initial_equity)
{
    if (symbols.empty())
        throw std::invalid_argument("No symbol data");

    long long range_start = date_to_epoch(start_date);
    long long range_end   = date_to_epoch(end_date);
    long long window      = static_cast<long long>(
                                config.is_days + config.oos_days) * 86400LL;

    int num_folds = 0;
    for (long long t = range_start; t + window <= range_end;
         t += config.oos_days * 86400LL) ++num_folds;

    int warmup_days = 0;
    for (int ep : config.entry_periods)
        warmup_days = std::max(warmup_days, ep);
    warmup_days = std::max(warmup_days, config.atr_period);

    size_t combos = config.entry_periods.size()
                  * config.trigger_atr_mults.size()
                  * config.tp_atr_mults.size()
                  * config.sl_atr_mults.size();

    std::cout << "\n[wfo] Strategy: " << strategy_mode_str(config.strategy_mode) << "\n"
              << "[wfo] IS=" << config.is_days
              << "  OOS=" << config.oos_days << " days\n"
              << "[wfo] Warmup=" << warmup_days << " trading days\n"
              << "[wfo] Folds=" << num_folds
              << "  Combos/fold=" << combos << "\n\n";

    WFOResult wfo_result;
    int fold_num = 0;

    for (long long fold_start = range_start;
         fold_start + window <= range_end;
         fold_start += config.oos_days * 86400LL)
    {
        ++fold_num;
        long long is_start  = fold_start;
        long long is_end    = fold_start + config.is_days  * 86400LL;
        long long oos_start = is_end;
        long long oos_end   = oos_start + config.oos_days * 86400LL;

        std::cout << "[wfo] Fold " << fold_num << "/" << num_folds
                  << "  IS: " << epoch_to_date(is_start)
                  << " -> "   << epoch_to_date(is_end)
                  << "  OOS: "<< epoch_to_date(oos_start)
                  << " -> "   << epoch_to_date(oos_end) << "\n";

        // Slice IS with warmup
        std::vector<SymbolData> is_data;
        for (const auto& s : symbols)
            is_data.push_back(
                slice_symbol_data(s, is_start, is_end, warmup_days));

        bool has_data = false;
        for (const auto& s : is_data)
            if (!s.bars_daily.empty()) { has_data = true; break; }
        if (!has_data) {
            std::cout << "[wfo] Fold " << fold_num << " skipped - no IS data\n";
            continue;
        }

        auto [best_cfg, is_result] = run_sweep_on_window(
            is_data, config, initial_equity);

        // Filter IS trades to actual IS window
        {
            auto& tr = is_result.trades;
            tr.erase(std::remove_if(tr.begin(), tr.end(),
                [is_start](const Trade& t) {
                    return t.entry_time < is_start;
                }), tr.end());
        }

        if (is_result.trades.empty()) {
            std::cout << "[wfo] Fold " << fold_num << " skipped - no IS trades\n";
            continue;
        }

        // Slice OOS with warmup
        std::vector<SymbolData> oos_data;
        for (const auto& s : symbols)
            oos_data.push_back(
                slice_symbol_data(s, oos_start, oos_end, warmup_days));

        SimulationResult oos_result;
        try {
            oos_result = run_simulation(oos_data, best_cfg, initial_equity);
        } catch (...) {
            std::cout << "[wfo] Fold " << fold_num << " skipped - OOS failed\n";
            continue;
        }

        // Filter OOS trades to actual OOS window
        {
            auto& tr = oos_result.trades;
            tr.erase(std::remove_if(tr.begin(), tr.end(),
                [oos_start](const Trade& t) {
                    return t.entry_time < oos_start;
                }), tr.end());
        }

        FoldResult fold;
        fold.fold_num    = fold_num;
        fold.is_start    = epoch_to_date(is_start);
        fold.is_end      = epoch_to_date(is_end);
        fold.oos_start   = epoch_to_date(oos_start);
        fold.oos_end     = epoch_to_date(oos_end);
        fold.best_config = best_cfg;

        auto agg = [](const SimulationResult& r,
                      int& n, double& wr, double& pnl, double& pf) {
            n = static_cast<int>(r.trades.size());
            double gp = 0, gl = 0, wins = 0;
            for (const auto& t : r.trades) {
                pnl += t.pnl_usd;
                if (t.pnl_usd > 0) { gp += t.pnl_usd; ++wins; }
                else                  gl += t.pnl_usd;
            }
            wr = n > 0 ? wins / n : 0.0;
            pf = gl != 0 ? gp / std::abs(gl) : 0.0;
        };

        agg(is_result,  fold.is_trades,  fold.is_win_rate,
                        fold.is_net_pnl, fold.is_profit_factor);
        agg(oos_result, fold.oos_trades, fold.oos_win_rate,
                        fold.oos_net_pnl, fold.oos_profit_factor);

        fold.is_by_symbol  = extract_symbol_stats(is_result);
        fold.oos_by_symbol = extract_symbol_stats(oos_result);

        wfo_result.folds.push_back(std::move(fold));
        wfo_result.total_oos_trades  += fold.oos_trades;
        wfo_result.total_oos_net_pnl += fold.oos_net_pnl;
    }

    if (!wfo_result.folds.empty()) {
        double sum_pf = 0, sum_wr = 0;
        for (const auto& f : wfo_result.folds) {
            sum_pf += f.oos_profit_factor;
            sum_wr += f.oos_win_rate;
        }
        int n = static_cast<int>(wfo_result.folds.size());
        wfo_result.avg_oos_profit_factor = sum_pf / n;
        wfo_result.avg_oos_win_rate      = sum_wr / n;
    }

    return wfo_result;
}

// ─────────────────────────────────────────────
//  print_wfo_results
// ─────────────────────────────────────────────

void print_wfo_results(const WFOResult& result) {
    if (result.folds.empty()) {
        std::cout << "[wfo] No folds to display\n";
        return;
    }

    for (const auto& f : result.folds) {
        std::cout << std::string(84, '=') << "\n";
        std::cout << "Fold " << std::setw(4) << f.fold_num
                  << "  IS: "  << f.is_start  << " -> " << f.is_end
                  << "  OOS: " << f.oos_start << " -> " << f.oos_end << "\n";
        std::cout << "Best: Entry=" << f.best_config.entry_donchian_period
                  << "  Trig=" << std::fixed << std::setprecision(2)
                  << f.best_config.trigger_atr_mult << "xATR"
                  << "  TP=" << f.best_config.tp_atr_mult << "xATR"
                  << "  SL=" << f.best_config.sl_atr_mult << "xATR\n";
        std::cout << std::string(84, '-') << "\n";
        std::cout << std::left
            << std::setw(8)  << "Symbol"
            << std::setw(7)  << "IS.N"
            << std::setw(7)  << "IS.WR"
            << std::setw(10) << "IS.PnL"
            << std::setw(7)  << "IS.PF"
            << "  |  "
            << std::setw(7)  << "OOS.N"
            << std::setw(7)  << "OOS.WR"
            << std::setw(10) << "OOS.PnL"
            << std::setw(7)  << "OOS.PF"
            << "\n";
        std::cout << std::string(84, '-') << "\n";

        std::unordered_map<std::string, const SymbolFoldStats*> oos_map;
        for (const auto& s : f.oos_by_symbol)
            oos_map[s.symbol] = &s;

        for (const auto& is : f.is_by_symbol) {
            auto it = oos_map.find(is.symbol);
            std::cout << std::left << std::fixed
                << std::setw(8)  << is.symbol
                << std::setw(7)  << is.trades
                << std::setw(7)  << std::setprecision(1) << is.win_rate * 100
                << std::setw(10) << std::setprecision(2) << is.net_pnl
                << std::setw(7)  << std::setprecision(2) << is.profit_factor
                << "  |  ";
            if (it != oos_map.end()) {
                const auto& o = *it->second;
                std::cout
                    << std::setw(7)  << o.trades
                    << std::setw(7)  << std::setprecision(1) << o.win_rate * 100
                    << std::setw(10) << std::setprecision(2) << o.net_pnl
                    << std::setw(7)  << std::setprecision(2) << o.profit_factor;
            } else {
                std::cout << std::setw(7) << 0 << std::setw(7) << "0.0"
                          << std::setw(10) << "0.00" << std::setw(7) << "0.00";
            }
            std::cout << "\n";
        }

        std::cout << std::string(84, '-') << "\n";
        std::cout << std::left << std::fixed
            << std::setw(8)  << "TOTAL"
            << std::setw(7)  << f.is_trades
            << std::setw(7)  << std::setprecision(1) << f.is_win_rate * 100
            << std::setw(10) << std::setprecision(2) << f.is_net_pnl
            << std::setw(7)  << std::setprecision(2) << f.is_profit_factor
            << "  |  "
            << std::setw(7)  << f.oos_trades
            << std::setw(7)  << std::setprecision(1) << f.oos_win_rate * 100
            << std::setw(10) << std::setprecision(2) << f.oos_net_pnl
            << std::setw(7)  << std::setprecision(2) << f.oos_profit_factor
            << "\n";
    }

    std::cout << std::string(84, '=') << "\n";
    std::cout << "WALK-FORWARD SUMMARY\n";
    std::cout << std::string(84, '-') << "\n";
    std::cout << std::fixed
        << "Total folds          : " << result.folds.size()              << "\n"
        << "Total OOS trades     : " << result.total_oos_trades           << "\n"
        << "Total OOS net P&L    : $" << std::setprecision(2)
                                       << result.total_oos_net_pnl        << "\n"
        << "Avg OOS profit factor: "  << std::setprecision(3)
                                       << result.avg_oos_profit_factor    << "\n"
        << "Avg OOS win rate     : "  << std::setprecision(1)
                                       << result.avg_oos_win_rate * 100.0 << "%\n";
    std::cout << std::string(84, '=') << "\n";
}
