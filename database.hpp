#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include <string>
#include <vector>
#include <memory>

// Forward declarations
namespace duckdb { class DuckDB; class Connection; }
namespace pqxx   { class connection; }

// ─────────────────────────────────────────────
//  PostgreSQL connection config
//  Used only for saving results
// ─────────────────────────────────────────────

struct DatabaseConfig {
    std::string host     = "localhost";
    std::string port     = "5432";
    std::string dbname   = "massive";
    std::string user     = "postgres";
    std::string password = "";

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

// ─────────────────────────────────────────────
//  ResultWriter
//  Saves backtest results to PostgreSQL.
//
//  Tables (created on first use):
//    bt_runs         — one row per backtest run
//    bt_trades       — one row per closed trade
//    bt_equity       — bar-by-bar equity curve
//    bt_stats        — summary metrics per run
//    bt_symbol_stats — per-symbol breakdown
// ─────────────────────────────────────────────

class ResultWriter {
public:
    explicit ResultWriter(const DatabaseConfig& config);
    ~ResultWriter();

    // Non-copyable
    ResultWriter(const ResultWriter&)            = delete;
    ResultWriter& operator=(const ResultWriter&) = delete;

    // Create all result tables if they don't exist.
    void create_tables();

    // Save a complete backtest run. Returns run_id.
    std::string save(
        const SimulationResult& result,
        const StatsResult&      stats,
        const BacktestConfig&   config,
        const std::string&      label = "");

    // Remove all data for a given run_id
    void delete_run(const std::string& run_id);

private:
    std::unique_ptr<pqxx::connection> conn_;

    std::string save_run_metadata(
        const BacktestConfig& config,
        const std::string&    label,
        const StatsResult&    stats);

    void save_trades(
        const std::string&        run_id,
        const std::vector<Trade>& trades);

    void save_equity(
        const std::string&              run_id,
        const std::vector<EquityPoint>& curve);

    void save_stats(
        const std::string& run_id,
        const StatsResult& stats);
};