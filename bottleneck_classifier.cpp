#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace agra::analysis {

// ── Thresholds ───────────────────────────────────────────────────────────────
// All numeric thresholds are named constants so the classification logic is
// self-documenting and easy to tune from unit tests or config in the future.

// Game CPU EWMA (medium-term) below this → consider idle / menu
static constexpr double kIdleCpuThreshold       = 15.0;  // %

// GPU-bound heuristic: game is running but neither game nor system CPU is high
static constexpr double kGpuBoundGameCpuMax     = 35.0;  // %
static constexpr double kGpuBoundSystemCpuMax   = 45.0;  // %

// CPU-bound: sustained high demand from the game process
static constexpr double kCpuBoundDemandMin      = 0.68;  // normalised 0–1
static constexpr double kCpuBoundSystemCpuMin   = 65.0;  // %
static constexpr double kCpuBoundBackgroundMax  = 30.0;  // % (background CPU)

// Background contention
static constexpr double kContentionHighThreshold = 0.42;  // contention index
static constexpr double kContentionDemandMin     = 0.28;  // at least medium demand
static constexpr double kContentionDemandMax     = 0.75;  // but not fully CPU-bound

// Memory pressure: working set is large relative to RAM
// We use an absolute heuristic: > 2 GB working set AND low CPU
static constexpr double kMemoryPressureWSMin_MB  = 2048.0; // MB
static constexpr double kMemoryPressureCpuMax    = 55.0;   // %

// I/O related: high I/O bandwidth + medium CPU + moderate demand
static constexpr double kIoReadThreshold_MB_s    = 100.0;  // MB/s
static constexpr double kIoWriteThreshold_MB_s   = 50.0;   // MB/s
static constexpr double kIoDemandMin             = 0.20;
static constexpr double kIoDemandMax             = 0.70;
static constexpr double kIoCpuMax                = 60.0;   // %

// Mixed: two or more "moderate" signals are simultaneously elevated
static constexpr double kMixedContentionMin      = 0.28;
static constexpr double kMixedDemandMin          = 0.45;

// ─────────────────────────────────────────────────────────────────────────────

BottleneckClassifier::BottleneckClassifier(const core::AppConfig& config)
    : config_(config) {}

// ── finalise ─────────────────────────────────────────────────────────────────
// Set requires_action only when:
//   • confidence meets the global threshold from config, AND
//   • the type is one where AGRA can actually DO something useful.
//   GPU-bound and Memory-pressure verdicts are informational only —
//   raising process priority does not help a GPU-bound title.

BottleneckResult BottleneckClassifier::finalise(BottleneckResult r) const {
    using BT = core::BottleneckType;

    const bool above_threshold = (r.confidence >= config_.min_confidence_threshold);

    const bool actionable_type =
        (r.type == BT::CpuBound) ||
        (r.type == BT::BackgroundContention) ||
        (r.type == BT::IoRelated);

    r.requires_action = above_threshold && actionable_type;
    return r;
}

// ── idle check ───────────────────────────────────────────────────────────────

BottleneckResult BottleneckClassifier::try_classify_idle(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& /*m*/) const
{
    if (a.game_cpu_medium_ewma >= kIdleCpuThreshold) {
        return {};  // Not idle
    }

    BottleneckResult r;
    r.type = core::BottleneckType::Unknown;
    r.confidence = 0.90;
    r.explanation = std::format(
        "Game CPU utilisation is very low ({:.1f}% medium-term EWMA). "
        "The game appears to be idle, in a menu, or loading. "
        "No allocation action is appropriate.",
        a.game_cpu_medium_ewma);
    // requires_action intentionally left false — idle is never actionable
    return r;
}

// ── GPU-bound heuristic ───────────────────────────────────────────────────────
// We can only infer GPU-bound from CPU signals. If the game is running
// (frames are clearly being produced from the player's perspective) yet
// game CPU and total system CPU are both low, the bottleneck is most likely
// the GPU, not the CPU. AGRA cannot improve this situation by raising CPU
// priority, so requires_action remains false.

