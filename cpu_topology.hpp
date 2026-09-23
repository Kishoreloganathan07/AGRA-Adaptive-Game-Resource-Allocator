#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <vector>
#include <string>
#include <cstdint>

namespace agra::discovery {

enum class CoreType {
    Standard,       // Uniform core or unknown
    Performance,    // P-Core (Efficiency class > 0)
    Efficiency      // E-Core (Efficiency class 0 on hybrid systems)
};

inline std::string_view core_type_to_string(CoreType type) noexcept {
    switch (type) {
        case CoreType::Performance: return "Performance (P-Core)";
        case CoreType::Efficiency: return "Efficiency (E-Core)";
        default: return "Standard";
    }
}

struct LogicalProcessor {
    std::uint32_t processor_index{0};
    std::uint32_t numa_node{0};
    std::uint32_t core_index{0};
    CoreType core_type{CoreType::Standard};
    std::uint8_t efficiency_class{0};
    core::AffinityMask single_core_mask{0};
};

struct PhysicalCore {
    std::uint32_t core_index{0};
    CoreType core_type{CoreType::Standard};
    std::uint8_t efficiency_class{0};
    std::vector<std::uint32_t> logical_processor_indices;
    core::AffinityMask affinity_mask{0};
};

struct CpuTopology {
    std::string cpu_brand_string;
    std::uint32_t physical_socket_count{0};
    std::uint32_t physical_core_count{0};
    std::uint32_t logical_processor_count{0};
    std::uint32_t numa_node_count{0};
    bool is_hybrid_architecture{false}; // P-cores + E-cores detected
    std::uint32_t p_core_count{0};
    std::uint32_t e_core_count{0};

    core::AffinityMask all_cores_mask{0};
    core::AffinityMask p_cores_mask{0};
    core::AffinityMask e_cores_mask{0};

    std::vector<PhysicalCore> physical_cores;
    std::vector<LogicalProcessor> logical_processors;

    [[nodiscard]] std::string to_summary_string() const;
};

class CpuTopologyDetector {
public:
    static core::Result<CpuTopology, core::Error> detect();
};

} // namespace agra::discovery
