#include "test_framework.hpp"
#include "agra/monitoring/process_sampler.hpp"

#include <windows.h>
#include <thread>
#include <chrono>

using namespace agra::monitoring;

AGRA_TEST_CASE("Monitoring - Process Sampler Attach and Sample Self") {
    ProcessSampler sampler;
    DWORD current_pid = ::GetCurrentProcessId();

    auto attach_res = sampler.attach(current_pid);
    AGRA_CHECK(attach_res.is_ok());
    AGRA_CHECK(sampler.is_attached());
    AGRA_CHECK_EQ(sampler.attached_pid(), current_pid);

    auto sample1 = sampler.sample();
    AGRA_CHECK(sample1.is_alive);
    AGRA_CHECK(sample1.working_set_mb > 0);
    AGRA_CHECK(sample1.thread_count > 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto sample2 = sampler.sample();
    AGRA_CHECK(sample2.is_alive);
    AGRA_CHECK(sample2.cpu_percent >= 0.0);
    AGRA_CHECK(sample2.cpu_percent <= 100.0);

    sampler.detach();
    AGRA_CHECK(!sampler.is_attached());
}
