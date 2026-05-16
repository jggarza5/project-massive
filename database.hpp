#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include <string>
#include <vector>
#include <memory>

namespace duckdb { class DuckDB; class Connection; }
namespace pqxx   { class connection; }

// ─────────────────────────────────────────────
//  DatabaseConfig
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
//  make_instrument / known_symbols
// ─────────────────────────────────────────────

Instrument make_instrument(const std::string& symbol);
const std::vector<std::string>& known_symbols();

// ─────────────────────────────────────────────
//  BarLoader
//  Loads daily and sub-bar data from Parquet.
//  Both files use the same Bar struct with
//  mid-price stored in bid_ and ask_ fields.
// ─────────────────────────────────────────────

class BarLoader {
public:
    BarLoader(
        const std::string& parquet_daily,   // daily_2009_2026.parquet
        const std::string& parquet_sub);    // sub_30min_2009_2026.parquet
    ~BarLoader();

    BarLoader(const BarLoader&)            = delete;
    BarLoader& operator=(const BarLoader&) = delete;

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
    std::string                         parquet_daily_;
    std::string                         parquet_sub_;
    std::unique_ptr<duckdb::DuckDB>     db_;
    std::unique_ptr<duckdb::Connection> con_;
};

// ─────────────────────────────────────────────
//  ResultWriter
//  Saves backtest results to PostgreSQL.
// ─────────────────────────────────────────────

class ResultWriter {
public:
    explicit ResultWriter(const DatabaseConfig& config);
    ~ResultWriter();

    ResultWriter(const ResultWriter&)            = delete;
    ResultWriter& operator=(const ResultWriter&) = delete;

    void        create_tables();
    std::string save(
        const SimulationResult& result,
        const StatsResult&      stats,
        const BacktestConfig&   config,
        const std::string&      label = "");
    void        delete_run(const std::string& run_id);

private:
    std::unique_ptr<pqxx::connection> conn_;

    std::string save_run_metadata(
        const BacktestConfig& config,
        const std::string&    label,
        const StatsResult&    stats);
    void save_trades(const std::string& run_id,
                     const std::vector<Trade>& trades);
    void save_equity(const std::string& run_id,
                     const std::vector<EquityPoint>& curve);
    void save_stats (const std::string& run_id,
                     const StatsResult& stats);
};