BottleneckResult BottleneckClassifier::try_classify_gpu_bound(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    if (a.game_cpu_medium_ewma >= kGpuBoundGameCpuMax)  return {};
    if (m.system_cpu_percent   >= kGpuBoundSystemCpuMax) return {};
    if (a.is_idle_or_menu)                               return {}; // Idle takes priority

    BottleneckResult r;
    r.type = core::BottleneckType::GpuBoundOrCpuNotPrimary;

    // Confidence scales inversely with game CPU: the lower the game CPU,
    // the more certain the CPU is not the bottleneck.
    double cpu_factor = 1.0 - (a.game_cpu_medium_ewma / kGpuBoundGameCpuMax);
    r.confidence = std::clamp(0.40 + cpu_factor * 0.25, 0.40, 0.65);

    r.explanation = std::format(
        "Game CPU utilisation is moderate-to-low ({:.1f}% EWMA) while system "
        "CPU is also low ({:.1f}%). "
        "The primary bottleneck is likely the GPU or another non-CPU resource. "
        "Raising CPU scheduling preference is unlikely to improve performance. "
        "AGRA is operating in read-only / informational mode for this condition.",
        a.game_cpu_medium_ewma, m.system_cpu_percent);

    // GPU-bound is NOT actionable via CPU controls
    return finalise(r);
}

// ── CPU-bound ────────────────────────────────────────────────────────────────
// The game is consistently consuming a large fraction of the CPU budget AND
// the total system utilisation is high. Background processes are not the
// primary cause (covered by BackgroundContention rule). Raising the game's
// scheduling preference relative to lower-priority background work may help.
//
// Confidence calculation:
//   base = 0.60
//   +0.20 if estimated_demand > 0.80  (strong sustained signal)
//   +0.08 if stability_score > 0.70   (steady workload, not transient)
//   +0.04 if is_sustained_heavy       (medium-term EWMA confirmed)
//   max   = 0.92

BottleneckResult BottleneckClassifier::try_classify_cpu_bound(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    if (a.estimated_demand     < kCpuBoundDemandMin)    return {};
    if (m.system_cpu_percent   < kCpuBoundSystemCpuMin) return {};
    if (m.background_cpu_percent > kCpuBoundBackgroundMax) return {}; // → contention rule

    double conf = 0.60;
    if (a.estimated_demand > 0.80) conf += 0.20;
    if (a.stability_score  > 0.70) conf += 0.08;
    if (a.is_sustained_heavy)      conf += 0.04;
    conf = std::clamp(conf, 0.0, 0.92);

    std::ostringstream os;
    os << std::format(
        "Game CPU demand is high (estimated {:.0f}%, medium EWMA {:.1f}%). "
        "System utilisation is {:.1f}%, indicating a CPU-bound workload. "
        "Background CPU consumption is low ({:.1f}%), so the game itself "
        "is the dominant consumer. ",
        a.estimated_demand * 100.0,
        a.game_cpu_medium_ewma,
        m.system_cpu_percent,
        m.background_cpu_percent);

    if (a.is_sustained_heavy) {
        os << "Workload has been sustained above the high-demand threshold. ";
    }
    if (a.is_transient_spike) {
        os << "A transient spike is contributing to current demand. ";
    }

    BottleneckResult r;
    r.type        = core::BottleneckType::CpuBound;
    r.confidence  = conf;
    r.explanation = os.str();
    return finalise(r);
}

// ── Background contention ─────────────────────────────────────────────────────
// The contention index (background CPU / remaining CPU after game) is high,
// meaning background processes are competing meaningfully with the game.
// The game itself is not fully saturating the CPU (that is CPU-bound). The
// appropriate intervention is to reduce the priority of background-safe
// processes rather than raising the game's own priority aggressively.
//
// Confidence calculation:
//   base = 0.50
//   +0.25 if contention_ewma > 0.60
//   +0.10 if demand is solidly in the medium range (not barely active)
//   max   = 0.85

BottleneckResult BottleneckClassifier::try_classify_background_contention(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    if (a.contention_ewma   < kContentionHighThreshold) return {};
    if (a.estimated_demand  < kContentionDemandMin)     return {};
    if (a.estimated_demand  > kContentionDemandMax)     return {}; // → CPU-bound

    double conf = 0.50;
    if (a.contention_ewma > 0.60) conf += 0.25;
    if (a.estimated_demand > 0.40) conf += 0.10;
    conf = std::clamp(conf, 0.0, 0.85);

    BottleneckResult r;
    r.type       = core::BottleneckType::BackgroundContention;
    r.confidence = conf;
    r.explanation = std::format(
        "Background CPU contention index is elevated ({:.2f} EWMA, threshold {:.2f}). "
        "Background processes are consuming {:.1f}% CPU, competing with the game "
        "which is using {:.1f}% (demand estimate {:.0f}%). "
        "Reducing scheduling priority of non-critical background processes "
        "may free CPU headroom for the game.",
        a.contention_ewma, kContentionHighThreshold,
        m.background_cpu_percent,
        m.game_cpu_percent,
        a.estimated_demand * 100.0);
    return finalise(r);
}

