#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace agra::core {

struct AppConfig {
    // Monitoring intervals
    std::uint32_t sampling_interval_ms{250}; // 100 - 1000 ms

    // Policy & Allocation
    PolicyMode policy_mode{PolicyMode::Adaptive};
    double min_confidence_threshold{0.65};    // 0.1 - 1.0
    double high_demand_cpu_threshold{75.0};   // Percent (enter high state)
    double low_demand_cpu_threshold{40.0};    // Percent (exit high state)
    double contention_threshold{0.35};        // 0.0 - 1.0

    // Hysteresis & Fairness
    std::uint32_t hysteresis_enter_samples{3};
    std::uint32_t hysteresis_exit_samples{5};
    std::uint32_t cooldown_period_ms{3000};
    bool allow_background_throttling{true};
    bool prevent_starvation{true};
    std::uint32_t max_consecutive_boost_seconds{300}; // Cooldown forced after 5 minutes

    // Safety & Protected lists
    std::vector<std::string> protected_processes{
        "system",
        "registry",
        "smss.exe",
        "csrss.exe",
        "wininit.exe",
        "services.exe",
        "lsass.exe",
        "svchost.exe",
        "fontdrvhost.exe",
        "dwm.exe",
        "explorer.exe",
        "audiodg.exe",
        "spoolsv.exe",
        "securityhealthservice.exe",
        "msmpeng.exe",
        "agra.exe"
    };

    std::vector<std::string> user_exclusions;

    // Logging & Diagnostics
    LogSeverity log_level{LogSeverity::Info};
    std::string log_file_path{"agra.log"};
    bool enable_console_colors{true};

    // Benchmark settings
    std::uint32_t benchmark_duration_seconds{60};
    std::uint32_t benchmark_warmup_seconds{5};
    std::uint32_t benchmark_iterations{3};

    // Validation
    [[nodiscard]] Result<void, Error> validate() const;

    [[nodiscard]] bool is_process_protected(std::string_view process_name) const;
    [[nodiscard]] bool is_process_user_excluded(std::string_view process_name) const;
};

class ConfigManager {
public:
    ConfigManager() = default;

    [[nodiscard]] const AppConfig& get() const noexcept { return config_; }
    void set(const AppConfig& cfg) { config_ = cfg; }

    Result<void, Error> load_from_file(const std::string& filepath);
    Result<void, Error> save_to_file(const std::string& filepath) const;

    void reset_to_defaults();

private:
    AppConfig config_;
};

} // namespace agra::core
