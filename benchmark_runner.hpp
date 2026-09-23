#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/session/session_manager.hpp"

#include <vector>
#include <string>
#include <chrono>

namespace agra::benchmark {

enum class BenchmarkCondition {
    WindowsDefault,
    StaticPolicy,
    AgraConservative,
    AgraAdaptive,
    AgraAdaptivePlusBackground
};

inline std::string_view condition_to_string(BenchmarkCondition c) noexcept {
    switch (c) {
        case BenchmarkCondition::WindowsDefault: return "Windows Default";
        case BenchmarkCondition::StaticPolicy:   return "Static High Priority";
        case BenchmarkCondition::AgraConservative: return "AGRA Conservative";
        case BenchmarkCondition::AgraAdaptive:   return "AGRA Adaptive";
        case BenchmarkCondition::AgraAdaptivePlusBackground: return "AGRA Adaptive + Background";
        default: return "Unknown";
    }
}

struct ConditionResult {
    BenchmarkCondition condition;
    std::string condition_name;
    double duration_seconds{0.0};
    std::uint32_t samples_recorded{0};

    // Statistical summaries
    double avg_game_cpu{0.0};
    double max_game_cpu{0.0};
    double min_game_cpu{0.0};
    double avg_system_cpu{0.0};
    double avg_contention{0.0};
    double max_contention{0.0};
    double avg_overhead_us{0.0};

    // Telemetry integrity
    bool fps_available{false};
    double avg_fps{0.0};
    double one_percent_low_fps{0.0};
    std::string telemetry_note;
};

struct BenchmarkSuiteResult {
    std::string session_id;
    core::ProcessId target_pid{0};
    std::string target_name;
    core::TimePoint start_time{std::chrono::steady_clock::now()};
    core::TimePoint end_time{std::chrono::steady_clock::now()};
    std::vector<ConditionResult> condition_results;
    std::string comparative_summary;
};

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(session::SessionManager& session);

    // Run the full comparative benchmark on an active target
    [[nodiscard]] BenchmarkSuiteResult run_full_benchmark(
        core::ProcessId target_pid,
        const std::string& target_name,
        std::uint32_t seconds_per_condition = 10);

    // Run benchmark in simulated offline mode (synthetic workload)
    [[nodiscard]] BenchmarkSuiteResult run_simulated_benchmark(
        std::uint32_t ticks_per_condition = 20);

private:
    session::SessionManager& session_;
};

} // namespace agra::benchmark
