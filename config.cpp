#include "config.hpp"
#include <toml++/toml.hpp>
#include <stdexcept>
#include <iostream>

AppConfig load_config(const std::string& path) {
    toml::table tbl;

    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            std::string("Failed to parse config file: ") + e.what());
    }

    AppConfig cfg;

    // ── Database ──────────────────────────────────
    cfg.db.host     = tbl["database"]["host"].value_or<std::string>("localhost");
    cfg.db.port     = tbl["database"]["port"].value_or<std::string>("5432");
    cfg.db.dbname   = tbl["database"]["dbname"].value_or<std::string>("massive");
    cfg.db.user     = tbl["database"]["user"].value_or<std::string>("postgres");
    cfg.db.password = tbl["database"]["password"].value_or<std::string>("");

    // ── Data ──────────────────────────────────────
    cfg.parquet_30   = tbl["data"]["parquet_30"].value_or<std::string>("");
    cfg.parquet_1min = tbl["data"]["parquet_1min"].value_or<std::string>("");
    cfg.start_date   = tbl["data"]["start_date"].value_or<std::string>("");
    cfg.end_date     = tbl["data"]["end_date"].value_or<std::string>("");

    if (cfg.parquet_30.empty())
        throw std::runtime_error("config.toml: data.parquet_30 is required");
    if (cfg.parquet_1min.empty())
        throw std::runtime_error("config.toml: data.parquet_1min is required");
    if (cfg.start_date.empty())
        throw std::runtime_error("config.toml: data.start_date is required");
    if (cfg.end_date.empty())
        throw std::runtime_error("config.toml: data.end_date is required");

    // ── Account ───────────────────────────────────
    cfg.initial_equity = tbl["account"]["initial_equity"].value_or(10000.0);

    // ── Single run ────────────────────────────────
    cfg.single_label                  = tbl["single"]["label"].value_or<std::string>("run");
    cfg.single.lookback               = tbl["single"]["lookback"].value_or(10);
    cfg.single.divisor                = tbl["single"]["divisor"].value_or(4.0);
    cfg.single.take_profit_pips       = tbl["single"]["take_profit_pips"].value_or(50.0);
    cfg.single.stop_loss_pips         = tbl["single"]["stop_loss_pips"].value_or(25.0);
    cfg.single.lots                   = tbl["single"]["lots"].value_or(0.1);

    // ── Sweep ─────────────────────────────────────
    cfg.sweep.lots = tbl["sweep"]["lots"].value_or(0.1);

    // Helper to read a TOML array into a vector<T>
    auto read_array_int = [&](const std::string& key) {
        std::vector<int> out;
        if (auto arr = tbl["sweep"][key].as_array()) {
            arr->for_each([&](auto&& el) {
                if constexpr (toml::is_integer<decltype(el)>)
                    out.push_back(static_cast<int>(el.get()));
            });
        }
        return out;
    };

    auto read_array_dbl = [&](const std::string& key) {
        std::vector<double> out;
        if (auto arr = tbl["sweep"][key].as_array()) {
            arr->for_each([&](auto&& el) {
                if constexpr (toml::is_number<decltype(el)>)
                    out.push_back(static_cast<double>(el.get()));
            });
        }
        return out;
    };

    cfg.sweep.lookbacks = read_array_int("lookbacks");
    cfg.sweep.divisors  = read_array_dbl("divisors");
    cfg.sweep.tp_pips   = read_array_dbl("tp_pips");
    cfg.sweep.sl_pips   = read_array_dbl("sl_pips");

    if (cfg.sweep.lookbacks.empty())
        throw std::runtime_error("config.toml: sweep.lookbacks is required");
    if (cfg.sweep.divisors.empty())
        throw std::runtime_error("config.toml: sweep.divisors is required");
    if (cfg.sweep.tp_pips.empty())
        throw std::runtime_error("config.toml: sweep.tp_pips is required");
    if (cfg.sweep.sl_pips.empty())
        throw std::runtime_error("config.toml: sweep.sl_pips is required");

    // ── Summary ───────────────────────────────────
    std::cout << "[config] Loaded: " << path << "\n"
              << "[config] Date range: " << cfg.start_date
              << " to " << cfg.end_date << "\n"
              << "[config] Sweep combinations: "
              << cfg.sweep.lookbacks.size() *
                 cfg.sweep.divisors.size()  *
                 cfg.sweep.tp_pips.size()   *
                 cfg.sweep.sl_pips.size()
              << "\n";

    return cfg;
}