#include "test_framework.hpp"
#include "agra/monitoring/monitoring_engine.hpp"
#include "agra/core/config.hpp"
#include "agra/core/event_bus.hpp"

#include <windows.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace agra::monitoring;

AGRA_TEST_CASE("Monitoring - Engine Background Loop and Event Dispatch") {
    agra::core::AppConfig config;
    config.sampling_interval_ms = 50; // Fast sampling for unit test

    MonitoringEngine engine(config);
    DWORD current_pid = ::GetCurrentProcessId();

    std::atomic<int> sample_event_count{0};
    auto sub_id = agra::core::EventBus::instance().subscribe<agra::core::WorkloadSampleEvent>(
        [&](const agra::core::WorkloadSampleEvent& ev) {
            if (ev.pid == current_pid) {
                sample_event_count++;
            }
        }
    );

    auto start_res = engine.start(current_pid);
    AGRA_CHECK(start_res.is_ok());
    AGRA_CHECK(engine.is_running());
    AGRA_CHECK_EQ(engine.current_target_pid(), current_pid);

    // Let the engine collect samples
    std::this_thread::sleep_for(std::chrono::milliseconds(220));

    engine.stop();
    AGRA_CHECK(!engine.is_running());

    agra::core::EventBus::instance().unsubscribe(sub_id);

    AGRA_CHECK(sample_event_count.load() >= 2);

    auto latest = engine.get_latest_metrics();
    AGRA_CHECK(latest.system_cpu_percent >= 0.0);
    AGRA_CHECK(latest.system_cpu_percent <= 100.0);
    AGRA_CHECK(latest.contention_index >= 0.0);
    AGRA_CHECK(latest.contention_index <= 1.0);

    auto overhead = engine.get_overhead_stats();
    AGRA_CHECK(overhead.total_samples_collected >= 2);
    AGRA_CHECK(overhead.agra_memory_mb > 0);
}
