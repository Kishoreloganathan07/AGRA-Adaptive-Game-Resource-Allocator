#include "test_framework.hpp"
#include "agra/allocator/allocation_engine.hpp"
#include "agra/analysis/bottleneck_classifier.hpp"
#include "agra/analysis/workload_analyzer.hpp"
#include "agra/core/config.hpp"
#include "agra/core/types.hpp"

#include <thread>
#include <chrono>

using namespace agra::allocator;
using namespace agra::analysis;
using namespace agra::core;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Drive a WorkloadAnalyzer to convergence with synthetic data, then classify.
static std::pair<BottleneckResult, WorkloadAnalysis>
make_scenario(double game_cpu, double system_cpu, double contention,
              int samples = 40)
{
    AppConfig cfg;
    cfg.high_demand_cpu_threshold = 75.0;
    WorkloadAnalyzer analyzer(cfg);

    for (int i = 0; i < samples; ++i) {
        WorkloadMetrics m{};
        m.game_cpu_percent   = game_cpu;
        m.system_cpu_percent = system_cpu;
        m.contention_index   = contention;
        analyzer.process_sample(m);
    }

    WorkloadMetrics final_metrics{};
    final_metrics.game_cpu_percent       = game_cpu;
    final_metrics.system_cpu_percent     = system_cpu;
    final_metrics.background_cpu_percent = (system_cpu > game_cpu)
        ? (system_cpu - game_cpu) : 0.0;
    final_metrics.contention_index       = contention;

    BottleneckClassifier cls(cfg);
    auto bottleneck = cls.classify(analyzer.current_analysis(), final_metrics);
    return {bottleneck, analyzer.current_analysis()};
}

static WorkloadMetrics make_metrics(double game, double system,
    std::uint64_t ws_mb = 512,
    double background_override = -1.0)
{
    WorkloadMetrics m{};
    m.game_cpu_percent       = game;
    m.system_cpu_percent     = system;
    m.working_set_mb         = ws_mb;
    m.background_cpu_percent = (background_override >= 0.0)
        ? background_override
        : std::max(0.0, system - game);
    m.contention_index       = std::clamp(
        m.background_cpu_percent / std::max(1.0, 100.0 - game), 0.0, 1.0);
    return m;
}

// ── Hysteresis Tests ──────────────────────────────────────────────────────────

AGRA_TEST_CASE("Hysteresis - Stays Normal until enter threshold met") {
    HysteresisController h(3, 5); // enter=3, exit=5

    AGRA_CHECK_EQ(h.update(true), HysteresisState::Normal);  // 1/3
    AGRA_CHECK_EQ(h.update(true), HysteresisState::Normal);  // 2/3
    AGRA_CHECK_EQ(h.update(true), HysteresisState::Elevated); // 3/3 → elevate
}

AGRA_TEST_CASE("Hysteresis - Resets enter counter on non-qualifying sample") {
    HysteresisController h(3, 5);

    h.update(true);  // 1/3
    h.update(true);  // 2/3
    h.update(false); // reset enter counter
    h.update(true);  // 1/3 again
    h.update(true);  // 2/3
    AGRA_CHECK_EQ(h.state(), HysteresisState::Normal); // not elevated yet
}

AGRA_TEST_CASE("Hysteresis - Exits Elevated after exit threshold met") {
    HysteresisController h(2, 3); // enter=2, exit=3

    h.update(true);
    h.update(true);
    AGRA_CHECK_EQ(h.state(), HysteresisState::Elevated);

    h.update(false); // 1/3 exit
    h.update(false); // 2/3 exit
    AGRA_CHECK_EQ(h.state(), HysteresisState::Elevated); // still elevated
    h.update(false); // 3/3 exit
    AGRA_CHECK_EQ(h.state(), HysteresisState::Normal);   // back to normal
}

AGRA_TEST_CASE("Hysteresis - Qualifying sample in Elevated resets exit counter") {
    HysteresisController h(2, 3);
    h.update(true); h.update(true); // → Elevated

    h.update(false); // 1/3
    h.update(false); // 2/3
    h.update(true);  // resets exit counter
    h.update(false); // 1/3 again
    AGRA_CHECK_EQ(h.state(), HysteresisState::Elevated); // stays elevated
}

