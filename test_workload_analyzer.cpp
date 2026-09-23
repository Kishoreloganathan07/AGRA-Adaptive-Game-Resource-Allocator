#include "test_framework.hpp"
#include "agra/analysis/workload_analyzer.hpp"
#include "agra/core/config.hpp"

using namespace agra::analysis;

AGRA_TEST_CASE("Analysis - Workload Spike vs Sustained Detection") {
    agra::core::AppConfig config;
    config.high_demand_cpu_threshold = 75.0;

    WorkloadAnalyzer analyzer(config);

    // Feed baseline low activity (10%)
    for (int i = 0; i < 20; ++i) {
        agra::core::WorkloadMetrics m{};
        m.game_cpu_percent = 10.0;
        m.system_cpu_percent = 15.0;
        analyzer.process_sample(m);
    }

    auto analysis = analyzer.current_analysis();
    AGRA_CHECK(analysis.is_idle_or_menu);
    AGRA_CHECK(!analysis.is_sustained_heavy);
    AGRA_CHECK(!analysis.is_transient_spike);
    AGRA_CHECK(analysis.estimated_demand < 0.25);

    // Feed a single transient spike (85%)
    agra::core::WorkloadMetrics spike{};
    spike.game_cpu_percent = 85.0;
    spike.system_cpu_percent = 90.0;
    analyzer.process_sample(spike);

    analysis = analyzer.current_analysis();
    AGRA_CHECK(analysis.is_transient_spike);
    AGRA_CHECK(!analysis.is_sustained_heavy); // Not sustained yet!

    // Feed sustained heavy workload for 25 samples
    for (int i = 0; i < 25; ++i) {
        agra::core::WorkloadMetrics heavy{};
        heavy.game_cpu_percent = 85.0;
        heavy.system_cpu_percent = 90.0;
        analyzer.process_sample(heavy);
    }

    analysis = analyzer.current_analysis();
    AGRA_CHECK(analysis.is_sustained_heavy);
    AGRA_CHECK(!analysis.is_transient_spike);
    AGRA_CHECK(analysis.estimated_demand > 0.70);
    AGRA_CHECK(analysis.stability_score > 0.80);
}
