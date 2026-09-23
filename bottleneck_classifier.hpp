#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/analysis/workload_analyzer.hpp"

#include <string>

namespace agra::analysis {

// ─────────────────────────────────────────────────────────────────────────────
// BottleneckResult
//
// Immutable verdict produced by BottleneckClassifier::classify().
// Every field is filled; callers must never act on Unknown/low-confidence
// results — the classifier enforces that via requires_action.
// ─────────────────────────────────────────────────────────────────────────────
struct BottleneckResult {
    core::BottleneckType type{core::BottleneckType::Unknown};

    // 0.0 = no idea, 1.0 = certain.
    // Rule-based ceiling per classification; never inflated.
    double confidence{0.0};

    // Human-readable explanation suitable for UI / logs / export.
    // Format: one sentence per contributing factor.
    std::string explanation;

    // True only when confidence >= AppConfig::min_confidence_threshold
    // AND type is actionable (not Unknown / GPU-bound / idle).
    // Callers MUST check this before modifying any process state.
    bool requires_action{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// BottleneckClassifier
//
// Stateless conservative rule-based classifier.
//
// Design principles:
//   • Conservative: when in doubt → Unknown, requires_action = false.
//   • Explainable: every verdict includes a human-readable reason.
//   • No side effects: classify() is const / pure.
//   • Confidence caps per rule reflect their inherent certainty limits.
//
// Classification priority order (first matching rule wins):
//   1. Idle / menu state → Unknown (no action)
//   2. GPU-bound heuristic (low game CPU + low system CPU)
//   3. CPU-bound (high demand + high system utilization)
//   4. Background contention (high contention index + medium demand)
//   5. Memory pressure (large working set + low CPU)
//   6. I/O related (high I/O + medium demand + moderate CPU)
//   7. Mixed (multiple signals above threshold simultaneously)
//   8. Unknown (default conservative fallback)
// ─────────────────────────────────────────────────────────────────────────────
class BottleneckClassifier {
public:
    explicit BottleneckClassifier(const core::AppConfig& config);

    // Primary classification entry-point.
    // Takes the latest WorkloadAnalysis (smoothed trends, demand estimate,
    // stability score) and the raw WorkloadMetrics snapshot (for I/O,
    // memory, per-core data) and returns an immutable verdict.
    [[nodiscard]] BottleneckResult classify(
        const WorkloadAnalysis& analysis,
        const core::WorkloadMetrics& metrics) const;

private:
    const core::AppConfig& config_;

    // Internal helpers — each returns a partial BottleneckResult.
    // Confidence values are intentionally conservative:
    //   CPU_BOUND          max 0.92
    //   BACKGROUND_CONTENTION max 0.85
    //   GPU_BOUND          max 0.65 (user-mode heuristic only)
    //   MEMORY_PRESSURE    max 0.70
    //   IO_RELATED         max 0.68
    //   MIXED              max 0.55

    [[nodiscard]] BottleneckResult try_classify_idle(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_gpu_bound(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_cpu_bound(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_background_contention(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_memory_pressure(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_io_related(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    [[nodiscard]] BottleneckResult try_classify_mixed(
        const WorkloadAnalysis& a,
        const core::WorkloadMetrics& m) const;

    // Finalise a candidate result: if confidence >= threshold, set
    // requires_action = true (only for actionable types).
    [[nodiscard]] BottleneckResult finalise(BottleneckResult r) const;
};

} // namespace agra::analysis
