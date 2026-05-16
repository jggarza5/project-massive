#include "types.hpp"
#include <stdexcept>
#include <algorithm>
#include <cctype>

StrategyMode parse_strategy_mode(const std::string& s) {
    // Case-insensitive compare
    auto lower = [](std::string t) {
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        return t;
    };
    std::string l = lower(s);
    if (l == "mean-rev" || l == "meanrev" || l == "mean_rev" || l == "mr")
        return StrategyMode::MeanReversion;
    if (l == "trend" || l == "trend_continuation" || l == "tc")
        return StrategyMode::TrendContinuation;
    if (l == "both")
        return StrategyMode::Both;
    throw std::invalid_argument(
        "Unknown strategy_mode: '" + s +
        "'. Use 'Mean-rev', 'Trend', or 'Both'.");
}

std::string strategy_mode_str(StrategyMode mode) {
    switch (mode) {
        case StrategyMode::MeanReversion:    return "Mean-rev";
        case StrategyMode::TrendContinuation:return "Trend";
        case StrategyMode::Both:             return "Both";
    }
    return "Unknown";
}
