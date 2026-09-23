#include "agra/windows/windows_controller.hpp"
#include "agra/core/logger.hpp"
#include "agra/discovery/system_info.hpp"

#include <windows.h>
#include <processthreadsapi.h>
#include <psapi.h>
#include <format>
#include <algorithm>

namespace agra::windows {

namespace {

// Win32 ProcessPowerThrottling definition for EcoQoS (Win10 21H2+)
#ifndef ProcessPowerThrottling
#define ProcessPowerThrottling static_cast<PROCESS_INFORMATION_CLASS>(4)
#endif

#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif

struct LOCAL_PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
};

// Safe RAII handle for Win32 process handles
class ScopedProcessHandle {
public:
    explicit ScopedProcessHandle(HANDLE h) noexcept : handle_(h) {}
    ~ScopedProcessHandle() noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }

    ScopedProcessHandle(const ScopedProcessHandle&) = delete;
    ScopedProcessHandle& operator=(const ScopedProcessHandle&) = delete;

    ScopedProcessHandle(ScopedProcessHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    ScopedProcessHandle& operator=(ScopedProcessHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                ::CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool is_valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_{nullptr};
};

} // namespace

WindowsController::WindowsController(const core::AppConfig& config)
    : config_(config)
{
    detect_capabilities();
}

void WindowsController::detect_capabilities() {
    auto sys_caps = discovery::SystemInfoProvider::query_capabilities();
    caps_.is_elevated = sys_caps.is_process_elevated;
    caps_.cpu_sets_supported = sys_caps.cpu_sets_supported;
    caps_.eco_qos_supported = sys_caps.eco_qos_supported;
    caps_.can_set_priority = true;
    caps_.can_set_affinity = true;

    AGRA_LOG_INFO("WindowsController",
        "Capabilities initialized: Elevated={}, CPUSets={}, EcoQoS={}",
        caps_.is_elevated, caps_.cpu_sets_supported, caps_.eco_qos_supported);
}

core::PriorityLevel WindowsController::dword_to_priority_level(unsigned long priority_class) noexcept {
    switch (priority_class) {
        case IDLE_PRIORITY_CLASS:
            return core::PriorityLevel::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS:
            return core::PriorityLevel::BelowNormal;
        case NORMAL_PRIORITY_CLASS:
            return core::PriorityLevel::Normal;
        case ABOVE_NORMAL_PRIORITY_CLASS:
            return core::PriorityLevel::AboveNormal;
        case HIGH_PRIORITY_CLASS:
            return core::PriorityLevel::High;
        case REALTIME_PRIORITY_CLASS:
            // Safety: Realtime is never allowed; treat as High if encountered externally
            return core::PriorityLevel::High;
        default:
            return core::PriorityLevel::Normal;
    }
}

unsigned long WindowsController::priority_level_to_dword(core::PriorityLevel level) noexcept {
    switch (level) {
        case core::PriorityLevel::Idle:
            return IDLE_PRIORITY_CLASS;
        case core::PriorityLevel::BelowNormal:
            return BELOW_NORMAL_PRIORITY_CLASS;
        case core::PriorityLevel::Normal:
            return NORMAL_PRIORITY_CLASS;
        case core::PriorityLevel::AboveNormal:
            return ABOVE_NORMAL_PRIORITY_CLASS;
        case core::PriorityLevel::High:
            return HIGH_PRIORITY_CLASS;
        default:
            return NORMAL_PRIORITY_CLASS;
    }
}

bool WindowsController::is_process_alive(core::ProcessId pid) noexcept {
    if (pid == 0) return false;
    ScopedProcessHandle handle(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!handle.is_valid()) return false;

    DWORD exit_code = 0;
    if (::GetExitCodeProcess(handle.get(), &exit_code)) {
        return exit_code == STILL_ACTIVE;
    }
    return false;
}

core::Result<void, core::Error> WindowsController::validate_target(core::ProcessId pid) const {
    if (pid == 0 || pid == 4) {
        return core::Error::invalid_param("Cannot modify System process (PID 0 or 4).");
    }

    if (!is_process_alive(pid)) {
        return core::Error::not_found(std::format("Target process {} is not running.", pid));
    }

    // Inspect process name if accessible to verify protected list
    ScopedProcessHandle handle(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (handle.is_valid()) {
        WCHAR path_buf[MAX_PATH];
        DWORD size = MAX_PATH;
        if (::QueryFullProcessImageNameW(handle.get(), 0, path_buf, &size)) {
            std::wstring ws(path_buf);
            auto slash = ws.find_last_of(L"\\/");
            std::wstring filename = (slash == std::wstring::npos) ? ws : ws.substr(slash + 1);
            std::string name(filename.begin(), filename.end());

            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (config_.is_process_protected(lower_name)) {
                return core::Error::access_denied(
                    std::format("Process '{}' (PID {}) is protected by AGRA safety policy.", name, pid));
            }

            if (config_.is_process_user_excluded(lower_name)) {
                return core::Error::access_denied(
                    std::format("Process '{}' (PID {}) is excluded by user configuration.", name, pid));
            }
        }
    }

    return {};
}

core::Result<core::StateSnapshot, core::Error> WindowsController::capture_process_state(
    core::ProcessId pid,
    const std::string& process_name) const
{
    if (pid == 0) {
        return core::Error::invalid_param("Invalid PID 0.");
    }

    ScopedProcessHandle handle(::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE,
        pid));

    if (!handle.is_valid()) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("Failed to open process {} to capture state", pid));
    }

    core::StateSnapshot snapshot{};
    snapshot.pid = pid;
    snapshot.captured_at = std::chrono::system_clock::now();

    // Query name if not provided
    if (!process_name.empty()) {
        snapshot.process_name = process_name;
    } else {
        WCHAR path_buf[MAX_PATH];
        DWORD size = MAX_PATH;
        if (::QueryFullProcessImageNameW(handle.get(), 0, path_buf, &size)) {
            std::wstring ws(path_buf);
            auto slash = ws.find_last_of(L"\\/");
            std::wstring filename = (slash == std::wstring::npos) ? ws : ws.substr(slash + 1);
            snapshot.process_name = std::string(filename.begin(), filename.end());
        } else {
            snapshot.process_name = std::format("PID_{}", pid);
        }
    }

    // Query original priority
    DWORD prio_dword = ::GetPriorityClass(handle.get());
    if (prio_dword != 0) {
        snapshot.original_priority = dword_to_priority_level(prio_dword);
    } else {
        snapshot.original_priority = core::PriorityLevel::Normal;
    }

    // Query original affinity
    ULONG_PTR process_mask = 0;
    ULONG_PTR system_mask = 0;
    if (::GetProcessAffinityMask(handle.get(), &process_mask, &system_mask)) {
        snapshot.original_affinity = static_cast<core::AffinityMask>(process_mask);
    } else {
        snapshot.original_affinity = 0;
    }

    // Query original CPU Sets if supported
    if (caps_.cpu_sets_supported) {
        ULONG count = 0;
        // Query required buffer size
        if (!::GetProcessDefaultCpuSets(handle.get(), nullptr, 0, &count) && count > 0) {
            std::vector<ULONG> cpu_sets(count);
            if (::GetProcessDefaultCpuSets(handle.get(), cpu_sets.data(), count, &count)) {
                snapshot.original_cpu_sets.assign(cpu_sets.begin(), cpu_sets.begin() + count);
            }
        }
    }

    snapshot.valid = true;
    AGRA_LOG_DEBUG("WindowsController",
        "Captured snapshot for PID {} ({}): Priority={}, Affinity={:#x}, CPUSets count={}",
        pid, snapshot.process_name,
        core::priority_level_to_string(snapshot.original_priority),
        snapshot.original_affinity,
        snapshot.original_cpu_sets.size());

    return snapshot;
}

