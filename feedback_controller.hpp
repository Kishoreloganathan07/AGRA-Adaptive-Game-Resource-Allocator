#pragma once

#include "agra/core/types.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace agra::policy {

enum class FeedbackOutcome {
    PendingObservation,
    Improved,
    Neutral,
    Harmful
};

inline std::string_view feedback_outcome_to_string(FeedbackOutcome outcome) noexcept {
    switch (outcome) {
        case FeedbackOutcome::PendingObservation: return "Pending Observation";
        case FeedbackOutcome::Improved:           return "Improved (Retained)";
        case FeedbackOutcome::Neutral:            return "Neutral (Maintained)";
        case FeedbackOutcome::Harmful:            return "Harmful (Rolled Back)";
        default:                                  return "Unknown";
    }
}

// Record of a single intervention and its measured effect
struct InterventionRecord {
    std::uint64_t intervention_id{0};
    core::ProcessId target_pid{0};
    core::TimePoint applied_time{std::chrono::steady_clock::now()};
    core::PriorityLevel original_priority{core::PriorityLevel::Normal};
    core::PriorityLevel applied_priority{core::PriorityLevel::Normal};
    core::BottleneckType bottleneck{core::BottleneckType::Unknown};
    double score{0.0};
    std::string reason;

    // Baseline metrics (before intervention)
    double baseline_game_cpu{0.0};
    double baseline_contention{0.0};
    double baseline_system_cpu{0.0};

    // Observation metrics (after observation window)
    double observed_game_cpu{0.0};
    double observed_contention{0.0};
    double observed_system_cpu{0.0};

    std::uint32_t observation_samples_collected{0};
    std::uint32_t required_observation_samples{5};

    FeedbackOutcome outcome{FeedbackOutcome::PendingObservation};
    std::string outcome_explanation;
};

class FeedbackController {
public:
    explicit FeedbackController(std::uint32_t observation_samples = 4);

    // Record the start of a new intervention with its pre-intervention baseline
    void record_intervention(
        core::ProcessId target_pid,
        core::PriorityLevel original_priority,
        core::PriorityLevel applied_priority,
        const core::AllocationDecision& decision,
        const core::WorkloadMetrics& baseline);

    // Feed each new monitoring sample during the observation window.
    // Returns current outcome state (and whether rollback is requested).
    FeedbackOutcome feed_sample(const core::WorkloadMetrics& current_metrics);

    [[nodiscard]] bool is_observing() const noexcept { return active_intervention_.has_value(); }
    [[nodiscard]] const std::optional<InterventionRecord>& current_intervention() const noexcept {
        return active_intervention_;
    }

    [[nodiscard]] const std::vector<InterventionRecord>& history() const noexcept {
        return history_;
    }

    void reset();

private:
    std::uint32_t observation_samples_required_{4};
    std::optional<InterventionRecord> active_intervention_{std::nullopt};
    std::vector<InterventionRecord> history_;
    std::uint64_t next_id_{1};

    // Evaluate observed metrics vs baseline to determine outcome
    void evaluate_outcome();
};

} // namespace agra::policy
