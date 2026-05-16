#include "database.hpp"
#include <duckdb.hpp>
#include <pqxx/pqxx>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <ctime>

// ─────────────────────────────────────────────
//  known_symbols / make_instrument
// ─────────────────────────────────────────────

const std::vector<std::string>& known_symbols() {
    static const std::vector<std::string> s = {
        "EURUSD","GBPUSD","USDJPY","USDCHF",
        "AUDUSD","USDCAD","NZDUSD",
        "EURJPY","GBPJPY","AUDJPY"
    };
    return s;
}

Instrument make_instrument(const std::string& symbol) {
    // symbol, base, quote, profit_ccy, pip_size, lot_size, spread_pips
    static const std::vector<Instrument> catalog = {
        { "EURUSD","EUR","USD","USD", 0.0001, 100000.0, 1.5 },
        { "GBPUSD","GBP","USD","USD", 0.0001, 100000.0, 1.5 },
        { "AUDUSD","AUD","USD","USD", 0.0001, 100000.0, 1.5 },
        { "NZDUSD","NZD","USD","USD", 0.0001, 100000.0, 2.0 },
        { "USDJPY","USD","JPY","JPY", 0.01,   100000.0, 1.5 },
        { "USDCHF","USD","CHF","CHF", 0.0001, 100000.0, 2.0 },
        { "USDCAD","USD","CAD","CAD", 0.0001, 100000.0, 2.0 },
        { "EURJPY","EUR","JPY","JPY", 0.01,   100000.0, 3.0 },
        { "GBPJPY","GBP","JPY","JPY", 0.01,   100000.0, 3.0 },
        { "AUDJPY","AUD","JPY","JPY", 0.01,   100000.0, 3.0 },
    };
    for (const auto& i : catalog)
        if (i.symbol == symbol) return i;
    throw std::invalid_argument("Unknown symbol: " + symbol);
}

// ─────────────────────────────────────────────
//  DatabaseConfig
// ─────────────────────────────────────────────

std::string DatabaseConfig::connection_string() const {
    std::ostringstream ss;
    ss << "host=" << host << " port=" << port
       << " dbname=" << dbname << " user=" << user;
    if (!password.empty()) ss << " password=" << password;
    return ss.str();
}

// ─────────────────────────────────────────────
//  BarLoader
// ─────────────────────────────────────────────

BarLoader::BarLoader(
    const std::string& parquet_daily,
    const std::string& parquet_sub)
    : parquet_daily_(parquet_daily)
    , parquet_sub_  (parquet_sub)
{
    db_  = std::make_unique<duckdb::DuckDB>(nullptr);
    con_ = std::make_unique<duckdb::Connection>(*db_);

    for (const auto& path : { parquet_daily_, parquet_sub_ }) {
        auto r = con_->Query(
            "SELECT count(*) FROM read_parquet('" + path + "') LIMIT 1");
        if (r->HasError())
            throw std::runtime_error(
                "Cannot read Parquet: " + path + "\n" + r->GetError());
    }

    std::cout << "[bars] daily: " << parquet_daily_ << "\n"
              << "[bars]   sub: " << parquet_sub_   << "\n";
}

BarLoader::~BarLoader() = default;

std::vector<Bar> BarLoader::load_bars(
    const std::string& parquet_path,
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date)
{
    std::string q =
        "SELECT "
        "  epoch(bar_time)::BIGINT AS ts,"
        "  bid_open, bid_high, bid_low, bid_close,"
        "  ask_open, ask_high, ask_low, ask_close,"
        "  tick_count "
        "FROM read_parquet('" + parquet_path + "') "
        "WHERE symbol   = '" + symbol     + "' "
        "  AND bar_time >= '" + start_date + "'::TIMESTAMPTZ "
        "  AND bar_time  < '" + end_date   + "'::TIMESTAMPTZ "
        "ORDER BY bar_time ASC";

    auto r = con_->Query(q);
    if (r->HasError())
        throw std::runtime_error(
            "Query failed for " + symbol + ": " + r->GetError());

    std::vector<Bar> bars;
    bars.reserve(r->RowCount());

    for (auto& row : *r) {
        Bar b;
        b.timestamp  = row.GetValue<int64_t>(0);
        b.bid_open   = row.GetValue<double>(1);
        b.bid_high   = row.GetValue<double>(2);
        b.bid_low    = row.GetValue<double>(3);
        b.bid_close  = row.GetValue<double>(4);
        b.ask_open   = row.GetValue<double>(5);
        b.ask_high   = row.GetValue<double>(6);
        b.ask_low    = row.GetValue<double>(7);
        b.ask_close  = row.GetValue<double>(8);
        b.tick_count = row.GetValue<int32_t>(9);
        bars.push_back(b);
    }

    return bars;
}

