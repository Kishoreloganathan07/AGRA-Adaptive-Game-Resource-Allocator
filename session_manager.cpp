#include "agra/session/session_manager.hpp"
#include "agra/core/logger.hpp"
#include "agra/discovery/process_enumerator.hpp"

#include <format>

namespace agra::session {

SessionManager::SessionManager(core::ConfigManager& config_mgr)
    : config_mgr_(config_mgr)
    , windows_ctrl_(config_mgr.get())
    , safety_mgr_(windows_ctrl_, config_mgr.get())
    , monitoring_engine_(config_mgr.get())
    , analyzer_(config_mgr.get())
    , classifier_(config_mgr.get())
    , allocation_engine_(config_mgr.get())
    , policy_engine_(windows_ctrl_, safety_mgr_, config_mgr.get())
{
    // Subscribe to monitoring ticks
    sample_sub_token_ = core::EventBus::instance().subscribe<core::WorkloadSampleEvent>(
        [this](const core::WorkloadSampleEvent& evt) {
            on_workload_sample(evt.metrics);
        });

    // Subscribe to game exit events
    exit_sub_token_ = core::EventBus::instance().subscribe<core::GameExitedEvent>(
        [this](const core::GameExitedEvent& evt) {
            on_game_exited(evt.pid);
        });

    AGRA_LOG_INFO("SessionManager", "SessionManager initialized.");
}

SessionManager::~SessionManager() {
    stop_session();
    core::EventBus::instance().unsubscribe(sample_sub_token_);
    core::EventBus::instance().unsubscribe(exit_sub_token_);
}

core::Result<void, core::Error> SessionManager::start_session(
    core::ProcessId pid,
    const std::string& game_name)
{
    if (pid == 0) {
        return core::Error::invalid_param("Target PID cannot be 0.");
    }

    if (!windows::WindowsController::is_process_alive(pid)) {
        return core::Error::not_found(std::format("Target process {} is not running.", pid));
    }

    // Stop existing session if active
    if (is_active_.load()) {
        stop_session();
    }

    target_pid_ = pid;
    target_name_ = game_name.empty() ? std::format("PID_{}", pid) : game_name;
    session_start_time_ = std::chrono::steady_clock::now();
    ticks_processed_ = 0;

    // Reset analyzers and decision state
    analyzer_.reset();
    allocation_engine_.reset();
    policy_engine_.reset();

    // Start monitoring engine
    auto mon_res = monitoring_engine_.start(pid);
    if (mon_res.is_err()) {
        AGRA_LOG_ERROR("SessionManager",
            "Failed to start monitoring engine for PID {}: {}",
            pid, mon_res.error().message());
        return mon_res.error();
    }

    is_active_.store(true);
    AGRA_LOG_INFO("SessionManager",
        "Resource allocation session started for target: {} (PID {})",
        target_name_, pid);

    return {};
}

core::Result<bool, core::Error> SessionManager::auto_detect_and_start() {
    game::GameDetector detector(config_mgr_.get());
    auto games = detector.scan_for_games();
    if (games.empty()) {
        return false;
    }

    // Pick candidate with highest confidence
    const auto& best_game = games.front();
    auto start_res = start_session(best_game.pid, best_game.title);
    if (start_res.is_err()) {
        return start_res.error();
    }

    return true;
}

void SessionManager::stop_session() {
    if (!is_active_.exchange(false)) {
        return;
    }

    AGRA_LOG_INFO("SessionManager",
        "Stopping session for PID {} ({}). Restoring all policies...",
        target_pid_, target_name_);

    monitoring_engine_.stop();
    policy_engine_.reset();
    (void)safety_mgr_.restore_all();

    target_pid_ = 0;
    target_name_.clear();
}

void SessionManager::restore_all() {
    AGRA_LOG_INFO("SessionManager", "Emergency restore requested by user/system.");
    policy_engine_.reset();
    (void)safety_mgr_.restore_all();
}

void SessionManager::set_safe_mode(bool enabled) {
    safe_mode_.store(enabled);
    AGRA_LOG_INFO("SessionManager", "Safe mode {}", enabled ? "ENABLED" : "DISABLED");
    if (enabled && is_active_.load()) {
        // In safe mode, immediately revert any elevated policy
        policy_engine_.reset();
    }
}

void SessionManager::on_workload_sample(const core::WorkloadMetrics& metrics) {
    if (!is_active_.load()) return;

    ++ticks_processed_;

    // 1. Analyze workload dynamics (EWMA, variance, stability, spikes)
    analyzer_.process_sample(metrics);
    auto analysis = analyzer_.current_analysis();

    // 2. Classify bottleneck
    auto bottleneck = classifier_.classify(analysis, metrics);

    // 3. Compute adaptive allocation decision
    auto decision = allocation_engine_.decide(target_pid_, bottleneck, analysis, metrics);

    // In safe mode, force decision to read-only
    if (safe_mode_.load()) {
        decision.requires_action = false;
        decision.reason += " [Action suppressed: Safe Mode active]";
    }

    // 4. Apply policy via resource policy engine (unless suppressed)
    if (decision.requires_action) {
        (void)policy_engine_.apply_decision(target_pid_, target_name_, decision, metrics);
    }

    // 5. Closed-loop feedback verification
    policy_engine_.on_monitoring_sample(metrics);

    // 6. Background contention management (every ~10 ticks to avoid excessive enumerations)
    if (ticks_processed_ % 8 == 0 && metrics.contention_index > config_mgr_.get().contention_threshold) {
        auto all_procs = discovery::ProcessEnumerator::enumerate_all(config_mgr_.get());
        policy_engine_.manage_background_contention(target_pid_, all_procs, metrics.contention_index);
    }

    // Update thread-safe snapshot
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        latest_metrics_ = metrics;
        latest_analysis_ = analysis;
        latest_bottleneck_ = bottleneck;
        latest_decision_ = decision;
    }
}

void SessionManager::on_game_exited(core::ProcessId pid) {
    if (is_active_.load() && pid == target_pid_) {
        AGRA_LOG_WARN("SessionManager",
            "Target game process {} (PID {}) has terminated. Gracefully ending session.",
            target_name_, pid);
        stop_session();
    }
}

SessionSnapshot SessionManager::snapshot() const {
    SessionSnapshot s{};
    s.is_active = is_active_.load();
    s.safe_mode = safe_mode_.load();
    s.target_pid = target_pid_;
    s.target_name = target_name_;
    s.active_mode = config_mgr_.get().policy_mode;
    s.start_time = session_start_time_;
    s.ticks_processed = ticks_processed_;

    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        s.latest_metrics = latest_metrics_;
        s.latest_analysis = latest_analysis_;
        s.latest_bottleneck = latest_bottleneck_;
        s.latest_decision = latest_decision_;
    }

    s.latest_policy = policy_engine_.status();
    s.latest_overhead = monitoring_engine_.get_overhead_stats();

    return s;
}

} // namespace agra::session
