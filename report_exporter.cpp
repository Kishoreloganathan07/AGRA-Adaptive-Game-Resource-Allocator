#include "agra/benchmark/report_exporter.hpp"
#include "agra/core/logger.hpp"

#include <fstream>
#include <sstream>
#include <format>

namespace agra::benchmark {

std::string ReportExporter::to_json(const BenchmarkSuiteResult& result) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"benchmark_report\": {\n";
    os << "    \"session_id\": \"" << result.session_id << "\",\n";
    os << "    \"target_pid\": " << result.target_pid << ",\n";
    os << "    \"target_name\": \"" << result.target_name << "\",\n";
    os << "    \"summary\": \"" << result.comparative_summary << "\",\n";
    os << "    \"conditions\": [\n";

    for (size_t i = 0; i < result.condition_results.size(); ++i) {
        const auto& c = result.condition_results[i];
        os << "      {\n";
        os << "        \"condition\": \"" << c.condition_name << "\",\n";
        os << "        \"duration_seconds\": " << c.duration_seconds << ",\n";
        os << "        \"samples_recorded\": " << c.samples_recorded << ",\n";
        os << "        \"avg_game_cpu_percent\": " << c.avg_game_cpu << ",\n";
        os << "        \"max_game_cpu_percent\": " << c.max_game_cpu << ",\n";
        os << "        \"min_game_cpu_percent\": " << c.min_game_cpu << ",\n";
        os << "        \"avg_system_cpu_percent\": " << c.avg_system_cpu << ",\n";
        os << "        \"avg_contention_index\": " << c.avg_contention << ",\n";
        os << "        \"max_contention_index\": " << c.max_contention << ",\n";
        os << "        \"avg_agra_overhead_us\": " << c.avg_overhead_us << ",\n";
        os << "        \"fps_telemetry\": \"" << c.telemetry_note << "\"\n";
        os << "      }" << (i + 1 < result.condition_results.size() ? "," : "") << "\n";
    }

    os << "    ]\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

std::string ReportExporter::to_csv(const BenchmarkSuiteResult& result) {
    std::ostringstream os;
    os << "# AGRA Benchmark Comparative Report\n";
    os << "# Target: " << result.target_name << " (PID " << result.target_pid << ")\n";
    os << "# Session: " << result.session_id << "\n";
    os << "Condition,DurationSec,Samples,AvgGameCPU,MaxGameCPU,MinGameCPU,AvgSysCPU,AvgContention,MaxContention,OverheadMicrosec,TelemetryNote\n";

    for (const auto& c : result.condition_results) {
        os << "\"" << c.condition_name << "\","
           << c.duration_seconds << ","
           << c.samples_recorded << ","
           << c.avg_game_cpu << ","
           << c.max_game_cpu << ","
           << c.min_game_cpu << ","
           << c.avg_system_cpu << ","
           << c.avg_contention << ","
           << c.max_contention << ","
           << c.avg_overhead_us << ","
           << "\"" << c.telemetry_note << "\"\n";
    }

    return os.str();
}

std::string ReportExporter::session_to_json(const session::SessionSnapshot& s) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"target_pid\": " << s.target_pid << ",\n";
    os << "  \"target_name\": \"" << s.target_name << "\",\n";
    os << "  \"active_mode\": \"" << core::policy_mode_to_string(s.active_mode) << "\",\n";
    os << "  \"is_active\": " << (s.is_active ? "true" : "false") << ",\n";
    os << "  \"safe_mode\": " << (s.safe_mode ? "true" : "false") << ",\n";
    os << "  \"ticks_processed\": " << s.ticks_processed << ",\n";
    os << "  \"metrics\": {\n";
    os << "    \"game_cpu_percent\": " << s.latest_metrics.game_cpu_percent << ",\n";
    os << "    \"system_cpu_percent\": " << s.latest_metrics.system_cpu_percent << ",\n";
    os << "    \"contention_index\": " << s.latest_metrics.contention_index << ",\n";
    os << "    \"working_set_mb\": " << s.latest_metrics.working_set_mb << "\n";
    os << "  },\n";
    os << "  \"bottleneck\": {\n";
    os << "    \"type\": \"" << core::bottleneck_type_to_string(s.latest_bottleneck.type) << "\",\n";
    os << "    \"confidence\": " << s.latest_bottleneck.confidence << ",\n";
    os << "    \"explanation\": \"" << s.latest_bottleneck.explanation << "\"\n";
    os << "  },\n";
    os << "  \"decision\": {\n";
    os << "    \"score\": " << s.latest_decision.allocation_score << ",\n";
    os << "    \"target_priority\": \"" << core::priority_level_to_string(s.latest_decision.target_priority) << "\",\n";
    os << "    \"urgency\": \"" << core::allocation_urgency_to_string(s.latest_decision.urgency) << "\",\n";
    os << "    \"reason\": \"" << s.latest_decision.reason << "\"\n";
    os << "  },\n";
    os << "  \"overhead\": {\n";
    os << "    \"sampling_duration_us\": " << s.latest_overhead.sampling_duration_us << ",\n";
    os << "    \"agra_cpu_percent\": " << s.latest_overhead.agra_cpu_percent << ",\n";
    os << "    \"agra_memory_mb\": " << s.latest_overhead.agra_memory_mb << "\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

std::string ReportExporter::session_to_csv(const session::SessionSnapshot& s) {
    std::ostringstream os;
    os << "PID,Name,Mode,GameCPU,SystemCPU,Contention,Bottleneck,Confidence,Score,Priority,Overhead_us\n";
    os << s.target_pid << ","
       << "\"" << s.target_name << "\","
       << "\"" << core::policy_mode_to_string(s.active_mode) << "\","
       << s.latest_metrics.game_cpu_percent << ","
       << s.latest_metrics.system_cpu_percent << ","
       << s.latest_metrics.contention_index << ","
       << "\"" << core::bottleneck_type_to_string(s.latest_bottleneck.type) << "\","
       << s.latest_bottleneck.confidence << ","
       << s.latest_decision.allocation_score << ","
       << "\"" << core::priority_level_to_string(s.latest_decision.target_priority) << "\","
       << s.latest_overhead.sampling_duration_us << "\n";
    return os.str();
}

core::Result<void, core::Error> ReportExporter::save_to_file(
    const std::string& filepath,
    const std::string& content)
{
    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return core::Error::config(std::format("Failed to open file '{}' for writing.", filepath));
    }

    out << content;
    if (!out) {
        return core::Error::config(std::format("Write error on file '{}'.", filepath));
    }

    AGRA_LOG_INFO("ReportExporter", "Successfully exported report to '{}'", filepath);
    return {};
}

} // namespace agra::benchmark
