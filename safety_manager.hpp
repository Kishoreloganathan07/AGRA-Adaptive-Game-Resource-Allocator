#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"
#include "agra/core/config.hpp"
#include "agra/windows/windows_controller.hpp"

#include <unordered_map>
#include <mutex>
#include <vector>
#include <string>

namespace agra::safety {

class SafetyManager {
public:
    explicit SafetyManager(windows::WindowsController& controller, const core::AppConfig& config);
    ~SafetyManager();

    // Disable copy/move
    SafetyManager(const SafetyManager&) = delete;
    SafetyManager& operator=(const SafetyManager&) = delete;

    // Capture baseline snapshot before any modification.
    // Idempotent: if a snapshot already exists for this PID in this session,
    // the original baseline is retained and not overwritten.
    [[nodiscard]] core::Result<void, core::Error> capture_before_modify(
        core::ProcessId pid,
        const std::string& process_name = "");

    // Restore a single process to its original baseline state
    [[nodiscard]] core::Result<void, core::Error> restore_process(core::ProcessId pid);

    // Restore all currently tracked processes to their original baseline states
    [[nodiscard]] core::Result<std::uint32_t, core::Error> restore_all();

    // Check if a PID has a captured snapshot
    [[nodiscard]] bool has_snapshot(core::ProcessId pid) const;

    // Retrieve snapshot for inspection
    [[nodiscard]] std::optional<core::StateSnapshot> get_snapshot(core::ProcessId pid) const;

    // Retrieve all active snapshots
    [[nodiscard]] std::vector<core::StateSnapshot> all_snapshots() const;

    // Clear tracked snapshot without restoring (e.g. if process exited)
    void forget_process(core::ProcessId pid);

    // Install process exit & console break handlers for guaranteed restoration
    void install_signal_handlers();

    // Singleton access for emergency restoration from global signal/crash handlers
    static SafetyManager* active_instance();

private:
    windows::WindowsController& controller_;
    const core::AppConfig& config_;

    mutable std::mutex mutex_;
    std::unordered_map<core::ProcessId, core::StateSnapshot> snapshots_;
    bool signal_handlers_installed_{false};

    static SafetyManager* s_active_instance;
};

} // namespace agra::safety
