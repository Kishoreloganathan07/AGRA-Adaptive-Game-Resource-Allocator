#include "agra/allocator/allocation_engine.hpp"
#include "agra/core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace agra::allocator {

// ── HysteresisController ─────────────────────────────────────────────────────

HysteresisState HysteresisController::update(bool sample_qualifies) noexcept {
    if (state_ == HysteresisState::Normal) {
        if (sample_qualifies) {
            ++enter_count_;
            exit_count_ = 0;
            if (enter_count_ >= enter_threshold) {
                state_       = HysteresisState::Elevated;
                enter_count_ = 0;
                AGRA_LOG_DEBUG("Hysteresis", "→ Elevated (after {} qualifying samples)", enter_threshold);
            }
        } else {
            enter_count_ = 0;
        }
    } else { // Elevated
        if (!sample_qualifies) {
            ++exit_count_;
            enter_count_ = 0;
            if (exit_count_ >= exit_threshold) {
                state_      = HysteresisState::Normal;
                exit_count_ = 0;
                AGRA_LOG_DEBUG("Hysteresis", "→ Normal (after {} non-qualifying samples)", exit_threshold);
            }
        } else {
            exit_count_ = 0;
        }
    }
    return state_;
}

void HysteresisController::reset() noexcept {
    state_       = HysteresisState::Normal;
    enter_count_ = 0;
    exit_count_  = 0;
}

// ── AllocationEngine ──────────────────────────────────────────────────────────

AllocationEngine::AllocationEngine(const core::AppConfig& config)
    : config_(config)
    , hysteresis_(config.hysteresis_enter_samples, config.hysteresis_exit_samples)
{}

void AllocationEngine::reset() {
    hysteresis_.reset();
    currently_boosted_  = false;
    in_forced_cooldown_ = false;
    last_components_    = AllocationComponents{};
    AGRA_LOG_INFO("AllocationEngine", "State reset.");
}

// ── Score Calculation ─────────────────────────────────────────────────────────
//
// AllocationScore (0–100):
//
//   demand_component         = estimated_demand * 40.0
//   contention_component     = contention_ewma  * 25.0
//   stability_component      = (1 - stability_score) * 15.0   ← high = spiky
//   spike_urgency_component  = 10.0 if transient spike, else 0.0
//   confidence_component     = classifier_confidence * 10.0
//   fairness_penalty         = if system_cpu > 90% → 20.0 penalty (system overloaded)
//
// This is intentionally transparent and unit-testable.
// Weights are documented constants; no magic numbers in the logic.

static constexpr double kDemandWeight      = 60.0;
static constexpr double kContentionWeight  = 15.0;
static constexpr double kStabilityWeight   = 15.0;   // stability requirement weight
static constexpr double kSpikeWeight       = 10.0;
static constexpr double kConfidenceWeight  = 15.0;
static constexpr double kSystemOverloadThreshold = 90.0; // % total CPU

AllocationComponents AllocationEngine::compute_score(
    const analysis::BottleneckResult&  bottleneck,
    const analysis::WorkloadAnalysis&  a,
    const core::WorkloadMetrics&       m) const
{
    AllocationComponents c{};

    c.demand_component     = a.estimated_demand   * kDemandWeight;
    c.contention_component = a.contention_ewma    * kContentionWeight;

    // Stability requirement: sustained heavy gaming workloads require steady
    // scheduling preference to prevent frame stutter. For spiky/transient workloads,
    // urgency is captured in spike_urgency_component.
    if (a.is_sustained_heavy) {
        c.stability_component = a.stability_score * kStabilityWeight;
    } else if (a.is_transient_spike) {
        c.stability_component = 0.0;
    } else {
        c.stability_component = (1.0 - a.stability_score) * 10.0;
    }

    c.spike_urgency_component = a.is_transient_spike ? kSpikeWeight : 0.0;

    c.confidence_component = bottleneck.confidence * kConfidenceWeight;

    // Fairness penalty: if the total system CPU is already saturated, boosting
    // the game further could starve critical OS services.
    if (m.system_cpu_percent >= kSystemOverloadThreshold) {
        c.fairness_penalty = std::clamp(2.0 + (m.system_cpu_percent - kSystemOverloadThreshold) * 1.5, 2.0, 15.0);
    }

    double raw = c.demand_component
               + c.contention_component
               + c.stability_component
               + c.spike_urgency_component
               + c.confidence_component
               - c.fairness_penalty;

    c.final_score = std::clamp(raw, 0.0, 100.0);
    return c;
}

// ── Priority Selection ────────────────────────────────────────────────────────
//
// Score bands → PriorityLevel:
//
//   < 35        → Normal          (minimal intervention)
//   35 – 59     → AboveNormal     (moderate, all modes)
//   60 – 74     → AboveNormal     (elevated, Adaptive or Performance only)
//   >= 75       → High            (Performance mode only; Adaptive caps at AboveNormal)
//
// Hard safety limits:
//   • Default and Conservative modes: never exceed AboveNormal.
//   • Any mode: never High if system CPU > 95% (already severely loaded).
//   • REALTIME priority class is permanently excluded.