// ── Memory pressure ───────────────────────────────────────────────────────────
// If the process has a very large working set and CPU demand is moderate-to-low,
// memory paging/bandwidth may be the bottleneck. AGRA cannot directly control
// memory, but can report this for user awareness. Not actionable via CPU controls.

BottleneckResult BottleneckClassifier::try_classify_memory_pressure(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    if (static_cast<double>(m.working_set_mb) < kMemoryPressureWSMin_MB) return {};
    if (m.game_cpu_percent >= kMemoryPressureCpuMax) return {};

    // Confidence rises as working set grows further past threshold.
    // Apply a small stability penalty: if CPU is highly erratic alongside
    // a large working set, we are less certain memory is the primary cause.
    double ws_ratio = static_cast<double>(m.working_set_mb) / kMemoryPressureWSMin_MB;
    const double stability_penalty = (a.stability_score < 0.50) ? 0.05 : 0.0;
    double conf = std::clamp(0.45 + (ws_ratio - 1.0) * 0.15 - stability_penalty, 0.40, 0.70);

    BottleneckResult r;
    r.type       = core::BottleneckType::MemoryPressure;
    r.confidence = conf;
    r.explanation = std::format(
        "Game process working set is large ({} MB, threshold {} MB) "
        "while CPU utilisation is moderate ({:.1f}%). "
        "Memory bandwidth or paging may be limiting performance. "
        "CPU scheduling adjustments are unlikely to resolve this condition. "
        "Consider reducing background memory consumers.",
        m.working_set_mb, static_cast<std::uint64_t>(kMemoryPressureWSMin_MB),
        m.game_cpu_percent);
    // Memory pressure is informational — finalise will mark requires_action=false
    return finalise(r);
}

// ── I/O related ───────────────────────────────────────────────────────────────
// High I/O throughput with moderate CPU usage suggests a loading screen, asset
// streaming, or shader compilation phase. Raising CPU priority will not
// accelerate I/O-bound operations. Inform the user and hold off intervention.

BottleneckResult BottleneckClassifier::try_classify_io_related(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    const double read_mb_s  = static_cast<double>(m.io_read_bytes_sec)  / (1024.0 * 1024.0);
    const double write_mb_s = static_cast<double>(m.io_write_bytes_sec) / (1024.0 * 1024.0);
    const bool io_active = (read_mb_s >= kIoReadThreshold_MB_s) || (write_mb_s >= kIoWriteThreshold_MB_s);

    if (!io_active)                            return {};
    if (a.estimated_demand < kIoDemandMin)     return {};
    if (a.estimated_demand > kIoDemandMax)     return {};
    if (m.game_cpu_percent >= kIoCpuMax)       return {};

    double io_factor = std::clamp((read_mb_s + write_mb_s) / 400.0, 0.0, 1.0);
    double conf = std::clamp(0.50 + io_factor * 0.18, 0.50, 0.68);

    BottleneckResult r;
    r.type       = core::BottleneckType::IoRelated;
    r.confidence = conf;
    r.explanation = std::format(
        "High I/O activity detected (read: {:.1f} MB/s, write: {:.1f} MB/s) "
        "while CPU demand is in the medium range ({:.0f}%). "
        "The game may be loading assets or streaming content. "
        "CPU scheduling changes will not accelerate I/O throughput. "
        "AGRA will re-evaluate once I/O activity stabilises.",
        read_mb_s, write_mb_s, a.estimated_demand * 100.0);
    // I/O related with actionable=true is handled in finalise (IoRelated is in the actionable list)
    // but confidence ceiling is 0.68, which is just above the default min_confidence_threshold of 0.65
    return finalise(r);
}

// ── Mixed ────────────────────────────────────────────────────────────────────
// Multiple signals are above their moderate thresholds simultaneously. The
// classifier cannot assign a single dominant bottleneck with sufficient
// confidence to justify intervention. Report as Mixed, conservative action.