core::Result<void, core::Error> WindowsController::set_process_priority(
    core::ProcessId pid,
    core::PriorityLevel priority)
{
    // Realtime priority is strictly forbidden for stability and safety
    if (priority == core::PriorityLevel::High && config_.policy_mode == core::PolicyMode::Default) {
        return core::Error::access_denied("Policy mode is Default; cannot elevate to High.");
    }

    auto val_res = validate_target(pid);
    if (val_res.is_err()) {
        return val_res.error();
    }

    ScopedProcessHandle handle(::OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!handle.is_valid()) {
        DWORD err = ::GetLastError();
        AGRA_LOG_WARN("WindowsController",
            "OpenProcess(PROCESS_SET_INFORMATION) failed for PID {}: error code {}", pid, err);
        return core::Error::win32(err, std::format("Cannot open PID {} to adjust priority", pid));
    }

    DWORD dword_prio = priority_level_to_dword(priority);
    if (!::SetPriorityClass(handle.get(), dword_prio)) {
        DWORD err = ::GetLastError();
        AGRA_LOG_ERROR("WindowsController",
            "SetPriorityClass failed for PID {}: error code {}", pid, err);
        return core::Error::win32(err, std::format("SetPriorityClass failed for PID {}", pid));
    }

    // Verify change
    DWORD new_prio = ::GetPriorityClass(handle.get());
    if (new_prio != dword_prio) {
        AGRA_LOG_WARN("WindowsController",
            "Priority verification mismatch for PID {}: expected {}, got {}",
            pid, dword_prio, new_prio);
    } else {
        AGRA_LOG_INFO("WindowsController",
            "Successfully set priority of PID {} to {}",
            pid, core::priority_level_to_string(priority));
    }

    return {};
}

