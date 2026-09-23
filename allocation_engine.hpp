#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/analysis/workload_analyzer.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace agra::allocator {

// ─────────────────────────────────────────────────────────────────────────────
// HysteresisState
//
// Tracks whether the allocation engine is currently in a boosted/elevated
// state or in normal state.  Prevents rapid policy oscillation by requiring
// several consecutive samples above/below threshold before transitioning.
//
// State machine:
//
//   Normal ──(enter_count >= enter_threshold)──► Elevated
//   Elevated ──(exit_count >= exit_threshold)───► Normal
//
// Both counters reset when a transition occurs.
// ─────────────────────────────────────────────────────────────────────────────
enum class HysteresisState {
    Normal,    // Default, no active boost
    Elevated   // Active boost has been committed
};

struct HysteresisController {
    explicit HysteresisController(std::uint32_t enter_samples, std::uint32_t exit_samples)
        : enter_threshold(enter_samples), exit_threshold(exit_samples) {}

    // Call on every sample with whether this sample qualifies for elevation.
    // Returns the new state after considering the sample.
    HysteresisState update(bool sample_qualifies_for_elevation) noexcept;

    void reset() noexcept;

    [[nodiscard]] HysteresisState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t enter_count() const noexcept { return enter_count_; }
    [[nodiscard]] std::uint32_t exit_count()  const noexcept { return exit_count_;  }

private:
    HysteresisState state_{HysteresisState::Normal};
    std::uint32_t enter_threshold{3};
    std::uint32_t exit_threshold{5};
    std::uint32_t enter_count_{0};
    std::uint32_t exit_count_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// AllocationComponents
//
// Intermediate breakdown of the AllocationScore formula — kept separate so
// that every contributing factor is visible in logs and test assertions.
//
// AllocationScore (0–100) =
//   demand_component         (0–40)   CPU demand weight
//   contention_component     (0–25)   Background contention
//   stability_component      (0–15)   Instability bonus (spiky = more urgent)
//   spike_urgency_component  (0–10)   Transient spike bonus
//   confidence_component     (0–10)   Classifier confidence weight
//   - fairness_penalty       (0–20)   Subtracted if system already stressed
// ─────────────────────────────────────────────────────────────────────────────
struct AllocationComponents {
    double demand_component{0.0};
    double contention_component{0.0};
    double stability_component{0.0};
    double spike_urgency_component{0.0};
    double confidence_component{0.0};
    double fairness_penalty{0.0};
    double final_score{0.0};  // sum of above, clamped [0, 100]
};

// ─────────────────────────────────────────────────────────────────────────────
// AllocationEngine
//
// Stateful decision engine.  The caller should create one instance per
// monitored target process and call decide() on every monitoring tick.
//
// Responsibilities:
//   • Compute a normalised AllocationScore from workload signals.
//   • Apply hysteresis: only commit a policy when the signal is sustained.
//   • Select a safe PriorityLevel appropriate to the active PolicyMode.
//   • Enforce fairness: never recommend boosting when the system is so
//     loaded that background processes would be starved below minimum
//     responsiveness.
//   • Enforce a forced-cooldown after max_consecutive_boost_seconds to
//     prevent indefinite priority elevation.
//   • Produce a fully-populated, explainable AllocationDecision.
// ─────────────────────────────────────────────────────────────────────────────
class AllocationEngine {
public:
    explicit AllocationEngine(const core::AppConfig& config);

    // Primary entry-point.  Call once per monitoring tick.
    [[nodiscard]] core::AllocationDecision decide(
        core::ProcessId                          target_pid,
        const analysis::BottleneckResult&        bottleneck,
        const analysis::WorkloadAnalysis&        analysis,
        const core::WorkloadMetrics&             metrics);

    // Reset all stateful hysteresis and cooldown counters.
    // Call when the target process changes or the session restarts.
    void reset();

    // Expose internal state for diagnostics and tests.
    [[nodiscard]] HysteresisState hysteresis_state() const noexcept {
        return hysteresis_.state();
    }
    [[nodiscard]] AllocationComponents last_components() const noexcept {
        return last_components_;
    }
    [[nodiscard]] bool is_in_forced_cooldown() const noexcept {
        return in_forced_cooldown_;
    }

private:
    const core::AppConfig& config_;

    HysteresisController hysteresis_;

    // Timing for forced cooldown after prolonged boost
    std::chrono::steady_clock::time_point boost_start_time_;
    bool currently_boosted_{false};
    bool in_forced_cooldown_{false};
    std::chrono::steady_clock::time_point cooldown_end_time_;

    AllocationComponents last_components_;

    // Score calculation
    [[nodiscard]] AllocationComponents compute_score(
        const analysis::BottleneckResult&  bottleneck,
        const analysis::WorkloadAnalysis&  analysis,
        const core::WorkloadMetrics&       metrics) const;

    // Priority selection given score and active policy mode.
    // Safety limit: never returns PriorityLevel::High in Default or Conservative mode.
    // Safety limit: never returns PriorityLevel::High if system CPU > 95%.
    [[nodiscard]] core::PriorityLevel select_priority(
        double score,
        core::PolicyMode mode,
        double system_cpu_percent) const noexcept;

    // Urgency classification from score.
    [[nodiscard]] core::AllocationUrgency classify_urgency(double score) const noexcept;

    // Build the human-readable reason string.
    [[nodiscard]] std::string build_reason(
        const analysis::BottleneckResult& bottleneck,
        const AllocationComponents&       components,
        core::PriorityLevel               selected_priority,
        bool                              action_suppressed,
        const char*                       suppression_reason) const;
};

} // namespace agra::allocator
