#include "database.hpp"
#include <duckdb.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