core::Result<void, core::Error> WindowsController::set_process_affinity(
    core::ProcessId pid,
    core::AffinityMask affinity)
{
    if (affinity == 0) {
        return core::Error::invalid_param("Affinity mask cannot be zero (all cores disabled).");
    }

    auto val_res = validate_target(pid);
    if (val_res.is_err()) {
        return val_res.error();
    }

    ScopedProcessHandle handle(::OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid));
    if (!handle.is_valid()) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("Cannot open PID {} to adjust affinity", pid));
    }

    // Verify affinity against system mask
    ULONG_PTR current_proc = 0;
    ULONG_PTR sys_mask = 0;
    if (::GetProcessAffinityMask(handle.get(), &current_proc, &sys_mask)) {
        if ((affinity & sys_mask) != affinity) {
            return core::Error::invalid_param("Affinity mask specifies cores that do not exist on the system.");
        }
    }

    if (!::SetProcessAffinityMask(handle.get(), static_cast<DWORD_PTR>(affinity))) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("SetProcessAffinityMask failed for PID {}", pid));
    }

    AGRA_LOG_INFO("WindowsController",
        "Successfully set affinity mask of PID {} to {:#x}", pid, affinity);

    return {};
}

core::Result<void, core::Error> WindowsController::set_process_cpu_sets(
    core::ProcessId pid,
    const std::vector<std::uint32_t>& cpu_set_ids)
{
    if (!caps_.cpu_sets_supported) {
        return core::Error::unsupported("CPU Sets API is not supported on this Windows build.");
    }

    auto val_res = validate_target(pid);
    if (val_res.is_err()) {
        return val_res.error();
    }

    ScopedProcessHandle handle(::OpenProcess(PROCESS_SET_LIMITED_INFORMATION, FALSE, pid));
    if (!handle.is_valid()) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("Cannot open PID {} to configure CPU Sets", pid));
    }

    const ULONG count = static_cast<ULONG>(cpu_set_ids.size());
    const ULONG* data_ptr = count > 0 ? reinterpret_cast<const ULONG*>(cpu_set_ids.data()) : nullptr;

    if (!::SetProcessDefaultCpuSets(handle.get(), data_ptr, count)) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("SetProcessDefaultCpuSets failed for PID {}", pid));
    }

    AGRA_LOG_INFO("WindowsController",
        "Configured {} CPU Sets for PID {}", count, pid);

    return {};
}

core::Result<void, core::Error> WindowsController::set_process_eco_qos(
    core::ProcessId pid,
    bool enable)
{
    if (!caps_.eco_qos_supported) {
        return core::Error::unsupported("EcoQoS / ProcessPowerThrottling is not supported on this Windows build.");
    }

    auto val_res = validate_target(pid);
    if (val_res.is_err()) {
        return val_res.error();
    }

    ScopedProcessHandle handle(::OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid));
    if (!handle.is_valid()) {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("Cannot open PID {} to adjust EcoQoS", pid));
    }

    LOCAL_PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = 1;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

    if (!::SetProcessInformation(
            handle.get(),
            ProcessPowerThrottling,
            &throttling,
            sizeof(throttling)))
    {
        DWORD err = ::GetLastError();
        return core::Error::win32(err, std::format("SetProcessInformation (EcoQoS) failed for PID {}", pid));
    }

    AGRA_LOG_INFO("WindowsController",
        "Set EcoQoS on PID {} to {}", pid, enable ? "ENABLED" : "DISABLED");

    return {};
}

} // namespace agra::windows
