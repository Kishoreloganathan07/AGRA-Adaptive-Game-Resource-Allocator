#include "agra/analysis/workload_analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace agra::analysis {

WorkloadAnalyzer::WorkloadAnalyzer(const core::AppConfig& config)
    : config_(config) {}

void WorkloadAnalyzer::reset() {
    short_window_.clear();
    medium_window_.clear();
    contention_window_.clear();
    short_game_ewma_.reset();
    medium_game_ewma_.reset();
    system_cpu_ewma_.reset();
    contention_ewma_.reset();
    latest_analysis_ = WorkloadAnalysis{};
}

void WorkloadAnalyzer::process_sample(const core::WorkloadMetrics& metrics) {
    short_window_.push(metrics.game_cpu_percent);
    medium_window_.push(metrics.game_cpu_percent);
    contention_window_.push(metrics.contention_index);

    short_game_ewma_.update(metrics.game_cpu_percent);
    medium_game_ewma_.update(metrics.game_cpu_percent);
    system_cpu_ewma_.update(metrics.system_cpu_percent);
    contention_ewma_.update(metrics.contention_index);

    WorkloadAnalysis a{};
    a.game_cpu_instant = metrics.game_cpu_percent;
    a.game_cpu_short_ewma = short_game_ewma_.value();
    a.game_cpu_medium_ewma = medium_game_ewma_.value();
    a.game_cpu_variance = short_window_.variance();
    a.game_cpu_trend_slope = short_window_.slope();

    a.system_cpu_ewma = system_cpu_ewma_.value();
    a.contention_ewma = contention_ewma_.value();

    // Classification of workload dynamics
    a.is_sustained_heavy = (a.game_cpu_medium_ewma >= config_.high_demand_cpu_threshold);

    double delta_spike = metrics.game_cpu_percent - a.game_cpu_medium_ewma;
    a.is_transient_spike = (delta_spike > 25.0) && !a.is_sustained_heavy;

    a.is_idle_or_menu = (a.game_cpu_medium_ewma < 15.0);

    // Stability score (1.0 = rock solid steady, 0.0 = extreme fluctuations)
    double stddev = std::sqrt(a.game_cpu_variance);
    a.stability_score = std::clamp(1.0 - (stddev / 40.0), 0.0, 1.0);

    // Estimated Demand: 70% medium-term steady demand + 30% short-term + trend slope
    double blend = (0.70 * a.game_cpu_medium_ewma) + (0.30 * a.game_cpu_short_ewma);
    if (a.game_cpu_trend_slope > 0.0) {
        blend += std::clamp(a.game_cpu_trend_slope * 2.0, 0.0, 10.0);
    }

    a.estimated_demand = std::clamp(blend / 100.0, 0.0, 1.0);

    latest_analysis_ = a;
}

} // namespace agra::analysis
