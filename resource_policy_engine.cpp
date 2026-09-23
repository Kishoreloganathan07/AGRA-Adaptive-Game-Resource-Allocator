#include "agra/policy/resource_policy_engine.hpp"
#include "agra/core/logger.hpp"
#include "agra/discovery/process_enumerator.hpp"

#include <algorithm>
#include <format>

namespace agra::policy {

ResourcePolicyEngine::ResourcePolicyEngine(
    windows::WindowsController& controller,
    safety::SafetyManager& safety,
    const core::AppConfig& config)
    : controller_(controller)
    , safety_(safety)
    , config_(config)
    , feedback_(4)
{}

void ResourcePolicyEngine::reset() {
    restore_background_processes();
    if (current_target_pid_ != 0) {
        (void)revert_target(current_target_pid_);
    }
    feedback_.reset();
    current_target_pid_ = 0;
    current_target_name_.clear();
    current_applied_priority_ = core::PriorityLevel::Normal;
    is_boosted_ = false;
    throttled_pids_.clear();
    last_explanation_.clear();
}

core::Result<void, core::Error> ResourcePolicyEngine::apply_decision(
    core::ProcessId target_pid,
    const std::string& target_name,
    const core::AllocationDecision& decision,
    const core::WorkloadMetrics& current_metrics)
{
    if (target_pid == 0) {
        return core::Error::invalid_param("Target PID cannot be 0.");
    }

    current_target_pid_ = target_pid;
    current_target_name_ = target_name;
    last_explanation_ = decision.reason;

    // If decision does not require action (e.g. read-only, idle, GPU-bound),
    // revert any prior boost if currently elevated
    if (!decision.requires_action) {
        if (is_boosted_) {
            AGRA_LOG_INFO("ResourcePolicyEngine",
                "Decision no longer requires action. Reverting PID {} to baseline...", target_pid);
            (void)revert_target(target_pid);
        }
        return {};
    }

    // Safety: Capture pre-intervention baseline before any Win32 modification
    auto snap_res = safety_.capture_before_modify(target_pid, target_name);
    if (snap_res.is_err()) {
        AGRA_LOG_WARN("ResourcePolicyEngine",
            "Could not capture baseline for PID {}; falling back to safe mode: {}",
            target_pid, snap_res.error().message());
        return snap_res.error();
    }

    auto baseline_snap = safety_.get_snapshot(target_pid);
    core::PriorityLevel orig_prio = baseline_snap ? baseline_snap->original_priority : core::PriorityLevel::Normal;

    // Apply priority class if different from current
    if (decision.target_priority != current_applied_priority_ || !is_boosted_) {
        auto prio_res = controller_.set_process_priority(target_pid, decision.target_priority);
        if (prio_res.is_err()) {
            AGRA_LOG_WARN("ResourcePolicyEngine",
                "Failed to apply priority {} to PID {}: {}",
                core::priority_level_to_string(decision.target_priority),
                target_pid, prio_res.error().message());
            return prio_res.error();
        }

        current_applied_priority_ = decision.target_priority;
        is_boosted_ = (decision.target_priority != orig_prio);

        // Record for closed-loop feedback observation
        feedback_.record_intervention(target_pid, orig_prio, decision.target_priority, decision, current_metrics);
    }

    // Apply affinity if specified and non-zero
    if (decision.target_affinity != 0) {
        (void)controller_.set_process_affinity(target_pid, decision.target_affinity);
    }

    // Apply CPU sets if specified
    if (!decision.target_cpu_sets.empty() && controller_.capabilities().cpu_sets_supported) {
        (void)controller_.set_process_cpu_sets(target_pid, decision.target_cpu_sets);
    }

    return {};
}

void ResourcePolicyEngine::on_monitoring_sample(const core::WorkloadMetrics& current_metrics) {
    if (!feedback_.is_observing()) return;

    FeedbackOutcome outcome = feedback_.feed_sample(current_metrics);
    if (outcome == FeedbackOutcome::Harmful) {
        AGRA_LOG_WARN("ResourcePolicyEngine",
            "Feedback loop detected HARMFUL outcome on PID {}. Rolling back immediately!",
            current_target_pid_);
        if (current_target_pid_ != 0) {
            (void)revert_target(current_target_pid_);
        }
    }
}

core::Result<void, core::Error> ResourcePolicyEngine::revert_target(core::ProcessId target_pid) {
    if (target_pid == 0) return {};

    auto res = safety_.restore_process(target_pid);
    current_applied_priority_ = core::PriorityLevel::Normal;
    is_boosted_ = false;
    return res;
}

void ResourcePolicyEngine::manage_background_contention(
    core::ProcessId target_pid,
    const std::vector<discovery::DiscoveredProcess>& all_processes,
    double contention_index)
{
    if (!config_.allow_background_throttling) return;

    // Only intervene if contention is high
    if (contention_index < config_.contention_threshold) {
        if (!throttled_pids_.empty()) {
            restore_background_processes();
        }
        return;
    }

    // Sort processes by working set (proxy for resource weight) descending
    std::vector<discovery::DiscoveredProcess> candidates;
    for (const auto& p : all_processes) {
        if (p.pid == target_pid || p.pid == 0 || p.pid == 4) continue;
        if (p.working_set_mb < 10) continue; // ignore very small processes

        if (p.is_system_protected || p.is_user_excluded) continue;

        std::string lower = p.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (config_.is_process_protected(lower) || config_.is_process_user_excluded(lower)) continue;

        candidates.push_back(p);
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.working_set_mb > b.working_set_mb;
    });

    // Throttle up to 3 non-critical background processes using BelowNormal or EcoQoS
    size_t limit = (std::min)(size_t(3), candidates.size());
    for (size_t i = 0; i < limit; ++i) {
        auto pid = candidates[i].pid;
        if (std::find(throttled_pids_.begin(), throttled_pids_.end(), pid) != throttled_pids_.end()) {
            continue; // already throttled
        }

        auto snap_res = safety_.capture_before_modify(pid, candidates[i].name);
        if (snap_res.is_err()) continue;

        // Apply EcoQoS if supported, else BelowNormal priority
        if (controller_.capabilities().eco_qos_supported) {
            (void)controller_.set_process_eco_qos(pid, true);
        } else {
            (void)controller_.set_process_priority(pid, core::PriorityLevel::BelowNormal);
        }

        throttled_pids_.push_back(pid);
        AGRA_LOG_INFO("ResourcePolicyEngine",
            "Applied background throttling to PID {} ({}, RSS={} MB)",
            pid, candidates[i].name, candidates[i].working_set_mb);
    }
}

void ResourcePolicyEngine::restore_background_processes() {
    for (auto pid : throttled_pids_) {
        (void)safety_.restore_process(pid);
    }
    throttled_pids_.clear();
}

PolicyStatus ResourcePolicyEngine::status() const {
    PolicyStatus s{};
    s.active_mode = config_.policy_mode;
    s.game_priority = current_applied_priority_;
    s.is_game_boosted = is_boosted_;
    s.throttled_background_count = static_cast<std::uint32_t>(throttled_pids_.size());
    if (!feedback_.history().empty()) {
        s.last_feedback_outcome = feedback_.history().back().outcome;
    }
    s.current_explanation = last_explanation_;
    return s;
}

} // namespace agra::policy
