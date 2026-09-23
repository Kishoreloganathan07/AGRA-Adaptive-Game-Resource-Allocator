#include "agra/simulation/workload_simulator.hpp"
#include "agra/core/logger.hpp"

#include <format>
#include <sstream>

namespace agra::simulation {

WorkloadSimulator::WorkloadSimulator(const core::AppConfig& config)
    : config_(config)
{}

SimulationReport WorkloadSimulator::run_simulation() {
    AGRA_LOG_INFO("WorkloadSimulator", "=== [SIMULATION] Starting Controlled Workload Simulation ===");

    SimulationReport report{};
    report.start_time = std::chrono::steady_clock::now();

    analysis::WorkloadAnalyzer analyzer(config_);
    analysis::BottleneckClassifier classifier(config_);
    allocator::AllocationEngine engine(config_);

    struct Phase {
        std::string name;
        int ticks;
        double game_cpu;
        double system_cpu;
        double contention;
    };

    // 4 phases simulating dynamic gaming conditions:
    // Phase 1: Heavy sustained gameplay (Workload A: 85%, System: 90%, Contention: 0.05)
    // Phase 2: Menu / Loading drop (Workload A: 12%, System: 20%, Contention: 0.05)
    // Phase 3: Background Contention Surge (Workload A: 50%, System: 92%, Contention: 0.60)
    // Phase 4: Heavy Gameplay resumes with spike (Workload A: 92%, System: 95%, Contention: 0.05)
    const std::vector<Phase> phases = {
        {"Phase 1: Sustained Heavy Gameplay (A=85%)", 15, 85.0, 90.0, 0.05},
        {"Phase 2: Menu / Idle Transition (A=12%)", 12, 12.0, 20.0, 0.05},
        {"Phase 3: Background Contention Spike (Contention=0.60)", 15, 50.0, 92.0, 0.60},
        {"Phase 4: Resumed Heavy Gaming (A=92%)", 15, 92.0, 95.0, 0.05}
    };

    std::uint32_t step_counter = 0;
    core::PriorityLevel last_prio = core::PriorityLevel::Normal;

    for (const auto& p : phases) {
        AGRA_LOG_INFO("WorkloadSimulator", "[SIMULATION] Entering {}", p.name);

        for (int i = 0; i < p.ticks; ++i) {
            core::WorkloadMetrics m{};
            m.game_cpu_percent = p.game_cpu;
            m.system_cpu_percent = p.system_cpu;
            m.background_cpu_percent = (std::max)(0.0, p.system_cpu - p.game_cpu);
            m.contention_index = p.contention;

            analyzer.process_sample(m);
            auto an = analyzer.current_analysis();
            auto bn = classifier.classify(an, m);
            auto dec = engine.decide(1001, bn, an, m);

            if (dec.target_priority != last_prio) {
                ++report.total_transitions;
                last_prio = dec.target_priority;
            }

            SimStepResult step{};
            step.step_index = ++step_counter;
            step.scenario_name = p.name;
            step.metrics = m;
            step.bottleneck = bn;
            step.decision = dec;
            step.log_message = std::format(
                "[SIMULATION Step {:02d}] {} | Game CPU={:.1f}% Contention={:.2f} | Bottleneck={} | Priority={} Score={:.1f}",
                step.step_index, p.name, m.game_cpu_percent, m.contention_index,
                core::bottleneck_type_to_string(bn.type),
                core::priority_level_to_string(dec.target_priority),
                dec.allocation_score);

            report.steps.push_back(step);
        }
    }

    report.end_time = std::chrono::steady_clock::now();
    report.passed_all_assertions = (report.total_transitions >= 2);
    report.summary = std::format(
        "[SIMULATION COMPLETE] Executed {} steps across 4 phases. Observed {} policy transitions. Validation status: {}",
        step_counter, report.total_transitions, report.passed_all_assertions ? "PASSED" : "WARNING: Low transitions");

    AGRA_LOG_INFO("WorkloadSimulator", "{}", report.summary);
    return report;
}

std::string WorkloadSimulator::to_json(const SimulationReport& report) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"simulation_type\": \"" << report.test_name << "\",\n";
    os << "  \"total_steps\": " << report.steps.size() << ",\n";
    os << "  \"total_transitions\": " << report.total_transitions << ",\n";
    os << "  \"status\": \"" << (report.passed_all_assertions ? "PASSED" : "FAILED") << "\",\n";
    os << "  \"summary\": \"" << report.summary << "\",\n";
    os << "  \"steps\": [\n";

    for (size_t i = 0; i < report.steps.size(); ++i) {
        const auto& s = report.steps[i];
        os << "    {\n";
        os << "      \"step\": " << s.step_index << ",\n";
        os << "      \"scenario\": \"" << s.scenario_name << "\",\n";
        os << "      \"game_cpu_percent\": " << s.metrics.game_cpu_percent << ",\n";
        os << "      \"system_cpu_percent\": " << s.metrics.system_cpu_percent << ",\n";
        os << "      \"contention_index\": " << s.metrics.contention_index << ",\n";
        os << "      \"bottleneck\": \"" << core::bottleneck_type_to_string(s.bottleneck.type) << "\",\n";
        os << "      \"confidence\": " << s.bottleneck.confidence << ",\n";
        os << "      \"allocation_score\": " << s.decision.allocation_score << ",\n";
        os << "      \"recommended_priority\": \"" << core::priority_level_to_string(s.decision.target_priority) << "\",\n";
        os << "      \"requires_action\": " << (s.decision.requires_action ? "true" : "false") << "\n";
        os << "    }" << (i + 1 < report.steps.size() ? "," : "") << "\n";
    }

    os << "  ]\n";
    os << "}\n";
    return os.str();
}

std::string WorkloadSimulator::to_csv(const SimulationReport& report) {
    std::ostringstream os;
    os << "Step,Scenario,GameCPU,SystemCPU,ContentionIndex,Bottleneck,Confidence,Score,Priority,RequiresAction\n";
    for (const auto& s : report.steps) {
        os << s.step_index << ","
           << "\"" << s.scenario_name << "\","
           << s.metrics.game_cpu_percent << ","
           << s.metrics.system_cpu_percent << ","
           << s.metrics.contention_index << ","
           << "\"" << core::bottleneck_type_to_string(s.bottleneck.type) << "\","
           << s.bottleneck.confidence << ","
           << s.decision.allocation_score << ","
           << "\"" << core::priority_level_to_string(s.decision.target_priority) << "\","
           << (s.decision.requires_action ? "TRUE" : "FALSE") << "\n";
    }
    return os.str();
}

} // namespace agra::simulation
