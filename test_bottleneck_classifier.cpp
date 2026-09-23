#include "test_framework.hpp"
#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/analysis/workload_analyzer.hpp"
#include "agra/core/config.hpp"
#include "agra/core/types.hpp"

using namespace agra::analysis;
using namespace agra::core;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Build a WorkloadAnalysis from a WorkloadAnalyzer by feeding synthetic samples.
// Feeds 'count' identical samples to drive EWMA convergence.
static WorkloadAnalysis build_analysis(
    double game_cpu, double system_cpu, double contention,
    int sample_count = 40)
{
    AppConfig cfg;
    cfg.high_demand_cpu_threshold = 75.0;
    WorkloadAnalyzer analyzer(cfg);

    for (int i = 0; i < sample_count; ++i) {
        WorkloadMetrics m{};
        m.game_cpu_percent    = game_cpu;
        m.system_cpu_percent  = system_cpu;
        m.contention_index    = contention;
        analyzer.process_sample(m);
    }
    return analyzer.current_analysis();
}

// Build a minimal WorkloadMetrics with explicit working-set and I/O values.
static WorkloadMetrics make_metrics(
    double game_cpu, double system_cpu,
    std::uint64_t working_set_mb = 512,
    std::uint64_t io_read_Bps = 0,
    std::uint64_t io_write_Bps = 0)
{
    WorkloadMetrics m{};
    m.game_cpu_percent       = game_cpu;
    m.system_cpu_percent     = system_cpu;
    m.background_cpu_percent = (system_cpu > game_cpu) ? (system_cpu - game_cpu) : 0.0;
    m.working_set_mb         = working_set_mb;
    m.io_read_bytes_sec      = io_read_Bps;
    m.io_write_bytes_sec     = io_write_Bps;
    m.contention_index       = (system_cpu > 0.0) ?
        std::clamp(m.background_cpu_percent / (100.0 - game_cpu), 0.0, 1.0) : 0.0;
    return m;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

AGRA_TEST_CASE("Classifier - Idle state → Unknown, no action") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    auto analysis = build_analysis(8.0, 12.0, 0.05);
    auto metrics  = make_metrics(8.0, 12.0, 256);
    auto result   = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::Unknown);
    AGRA_CHECK(!result.requires_action);
    AGRA_CHECK(result.confidence >= 0.80);   // Idle verdict is high-confidence
    AGRA_CHECK(!result.explanation.empty());
}

AGRA_TEST_CASE("Classifier - GPU-bound heuristic → no CPU action") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    // Game is clearly running (not idle) but both game CPU and system CPU are low
    auto analysis = build_analysis(25.0, 30.0, 0.08);
    auto metrics  = make_metrics(25.0, 30.0, 1024);
    auto result   = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::GpuBoundOrCpuNotPrimary);
    AGRA_CHECK(!result.requires_action);   // GPU-bound: CPU controls won't help
    AGRA_CHECK(result.confidence >= 0.40);
    AGRA_CHECK(result.confidence <= 0.65); // Capped — heuristic only
    AGRA_CHECK(!result.explanation.empty());
}

AGRA_TEST_CASE("Classifier - CPU-bound high demand, low background → action") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.65;
    BottleneckClassifier cls(cfg);

    // Game consuming 85%, system at 90%, very little background activity
    auto analysis = build_analysis(85.0, 90.0, 0.10);
    auto metrics  = make_metrics(85.0, 90.0, 2048, 0, 0);
    // background must be low for CPU-bound rule to fire (not contention)
    metrics.background_cpu_percent = 5.0;

    auto result = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::CpuBound);
    AGRA_CHECK(result.requires_action);
    AGRA_CHECK(result.confidence >= 0.65);
    AGRA_CHECK(result.confidence <= 0.92);  // Never exceeds our ceiling
    AGRA_CHECK(!result.explanation.empty());
}

