#include "test_framework.hpp"
#include "agra/monitoring/cpu_sampler.hpp"

#include <thread>
#include <chrono>

using namespace agra::monitoring;

AGRA_TEST_CASE("Monitoring - System CPU Sampler") {
    CpuSampler sampler;

    // Initial sample establishes baseline
    auto snap1 = sampler.sample();

    // Sleep briefly to accumulate CPU cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto snap2 = sampler.sample();
    AGRA_CHECK(snap2.total_cpu_percent >= 0.0);
    AGRA_CHECK(snap2.total_cpu_percent <= 100.0);
    AGRA_CHECK(snap2.kernel_cpu_percent <= 100.0);
    AGRA_CHECK(snap2.user_cpu_percent <= 100.0);
}
