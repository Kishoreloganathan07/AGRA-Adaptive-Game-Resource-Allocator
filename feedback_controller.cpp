#include "agra/policy/feedback_controller.hpp"
#include "agra/core/logger.hpp"

#include <format>
#include <cmath>

namespace agra::policy {

FeedbackController::FeedbackController(std::uint32_t observation_samples)
    : observation_samples_required_(observation_samples > 0 ? observation_samples : 4)
{}

void FeedbackController::reset() {
    active_intervention_ = std::nullopt;
    history_.clear();
}

void FeedbackController::record_intervention(
    core::ProcessId target_pid,
    core::PriorityLevel original_priority,
    core::PriorityLevel applied_priority,
    const core::AllocationDecision& decision,
    const core::WorkloadMetrics& baseline)
{
    InterventionRecord rec{};
    rec.intervention_id = next_id_++;
    rec.target_pid = target_pid;
    rec.applied_time = std::chrono::steady_clock::now();
    rec.original_priority = original_priority;
    rec.applied_priority = applied_priority;
    rec.bottleneck = decision.detected_bottleneck;
    rec.score = decision.allocation_score;
    rec.reason = decision.reason;

    rec.baseline_game_cpu = baseline.game_cpu_percent;
    rec.baseline_contention = baseline.contention_index;
    rec.baseline_system_cpu = baseline.system_cpu_percent;

    rec.required_observation_samples = observation_samples_required_;
    rec.observation_samples_collected = 0;
    rec.outcome = FeedbackOutcome::PendingObservation;

    active_intervention_ = rec;

    AGRA_LOG_INFO("FeedbackController",
        "Recorded intervention #{} on PID {}: {} -> {} (Baseline: Game={:.1f}%, Contention={:.2f}, Sys={:.1f}%)",
        rec.intervention_id, target_pid,
        core::priority_level_to_string(original_priority),
        core::priority_level_to_string(applied_priority),
        rec.baseline_game_cpu, rec.baseline_contention, rec.baseline_system_cpu);
}

FeedbackOutcome FeedbackController::feed_sample(const core::WorkloadMetrics& current_metrics) {
    if (!active_intervention_.has_value()) {
        return FeedbackOutcome::Neutral;
    }

    auto& rec = active_intervention_.value();
    rec.observed_game_cpu += current_metrics.game_cpu_percent;
    rec.observed_contention += current_metrics.contention_index;
    rec.observed_system_cpu += current_metrics.system_cpu_percent;
    ++rec.observation_samples_collected;

    if (rec.observation_samples_collected >= rec.required_observation_samples) {
        // Compute averages across observation window
        rec.observed_game_cpu /= rec.observation_samples_collected;
        rec.observed_contention /= rec.observation_samples_collected;
        rec.observed_system_cpu /= rec.observation_samples_collected;

        evaluate_outcome();
        FeedbackOutcome final_outcome = rec.outcome;

        history_.push_back(rec);
        active_intervention_ = std::nullopt;

        return final_outcome;
    }

    return FeedbackOutcome::PendingObservation;
}

void FeedbackController::evaluate_outcome() {
    if (!active_intervention_.has_value()) return;

    auto& rec = active_intervention_.value();
    double delta_contention = rec.observed_contention - rec.baseline_contention;
    double delta_game_cpu   = rec.observed_game_cpu - rec.baseline_game_cpu;

    // Evaluation logic:
    // 1. Harmful:
    //    - If total system is completely locked up (> 98%) while game CPU actually decreased
    //    - Or contention increased severely (> +0.30)
    if (rec.observed_system_cpu >= 98.0 && delta_game_cpu < -5.0) {
        rec.outcome = FeedbackOutcome::Harmful;
        rec.outcome_explanation = std::format(
            "Harmful: System saturation reached {:.1f}% while game CPU dropped by {:.1f}%. Rolling back.",
            rec.observed_system_cpu, std::abs(delta_game_cpu));
        AGRA_LOG_WARN("FeedbackController", "Intervention #{}: {}", rec.intervention_id, rec.outcome_explanation);
        return;
    }

    if (delta_contention > 0.35) {
        rec.outcome = FeedbackOutcome::Harmful;
        rec.outcome_explanation = std::format(
            "Harmful: Contention index spiked from {:.2f} to {:.2f}. Rolling back.",
            rec.baseline_contention, rec.observed_contention);
        AGRA_LOG_WARN("FeedbackController", "Intervention #{}: {}", rec.intervention_id, rec.outcome_explanation);
        return;
    }

    // 2. Improved:
    //    - Contention decreased by at least 0.08, OR
    //    - Game CPU increased by at least 4% under heavy demand
    if (delta_contention <= -0.08 || delta_game_cpu >= 4.0) {
        rec.outcome = FeedbackOutcome::Improved;
        rec.outcome_explanation = std::format(
            "Improved: Contention shifted {:.2f} -> {:.2f}, Game CPU shifted {:.1f}% -> {:.1f}%. Retaining policy.",
            rec.baseline_contention, rec.observed_contention,
            rec.baseline_game_cpu, rec.observed_game_cpu);
        AGRA_LOG_INFO("FeedbackController", "Intervention #{}: {}", rec.intervention_id, rec.outcome_explanation);
        return;
    }

    // 3. Neutral:
    rec.outcome = FeedbackOutcome::Neutral;
    rec.outcome_explanation = std::format(
        "Neutral: Metrics remained stable (Contention: {:.2f} -> {:.2f}, Game CPU: {:.1f}% -> {:.1f}%). Maintained.",
        rec.baseline_contention, rec.observed_contention,
        rec.baseline_game_cpu, rec.observed_game_cpu);
    AGRA_LOG_INFO("FeedbackController", "Intervention #{}: {}", rec.intervention_id, rec.outcome_explanation);
}

} // namespace agra::policy
