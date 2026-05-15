#include "save_results.hpp"
#include <pqxx/pqxx>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <ctime>

// ─────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────

static std::string direction_str(Direction d) {
    return (d == Direction::Long) ? "Long" : "Short";
}

static std::string exit_reason_str(ExitReason r) {
    switch (r) {
        case ExitReason::TakeProfit: return "TakeProfit";
        case ExitReason::StopLoss:   return "StopLoss";
        case ExitReason::EndOfData:  return "EndOfData";
    }
    return "Unknown";
}

// Generate a unique run_id from current UTC time
// e.g. "20250115_143022"
static std::string generate_run_id(const std::string& label) {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &now);
#else
    gmtime_r(&now, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    std::string id = buf;
    if (!label.empty()) id += "_" + label;
    return id;
}

// Convert Unix epoch to PostgreSQL timestamptz literal
// e.g. "2025-01-01 00:00:00+00"
static std::string epoch_to_pg(long long epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S+00", &tm_buf);
    return buf;
}

// ─────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────

ResultWriter::ResultWriter(const DatabaseConfig& config) {
    try {
        conn_ = std::make_unique<pqxx::connection>(config.connection_string());
        if (!conn_->is_open())
            throw std::runtime_error("Failed to open connection");
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("ResultWriter connection failed: ") + e.what());
    }
}

ResultWriter::~ResultWriter() = default;

// ─────────────────────────────────────────────
//  create_tables
//  Idempotent — safe to call on every run.
//  TimescaleDB hypertable on bt_equity for
//  fast time-range queries on equity curve.
// ─────────────────────────────────────────────

void ResultWriter::create_tables() {
    pqxx::work txn(*conn_);

    // bt_runs: one row per backtest run
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_runs (
            run_id          TEXT PRIMARY KEY,
            label           TEXT,
            created_at      TIMESTAMPTZ DEFAULT NOW(),
            start_date      TEXT,
            end_date        TEXT,
            lookback        INT,
            divisor         DOUBLE PRECISION,
            tp_pips         DOUBLE PRECISION,
            sl_pips         DOUBLE PRECISION,
            lots            DOUBLE PRECISION,
            initial_equity  DOUBLE PRECISION,
            final_equity    DOUBLE PRECISION,
            net_pnl         DOUBLE PRECISION,
            total_trades    INT,
            win_rate        DOUBLE PRECISION,
            profit_factor   DOUBLE PRECISION,
            max_drawdown    DOUBLE PRECISION,
            sharpe_ratio    DOUBLE PRECISION,
            sortino_ratio   DOUBLE PRECISION
        )
    )");

    // bt_trades: one row per closed trade
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_trades (
            id              BIGSERIAL PRIMARY KEY,
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            symbol          TEXT,
            direction       TEXT,
            lots            DOUBLE PRECISION,
            entry_time      TIMESTAMPTZ,
            exit_time       TIMESTAMPTZ,
            entry_bar       INT,
            exit_bar        INT,
            duration_bars   INT,
            entry_price     DOUBLE PRECISION,
            exit_price      DOUBLE PRECISION,
            exit_reason     TEXT,
            raw_pnl         DOUBLE PRECISION,
            pnl_usd         DOUBLE PRECISION
        )
    )");

    txn.exec(R"(
        CREATE INDEX IF NOT EXISTS bt_trades_run_id_idx
        ON bt_trades(run_id)
    )");

    txn.exec(R"(
        CREATE INDEX IF NOT EXISTS bt_trades_symbol_idx
        ON bt_trades(run_id, symbol)
    )");

    // bt_equity: bar-by-bar equity snapshots
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_equity (
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            bar_time        TIMESTAMPTZ,
            equity          DOUBLE PRECISION,
            open_positions  INT,
            PRIMARY KEY (run_id, bar_time)
        )
    )");

    // bt_stats: summary metrics (key-value)
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_stats (
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            metric          TEXT,
            value           DOUBLE PRECISION,
            PRIMARY KEY (run_id, metric)
        )
    )");

    // bt_symbol_stats: per-symbol breakdown
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_symbol_stats (
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            symbol          TEXT,
            trades          INT,
            net_pnl         DOUBLE PRECISION,
            win_rate        DOUBLE PRECISION,
            PRIMARY KEY (run_id, symbol)
        )
    )");

    txn.commit();
    std::cout << "[db] Result tables ready\n";
}

// ─────────────────────────────────────────────
//  save_run_metadata
// ─────────────────────────────────────────────

std::string ResultWriter::save_run_metadata(
    const BacktestConfig& config,
    const std::string&    label,
    const StatsResult&    stats)
{
    std::string run_id = generate_run_id(label);
    pqxx::work txn(*conn_);

    txn.exec_params(
        R"(INSERT INTO bt_runs
           (run_id, label,
            lookback, divisor, tp_pips, sl_pips, lots,
            initial_equity, final_equity, net_pnl,
            total_trades, win_rate, profit_factor,
            max_drawdown, sharpe_ratio, sortino_ratio)
           VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16))",
        run_id, label,
        config.lookback, config.divisor,
        config.take_profit_pips, config.stop_loss_pips, config.lots,
        0.0,   // initial_equity — updated by caller
        0.0,   // final_equity
        stats.net_pnl,
        stats.total_trades, stats.win_rate, stats.profit_factor,
        stats.max_drawdown, stats.sharpe_ratio, stats.sortino_ratio
    );

    txn.commit();
    return run_id;
}

