#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"
#include "agra/core/config.hpp"
#include "agra/windows/windows_controller.hpp"
#include "agra/safety/safety_manager.hpp"
#include "agra/policy/feedback_controller.hpp"
#include "agra/discovery/process_enumerator.hpp"

#include <vector>
#include <string>

namespace agra::policy {

struct PolicyStatus {
    core::PolicyMode active_mode{core::PolicyMode::Default};
    core::PriorityLevel game_priority{core::PriorityLevel::Normal};
    bool is_game_boosted{false};
    std::uint32_t throttled_background_count{0};
    FeedbackOutcome last_feedback_outcome{FeedbackOutcome::Neutral};
    std::string current_explanation;
};

class ResourcePolicyEngine {
public:
    ResourcePolicyEngine(
        windows::WindowsController& controller,
        safety::SafetyManager& safety,
        const core::AppConfig& config);

    // Apply an allocation decision to the target process
    [[nodiscard]] core::Result<void, core::Error> apply_decision(
        core::ProcessId target_pid,
        const std::string& target_name,
        const core::AllocationDecision& decision,
        const core::WorkloadMetrics& current_metrics);

    // Process each new sample for closed-loop feedback verification
    void on_monitoring_sample(const core::WorkloadMetrics& current_metrics);

    // Revert target process policy back to original baseline
    [[nodiscard]] core::Result<void, core::Error> revert_target(core::ProcessId target_pid);

    // Throttle high-contention non-critical background processes
    void manage_background_contention(
        core::ProcessId target_pid,
        const std::vector<discovery::DiscoveredProcess>& all_processes,
        double contention_index);

    // Restore any background processes that were throttled
    void restore_background_processes();

    // Query current policy engine status
    [[nodiscard]] PolicyStatus status() const;

    [[nodiscard]] const FeedbackController& feedback() const noexcept { return feedback_; }

    void reset();

private:
    windows::WindowsController& controller_;
    safety::SafetyManager& safety_;
    const core::AppConfig& config_;
    FeedbackController feedback_;

    core::ProcessId current_target_pid_{0};
    std::string current_target_name_;
    core::PriorityLevel current_applied_priority_{core::PriorityLevel::Normal};
    bool is_boosted_{false};
    std::vector<core::ProcessId> throttled_pids_;
    std::string last_explanation_;
};

} // namespace agra::policy