std::vector<SymbolData> BarLoader::load_all_symbols(
    const std::string& start_date,
    const std::string& end_date)
{
    std::vector<SymbolData> out;
    out.reserve(known_symbols().size());

    for (const auto& sym : known_symbols()) {
        SymbolData d;
        d.instrument    = make_instrument(sym);
        d.open_position = std::nullopt;
        d.trades_today  = 0;
        d.pending_mr    = {};
        d.pending_trend = {};

        d.bars_daily = load_bars(parquet_daily_, sym, start_date, end_date);
        d.bars_sub   = load_bars(parquet_sub_,   sym, start_date, end_date);

        if (d.bars_daily.empty()) {
            std::cerr << "[bars] Warning: no daily bars for " << sym << "\n";
            continue;
        }
        if (d.bars_sub.empty()) {
            std::cerr << "[bars] Warning: no sub-bars for " << sym << "\n";
            continue;
        }

        std::cout << "[bars] " << sym
                  << "  daily=" << d.bars_daily.size()
                  << "  sub="   << d.bars_sub.size() << "\n";

        out.push_back(std::move(d));
    }

    return out;
}

// ─────────────────────────────────────────────
//  ResultWriter helpers
// ─────────────────────────────────────────────

static std::string direction_str(Direction d) {
    return (d == Direction::Long) ? "Long" : "Short";
}

static std::string mode_str(StrategyMode m) {
    return strategy_mode_str(m);
}

static std::string exit_reason_str(ExitReason r) {
    switch (r) {
        case ExitReason::TakeProfit: return "TakeProfit";
        case ExitReason::StopLoss:   return "StopLoss";
        case ExitReason::EndOfData:  return "EndOfData";
    }
    return "Unknown";
}

static std::string generate_run_id(const std::string& label) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    std::string id = buf;
    if (!label.empty()) id += "_" + label;
    return id;
}

static std::string epoch_to_pg(long long epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S+00", &tm);
    return buf;
}

// ─────────────────────────────────────────────
//  ResultWriter
// ─────────────────────────────────────────────

ResultWriter::ResultWriter(const DatabaseConfig& cfg) {
    conn_ = std::make_unique<pqxx::connection>(cfg.connection_string());
    if (!conn_->is_open())
        throw std::runtime_error("DB connection failed");
    std::cout << "[db] Connected to: " << cfg.dbname << "\n";
}

ResultWriter::~ResultWriter() = default;

void ResultWriter::create_tables() {
    pqxx::work txn(*conn_);

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_runs (
            run_id                  TEXT PRIMARY KEY,
            label                   TEXT,
            created_at              TIMESTAMPTZ DEFAULT NOW(),
            strategy_mode           TEXT,
            entry_donchian_period   INT,
            atr_period              INT,
            trigger_atr_mult        DOUBLE PRECISION,
            tp_atr_mult             DOUBLE PRECISION,
            sl_atr_mult             DOUBLE PRECISION,
            max_trades_per_day      INT,
            lots                    DOUBLE PRECISION,
            initial_equity          DOUBLE PRECISION,
            final_equity            DOUBLE PRECISION,
            net_pnl                 DOUBLE PRECISION,
            total_trades            INT,
            win_rate                DOUBLE PRECISION,
            profit_factor           DOUBLE PRECISION,
            max_drawdown            DOUBLE PRECISION,
            sharpe_ratio            DOUBLE PRECISION,
            sortino_ratio           DOUBLE PRECISION
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_trades (
            id              BIGSERIAL PRIMARY KEY,
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            symbol          TEXT,
            direction       TEXT,
            mode            TEXT,
            lots            DOUBLE PRECISION,
            entry_time      TIMESTAMPTZ,
            exit_time       TIMESTAMPTZ,
            entry_day_idx   INT,
            entry_sub_idx   INT,
            exit_sub_idx    INT,
            entry_price     DOUBLE PRECISION,
            exit_price      DOUBLE PRECISION,
            exit_reason     TEXT,
            raw_pnl         DOUBLE PRECISION,
            pnl_usd         DOUBLE PRECISION
        )
    )");

    txn.exec("CREATE INDEX IF NOT EXISTS bt_trades_run_idx ON bt_trades(run_id)");
    txn.exec("CREATE INDEX IF NOT EXISTS bt_trades_sym_idx ON bt_trades(run_id,symbol)");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_equity (
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            bar_time        TIMESTAMPTZ,
            equity          DOUBLE PRECISION,
            open_positions  INT,
            PRIMARY KEY (run_id, bar_time)
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_stats (
            run_id  TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            metric  TEXT,
            value   DOUBLE PRECISION,
            PRIMARY KEY (run_id, metric)
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_symbol_stats (
            run_id   TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            symbol   TEXT,
            trades   INT,
            net_pnl  DOUBLE PRECISION,
            win_rate DOUBLE PRECISION,
            PRIMARY KEY (run_id, symbol)
        )
    )");

    txn.commit();
    std::cout << "[db] Result tables ready\n";
}

std::string ResultWriter::save_run_metadata(
    const BacktestConfig& cfg,
    const std::string&    label,
    const StatsResult&    stats)
{
    std::string run_id = generate_run_id(label);
    pqxx::work txn(*conn_);
    txn.exec_params(
        R"(INSERT INTO bt_runs
           (run_id,label,strategy_mode,entry_donchian_period,atr_period,
            trigger_atr_mult,tp_atr_mult,sl_atr_mult,max_trades_per_day,lots,
            initial_equity,final_equity,net_pnl,total_trades,win_rate,
            profit_factor,max_drawdown,sharpe_ratio,sortino_ratio)
           VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19))",
        run_id, label, mode_str(cfg.strategy_mode),
        cfg.entry_donchian_period, cfg.atr_period,
        cfg.trigger_atr_mult, cfg.tp_atr_mult, cfg.sl_atr_mult,
        cfg.max_trades_per_day, cfg.lots,
        0.0, 0.0,
        stats.net_pnl, stats.total_trades, stats.win_rate,
        stats.profit_factor, stats.max_drawdown,
        stats.sharpe_ratio, stats.sortino_ratio);
    txn.commit();
    return run_id;
}