// ─────────────────────────────────────────────
//  save_trades
//  Uses a single bulk insert via pqxx stream
//  for performance on large trade lists.
// ─────────────────────────────────────────────

void ResultWriter::save_trades(
    const std::string&        run_id,
    const std::vector<Trade>& trades)
{
    if (trades.empty()) return;

    pqxx::work txn(*conn_);

    auto stream = pqxx::stream_to::table(
        txn,
        {"bt_trades"},
        {"run_id","symbol","direction","lots",
         "entry_time","exit_time",
         "entry_bar","exit_bar","duration_bars",
         "entry_price","exit_price","exit_reason",
         "raw_pnl","pnl_usd"}
    );

    for (const auto& t : trades) {
        stream.write_values(
            run_id,
            t.symbol,
            direction_str(t.direction),
            t.lots,
            epoch_to_pg(t.entry_time),
            epoch_to_pg(t.exit_time),
            t.entry_bar,
            t.exit_bar,
            t.duration_bars(),
            t.entry_price,
            t.exit_price,
            exit_reason_str(t.exit_reason),
            t.raw_pnl,
            t.pnl_usd
        );
    }

    stream.complete();
    txn.commit();

    std::cout << "[db] Saved " << trades.size() << " trades\n";
}

// ─────────────────────────────────────────────
//  save_equity
//  Bulk insert equity curve via stream.
// ─────────────────────────────────────────────

void ResultWriter::save_equity(
    const std::string&              run_id,
    const std::vector<EquityPoint>& curve)
{
    if (curve.empty()) return;

    pqxx::work txn(*conn_);

    auto stream = pqxx::stream_to::table(
        txn,
        {"bt_equity"},
        {"run_id","bar_time","equity","open_positions"}
    );

    for (const auto& ep : curve) {
        stream.write_values(
            run_id,
            epoch_to_pg(ep.timestamp),
            ep.equity,
            ep.open_positions
        );
    }

    stream.complete();
    txn.commit();

    std::cout << "[db] Saved " << curve.size() << " equity points\n";
}

// ─────────────────────────────────────────────
//  save_stats
// ─────────────────────────────────────────────

void ResultWriter::save_stats(
    const std::string& run_id,
    const StatsResult& s)
{
    pqxx::work txn(*conn_);

    // Summary metrics
    auto ins = [&](const std::string& metric, double value) {
        txn.exec_params(
            "INSERT INTO bt_stats(run_id,metric,value) VALUES($1,$2,$3)",
            run_id, metric, value);
    };

    ins("total_trades",      s.total_trades);
    ins("winning_trades",    s.winning_trades);
    ins("losing_trades",     s.losing_trades);
    ins("win_rate",          s.win_rate);
    ins("net_pnl",           s.net_pnl);
    ins("gross_profit",      s.gross_profit);
    ins("gross_loss",        s.gross_loss);
    ins("profit_factor",     s.profit_factor);
    ins("expectancy",        s.expectancy);
    ins("avg_win",           s.avg_win);
    ins("avg_loss",          s.avg_loss);
    ins("avg_rr",            s.avg_rr);
    ins("max_drawdown",      s.max_drawdown);
    ins("max_drawdown_pct",  s.max_drawdown_pct);
    ins("sharpe_ratio",      s.sharpe_ratio);
    ins("sortino_ratio",     s.sortino_ratio);
    ins("avg_duration_bars", s.avg_duration_bars);
    ins("max_duration_bars", s.max_duration_bars);

    // Per-symbol breakdown
    for (const auto& ss : s.by_symbol) {
        txn.exec_params(
            R"(INSERT INTO bt_symbol_stats
               (run_id, symbol, trades, net_pnl, win_rate)
               VALUES ($1,$2,$3,$4,$5))",
            run_id, ss.symbol, ss.trades, ss.net_pnl, ss.win_rate
        );
    }

    txn.commit();
    std::cout << "[db] Saved stats for run: " << run_id << "\n";
}

// ─────────────────────────────────────────────
//  save (public entry point)
// ─────────────────────────────────────────────

std::string ResultWriter::save(
    const SimulationResult& result,
    const StatsResult&      stats,
    const BacktestConfig&   config,
    const std::string&      label)
{
    std::string run_id = save_run_metadata(config, label, stats);
    save_trades(run_id, result.trades);
    save_equity(run_id, result.equity_curve);
    save_stats (run_id, stats);
    std::cout << "[db] Run saved: " << run_id << "\n";
    return run_id;
}

// ─────────────────────────────────────────────
//  delete_run
//  CASCADE on bt_runs deletes all child rows
//  in bt_trades, bt_equity, bt_stats,
//  bt_symbol_stats automatically.
// ─────────────────────────────────────────────

void ResultWriter::delete_run(const std::string& run_id) {
    pqxx::work txn(*conn_);
    txn.exec_params(
        "DELETE FROM bt_runs WHERE run_id = $1", run_id);
    txn.commit();
    std::cout << "[db] Deleted run: " << run_id << "\n";
}