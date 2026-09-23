#include "test_framework.hpp"
#include "agra/discovery/cpu_topology.hpp"

using namespace agra::discovery;

AGRA_TEST_CASE("Discovery - CPU Topology Detection") {
    auto topo_res = CpuTopologyDetector::detect();
    AGRA_CHECK(topo_res.is_ok());

    const auto& topo = topo_res.value();
    AGRA_CHECK(!topo.cpu_brand_string.empty());
    AGRA_CHECK(topo.physical_core_count > 0);
    AGRA_CHECK(topo.logical_processor_count >= topo.physical_core_count);
    AGRA_CHECK(topo.all_cores_mask > 0);
    AGRA_CHECK(topo.logical_processors.size() == topo.logical_processor_count);

    std::string summary = topo.to_summary_string();
    AGRA_CHECK(!summary.empty());
    AGRA_CHECK(summary.find("Processor:") != std::string::npos);
}