static constexpr double kScoreAboveNormalLow  = 35.0;
static constexpr double kScoreAboveNormalHigh = 60.0;
static constexpr double kScoreHighDemand      = 75.0;
static constexpr double kSystemCpuHighLimit   = 95.0;

core::PriorityLevel AllocationEngine::select_priority(
    double score, core::PolicyMode mode, double system_cpu) const noexcept
{
    using PL = core::PriorityLevel;
    using PM = core::PolicyMode;

    // Hard safety: system already near-saturated
    const bool system_overloaded = (system_cpu >= kSystemCpuHighLimit);

    if (score < kScoreAboveNormalLow) {
        return PL::Normal;
    }
    if (score < kScoreAboveNormalHigh || system_overloaded) {
        // AboveNormal is permitted in any mode; it is the minimum boost step.
        return PL::AboveNormal;
    }
    if (score >= kScoreHighDemand && mode == PM::Performance && !system_overloaded) {
        // High priority: only in Performance mode, and only when system has headroom.
        return PL::High;
    }
    // Adaptive and Conservative: cap at AboveNormal regardless of score.
    return PL::AboveNormal;
}

// ── Urgency ───────────────────────────────────────────────────────────────────

core::AllocationUrgency AllocationEngine::classify_urgency(double score) const noexcept {
    if (score >= 75.0) return core::AllocationUrgency::Immediate;
    if (score >= 60.0) return core::AllocationUrgency::High;
    if (score >= 35.0) return core::AllocationUrgency::Medium;
    return core::AllocationUrgency::Low;
}

// ── Reason Builder ────────────────────────────────────────────────────────────

std::string AllocationEngine::build_reason(
    const analysis::BottleneckResult& bottleneck,
    const AllocationComponents&       c,
    core::PriorityLevel               selected_priority,
    bool                              action_suppressed,
    const char*                       suppression_reason) const
{
    std::ostringstream os;

    os << std::format("Bottleneck: {} (conf {:.0f}%). ",
        core::bottleneck_type_to_string(bottleneck.type),
        bottleneck.confidence * 100.0);

    os << std::format("Score: {:.1f}/100 "
        "[demand={:.1f} contention={:.1f} stability={:.1f} spike={:.1f} conf={:.1f} penalty=-{:.1f}]. ",
        c.final_score,
        c.demand_component, c.contention_component,
        c.stability_component, c.spike_urgency_component,
        c.confidence_component, c.fairness_penalty);

    if (action_suppressed) {
        os << std::format("Action suppressed: {}. No policy change applied.", suppression_reason);
    } else {
        os << std::format("Decision: set priority to {}.",
            core::priority_level_to_string(selected_priority));
    }

    return os.str();
}

// ── Primary decide() ──────────────────────────────────────────────────────────

