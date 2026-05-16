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
            std::string("Failed to parse config: ") + e.what());
    }

    AppConfig cfg;

    cfg.db.host     = tbl["database"]["host"].value_or<std::string>("localhost");
    cfg.db.port     = tbl["database"]["port"].value_or<std::string>("5432");
    cfg.db.dbname   = tbl["database"]["dbname"].value_or<std::string>("massive");
    cfg.db.user     = tbl["database"]["user"].value_or<std::string>("postgres");
    cfg.db.password = tbl["database"]["password"].value_or<std::string>("");

    cfg.parquet_daily   = tbl["data"]["parquet_daily"].value_or<std::string>("");
    cfg.parquet_sub     = tbl["data"]["parquet_sub"].value_or<std::string>("");
    cfg.start_date      = tbl["data"]["start_date"].value_or<std::string>("");
    cfg.end_date        = tbl["data"]["end_date"].value_or<std::string>("");
    cfg.sub_bar_minutes = tbl["data"]["sub_bar_minutes"].value_or(30);

    if (cfg.parquet_daily.empty() || cfg.parquet_sub.empty()
        || cfg.start_date.empty() || cfg.end_date.empty())
        throw std::runtime_error("config: data paths and dates required");

    cfg.initial_equity = tbl["account"]["initial_equity"].value_or(10000.0);

    // ── Single ────────────────────────────────
    cfg.single_label                    = tbl["single"]["label"].value_or<std::string>("run");
    cfg.single.strategy_mode            = parse_strategy_mode(
        tbl["single"]["strategy_mode"].value_or<std::string>("Mean-rev"));
    cfg.single.entry_donchian_period    = tbl["single"]["entry_donchian_period"].value_or(20);
    cfg.single.atr_period               = tbl["single"]["atr_period"].value_or(14);
    cfg.single.trigger_atr_mult         = tbl["single"]["trigger_atr_mult"].value_or(0.25);
    cfg.single.tp_atr_mult              = tbl["single"]["tp_atr_mult"].value_or(1.0);
    cfg.single.sl_atr_mult              = tbl["single"]["sl_atr_mult"].value_or(2.0);
    cfg.single.lots                     = tbl["single"]["lots"].value_or(0.1);
    cfg.single.max_trades_per_day       = tbl["single"]["max_trades_per_day"].value_or(3);

    // Helper: read array from a table section
    auto read_int_arr = [](const toml::table& t, const std::string& k) {
        std::vector<int> out;
        if (auto arr = t[k].as_array())
            arr->for_each([&](auto&& el) {
                if constexpr (toml::is_integer<decltype(el)>)
                    out.push_back(static_cast<int>(el.get()));
            });
        return out;
    };

    auto read_dbl_arr = [](const toml::table& t, const std::string& k) {
        std::vector<double> out;
        if (auto arr = t[k].as_array())
            arr->for_each([&](auto&& el) {
                if constexpr (toml::is_number<decltype(el)>)
                    out.push_back(static_cast<double>(el.get()));
            });
        return out;
    };

    // ── Sweep ─────────────────────────────────
    if (auto* s = tbl["sweep"].as_table()) {
        cfg.sweep.strategy_mode      = parse_strategy_mode(
            (*s)["strategy_mode"].value_or<std::string>("Mean-rev"));
        cfg.sweep.lots               = (*s)["lots"].value_or(0.1);
        cfg.sweep.atr_period         = (*s)["atr_period"].value_or(14);
        cfg.sweep.max_trades_per_day = (*s)["max_trades_per_day"].value_or(3);
        cfg.sweep.entry_periods      = read_int_arr(*s, "entry_periods");
        cfg.sweep.trigger_atr_mults  = read_dbl_arr(*s, "trigger_atr_mults");
        cfg.sweep.tp_atr_mults       = read_dbl_arr(*s, "tp_atr_mults");
        cfg.sweep.sl_atr_mults       = read_dbl_arr(*s, "sl_atr_mults");
    }

    // ── WFO ───────────────────────────────────
    if (auto* w = tbl["wfo"].as_table()) {
        cfg.wfo.strategy_mode        = parse_strategy_mode(
            (*w)["strategy_mode"].value_or<std::string>("Mean-rev"));
        cfg.wfo.is_days              = (*w)["is_days"].value_or(252);
        cfg.wfo.oos_days             = (*w)["oos_days"].value_or(63);
        cfg.wfo.lots                 = (*w)["lots"].value_or(0.1);
        cfg.wfo.atr_period           = (*w)["atr_period"].value_or(14);
        cfg.wfo.max_trades_per_day   = (*w)["max_trades_per_day"].value_or(3);
        cfg.wfo.entry_periods        = read_int_arr(*w, "entry_periods");
        cfg.wfo.trigger_atr_mults    = read_dbl_arr(*w, "trigger_atr_mults");
        cfg.wfo.tp_atr_mults         = read_dbl_arr(*w, "tp_atr_mults");
        cfg.wfo.sl_atr_mults         = read_dbl_arr(*w, "sl_atr_mults");
    }

    size_t combos = cfg.sweep.entry_periods.size()
                  * cfg.sweep.trigger_atr_mults.size()
                  * cfg.sweep.tp_atr_mults.size()
                  * cfg.sweep.sl_atr_mults.size();

    std::cout << "[config] Loaded: " << path << "\n"
              << "[config] Range: " << cfg.start_date
              << " to " << cfg.end_date << "\n"
              << "[config] Sweep combinations: " << combos << "\n";

    return cfg;
}