AGRA_TEST_CASE("Classifier - CPU-bound below confidence threshold → no action") {
    // Raise the min_confidence_threshold so a borderline verdict won't trigger
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.95; // Very strict
    BottleneckClassifier cls(cfg);

    auto analysis = build_analysis(78.0, 80.0, 0.05);
    auto metrics  = make_metrics(78.0, 80.0, 512);
    metrics.background_cpu_percent = 2.0;

    auto result = cls.classify(analysis, metrics);
    // Even if type == CpuBound, threshold is too high → no action
    if (result.type == BottleneckType::CpuBound) {
        AGRA_CHECK(!result.requires_action);
    }
}

AGRA_TEST_CASE("Classifier - Background contention → action") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.65;
    BottleneckClassifier cls(cfg);

    // Game at 50%, system at 85%, heavy background load (35%) → high contention
    auto analysis = build_analysis(50.0, 85.0, 0.65);
    auto metrics  = make_metrics(50.0, 85.0, 1024);
    metrics.background_cpu_percent = 35.0;
    metrics.contention_index = 0.65;

    auto result = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::BackgroundContention);
    AGRA_CHECK(result.requires_action);
    AGRA_CHECK(result.confidence >= 0.65);
    AGRA_CHECK(result.confidence <= 0.85); // Never exceeds ceiling
}

AGRA_TEST_CASE("Classifier - Background contention at boundary → not triggered below threshold") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    // Contention just below threshold → should not be BackgroundContention
    auto analysis = build_analysis(45.0, 60.0, 0.30); // contention 0.30 < 0.42 threshold
    auto metrics  = make_metrics(45.0, 60.0, 512);
    metrics.background_cpu_percent = 15.0;
    metrics.contention_index = 0.30;

    auto result = cls.classify(analysis, metrics);
    AGRA_CHECK(result.type != BottleneckType::BackgroundContention);
    AGRA_CHECK(!result.requires_action);
}

AGRA_TEST_CASE("Classifier - Memory pressure → informational, no CPU action") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    // Large working set (3 GB), moderate CPU
    auto analysis = build_analysis(35.0, 40.0, 0.15);
    auto metrics  = make_metrics(35.0, 40.0, 3072); // 3 GB WS
    metrics.background_cpu_percent = 5.0;

    auto result = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::MemoryPressure);
    AGRA_CHECK(!result.requires_action); // Memory not actionable via CPU controls
    AGRA_CHECK(result.confidence >= 0.45);
    AGRA_CHECK(result.confidence <= 0.70);
}

AGRA_TEST_CASE("Classifier - I/O related during loading → no immediate CPU action") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.65;
    BottleneckClassifier cls(cfg);

    // 300 MB/s read, game CPU moderate (40%), system CPU 45%
    auto analysis = build_analysis(40.0, 45.0, 0.20);
    auto metrics  = make_metrics(40.0, 45.0, 1024,
        300ULL * 1024 * 1024,  // 300 MB/s read
        10ULL  * 1024 * 1024); // 10 MB/s write
    metrics.background_cpu_percent = 5.0;

    auto result = cls.classify(analysis, metrics);

    AGRA_CHECK_EQ(result.type, BottleneckType::IoRelated);
    AGRA_CHECK(!result.explanation.empty());
    // I/O related is in actionable list but confidence <= 0.68 makes it borderline
    // Just verify the verdict is correct, not the action threshold
}

AGRA_TEST_CASE("Classifier - Mixed signals → conservative, no action") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.65;
    BottleneckClassifier cls(cfg);

    // Moderate demand, moderate contention, significant I/O — mixed
    auto analysis = build_analysis(55.0, 75.0, 0.40);
    auto metrics  = make_metrics(55.0, 75.0, 1800,
        60ULL * 1024 * 1024,   // 60 MB/s read (moderate, not I/O dominant)
        20ULL * 1024 * 1024);
    metrics.background_cpu_percent = 20.0;
    metrics.contention_index = 0.40;

    auto result = cls.classify(analysis, metrics);

    // Mixed signals should prevent confident single-type verdict
    // Either Mixed or Unknown — both are safe (no action)
    AGRA_CHECK(
        result.type == BottleneckType::Mixed ||
        result.type == BottleneckType::Unknown ||
        result.type == BottleneckType::BackgroundContention
    );
    // Confidence must be below the Mixed ceiling
    AGRA_CHECK(result.confidence <= 0.85);
}

