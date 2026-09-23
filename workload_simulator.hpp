#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/analysis/workload_analyzer.hpp"
#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/allocator/allocation_engine.hpp"

#include <vector>
#include <string>

namespace agra::simulation {

struct SimStepResult {
    std::uint32_t step_index{0};
    std::string scenario_name;
    core::WorkloadMetrics metrics;
    analysis::BottleneckResult bottleneck;
    core::AllocationDecision decision;
    std::string log_message;
};

struct SimulationReport {
    std::string test_name{"AGRA Controlled Workload Simulation"};
    core::TimePoint start_time{std::chrono::steady_clock::now()};
    core::TimePoint end_time{std::chrono::steady_clock::now()};
    std::vector<SimStepResult> steps;
    std::uint32_t total_transitions{0};
    bool passed_all_assertions{true};
    std::string summary;
};

class WorkloadSimulator {
public:
    explicit WorkloadSimulator(const core::AppConfig& config);

    // Run the full multi-phase simulation suite
    [[nodiscard]] SimulationReport run_simulation();

    // Export simulation report to JSON
    [[nodiscard]] static std::string to_json(const SimulationReport& report);

    // Export simulation report to CSV
    [[nodiscard]] static std::string to_csv(const SimulationReport& report);

private:
    const core::AppConfig& config_;
};

} // namespace agra::simulation