void ResultWriter::save_trades(
    const std::string&        run_id,
    const std::vector<Trade>& trades)
{
    if (trades.empty()) return;
    pqxx::work txn(*conn_);

    auto stream = pqxx::stream_to::table(txn, {"bt_trades"},
        {"run_id","symbol","direction","mode","lots",
         "entry_time","exit_time",
         "entry_day_idx","entry_sub_idx","exit_sub_idx",
         "entry_price","exit_price","exit_reason",
         "raw_pnl","pnl_usd"});

    for (const auto& t : trades) {
        stream.write_values(
            run_id, t.symbol, direction_str(t.direction),
            mode_str(t.mode), t.lots,
            epoch_to_pg(t.entry_time), epoch_to_pg(t.exit_time),
            t.entry_day_idx, t.entry_sub_idx, t.exit_sub_idx,
            t.entry_price, t.exit_price, exit_reason_str(t.exit_reason),
            t.raw_pnl, t.pnl_usd);
    }

    stream.complete();
    txn.commit();
    std::cout << "[db] Saved " << trades.size() << " trades\n";
}

void ResultWriter::save_equity(
    const std::string&              run_id,
    const std::vector<EquityPoint>& curve)
{
    if (curve.empty()) return;
    pqxx::work txn(*conn_);

    auto stream = pqxx::stream_to::table(txn, {"bt_equity"},
        {"run_id","bar_time","equity","open_positions"});

    for (const auto& ep : curve)
        stream.write_values(run_id, epoch_to_pg(ep.timestamp),
                            ep.equity, ep.open_positions);

    stream.complete();
    txn.commit();
}

void ResultWriter::save_stats(
    const std::string& run_id,
    const StatsResult& s)
{
    pqxx::work txn(*conn_);

    auto ins = [&](const std::string& m, double v) {
        txn.exec_params(
            "INSERT INTO bt_stats(run_id,metric,value) VALUES($1,$2,$3)",
            run_id, m, v);
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

    for (const auto& ss : s.by_symbol) {
        txn.exec_params(
            "INSERT INTO bt_symbol_stats(run_id,symbol,trades,net_pnl,win_rate)"
            " VALUES($1,$2,$3,$4,$5)",
            run_id, ss.symbol, ss.trades, ss.net_pnl, ss.win_rate);
    }

    txn.commit();
    std::cout << "[db] Saved stats: " << run_id << "\n";
}

std::string ResultWriter::save(
    const SimulationResult& result,
    const StatsResult&      stats,
    const BacktestConfig&   cfg,
    const std::string&      label)
{
    auto run_id = save_run_metadata(cfg, label, stats);
    save_trades(run_id, result.trades);
    save_equity(run_id, result.equity_curve);
    save_stats (run_id, stats);
    std::cout << "[db] Run saved: " << run_id << "\n";
    return run_id;
}

void ResultWriter::delete_run(const std::string& run_id) {
    pqxx::work txn(*conn_);
    txn.exec_params("DELETE FROM bt_runs WHERE run_id=$1", run_id);
    txn.commit();
}