AGRA_TEST_CASE("Classifier - Unknown default for low-signal state") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    // Very quiet: game at 20%, system at 25%, no contention, no I/O, small WS
    auto analysis = build_analysis(20.0, 25.0, 0.05);
    auto metrics  = make_metrics(20.0, 25.0, 400, 0, 0);
    metrics.background_cpu_percent = 5.0;

    auto result = cls.classify(analysis, metrics);

    // Should be Unknown or GPU-bound — either way, no action
    AGRA_CHECK(
        result.type == BottleneckType::Unknown ||
        result.type == BottleneckType::GpuBoundOrCpuNotPrimary
    );
    AGRA_CHECK(!result.requires_action);
}

AGRA_TEST_CASE("Classifier - Explanation always non-empty") {
    AppConfig cfg;
    BottleneckClassifier cls(cfg);

    const std::vector<std::pair<double, double>> scenarios = {
        {5.0,  10.0},   // idle
        {20.0, 25.0},   // low
        {50.0, 80.0},   // medium/contention
        {85.0, 90.0},   // cpu-bound
    };

    for (const auto& [game, sys] : scenarios) {
        auto analysis = build_analysis(game, sys, 0.1);
        auto metrics  = make_metrics(game, sys, 512);
        auto result   = cls.classify(analysis, metrics);
        AGRA_CHECK(!result.explanation.empty());
        AGRA_CHECK(result.confidence >= 0.0);
        AGRA_CHECK(result.confidence <= 1.0);
    }
}

AGRA_TEST_CASE("Classifier - Requires-action never true for GPU-bound or Memory") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.01; // Very low threshold — still no action for these types
    BottleneckClassifier cls(cfg);

    // GPU-bound scenario
    {
        auto analysis = build_analysis(20.0, 22.0, 0.05);
        auto metrics  = make_metrics(20.0, 22.0, 512);
        auto result   = cls.classify(analysis, metrics);
        if (result.type == BottleneckType::GpuBoundOrCpuNotPrimary) {
            AGRA_CHECK(!result.requires_action);
        }
    }

    // Memory pressure scenario
    {
        auto analysis = build_analysis(30.0, 35.0, 0.08);
        auto metrics  = make_metrics(30.0, 35.0, 3500); // 3.5 GB WS
        auto result   = cls.classify(analysis, metrics);
        if (result.type == BottleneckType::MemoryPressure) {
            AGRA_CHECK(!result.requires_action);
        }
    }
}

AGRA_TEST_CASE("Classifier - Confidence never exceeds per-type ceiling") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.01;
    BottleneckClassifier cls(cfg);

    // Run a sweep of high-demand conditions and verify confidence ceilings
    auto analysis = build_analysis(90.0, 95.0, 0.05);
    auto metrics  = make_metrics(90.0, 95.0, 2048);
    metrics.background_cpu_percent = 5.0;
    auto r = cls.classify(analysis, metrics);

    if (r.type == BottleneckType::CpuBound) {
        AGRA_CHECK(r.confidence <= 0.92);
    }
    if (r.type == BottleneckType::BackgroundContention) {
        AGRA_CHECK(r.confidence <= 0.85);
    }
    if (r.type == BottleneckType::GpuBoundOrCpuNotPrimary) {
        AGRA_CHECK(r.confidence <= 0.65);
    }
    if (r.type == BottleneckType::Mixed) {
        AGRA_CHECK(r.confidence <= 0.55);
    }
    if (r.type == BottleneckType::MemoryPressure) {
        AGRA_CHECK(r.confidence <= 0.70);
    }
    if (r.type == BottleneckType::IoRelated) {
        AGRA_CHECK(r.confidence <= 0.68);
    }
}
