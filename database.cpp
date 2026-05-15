#include "database.hpp"
#include <pqxx/pqxx>
#include <stdexcept>
#include <sstream>
#include <iostream>

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
//  Hardcoded metadata for all 10 pairs.
//
//  profit_ccy is the quote currency — this is
//  what raw P&L is denominated in before USD
//  conversion.
//
//  max_spread_pips:
//    3.0 for majors (tighter liquidity)
//    5.0 for JPY crosses (wider spreads common)
// ─────────────────────────────────────────────

Instrument make_instrument(const std::string& symbol) {
    // symbol, base, quote, profit_ccy, pip_size, lot_size, max_spread_pips
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

    for (const auto& inst : catalog) {
        if (inst.symbol == symbol) return inst;
    }

    throw std::invalid_argument("Unknown symbol: " + symbol);
}

// ─────────────────────────────────────────────
//  DatabaseConfig::connection_string
// ─────────────────────────────────────────────

std::string DatabaseConfig::connection_string() const {
    std::ostringstream ss;
    ss << "host="     << host
       << " port="    << port
       << " dbname="  << dbname
       << " user="    << user;
    if (!password.empty())
        ss << " password=" << password;
    return ss.str();
}

// ─────────────────────────────────────────────
//  Database constructor / destructor
// ─────────────────────────────────────────────

Database::Database(const DatabaseConfig& config) {
    try {
        conn_ = std::make_unique<pqxx::connection>(config.connection_string());
        if (!conn_->is_open())
            throw std::runtime_error("Failed to open database connection");
        std::cout << "[db] Connected to: " << config.dbname
                  << " on " << config.host << "\n";
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Database connection failed: ") + e.what());
    }
}

Database::~Database() = default;

bool Database::is_connected() const {
    return conn_ && conn_->is_open();
}

// ─────────────────────────────────────────────
//  Database::load_bars
//
//  Queries bars_30min for one symbol.
//  bar_time is stored as timestamptz —
//  we extract Unix epoch via EXTRACT(EPOCH).
//
//  Expected table/view schema:
//    bar_time    timestamptz
//    symbol      text
//    bid_open    double precision
//    bid_high    double precision
//    bid_low     double precision
//    bid_close   double precision
//    ask_open    double precision
//    ask_high    double precision
//    ask_low     double precision
//    ask_close   double precision
//    tick_count  integer
// ─────────────────────────────────────────────

std::vector<Bar> Database::load_bars(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date)
{
    pqxx::work txn(*conn_);

    std::string query = R"(
        SELECT
            EXTRACT(EPOCH FROM bar_time)::bigint AS ts,
            bid_open, bid_high, bid_low, bid_close,
            ask_open, ask_high, ask_low, ask_close,
            tick_count
        FROM bars_30min
        WHERE symbol     = )" + txn.quote(symbol) + R"(
          AND bar_time  >= )" + txn.quote(start_date) + R"(::timestamptz
          AND bar_time   < )" + txn.quote(end_date)   + R"(::timestamptz
        ORDER BY bar_time ASC
    )";

    pqxx::result rows = txn.exec(query);
    txn.commit();

    std::vector<Bar> bars;
    bars.reserve(rows.size());

    for (const auto& row : rows) {
        Bar bar;
        bar.timestamp  = row["ts"].as<long long>();
        bar.bid_open   = row["bid_open"].as<double>();
        bar.bid_high   = row["bid_high"].as<double>();
        bar.bid_low    = row["bid_low"].as<double>();
        bar.bid_close  = row["bid_close"].as<double>();
        bar.ask_open   = row["ask_open"].as<double>();
        bar.ask_high   = row["ask_high"].as<double>();
        bar.ask_low    = row["ask_low"].as<double>();
        bar.ask_close  = row["ask_close"].as<double>();
        bar.tick_count = row["tick_count"].as<int>();
        bars.push_back(bar);
    }

    std::cout << "[db] Loaded " << bars.size()
              << " bars for " << symbol << "\n";

    return bars;
}

// ─────────────────────────────────────────────
//  Database::load_all_symbols
//  Loads all 10 pairs and returns SymbolStates
//  ready to pass directly to run_simulation().
// ─────────────────────────────────────────────

std::vector<SymbolState> Database::load_all_symbols(
    const std::string& start_date,
    const std::string& end_date)
{
    std::vector<SymbolState> states;
    states.reserve(known_symbols().size());

    for (const auto& sym : known_symbols()) {
        SymbolState state;
        state.instrument = make_instrument(sym);
        state.bars       = load_bars(sym, start_date, end_date);

        if (state.bars.empty()) {
            std::cerr << "[db] Warning: no bars loaded for " << sym << "\n";
            continue;
        }

        states.push_back(std::move(state));
    }

    return states;
}