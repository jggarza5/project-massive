#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include <string>
#include <vector>
#include <memory>

// Forward declarations
namespace duckdb { class DuckDB; class Connection; }

// ─────────────────────────────────────────────
//  PostgreSQL connection config
//  Used only for saving results
// ─────────────────────────────────────────────

struct DatabaseConfig {
    std::string host     = "localhost";
    std::string port     = "6543";
    std::string dbname   = "postgres";
    std::string user     = "postgres";
    std::string password = "password";

    std::string connection_string() const;
};

// ─────────────────────────────────────────────
//  make_instrument
//  Returns static metadata for a known FX pair.
//  Throws std::invalid_argument for unknown symbols.
// ─────────────────────────────────────────────

Instrument make_instrument(const std::string& symbol);

// ─────────────────────────────────────────────
//  known_symbols
//  The 10 pairs used in this project
// ─────────────────────────────────────────────

const std::vector<std::string>& known_symbols();

// ─────────────────────────────────────────────
//  BarLoader
//  Loads bars from Parquet files via DuckDB.
//
//  Two resolutions:
//    parquet_30: bars_2025.parquet      (signal)
//    parquet_1:  bars_2025_1min.parquet (entry/exit)
// ─────────────────────────────────────────────

class BarLoader {
public:
    BarLoader(
        const std::string& parquet_30,   // 30-min bars
        const std::string& parquet_1);   // 1-min bars
    ~BarLoader();

    // Non-copyable
    BarLoader(const BarLoader&)            = delete;
    BarLoader& operator=(const BarLoader&) = delete;

    // Load bars at one resolution for one symbol
    std::vector<Bar> load_bars(
        const std::string& parquet_path,
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date);

    // Load all 10 pairs at both resolutions.
    // Returns SymbolData ready for run_simulation().
    std::vector<SymbolData> load_all_symbols(
        const std::string& start_date,
        const std::string& end_date);

private:
    std::string                         parquet_30_;
    std::string                         parquet_1_;
    std::unique_ptr<duckdb::DuckDB>     db_;
    std::unique_ptr<duckdb::Connection> con_;
};