core::AllocationDecision AllocationEngine::decide(
    core::ProcessId                    target_pid,
    const analysis::BottleneckResult&  bottleneck,
    const analysis::WorkloadAnalysis&  analysis,
    const core::WorkloadMetrics&       metrics)
{
    using namespace std::chrono;

    core::AllocationDecision decision{};
    decision.target_pid         = target_pid;
    decision.active_mode        = config_.policy_mode;
    decision.detected_bottleneck = bottleneck.type;
    decision.decision_time      = steady_clock::now();

    // ── 1. Compute score ──────────────────────────────────────────────────────
    last_components_ = compute_score(bottleneck, analysis, metrics);
    decision.allocation_score = last_components_.final_score;
    decision.confidence       = bottleneck.confidence;
    decision.urgency          = classify_urgency(last_components_.final_score);

    // ── 2. Default mode: read-only, no intervention ───────────────────────────
    if (config_.policy_mode == core::PolicyMode::Default) {
        decision.target_priority = core::PriorityLevel::Normal;
        decision.requires_action = false;
        decision.reason = build_reason(bottleneck, last_components_,
                                       core::PriorityLevel::Normal,
                                       true, "policy mode is Default (read-only)");
        AGRA_LOG_DEBUG("AllocationEngine", "[PID {}] Default mode — no intervention. Score={:.1f}",
                        target_pid, last_components_.final_score);
        return decision;
    }

    // ── 3. Check if bottleneck requires action ────────────────────────────────
    if (!bottleneck.requires_action) {
        decision.target_priority = core::PriorityLevel::Normal;
        decision.requires_action = false;
        decision.reason = build_reason(bottleneck, last_components_,
                                       core::PriorityLevel::Normal,
                                       true, "classifier did not flag requires_action");
        // Hysteresis: this sample does not qualify for elevation
        hysteresis_.update(false);
        return decision;
    }

    // ── 4. Forced cooldown check ──────────────────────────────────────────────
    auto now = steady_clock::now();
    if (in_forced_cooldown_) {
        if (now < cooldown_end_time_) {
            decision.target_priority = core::PriorityLevel::Normal;
            decision.requires_action = false;
            auto remaining_ms = duration_cast<milliseconds>(cooldown_end_time_ - now).count();
            decision.reason = build_reason(bottleneck, last_components_,
                core::PriorityLevel::Normal, true,
                std::format("forced cooldown active ({} ms remaining)", remaining_ms).c_str());
            hysteresis_.update(false);
            AGRA_LOG_DEBUG("AllocationEngine", "[PID {}] In forced cooldown ({} ms left).",
                            target_pid, remaining_ms);
            return decision;
        } else {
            // Cooldown expired — resume normal operation
            in_forced_cooldown_  = false;
            currently_boosted_   = false;
            hysteresis_.reset();
            AGRA_LOG_INFO("AllocationEngine", "[PID {}] Forced cooldown expired. Resuming.", target_pid);
        }
    }

    // ── 5. Hysteresis ─────────────────────────────────────────────────────────
    // This sample qualifies for elevation (bottleneck.requires_action == true).
    // The hysteresis controller decides whether we have enough consecutive
    // qualifying samples to actually commit the boost.
    bool sample_qualifies = (last_components_.final_score >= kScoreAboveNormalLow);
    HysteresisState hyst = hysteresis_.update(sample_qualifies);

    if (hyst == HysteresisState::Normal) {
        // Not yet enough consecutive qualifying samples
        decision.target_priority = core::PriorityLevel::Normal;
        decision.requires_action = false;
        decision.reason = build_reason(bottleneck, last_components_,
            core::PriorityLevel::Normal, true,
            std::format("hysteresis not met ({}/{} entry samples)",
                hysteresis_.enter_count(),
                config_.hysteresis_enter_samples).c_str());
        AGRA_LOG_DEBUG("AllocationEngine", "[PID {}] Hysteresis pending {}/{}.",
            target_pid, hysteresis_.enter_count(), config_.hysteresis_enter_samples);
        return decision;
    }

    // ── 6. Select priority ────────────────────────────────────────────────────
    core::PriorityLevel prio = select_priority(
        last_components_.final_score,
        config_.policy_mode,
        metrics.system_cpu_percent);

    // ── 7. Starvation prevention ──────────────────────────────────────────────
    // If background CPU (excluding the game) is already very low, raising the
    // game priority further risks completely starving the rest of the system.
    // In that case, cap at AboveNormal and log a starvation-prevention event.
    if (config_.prevent_starvation && prio == core::PriorityLevel::High) {
        const double available_for_background = (std::max)(0.0,
            100.0 - metrics.game_cpu_percent);
        if (available_for_background < 10.0) {
            prio = core::PriorityLevel::AboveNormal;
            AGRA_LOG_WARN("AllocationEngine",
                "[PID {}] Starvation prevention: capped at AboveNormal "
                "(background headroom only {:.1f}%).",
                target_pid, available_for_background);
        }
    }

    // ── 8. Boost timing / forced-cooldown accounting ──────────────────────────
    if (!currently_boosted_) {
        currently_boosted_ = true;
        boost_start_time_  = now;
        AGRA_LOG_INFO("AllocationEngine", "[PID {}] Boost session started.", target_pid);
    }

    auto boost_duration_sec = duration_cast<seconds>(now - boost_start_time_).count();
    if (boost_duration_sec >= static_cast<long long>(config_.max_consecutive_boost_seconds)) {
        // Force a cooldown period to reset the system to normal baseline
        in_forced_cooldown_  = true;
        currently_boosted_   = false;
        cooldown_end_time_   = now + milliseconds(config_.cooldown_period_ms);
        hysteresis_.reset();

        decision.target_priority = core::PriorityLevel::Normal;
        decision.requires_action = false;
        decision.reason = build_reason(bottleneck, last_components_,
            core::PriorityLevel::Normal, true,
            std::format("max boost duration ({} s) reached — entering {}ms cooldown",
                config_.max_consecutive_boost_seconds,
                config_.cooldown_period_ms).c_str());
        AGRA_LOG_WARN("AllocationEngine",
            "[PID {}] Max boost duration reached. Entering {} ms forced cooldown.",
            target_pid, config_.cooldown_period_ms);
        return decision;
    }

    // ── 9. Commit decision ────────────────────────────────────────────────────
    decision.target_priority = prio;
    decision.requires_action = true;
    decision.expected_effect = std::format(
        "Set process priority to {} to improve CPU scheduling preference. "
        "Background processes at Normal/BelowNormal will yield to the game workload. "
        "Windows kernel remains responsible for actual scheduling.",
        core::priority_level_to_string(prio));
    decision.reason = build_reason(bottleneck, last_components_, prio, false, nullptr);

    AGRA_LOG_INFO("AllocationEngine",
        "[PID {}] Decision: {} | Score={:.1f} | Urgency={} | Confidence={:.2f}",
        target_pid,
        core::priority_level_to_string(prio),
        last_components_.final_score,
        core::allocation_urgency_to_string(decision.urgency),
        bottleneck.confidence);

    return decision;
}

} // namespace agra::allocator
