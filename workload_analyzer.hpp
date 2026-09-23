#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/analysis/rolling_window.hpp"

namespace agra::analysis {

struct WorkloadAnalysis {
    double game_cpu_instant{0.0};
    double game_cpu_short_ewma{0.0};
    double game_cpu_medium_ewma{0.0};
    double game_cpu_variance{0.0};
    double game_cpu_trend_slope{0.0};

    double system_cpu_ewma{0.0};
    double contention_ewma{0.0};

    bool is_sustained_heavy{false};
    bool is_transient_spike{false};
    bool is_idle_or_menu{false};

    double estimated_demand{0.0};   // 0.0 to 1.0
    double stability_score{1.0};    // 0.0 (erratic) to 1.0 (smooth)
};

class WorkloadAnalyzer {
public:
    explicit WorkloadAnalyzer(const core::AppConfig& config);

    void process_sample(const core::WorkloadMetrics& metrics);
    void reset();

    [[nodiscard]] const WorkloadAnalysis& current_analysis() const noexcept {
        return latest_analysis_;
    }

private:
    const core::AppConfig& config_;

    RollingWindow short_window_{8};    // ~2 seconds at 250ms
    RollingWindow medium_window_{28};  // ~7 seconds at 250ms
    RollingWindow contention_window_{12};

    Ewma short_game_ewma_{0.35};
    Ewma medium_game_ewma_{0.15};
    Ewma system_cpu_ewma_{0.20};
    Ewma contention_ewma_{0.25};

    WorkloadAnalysis latest_analysis_;
};

} // namespace agra::analysis
