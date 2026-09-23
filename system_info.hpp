#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <string>
#include <cstdint>

namespace agra::discovery {

struct SystemMemoryInfo {
    std::uint64_t total_physical_mb{0};
    std::uint64_t available_physical_mb{0};
    std::uint64_t total_page_file_mb{0};
    std::uint64_t available_page_file_mb{0};
    std::uint32_t memory_load_percent{0};
};

struct PowerStatusInfo {
    core::PowerState power_state{core::PowerState::Unknown};
    bool is_on_battery{false};
    std::uint8_t battery_life_percent{100};
    bool battery_saver_active{false};
};

struct WindowsVersionInfo {
    std::uint32_t major{10};
    std::uint32_t minor{0};
    std::uint32_t build_number{0};
    std::string display_name{"Windows 10/11"};
};

struct SystemCapabilities {
    bool cpu_sets_supported{false};
    bool eco_qos_supported{false};
    bool is_process_elevated{false};
};

class SystemInfoProvider {
public:
    static SystemMemoryInfo query_memory();
    static PowerStatusInfo query_power_status();
    static WindowsVersionInfo query_windows_version();
    static SystemCapabilities query_capabilities();
};

} // namespace agra::discovery
