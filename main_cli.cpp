#include "agra/version.hpp"
#include "agra/core/types.hpp"
#include "agra/core/logger.hpp"
#include "agra/core/config.hpp"
#include "agra/discovery/cpu_topology.hpp"
#include "agra/discovery/system_info.hpp"
#include "agra/discovery/process_enumerator.hpp"
#include "agra/game/game_detector.hpp"
#include "agra/game/game_database.hpp"
#include "agra/game/game_profile.hpp"
#include "agra/session/session_manager.hpp"
#include "agra/benchmark/benchmark_runner.hpp"
#include "agra/benchmark/report_exporter.hpp"
#include "agra/simulation/workload_simulator.hpp"
#include "agra/ui/terminal_dashboard.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <format>

namespace {

void print_banner() {
    std::cout << R"(
=============================================================
 AGRA — Adaptive Game Resource Allocator
 Version: )" << agra::VERSION_STRING << R"(
 Windows 10/11 64-bit Architecture
=============================================================
)" << std::endl;
}

void print_help() {
    std::cout << R"(Usage: agra.exe [options]

Commands & Options:
  --help, -h               Display this help message
  --version, -v            Display version information
  --status                 Display current hardware topology and AGRA status
  --list-games             List running potential game and user processes
  --list-processes         List all running processes across the system
  --detect                 Run real-time game detection probe
  --profile <name>         Load specific game profile
  --start [PID]            Start adaptive resource allocation session (or auto-detect)
  --stop                   Stop active session and revert policies
  --restore                Immediately restore all processes to original baseline
  --benchmark [PID]        Run automated comparative benchmark across 5 conditions
  --simulate               Run controlled workload simulation (Workload A, B, Contention)
  --safe-mode              Run in strict read-only / conservative mode
  --export <file>          Export benchmark or simulation telemetry (JSON/CSV)
  --config <file>          Path to custom configuration file

Principles:
  - AGRA does not replace the Windows kernel scheduler.
  - Controls: Process Priority, Affinity, CPU Sets, EcoQoS Background Throttling.
  - Strictly safe: No injection, no driver bypass, no anti-cheat evasion.
)" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace agra::core;
    using namespace agra::discovery;
    using namespace agra::game;
    using namespace agra::session;
    using namespace agra::benchmark;
    using namespace agra::simulation;
    using namespace agra::ui;

    Logger::instance().init(LogSeverity::Info, "agra.log");

    ConfigManager config_mgr;
    auto load_res = config_mgr.load_from_file("config.json");
    if (load_res.is_err()) {
        // Fallback default config
    }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        print_banner();
        print_help();
        return 0;
    }

    if (args[0] == "--version" || args[0] == "-v") {
        std::cout << agra::APP_NAME << " version " << agra::VERSION_STRING << "\n";
        return 0;
    }

    if (args[0] == "--status") {
        print_banner();
        auto topo_res = CpuTopologyDetector::detect();
        auto mem = SystemInfoProvider::query_memory();
        auto ver = SystemInfoProvider::query_windows_version();
        auto power = SystemInfoProvider::query_power_status();
        auto caps = SystemInfoProvider::query_capabilities();

        std::cout << "Operating System:\n";
        std::cout << "  Platform:            " << ver.display_name << "\n";
        std::cout << "  Privilege:           " << (caps.is_process_elevated ? "Administrator (Elevated)" : "Standard User") << "\n";
        std::cout << "  Power Source:        " << (power.is_on_battery ? "Battery (" + std::to_string(power.battery_life_percent) + "%)" : "AC Power (Plugged In)") << "\n";
        std::cout << "  RAM:                 " << mem.available_physical_mb << " MB Free / " << mem.total_physical_mb << " MB Total (" << mem.memory_load_percent << "% Load)\n\n";

        if (topo_res.is_ok()) {
            std::cout << "CPU Hardware Topology:\n";
            std::cout << "  " << topo_res.value().to_summary_string() << "\n\n";
        }

        std::cout << "Kernel Capabilities:\n";
        std::cout << "  CPU Sets Support:    " << (caps.cpu_sets_supported ? "Available (Win10 1607+)" : "Unavailable") << "\n";
        std::cout << "  EcoQoS Support:      " << (caps.eco_qos_supported ? "Available (Win10 21H2+)" : "Unavailable") << "\n\n";

        std::cout << "AGRA Session Status:\n";
        std::cout << "  Engine State:        Idle / Ready\n";
        std::cout << "  Active Policy Mode:  " << policy_mode_to_string(config_mgr.get().policy_mode) << "\n";
        std::cout << "  Sampling Interval:   " << config_mgr.get().sampling_interval_ms << " ms\n";
        std::cout << "  Protected Processes: " << config_mgr.get().protected_processes.size() << " registered\n";
        return 0;
    }

    if (args[0] == "--list-games" || args[0] == "--list-processes") {
        print_banner();
        bool show_all = (args[0] == "--list-processes");
        auto processes = ProcessEnumerator::enumerate_all(config_mgr.get());

        std::cout << std::format("{:<8} {:<30} {:<15} {:<12} {:<10} {:<12}\n",
                                 "PID", "Process Name", "Priority", "Threads", "RAM (MB)", "Status");
        std::cout << std::string(90, '-') << "\n";

        int count = 0;
        for (const auto& p : processes) {
            if (!show_all && p.is_system_protected) {
                continue;
            }

            std::string status = p.is_system_protected ? "Protected" : (p.is_user_excluded ? "Excluded" : "Monitored");
            std::cout << std::format("{:<8} {:<30} {:<15} {:<12} {:<10} {:<12}\n",
                                     p.pid,
                                     p.name.substr(0, 29),
                                     priority_level_to_string(p.priority),
                                     p.thread_count,
                                     p.working_set_mb,
                                     status);
            count++;
        }
        std::cout << std::string(90, '-') << "\n";
        std::cout << "Total listed: " << count << " processes.\n";
        return 0;
    }

    if (args[0] == "--detect") {
        print_banner();
        std::cout << "Scanning for active games using database matching and foreground heuristics...\n\n";

        GameDetector detector(config_mgr.get());
        auto candidates = detector.scan_for_games();

        if (candidates.empty()) {
            std::cout << "No active games automatically detected.\n";
            std::cout << "Tip: Use 'agra.exe --list-games' to view running processes, or attach manually via PID.\n";
        } else {
            std::cout << std::format("{:<8} {:<30} {:<25} {:<12} {:<12}\n",
                                     "PID", "Game Title", "Detection Method", "Fullscreen", "Confidence");
            std::cout << std::string(90, '-') << "\n";
            for (const auto& g : candidates) {
                std::cout << std::format("{:<8} {:<30} {:<25} {:<12} {:<12}\n",
                                         g.pid,
                                         g.title.substr(0, 29),
                                         g.detection_method,
                                         g.is_fullscreen ? "YES" : "NO",
                                         std::format("{:.0f}%", g.confidence * 100.0));
            }
            std::cout << std::string(90, '-') << "\n";
            std::cout << "Detected " << candidates.size() << " active game target(s).\n";
        }
        return 0;
    }

    if (args[0] == "--profile") {
        print_banner();
        if (args.size() < 2) {
            std::cout << "Available saved profiles:\n";
            auto profiles = ProfileStorage::list_saved_profiles();
            if (profiles.empty()) {
                std::cout << "  (No custom profiles saved yet in profiles/)\n";
            } else {
                for (const auto& name : profiles) {
                    std::cout << "  - " << name << "\n";
                }
            }
            return 0;
        }

        std::string profile_name = args[1];
        auto load_prof = ProfileStorage::load_profile(profile_name);
        if (load_prof.is_err()) {
            std::cout << "Error: " << load_prof.error().message() << "\n";
            return 1;
        }

        const auto& p = load_prof.value();
        std::cout << "Loaded Profile: " << p.profile_name << "\n";
        std::cout << "  Target Binary:       " << p.executable_name << "\n";
        std::cout << "  Preferred Policy:    " << policy_mode_to_string(p.preferred_policy_mode) << "\n";
        std::cout << "  Preferred Priority:  " << priority_level_to_string(p.preferred_priority) << "\n";
        std::cout << "  Throttle Background: " << (p.throttle_background_processes ? "Enabled" : "Disabled") << "\n";
        return 0;
    }

    if (args[0] == "--restore") {
        print_banner();
        std::cout << "Executing immediate emergency restoration of all processes...\n";
        SessionManager session(config_mgr);
        session.restore_all();
        std::cout << "All tracked processes successfully restored to original baseline.\n";
        return 0;
    }

    if (args[0] == "--simulate") {
        print_banner();
        std::cout << "[SIMULATION] Running Controlled Multi-Phase Workload Simulation...\n\n";
        WorkloadSimulator sim(config_mgr.get());
        auto sim_report = sim.run_simulation();

        std::cout << sim_report.summary << "\n";
        std::cout << "\nSteps Preview:\n";
        for (size_t i = 0; i < (std::min)(size_t(8), sim_report.steps.size()); ++i) {
            std::cout << "  " << sim_report.steps[i].log_message << "\n";
        }

        // Export if --export specified or default
        std::string export_file = "simulation_report.json";
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--export" && i + 1 < args.size()) {
                export_file = args[i + 1];
            }
        }
        std::string content = (export_file.ends_with(".csv")) ? WorkloadSimulator::to_csv(sim_report) : WorkloadSimulator::to_json(sim_report);
        (void)ReportExporter::save_to_file(export_file, content);
        std::cout << "\nSimulation report exported to: " << export_file << "\n";
        return 0;
    }

    if (args[0] == "--benchmark") {
        print_banner();
        SessionManager session(config_mgr);
        BenchmarkRunner runner(session);

        ProcessId target_pid = 0;
        std::string target_name;

        if (args.size() > 1 && args[1].rfind("--", 0) != 0) {
            try {
                target_pid = static_cast<ProcessId>(std::stoul(args[1]));
                target_name = std::format("PID_{}", target_pid);
            } catch (...) {
                target_pid = 0;
            }
        }

        BenchmarkSuiteResult report;
        if (target_pid != 0) {
            report = runner.run_full_benchmark(target_pid, target_name, 5);
        } else {
            std::cout << "No specific PID provided. Running automated benchmark suite in simulated telemetry mode...\n\n";
            report = runner.run_simulated_benchmark(15);
        }

        std::cout << "\nBenchmark Comparative Results:\n";
        std::cout << std::format("{:<28} {:<12} {:<12} {:<15} {:<15}\n",
                                 "Condition", "Avg Game CPU", "Contention", "Overhead (us)", "Telemetry");
        std::cout << std::string(85, '-') << "\n";
        for (const auto& c : report.condition_results) {
            std::cout << std::format("{:<28} {:<12.1f} {:<12.2f} {:<15.1f} {:<15}\n",
                                     c.condition_name, c.avg_game_cpu, c.avg_contention, c.avg_overhead_us,
                                     c.fps_available ? "FPS Tracked" : "Kernel Only");
        }
        std::cout << std::string(85, '-') << "\n";
        std::cout << report.comparative_summary << "\n";

        std::string export_file = "benchmark_report.json";
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--export" && i + 1 < args.size()) {
                export_file = args[i + 1];
            }
        }
        std::string content = (export_file.ends_with(".csv")) ? ReportExporter::to_csv(report) : ReportExporter::to_json(report);
        (void)ReportExporter::save_to_file(export_file, content);
        std::cout << "Benchmark report exported to: " << export_file << "\n";
        return 0;
    }

    if (args[0] == "--start" || args[0] == "--safe-mode") {
        SessionManager session(config_mgr);
        if (args[0] == "--safe-mode") {
            session.set_safe_mode(true);
        }

        ProcessId target_pid = 0;
        if (args.size() > 1 && args[1].rfind("--", 0) != 0) {
            try {
                target_pid = static_cast<ProcessId>(std::stoul(args[1]));
            } catch (...) {
                target_pid = 0;
            }
        }

        if (target_pid != 0) {
            auto start_res = session.start_session(target_pid, std::format("PID_{}", target_pid));
            if (start_res.is_err()) {
                std::cout << "Failed to attach to PID " << target_pid << ": " << start_res.error().message() << "\n";
                return 1;
            }
        } else {
            auto auto_res = session.auto_detect_and_start();
            if (auto_res.is_err()) {
                std::cout << "Auto-detection error: " << auto_res.error().message() << "\n";
            }
        }

        TerminalDashboard dashboard(session);
        dashboard.run_interactive();
        return 0;
    }

    print_banner();
    print_help();
    return 0;
}
