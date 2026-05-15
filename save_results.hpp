#pragma once

#include "types.hpp"
#include "simulation.hpp"
#include "stats.hpp"
#include "database.hpp"
#include <string>
#include <memory>

// Forward declare to avoid exposing pqxx everywhere
namespace pqxx { class connection; }

// ─────────────────────────────────────────────
//  ResultWriter
//  Saves backtest results to PostgreSQL.
//  Each run is tagged with a unique run_id
//  so you can compare across parameter sets.
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
    // Safe to call on every run.
    void create_tables();

    // Save a complete backtest run.
    // Returns the run_id assigned to this run.
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