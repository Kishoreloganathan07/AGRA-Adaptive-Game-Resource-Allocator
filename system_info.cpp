#include "agra/discovery/system_info.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <format>

namespace agra::discovery {

namespace {

using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);

} // namespace

SystemMemoryInfo SystemInfoProvider::query_memory() {
    SystemMemoryInfo info{};
    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);

    if (GlobalMemoryStatusEx(&mem_status)) {
        info.total_physical_mb = mem_status.ullTotalPhys / (1024 * 1024);
        info.available_physical_mb = mem_status.ullAvailPhys / (1024 * 1024);
        info.total_page_file_mb = mem_status.ullTotalPageFile / (1024 * 1024);
        info.available_page_file_mb = mem_status.ullAvailPageFile / (1024 * 1024);
        info.memory_load_percent = mem_status.dwMemoryLoad;
    }
    return info;
}

PowerStatusInfo SystemInfoProvider::query_power_status() {
    PowerStatusInfo info{};
    SYSTEM_POWER_STATUS sps{};

    if (GetSystemPowerStatus(&sps)) {
        if (sps.ACLineStatus == 1) {
            info.power_state = core::PowerState::AcPower;
            info.is_on_battery = false;
        } else if (sps.ACLineStatus == 0) {
            info.power_state = core::PowerState::Battery;
            info.is_on_battery = true;
        } else {
            info.power_state = core::PowerState::Unknown;
        }

        if (sps.BatteryLifePercent != 255) {
            info.battery_life_percent = sps.BatteryLifePercent;
        }

        info.battery_saver_active = (sps.SystemStatusFlag == 1);
    }
    return info;
}

WindowsVersionInfo SystemInfoProvider::query_windows_version() {
    WindowsVersionInfo ver{};
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(hNtdll, "RtlGetVersion"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (rtl_get_version) {
            OSVERSIONINFOEXW osvi{};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (rtl_get_version(&osvi) == 0) {
                ver.major = osvi.dwMajorVersion;
                ver.minor = osvi.dwMinorVersion;
                ver.build_number = osvi.dwBuildNumber;
                if (ver.build_number >= 22000) {
                    ver.display_name = std::format("Windows 11 (Build {})", ver.build_number);
                } else {
                    ver.display_name = std::format("Windows 10 (Build {})", ver.build_number);
                }
                return ver;
            }
        }
    }
    return ver;
}

SystemCapabilities SystemInfoProvider::query_capabilities() {
    SystemCapabilities caps{};

    // 1. Elevation check
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation{};
        DWORD cbSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            caps.is_process_elevated = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }

    // 2. CPU Sets support check (Windows 10 1607+ / Build 14393+)
    WindowsVersionInfo ver = query_windows_version();
    caps.cpu_sets_supported = (ver.build_number >= 14393);

    // 3. EcoQoS support check (Windows 11 / Windows 10 21H2+ / Build 19044+)
    caps.eco_qos_supported = (ver.build_number >= 19044);

    return caps;
}

} // namespace agra::discovery