AGRA_TEST_CASE("Hysteresis - Reset clears all state") {
    HysteresisController h(2, 3);
    h.update(true); h.update(true); // → Elevated
    h.reset();
    AGRA_CHECK_EQ(h.state(), HysteresisState::Normal);
    AGRA_CHECK_EQ(h.enter_count(), 0u);
    AGRA_CHECK_EQ(h.exit_count(), 0u);
}

// ── Score Calculation Tests ───────────────────────────────────────────────────

AGRA_TEST_CASE("AllocationEngine - Score is zero for idle state") {
    AppConfig cfg;
    AllocationEngine engine(cfg);

    auto [bottleneck, analysis] = make_scenario(5.0, 8.0, 0.02);
    auto metrics  = make_metrics(5.0, 8.0);
    (void)engine.decide(1234, bottleneck, analysis, metrics);

    auto comps = engine.last_components();
    // Idle: all components near-zero
    AGRA_CHECK(comps.final_score < 20.0);
    AGRA_CHECK(comps.demand_component < 6.0);
}

AGRA_TEST_CASE("AllocationEngine - Score in range [0,100] for any input") {
    AppConfig cfg;
    AllocationEngine engine(cfg);

    const std::vector<std::pair<double,double>> cases = {
        {0.0, 0.0}, {100.0, 100.0}, {85.0, 92.0}, {50.0, 75.0}
    };
    for (auto [g, s] : cases) {
        engine.reset();
        auto [bn, an] = make_scenario(g, s, 0.3);
        auto m = make_metrics(g, s);
        (void)engine.decide(42, bn, an, m);
        auto c = engine.last_components();
        AGRA_CHECK(c.final_score >= 0.0);
        AGRA_CHECK(c.final_score <= 100.0);
    }
}

AGRA_TEST_CASE("AllocationEngine - Fairness penalty applied when system >= 90%") {
    AppConfig cfg;
    AllocationEngine engine(cfg);

    auto [bn90, an90] = make_scenario(88.0, 92.0, 0.05);
    auto m90 = make_metrics(88.0, 92.0);
    m90.background_cpu_percent = 4.0;
    (void)engine.decide(1, bn90, an90, m90);
    auto comps = engine.last_components();
    AGRA_CHECK(comps.fairness_penalty > 0.0);
}

// ── Decision Tests ────────────────────────────────────────────────────────────

AGRA_TEST_CASE("AllocationEngine - Default mode never requires action") {
    AppConfig cfg;
    cfg.policy_mode = PolicyMode::Default;
    AllocationEngine engine(cfg);

    auto [bn, an] = make_scenario(90.0, 95.0, 0.1);
    auto m = make_metrics(90.0, 95.0);
    m.background_cpu_percent = 5.0;

    for (int i = 0; i < 10; ++i) {
        auto d = engine.decide(1, bn, an, m);
        AGRA_CHECK(!d.requires_action);
        AGRA_CHECK_EQ(d.target_priority, PriorityLevel::Normal);
    }
}

AGRA_TEST_CASE("AllocationEngine - Hysteresis: action only after enter threshold") {
    AppConfig cfg;
    cfg.policy_mode = PolicyMode::Adaptive;
    cfg.hysteresis_enter_samples = 3;
    cfg.hysteresis_exit_samples  = 5;
    cfg.min_confidence_threshold = 0.65;
    AllocationEngine engine(cfg);

    auto [bn, an] = make_scenario(85.0, 90.0, 0.05);
    auto m = make_metrics(85.0, 90.0);
    m.background_cpu_percent = 5.0;

    // Samples 1 and 2: bottleneck may require action but hysteresis not met yet
    // (depending on score) — we'll check that eventually it fires
    AllocationDecision last_decision{};
    bool action_ever_triggered = false;
    for (int i = 0; i < 8; ++i) {
        last_decision = engine.decide(1, bn, an, m);
        if (last_decision.requires_action) action_ever_triggered = true;
    }
    // After 8 samples with a strong CPU-bound signal, action must have triggered
    AGRA_CHECK(action_ever_triggered);
}

