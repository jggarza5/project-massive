#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include <string>
#include <vector>
#include <memory>

// Forward declare libpqxx connection to avoid
// exposing it in every translation unit
namespace pqxx { class connection; }

// ─────────────────────────────────────────────
//  DatabaseConfig
//  Connection parameters for PostgreSQL
// ─────────────────────────────────────────────

struct DatabaseConfig {
    std::string host     = "localhost";
    std::string port     = "5432";
    std::string dbname   = "massive";
    std::string user     = "postgres";
    std::string password = "";

    // Returns a libpqxx connection string
    std::string connection_string() const;
};

// ─────────────────────────────────────────────
//  Database
//  RAII wrapper around a PostgreSQL connection.
//  One instance per backtester run.
// ─────────────────────────────────────────────

class Database {
public:
    explicit Database(const DatabaseConfig& config);
    ~Database();

    // Non-copyable
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Load 30min bars for one symbol within a date range.
    // Returns bars sorted ascending by bar_time.
    // Expects a materialized view or table named bars_30min.
    std::vector<Bar> load_bars(
        const std::string& symbol,
        const std::string& start_date,   // "YYYY-MM-DD"
        const std::string& end_date);    // "YYYY-MM-DD" exclusive

    // Load all 10 pairs and return ready-to-use SymbolStates
    std::vector<SymbolState> load_all_symbols(
        const std::string& start_date,
        const std::string& end_date);

    // Verify connection is alive
    bool is_connected() const;

private:
    std::unique_ptr<pqxx::connection> conn_;
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