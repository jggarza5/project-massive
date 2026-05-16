#include "database.hpp"
#include <duckdb.hpp>
#include <pqxx/pqxx>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <ctime>

// ─────────────────────────────────────────────
//  known_symbols
// ─────────────────────────────────────────────

const std::vector<std::string>& known_symbols() {
    static const std::vector<std::string> symbols = {
        "EURUSD", "GBPUSD", "USDJPY", "USDCHF",
        "AUDUSD", "USDCAD", "NZDUSD",
        "EURJPY", "GBPJPY", "AUDJPY"
    };
    return symbols;
}

// ─────────────────────────────────────────────
//  make_instrument
// ─────────────────────────────────────────────

Instrument make_instrument(const std::string& symbol) {
    static const std::vector<Instrument> catalog = {
        { "EURUSD", "EUR", "USD", "USD", 0.0001, 100000.0, 3.0 },
        { "GBPUSD", "GBP", "USD", "USD", 0.0001, 100000.0, 3.0 },
        { "AUDUSD", "AUD", "USD", "USD", 0.0001, 100000.0, 3.0 },
        { "NZDUSD", "NZD", "USD", "USD", 0.0001, 100000.0, 3.0 },
        { "USDJPY", "USD", "JPY", "JPY", 0.01,   100000.0, 3.0 },
        { "USDCHF", "USD", "CHF", "CHF", 0.0001, 100000.0, 3.0 },
        { "USDCAD", "USD", "CAD", "CAD", 0.0001, 100000.0, 3.0 },
        { "EURJPY", "EUR", "JPY", "JPY", 0.01,   100000.0, 5.0 },
        { "GBPJPY", "GBP", "JPY", "JPY", 0.01,   100000.0, 5.0 },
        { "AUDJPY", "AUD", "JPY", "JPY", 0.01,   100000.0, 5.0 },
    };

    for (const auto& inst : catalog)
        if (inst.symbol == symbol) return inst;

    throw std::invalid_argument("Unknown symbol: " + symbol);
}

// ─────────────────────────────────────────────
//  DatabaseConfig
// ─────────────────────────────────────────────

std::string DatabaseConfig::connection_string() const {
    std::ostringstream ss;
    ss << "host="    << host
       << " port="   << port
       << " dbname=" << dbname
       << " user="   << user;
    if (!password.empty())
        ss << " password=" << password;
    return ss.str();
}

// ─────────────────────────────────────────────
//  BarLoader
// ─────────────────────────────────────────────

BarLoader::BarLoader(
    const std::string& parquet_30,
    const std::string& parquet_1)
    : parquet_30_(parquet_30)
    , parquet_1_ (parquet_1)
{
    db_  = std::make_unique<duckdb::DuckDB>(nullptr);
    con_ = std::make_unique<duckdb::Connection>(*db_);

    // Verify both files are accessible
    for (const auto& path : { parquet_30_, parquet_1_ }) {
        auto result = con_->Query(
            "SELECT count(*) FROM read_parquet('" + path + "') LIMIT 1");
        if (result->HasError())
            throw std::runtime_error(
                "Cannot read Parquet file: " + path +
                "\n" + result->GetError());
    }

    std::cout << "[bars] 30-min: " << parquet_30_ << "\n";
    std::cout << "[bars]  1-min: " << parquet_1_  << "\n";
}

BarLoader::~BarLoader() = default;