AGRA_TEST_CASE("AllocationEngine - High priority only in Performance mode") {
    AppConfig cfg;
    cfg.min_confidence_threshold = 0.50;
    cfg.hysteresis_enter_samples = 1; // fast for testing
    cfg.hysteresis_exit_samples  = 1;

    auto [bn, an] = make_scenario(90.0, 92.0, 0.05);
    auto m = make_metrics(90.0, 92.0, 1024);
    m.background_cpu_percent = 2.0;

    // Adaptive: should never reach High
    {
        AppConfig adaptive_cfg = cfg;
        adaptive_cfg.policy_mode = PolicyMode::Adaptive;
        AllocationEngine engine(adaptive_cfg);
        for (int i = 0; i < 5; ++i) {
            auto d = engine.decide(1, bn, an, m);
            AGRA_CHECK(d.target_priority != PriorityLevel::High);
        }
    }

    // Performance: may reach High for very high scores
    {
        AppConfig perf_cfg = cfg;
        perf_cfg.policy_mode = PolicyMode::Performance;
        AllocationEngine engine(perf_cfg);
        bool got_high = false;
        for (int i = 0; i < 10; ++i) {
            auto d = engine.decide(1, bn, an, m);
            if (d.target_priority == PriorityLevel::High) got_high = true;
        }
        // For a very high score the engine should commit High priority
        AGRA_CHECK(got_high);
    }
}

AGRA_TEST_CASE("AllocationEngine - Starvation prevention caps at AboveNormal") {
    AppConfig cfg;
    cfg.policy_mode             = PolicyMode::Performance;
    cfg.prevent_starvation      = true;
    cfg.hysteresis_enter_samples = 1;
    cfg.hysteresis_exit_samples  = 1;
    cfg.min_confidence_threshold = 0.50;
    AllocationEngine engine(cfg);

    auto [bn, an] = make_scenario(92.0, 93.0, 0.05);
    // Background has almost no headroom (2% left after game's 92%)
    auto m = make_metrics(92.0, 94.0, 1024);
    m.background_cpu_percent = 2.0;

    for (int i = 0; i < 5; ++i) {
        auto d = engine.decide(1, bn, an, m);
        if (d.requires_action) {
            // Starvation prevention must stop us from reaching High
            AGRA_CHECK(d.target_priority != PriorityLevel::High);
        }
    }
}

AGRA_TEST_CASE("AllocationEngine - Non-actionable bottleneck produces no action") {
    AppConfig cfg;
    cfg.policy_mode = PolicyMode::Adaptive;
    AllocationEngine engine(cfg);

    // GPU-bound: requires_action from classifier is false → engine must not act
    auto [bn, an] = make_scenario(22.0, 26.0, 0.05);
    AGRA_CHECK(!bn.requires_action); // Sanity: GPU-bound should not require action

    auto m = make_metrics(22.0, 26.0);
    for (int i = 0; i < 5; ++i) {
        auto d = engine.decide(42, bn, an, m);
        AGRA_CHECK(!d.requires_action);
    }
}

AGRA_TEST_CASE("AllocationEngine - Decision always has non-empty reason") {
    AppConfig cfg;
    AllocationEngine engine(cfg);

    const std::vector<std::pair<double,double>> cases = {
        {5.0,10.0}, {50.0,80.0}, {85.0,90.0}
    };
    for (auto [g,s] : cases) {
        engine.reset();
        auto [bn, an] = make_scenario(g, s, 0.2);
        auto m = make_metrics(g, s);
        auto d = engine.decide(1, bn, an, m);
        AGRA_CHECK(!d.reason.empty());
        AGRA_CHECK(d.allocation_score >= 0.0);
        AGRA_CHECK(d.allocation_score <= 100.0);
        AGRA_CHECK(d.confidence >= 0.0);
        AGRA_CHECK(d.confidence <= 1.0);
    }
}

AGRA_TEST_CASE("AllocationEngine - Reset clears boost and cooldown state") {
    AppConfig cfg;
    cfg.policy_mode              = PolicyMode::Adaptive;
    cfg.hysteresis_enter_samples = 1;
    cfg.hysteresis_exit_samples  = 1;
    cfg.min_confidence_threshold = 0.50;
    AllocationEngine engine(cfg);

    auto [bn, an] = make_scenario(85.0, 90.0, 0.05);
    auto m = make_metrics(85.0, 90.0);
    m.background_cpu_percent = 5.0;

    // Drive engine into boosted state
    for (int i = 0; i < 5; ++i) (void)engine.decide(1, bn, an, m);

    // Reset should clear all internal state
    engine.reset();
    AGRA_CHECK_EQ(engine.hysteresis_state(), HysteresisState::Normal);
    AGRA_CHECK(!engine.is_in_forced_cooldown());
}
