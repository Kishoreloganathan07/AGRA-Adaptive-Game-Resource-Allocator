#include "test_framework.hpp"
#include "agra/discovery/system_info.hpp"

using namespace agra::discovery;

AGRA_TEST_CASE("Discovery - System Memory Information") {
    auto mem = SystemInfoProvider::query_memory();
    AGRA_CHECK(mem.total_physical_mb > 0);
    AGRA_CHECK(mem.available_physical_mb > 0);
    AGRA_CHECK(mem.available_physical_mb <= mem.total_physical_mb);
    AGRA_CHECK(mem.memory_load_percent <= 100);
}

AGRA_TEST_CASE("Discovery - Windows Version Information") {
    auto ver = SystemInfoProvider::query_windows_version();
    AGRA_CHECK(ver.major >= 10);
    AGRA_CHECK(ver.build_number > 0);
    AGRA_CHECK(!ver.display_name.empty());
}

AGRA_TEST_CASE("Discovery - Power Status") {
    auto power = SystemInfoProvider::query_power_status();
    // Power state can be AC or Battery on real laptops/desktops
    AGRA_CHECK(power.battery_life_percent <= 100);
}

AGRA_TEST_CASE("Discovery - System Capabilities") {
    auto caps = SystemInfoProvider::query_capabilities();
    // On Windows 10/11 build >= 14393, CPU sets are supported
    AGRA_CHECK(caps.cpu_sets_supported);
}
