#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <optional>
#include <variant>

namespace agra::core {

// Strong typed identifiers
using ProcessId = std::uint32_t;
using ThreadId = std::uint32_t;
using AffinityMask = std::uint64_t;
using TimePoint = std::chrono::steady_clock::time_point;
using SystemTimePoint = std::chrono::system_clock::time_point;

// Resource Policy Operating Modes
enum class PolicyMode {
    Default,        // Baseline Windows behavior, minimal intervention
    Conservative,   // Small adjustments only when confidence is high
    Adaptive,       // Dynamic workload-aware real-time adjustments
    Performance     // Aggressive scheduling preference within strict safety limits
};

inline std::string_view policy_mode_to_string(PolicyMode mode) noexcept {
    switch (mode) {
        case PolicyMode::Default: return "Default";
        case PolicyMode::Conservative: return "Conservative";
        case PolicyMode::Adaptive: return "Adaptive";
        case PolicyMode::Performance: return "Performance";
        default: return "Unknown";
    }
}

// Windows Priority Classes (Strictly user-mode, Realtime excluded for safety)
enum class PriorityLevel {
    Idle,           // IDLE_PRIORITY_CLASS
    BelowNormal,    // BELOW_NORMAL_PRIORITY_CLASS
    Normal,         // NORMAL_PRIORITY_CLASS
    AboveNormal,    // ABOVE_NORMAL_PRIORITY_CLASS
    High            // HIGH_PRIORITY_CLASS (Safety limit: never REALTIME)
};

inline std::string_view priority_level_to_string(PriorityLevel level) noexcept {
    switch (level) {
        case PriorityLevel::Idle: return "Idle";
        case PriorityLevel::BelowNormal: return "BelowNormal";
        case PriorityLevel::Normal: return "Normal";
        case PriorityLevel::AboveNormal: return "AboveNormal";
        case PriorityLevel::High: return "High";
        default: return "Unknown";
    }
}

// Bottleneck Classifications
enum class BottleneckType {
    Unknown,
    CpuBound,
    GpuBoundOrCpuNotPrimary,
    BackgroundContention,
    IoRelated,
    MemoryPressure,
    Mixed
};

inline std::string_view bottleneck_type_to_string(BottleneckType type) noexcept {
    switch (type) {
        case BottleneckType::Unknown: return "Unknown";
        case BottleneckType::CpuBound: return "CPU-Bound";
        case BottleneckType::GpuBoundOrCpuNotPrimary: return "GPU-Bound or CPU Not Primary";
        case BottleneckType::BackgroundContention: return "Background Contention";
        case BottleneckType::IoRelated: return "I/O-Related";
        case BottleneckType::MemoryPressure: return "Memory Pressure";
        case BottleneckType::Mixed: return "Mixed Bottleneck";
        default: return "Unknown";
    }
}

// Allocation Urgency
enum class AllocationUrgency {
    Low,
    Medium,
    High,
    Immediate
};

inline std::string_view allocation_urgency_to_string(AllocationUrgency urgency) noexcept {
    switch (urgency) {
        case AllocationUrgency::Low: return "Low";
        case AllocationUrgency::Medium: return "Medium";
        case AllocationUrgency::High: return "High";
        case AllocationUrgency::Immediate: return "Immediate";
        default: return "Unknown";
    }
}

// Background Process Classification
enum class ProcessClassification {
    SafeToReduce,       // Background utilities, non-critical background tasks
    UserExcluded,       // Explicitly protected by user in configuration
    SystemCritical,     // Windows core OS, services, audio, input, security
    Unknown             // Unclassified (Safety default: do not touch)
};

inline std::string_view process_classification_to_string(ProcessClassification cls) noexcept {
    switch (cls) {
        case ProcessClassification::SafeToReduce: return "SafeToReduce";
        case ProcessClassification::UserExcluded: return "UserExcluded";
        case ProcessClassification::SystemCritical: return "SystemCritical";
        case ProcessClassification::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

// Logging Severities
enum class LogSeverity {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

inline std::string_view log_severity_to_string(LogSeverity sev) noexcept {
    switch (sev) {
        case LogSeverity::Trace: return "TRACE";
        case LogSeverity::Debug: return "DEBUG";
        case LogSeverity::Info: return "INFO";
        case LogSeverity::Warning: return "WARN";
        case LogSeverity::Error: return "ERROR";
        case LogSeverity::Critical: return "CRIT";
        default: return "UNKNOWN";
    }
}

// Power State
enum class PowerState {
    AcPower,
    Battery,
    Unknown
};

// Target Process Metadata
struct ProcessInfo {
    ProcessId pid{0};
    std::string name;
    std::string executable_path;
    PriorityLevel priority{PriorityLevel::Normal};
    AffinityMask affinity_mask{0};
    std::uint32_t thread_count{0};
    double cpu_usage_percent{0.0};
    std::uint64_t working_set_bytes{0};
    bool is_elevated{false};
    bool is_responding{true};
};

// Workload Metrics Snapshot
struct WorkloadMetrics {
    TimePoint timestamp{std::chrono::steady_clock::now()};
    double game_cpu_percent{0.0};
    double system_cpu_percent{0.0};
    double background_cpu_percent{0.0};
    std::vector<double> per_core_percent;
    std::uint64_t working_set_mb{0};
    std::uint64_t io_read_bytes_sec{0};
    std::uint64_t io_write_bytes_sec{0};
    std::uint32_t context_switches_sec{0};
    double contention_index{0.0}; // 0.0 (clean) to 1.0 (severe contention)
};

// Explainable Allocation Decision
struct AllocationDecision {
    ProcessId target_pid{0};
    PolicyMode active_mode{PolicyMode::Default};
    PriorityLevel target_priority{PriorityLevel::Normal};
    AffinityMask target_affinity{0};
    std::vector<std::uint32_t> target_cpu_sets;
    double confidence{0.0};          // 0.0 to 1.0
    AllocationUrgency urgency{AllocationUrgency::Low};
    double allocation_score{0.0};
    std::string reason;
    std::string expected_effect;
    BottleneckType detected_bottleneck{BottleneckType::Unknown};
    TimePoint decision_time{std::chrono::steady_clock::now()};
    bool requires_action{false};
};

// Rollback Snapshot
struct StateSnapshot {
    ProcessId pid{0};
    std::string process_name;
    PriorityLevel original_priority{PriorityLevel::Normal};
    AffinityMask original_affinity{0};
    std::vector<std::uint32_t> original_cpu_sets;
    SystemTimePoint captured_at{std::chrono::system_clock::now()};
    bool valid{false};
};

// A lightweight, generic Result<T, E> type for standard-compliant error propagation
template <typename T, typename E>
class Result {
public:
    Result(const T& val) : data_(val) {}
    Result(T&& val) : data_(std::move(val)) {}
    Result(const E& err) : data_(err) {}
    Result(E&& err) : data_(std::move(err)) {}

    [[nodiscard]] bool is_ok() const noexcept { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool is_err() const noexcept { return std::holds_alternative<E>(data_); }

    [[nodiscard]] const T& value() const { return std::get<T>(data_); }
    [[nodiscard]] T& value() { return std::get<T>(data_); }

    [[nodiscard]] const E& error() const { return std::get<E>(data_); }
    [[nodiscard]] E& error() { return std::get<E>(data_); }

    [[nodiscard]] std::optional<T> ok() const {
        if (is_ok()) return std::get<T>(data_);
        return std::nullopt;
    }

private:
    std::variant<T, E> data_;
};

// Specialization for Result<void, E>
template <typename E>
class Result<void, E> {
public:
    Result() : error_(std::nullopt) {}
    Result(const E& err) : error_(err) {}
    Result(E&& err) : error_(std::move(err)) {}

    [[nodiscard]] bool is_ok() const noexcept { return !error_.has_value(); }
    [[nodiscard]] bool is_err() const noexcept { return error_.has_value(); }

    [[nodiscard]] const E& error() const { return error_.value(); }
    [[nodiscard]] E& error() { return error_.value(); }

private:
    std::optional<E> error_;
};

} // namespace agra::core
