#include "test_framework.hpp"
#include "agra/discovery/process_enumerator.hpp"
#include "agra/core/config.hpp"

#include <windows.h>

using namespace agra::discovery;

AGRA_TEST_CASE("Discovery - Process Enumeration") {
    agra::core::AppConfig config;
    auto processes = ProcessEnumerator::enumerate_all(config);

    AGRA_CHECK(!processes.empty());

    // Current process must be in the list
    DWORD current_pid = ::GetCurrentProcessId();
    bool found_self = false;

    for (const auto& proc : processes) {
        if (proc.pid == current_pid) {
            found_self = true;
            AGRA_CHECK(proc.thread_count > 0);
            break;
        }
    }

    AGRA_CHECK(found_self);
}

AGRA_TEST_CASE("Discovery - Inspect Specific Process") {
    agra::core::AppConfig config;
    DWORD current_pid = ::GetCurrentProcessId();

    auto inspect_res = ProcessEnumerator::inspect_process(current_pid, config);
    AGRA_CHECK(inspect_res.is_ok());

    const auto& self_info = inspect_res.value();
    AGRA_CHECK_EQ(self_info.pid, current_pid);
    AGRA_CHECK(!self_info.name.empty());
    AGRA_CHECK(!self_info.executable_path.empty());
}