std::vector<Bar> BarLoader::load_bars(
    const std::string& parquet_path,
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date)
{
    std::string query =
        "SELECT "
        "    epoch(bar_time)::BIGINT AS ts, "
        "    bid_open, bid_high, bid_low, bid_close, "
        "    ask_open, ask_high, ask_low, ask_close, "
        "    tick_count "
        "FROM read_parquet('" + parquet_path + "') "
        "WHERE symbol   = '" + symbol     + "' "
        "  AND bar_time >= '" + start_date + "'::TIMESTAMPTZ "
        "  AND bar_time  < '" + end_date   + "'::TIMESTAMPTZ "
        "ORDER BY bar_time ASC";

    auto result = con_->Query(query);
    if (result->HasError())
        throw std::runtime_error(
            "Bar query failed for " + symbol + ": " + result->GetError());

    std::vector<Bar> bars;
    bars.reserve(result->RowCount());

    for (auto& row : *result) {
        Bar bar;
        bar.timestamp  = row.GetValue<int64_t>(0);
        bar.bid_open   = row.GetValue<double>(1);
        bar.bid_high   = row.GetValue<double>(2);
        bar.bid_low    = row.GetValue<double>(3);
        bar.bid_close  = row.GetValue<double>(4);
        bar.ask_open   = row.GetValue<double>(5);
        bar.ask_high   = row.GetValue<double>(6);
        bar.ask_low    = row.GetValue<double>(7);
        bar.ask_close  = row.GetValue<double>(8);
        bar.tick_count = row.GetValue<int32_t>(9);
        bars.push_back(bar);
    }

    return bars;
}

std::vector<SymbolData> BarLoader::load_all_symbols(
    const std::string& start_date,
    const std::string& end_date)
{
    std::vector<SymbolData> symbols;
    symbols.reserve(known_symbols().size());

    for (const auto& sym : known_symbols()) {
        SymbolData data;
        data.instrument    = make_instrument(sym);
        data.open_position = std::nullopt;

        data.bars_30 = load_bars(parquet_30_, sym, start_date, end_date);
        data.bars_1  = load_bars(parquet_1_,  sym, start_date, end_date);

        if (data.bars_30.empty()) {
            std::cerr << "[bars] Warning: no 30-min bars for " << sym << "\n";
            continue;
        }
        if (data.bars_1.empty()) {
            std::cerr << "[bars] Warning: no 1-min bars for " << sym << "\n";
            continue;
        }

        std::cout << "[bars] " << sym
                  << "  30min=" << data.bars_30.size()
                  << "  1min="  << data.bars_1.size()  << "\n";

        symbols.push_back(std::move(data));
    }

    return symbols;
}

// ─────────────────────────────────────────────
//  ResultWriter
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

ResultWriter::ResultWriter(const DatabaseConfig& config) {
    try {
        conn_ = std::make_unique<pqxx::connection>(config.connection_string());
        if (!conn_->is_open())
            throw std::runtime_error("Failed to open connection");
        std::cout << "[db] Connected to: " << config.dbname
                  << " on " << config.host << "\n";
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("ResultWriter connection failed: ") + e.what());
    }
}

ResultWriter::~ResultWriter() = default;

void ResultWriter::create_tables() {
    pqxx::work txn(*conn_);

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS bt_runs (
            run_id          TEXT PRIMARY KEY,
            label           TEXT,
            created_at      TIMESTAMPTZ DEFAULT NOW(),
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
            run_id          TEXT REFERENCES bt_runs(run_id) ON DELETE CASCADE,
            metric          TEXT,
            value           DOUBLE PRECISION,
            PRIMARY KEY (run_id, metric)
        )
    )");

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
        0.0, 0.0,
        stats.net_pnl,
        stats.total_trades, stats.win_rate, stats.profit_factor,
        stats.max_drawdown, stats.sharpe_ratio, stats.sortino_ratio
    );

    txn.commit();
    return run_id;
}

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

void ResultWriter::save_stats(
    const std::string& run_id,
    const StatsResult& s)
{
    pqxx::work txn(*conn_);

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

void ResultWriter::delete_run(const std::string& run_id) {
    pqxx::work txn(*conn_);
    txn.exec_params(
        "DELETE FROM bt_runs WHERE run_id = $1", run_id);
    txn.commit();
    std::cout << "[db] Deleted run: " << run_id << "\n";
}