BottleneckResult BottleneckClassifier::try_classify_mixed(
    const WorkloadAnalysis& a,
    const core::WorkloadMetrics& m) const
{
    int signals = 0;
    std::ostringstream contributing;

    if (a.estimated_demand  >= kMixedDemandMin)    { ++signals; contributing << "elevated CPU demand; "; }
    if (a.contention_ewma   >= kMixedContentionMin){ ++signals; contributing << "background contention; "; }

    const double read_mb_s = static_cast<double>(m.io_read_bytes_sec) / (1024.0 * 1024.0);
    if (read_mb_s >= kIoReadThreshold_MB_s * 0.5)  { ++signals; contributing << "moderate I/O activity; "; }

    const double ws_mb = static_cast<double>(m.working_set_mb);
    if (ws_mb >= kMemoryPressureWSMin_MB * 0.75)    { ++signals; contributing << "large working set; "; }

    if (signals < 2) return {};  // Not genuinely mixed

    double conf = std::clamp(0.35 + signals * 0.05, 0.35, 0.55);

    BottleneckResult r;
    r.type       = core::BottleneckType::Mixed;
    r.confidence = conf;
    r.explanation = std::format(
        "Multiple resource signals are concurrently elevated ({}contributing factors). "
        "Signals: {}. "
        "No single bottleneck type dominates with sufficient confidence (confidence {:.0f}%). "
        "AGRA will maintain conservative mode and re-evaluate on the next sample.",
        signals,
        contributing.str(),
        conf * 100.0);
    // Mixed is not in the actionable types list → requires_action will be false
    return finalise(r);
}

// ── Primary classify() ────────────────────────────────────────────────────────
// Rules are evaluated in priority order. The first rule that returns a
// non-default (type != Unknown) result wins. If all rules return Unknown,
// we fall through to the safe Unknown default.

BottleneckResult BottleneckClassifier::classify(
    const WorkloadAnalysis& analysis,
    const core::WorkloadMetrics& metrics) const
{
    // Rule 1 — Idle
    {
        auto r = try_classify_idle(analysis, metrics);
        if (r.type != core::BottleneckType::Unknown || r.confidence > 0.5) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f}) req_action={}",
                           core::bottleneck_type_to_string(r.type),
                           r.confidence, r.requires_action);
            return r;
        }
    }

    // Rule 2 — GPU-bound
    {
        auto r = try_classify_gpu_bound(analysis, metrics);
        if (r.type == core::BottleneckType::GpuBoundOrCpuNotPrimary) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f})",
                           core::bottleneck_type_to_string(r.type), r.confidence);
            return r;
        }
    }

    // Rule 3 — CPU-bound
    {
        auto r = try_classify_cpu_bound(analysis, metrics);
        if (r.type == core::BottleneckType::CpuBound) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f}) req_action={}",
                           core::bottleneck_type_to_string(r.type),
                           r.confidence, r.requires_action);
            return r;
        }
    }

    // Rule 4 — Background contention
    {
        auto r = try_classify_background_contention(analysis, metrics);
        if (r.type == core::BottleneckType::BackgroundContention) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f}) req_action={}",
                           core::bottleneck_type_to_string(r.type),
                           r.confidence, r.requires_action);
            return r;
        }
    }

    // Rule 5 — Memory pressure
    {
        auto r = try_classify_memory_pressure(analysis, metrics);
        if (r.type == core::BottleneckType::MemoryPressure) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f})",
                           core::bottleneck_type_to_string(r.type), r.confidence);
            return r;
        }
    }

    // Rule 6 — I/O related
    {
        auto r = try_classify_io_related(analysis, metrics);
        if (r.type == core::BottleneckType::IoRelated) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: {} (conf={:.2f})",
                           core::bottleneck_type_to_string(r.type), r.confidence);
            return r;
        }
    }

    // Rule 7 — Mixed
    {
        auto r = try_classify_mixed(analysis, metrics);
        if (r.type == core::BottleneckType::Mixed) {
            AGRA_LOG_DEBUG("Classifier", "Verdict: Mixed (conf={:.2f})", r.confidence);
            return r;
        }
    }

    // Rule 8 — Safe default (Unknown)
    BottleneckResult unknown{};
    unknown.type       = core::BottleneckType::Unknown;
    unknown.confidence = 0.20;
    unknown.explanation =
        "Insufficient signal to determine the primary bottleneck type. "
        "Workload may be in a transitional state or metrics are still stabilising. "
        "AGRA will continue sampling and re-evaluate on the next interval. "
        "No allocation change is made.";
    unknown.requires_action = false;

    AGRA_LOG_DEBUG("Classifier", "Verdict: Unknown (conf=0.20) — insufficient signal");
    return unknown;
}

} // namespace agra::analysis
