#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"
#include "agra/core/config.hpp"

#include <vector>
#include <cstdint>
#include <string>
#include <optional>

namespace agra::windows {

// Capability flags detected on the running Windows kernel
struct WindowsCapabilities {
    bool can_set_priority{true};
    bool can_set_affinity{true};
    bool cpu_sets_supported{false};
    bool eco_qos_supported{false};
    bool is_elevated{false};
};

class WindowsController {
public:
    explicit WindowsController(const core::AppConfig& config);
    ~WindowsController() = default;

    // Discover system capabilities at runtime
    [[nodiscard]] WindowsCapabilities capabilities() const noexcept { return caps_; }

    // Query current live state of a process
    [[nodiscard]] core::Result<core::StateSnapshot, core::Error> capture_process_state(
        core::ProcessId pid,
        const std::string& process_name = "") const;

    // Apply process base priority class
    [[nodiscard]] core::Result<void, core::Error> set_process_priority(
        core::ProcessId pid,
        core::PriorityLevel priority);

    // Apply CPU affinity mask
    [[nodiscard]] core::Result<void, core::Error> set_process_affinity(
        core::ProcessId pid,
        core::AffinityMask affinity);

    // Apply CPU Sets (soft affinity introduced in Win10 1607+)
    [[nodiscard]] core::Result<void, core::Error> set_process_cpu_sets(
        core::ProcessId pid,
        const std::vector<std::uint32_t>& cpu_set_ids);

    // Apply EcoQoS / Power Throttling to background processes (Win10 21H2+)
    [[nodiscard]] core::Result<void, core::Error> set_process_eco_qos(
        core::ProcessId pid,
        bool enable);

    // Convert Windows priority class DWORD to core::PriorityLevel
    [[nodiscard]] static core::PriorityLevel dword_to_priority_level(unsigned long priority_class) noexcept;

    // Convert core::PriorityLevel to Windows priority class DWORD
    [[nodiscard]] static unsigned long priority_level_to_dword(core::PriorityLevel level) noexcept;

    // Check if target process is alive
    [[nodiscard]] static bool is_process_alive(core::ProcessId pid) noexcept;

private:
    const core::AppConfig& config_;
    WindowsCapabilities caps_{};

    void detect_capabilities();

    // Validates target process against protection rules before any write operation
    [[nodiscard]] core::Result<void, core::Error> validate_target(core::ProcessId pid) const;
};

} // namespace agra::windows
