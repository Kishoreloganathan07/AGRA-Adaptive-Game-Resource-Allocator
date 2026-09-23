#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/core/error.hpp"
#include "agra/core/event_bus.hpp"
#include "agra/windows/windows_controller.hpp"
#include "agra/safety/safety_manager.hpp"
#include "agra/monitoring/monitoring_engine.hpp"
#include "agra/analysis/workload_analyzer.hpp"
#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/allocator/allocation_engine.hpp"
#include "agra/policy/resource_policy_engine.hpp"
#include "agra/game/game_detector.hpp"

#include <memory>
#include <mutex>
#include <atomic>
#include <string>

namespace agra::session {

struct SessionSnapshot {
    bool is_active{false};
    bool safe_mode{false};
    core::ProcessId target_pid{0};
    std::string target_name;
    core::PolicyMode active_mode{core::PolicyMode::Default};

    core::WorkloadMetrics latest_metrics{};
    analysis::WorkloadAnalysis latest_analysis{};
    analysis::BottleneckResult latest_bottleneck{};
    core::AllocationDecision latest_decision{};
    policy::PolicyStatus latest_policy{};
    monitoring::AgraOverheadStats latest_overhead{};

    std::uint64_t ticks_processed{0};
    core::TimePoint start_time{std::chrono::steady_clock::now()};
};

class SessionManager {
public:
    explicit SessionManager(core::ConfigManager& config_mgr);
    ~SessionManager();

    // Disable copy/move
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // Attach and start optimizing target game process
    [[nodiscard]] core::Result<void, core::Error> start_session(
        core::ProcessId pid,
        const std::string& game_name = "");

    // Automatically scan for game and start session if detected
    [[nodiscard]] core::Result<bool, core::Error> auto_detect_and_start();

    // Stop active session and restore all modified processes to baseline
    void stop_session();

    // Immediately restore all processes (emergency rollback)
    void restore_all();

    // Enable/disable read-only safe mode
    void set_safe_mode(bool enabled);

    // Thread-safe snapshot of the complete live session state
    [[nodiscard]] SessionSnapshot snapshot() const;

    // Accessors for subsystems
    [[nodiscard]] const core::AppConfig& config() const noexcept { return config_mgr_.get(); }
    [[nodiscard]] windows::WindowsController& windows_ctrl() noexcept { return windows_ctrl_; }
    [[nodiscard]] safety::SafetyManager& safety_mgr() noexcept { return safety_mgr_; }

private:
    core::ConfigManager& config_mgr_;
    windows::WindowsController windows_ctrl_;
    safety::SafetyManager safety_mgr_;
    monitoring::MonitoringEngine monitoring_engine_;
    analysis::WorkloadAnalyzer analyzer_;
    analysis::BottleneckClassifier classifier_;
    allocator::AllocationEngine allocation_engine_;
    policy::ResourcePolicyEngine policy_engine_;

    std::atomic<bool> is_active_{false};
    std::atomic<bool> safe_mode_{false};
    core::ProcessId target_pid_{0};
    std::string target_name_;
    core::TimePoint session_start_time_{std::chrono::steady_clock::now()};
    std::uint64_t ticks_processed_{0};

    mutable std::mutex snapshot_mutex_;
    core::WorkloadMetrics latest_metrics_{};
    analysis::WorkloadAnalysis latest_analysis_{};
    analysis::BottleneckResult latest_bottleneck_{};
    core::AllocationDecision latest_decision_{};

    core::EventBus::SubscriptionId sample_sub_token_{0};
    core::EventBus::SubscriptionId exit_sub_token_{0};

    void on_workload_sample(const core::WorkloadMetrics& metrics);
    void on_game_exited(core::ProcessId pid);
};

} // namespace agra::session
