#include "agra/safety/safety_manager.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <csignal>
#include <format>

namespace agra::safety {

SafetyManager* SafetyManager::s_active_instance = nullptr;

namespace {

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT: {
            AGRA_LOG_WARN("SafetyManager",
                "Console termination event {} intercepted. Executing emergency rollback...", ctrl_type);
            auto* inst = SafetyManager::active_instance();
            if (inst != nullptr) {
                (void)inst->restore_all();
            }
            return TRUE;
        }
        default:
            return FALSE;
    }
}

void signal_handler(int sig) {
    AGRA_LOG_WARN("SafetyManager", "Signal {} caught. Executing emergency rollback...", sig);
    auto* inst = SafetyManager::active_instance();
    if (inst != nullptr) {
        (void)inst->restore_all();
    }
}

} // namespace

SafetyManager::SafetyManager(windows::WindowsController& controller, const core::AppConfig& config)
    : controller_(controller)
    , config_(config)
{
    s_active_instance = this;
    install_signal_handlers();
}

SafetyManager::~SafetyManager() {
    // Restore all processes on shutdown
    (void)restore_all();
    if (s_active_instance == this) {
        s_active_instance = nullptr;
    }
}

SafetyManager* SafetyManager::active_instance() {
    return s_active_instance;
}

void SafetyManager::install_signal_handlers() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (signal_handlers_installed_) return;

    if (::SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        AGRA_LOG_INFO("SafetyManager", "Registered Windows ConsoleCtrlHandler for safe rollback.");
    } else {
        AGRA_LOG_WARN("SafetyManager", "Failed to register ConsoleCtrlHandler.");
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    signal_handlers_installed_ = true;
}

core::Result<void, core::Error> SafetyManager::capture_before_modify(
    core::ProcessId pid,
    const std::string& process_name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // If we already hold a baseline snapshot for this PID, preserve the original baseline!
    if (snapshots_.find(pid) != snapshots_.end()) {
        AGRA_LOG_DEBUG("SafetyManager",
            "Baseline snapshot already exists for PID {} ({}); preserving original baseline.",
            pid, snapshots_[pid].process_name);
        return {};
    }

    auto snap_res = controller_.capture_process_state(pid, process_name);
    if (snap_res.is_err()) {
        AGRA_LOG_ERROR("SafetyManager",
            "Failed to capture pre-intervention snapshot for PID {}: {}",
            pid, snap_res.error().message());
        return snap_res.error();
    }

    snapshots_[pid] = snap_res.value();
    AGRA_LOG_INFO("SafetyManager",
        "Saved pre-intervention baseline snapshot for PID {} ({}): Priority={}, Affinity={:#x}",
        pid, snapshots_[pid].process_name,
        core::priority_level_to_string(snapshots_[pid].original_priority),
        snapshots_[pid].original_affinity);

    return {};
}

core::Result<void, core::Error> SafetyManager::restore_process(core::ProcessId pid) {
    core::StateSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = snapshots_.find(pid);
        if (it == snapshots_.end()) {
            return core::Error::not_found(
                std::format("No snapshot found for PID {} to restore.", pid));
        }
        snapshot = it->second;
    }

    if (!windows::WindowsController::is_process_alive(pid)) {
        AGRA_LOG_INFO("SafetyManager",
            "Process PID {} has already exited; removing snapshot without restoring.", pid);
        forget_process(pid);
        return {};
    }

    AGRA_LOG_INFO("SafetyManager",
        "Restoring PID {} ({}) to original baseline: Priority={}, Affinity={:#x}...",
        pid, snapshot.process_name,
        core::priority_level_to_string(snapshot.original_priority),
        snapshot.original_affinity);

    // Restore priority
    auto prio_res = controller_.set_process_priority(pid, snapshot.original_priority);
    if (prio_res.is_err()) {
        AGRA_LOG_WARN("SafetyManager",
            "Failed to restore priority for PID {}: {}", pid, prio_res.error().message());
    }

    // Restore affinity if original affinity was non-zero
    if (snapshot.original_affinity != 0) {
        auto aff_res = controller_.set_process_affinity(pid, snapshot.original_affinity);
        if (aff_res.is_err()) {
            AGRA_LOG_WARN("SafetyManager",
                "Failed to restore affinity for PID {}: {}", pid, aff_res.error().message());
        }
    }

    // Restore CPU sets if original had CPU sets
    if (!snapshot.original_cpu_sets.empty() && controller_.capabilities().cpu_sets_supported) {
        auto cset_res = controller_.set_process_cpu_sets(pid, snapshot.original_cpu_sets);
        if (cset_res.is_err()) {
            AGRA_LOG_WARN("SafetyManager",
                "Failed to restore CPU Sets for PID {}: {}", pid, cset_res.error().message());
        }
    }

    // Disable EcoQoS if it was altered
    if (controller_.capabilities().eco_qos_supported) {
        (void)controller_.set_process_eco_qos(pid, false);
    }

    forget_process(pid);
    AGRA_LOG_INFO("SafetyManager", "PID {} successfully restored to baseline.", pid);
    return {};
}

core::Result<std::uint32_t, core::Error> SafetyManager::restore_all() {
    std::vector<core::ProcessId> pids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pids.reserve(snapshots_.size());
        for (const auto& [pid, _] : snapshots_) {
            pids.push_back(pid);
        }
    }

    if (pids.empty()) {
        AGRA_LOG_DEBUG("SafetyManager", "restore_all: No active snapshots to restore.");
        return 0u;
    }

    AGRA_LOG_INFO("SafetyManager", "restore_all: Restoring {} tracked process(es)...", pids.size());
    std::uint32_t count = 0;
    for (auto pid : pids) {
        auto res = restore_process(pid);
        if (res.is_ok()) {
            ++count;
        }
    }

    AGRA_LOG_INFO("SafetyManager", "restore_all: Completed restoration of {} process(es).", count);
    return count;
}

bool SafetyManager::has_snapshot(core::ProcessId pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshots_.find(pid) != snapshots_.end();
}

std::optional<core::StateSnapshot> SafetyManager::get_snapshot(core::ProcessId pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(pid);
    if (it != snapshots_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<core::StateSnapshot> SafetyManager::all_snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<core::StateSnapshot> list;
    list.reserve(snapshots_.size());
    for (const auto& [_, snap] : snapshots_) {
        list.push_back(snap);
    }
    return list;
}

void SafetyManager::forget_process(core::ProcessId pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_.erase(pid);
}

} // namespace agra::safety
