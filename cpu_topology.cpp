#include "agra/discovery/cpu_topology.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <sstream>
#include <format>
#include <algorithm>
#include <array>
#include <cstring>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace agra::discovery {

namespace {

std::string query_cpu_brand_string() {
    std::array<int, 4> cpu_info{};
    std::string brand;
    brand.reserve(48);

#if defined(_MSC_VER)
    __cpuid(cpu_info.data(), 0x80000000);
#else
    __cpuid(0x80000000, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif

    unsigned int nExIds = cpu_info[0];
    if (nExIds >= 0x80000004) {
        char brand_part[49] = {0};
        for (unsigned int i = 0x80000002; i <= 0x80000004; ++i) {
#if defined(_MSC_VER)
            __cpuid(cpu_info.data(), i);
#else
            __cpuid(i, cpu_info[0], cpu_info[1], cpu_info[2], cpu_info[3]);
#endif
            std::memcpy(brand_part + (i - 0x80000002) * 16, cpu_info.data(), 16);
        }
        brand = brand_part;
    }

    // Trim whitespace
    auto first = brand.find_first_not_of(" ");
    if (first != std::string::npos) {
        auto last = brand.find_last_not_of(" ");
        brand = brand.substr(first, last - first + 1);
    }

    return brand.empty() ? "Generic x86_64 Processor" : brand;
}

} // namespace

std::string CpuTopology::to_summary_string() const {
    std::ostringstream ss;
    ss << "Processor: " << cpu_brand_string << "\n"
       << "Topology: " << physical_socket_count << " Socket(s), "
       << physical_core_count << " Physical Core(s), "
       << logical_processor_count << " Logical Processor(s)\n"
       << "NUMA Nodes: " << numa_node_count << "\n"
       << "Architecture: " << (is_hybrid_architecture ? "Heterogeneous (Hybrid P/E-Cores)" : "Homogeneous") << "\n";
    if (is_hybrid_architecture) {
        ss << "  P-Cores: " << p_core_count << " (Affinity Mask: 0x" << std::hex << p_cores_mask << std::dec << ")\n"
           << "  E-Cores: " << e_core_count << " (Affinity Mask: 0x" << std::hex << e_cores_mask << std::dec << ")\n";
    }
    ss << "Total Affinity Mask: 0x" << std::hex << all_cores_mask << std::dec;
    return ss.str();
}

core::Result<CpuTopology, core::Error> CpuTopologyDetector::detect() {
    CpuTopology topo{};
    topo.cpu_brand_string = query_cpu_brand_string();

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return core::Error::last_win32("GetLogicalProcessorInformationEx initial buffer query failed");
    }

    std::vector<std::uint8_t> buffer(length);
    auto* ptr = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

    if (!GetLogicalProcessorInformationEx(RelationAll, ptr, &length)) {
        return core::Error::last_win32("GetLogicalProcessorInformationEx failed");
    }

    std::uint8_t* byte_ptr = buffer.data();
    std::uint8_t* end_ptr = buffer.data() + length;

    std::uint32_t core_idx = 0;
    std::uint8_t max_efficiency_class = 0;
    bool has_different_efficiencies = false;
    std::uint8_t first_efficiency_class = 0;
    bool first_core = true;

    while (byte_ptr < end_ptr) {
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(byte_ptr);

        if (info->Relationship == RelationProcessorCore) {
            PhysicalCore core{};
            core.core_index = core_idx++;
            core.efficiency_class = info->Processor.EfficiencyClass;

            if (first_core) {
                first_efficiency_class = core.efficiency_class;
                first_core = false;
            } else if (core.efficiency_class != first_efficiency_class) {
                has_different_efficiencies = true;
            }

            max_efficiency_class = (std::max)(max_efficiency_class, core.efficiency_class);

            // Group mask
            for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                KAFFINITY mask = info->Processor.GroupMask[g].Mask;
                core.affinity_mask |= static_cast<core::AffinityMask>(mask);

                for (std::uint32_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
                    if ((mask >> bit) & 1ULL) {
                        core.logical_processor_indices.push_back(bit);
                    }
                }
            }

            topo.all_cores_mask |= core.affinity_mask;
            topo.physical_cores.push_back(core);
            topo.physical_core_count++;
        } else if (info->Relationship == RelationNumaNode) {
            topo.numa_node_count++;
        } else if (info->Relationship == RelationProcessorPackage) {
            topo.physical_socket_count++;
        }

        byte_ptr += info->Size;
    }

    if (topo.physical_socket_count == 0) topo.physical_socket_count = 1;
    if (topo.numa_node_count == 0) topo.numa_node_count = 1;

    // Detect hybrid P/E core configuration
    if (has_different_efficiencies && max_efficiency_class > 0) {
        topo.is_hybrid_architecture = true;
        for (auto& core : topo.physical_cores) {
            if (core.efficiency_class == max_efficiency_class) {
                core.core_type = CoreType::Performance;
                topo.p_core_count++;
                topo.p_cores_mask |= core.affinity_mask;
            } else {
                core.core_type = CoreType::Efficiency;
                topo.e_core_count++;
                topo.e_cores_mask |= core.affinity_mask;
            }
        }
    } else {
        topo.is_hybrid_architecture = false;
        for (auto& core : topo.physical_cores) {
            core.core_type = CoreType::Standard;
        }
        topo.p_core_count = topo.physical_core_count;
        topo.p_cores_mask = topo.all_cores_mask;
    }

    // Build logical processor entries
    std::uint32_t log_idx = 0;
    for (const auto& core : topo.physical_cores) {
        for (std::uint32_t bit : core.logical_processor_indices) {
            LogicalProcessor lp{};
            lp.processor_index = log_idx++;
            lp.core_index = core.core_index;
            lp.core_type = core.core_type;
            lp.efficiency_class = core.efficiency_class;
            lp.single_core_mask = (1ULL << bit);
            topo.logical_processors.push_back(lp);
        }
    }
    topo.logical_processor_count = static_cast<std::uint32_t>(topo.logical_processors.size());

    AGRA_LOG_INFO("Topology", "CPU Topology Discovered: {} Physical Cores, {} Logical Cores (Hybrid: {})",
                  topo.physical_core_count, topo.logical_processor_count, topo.is_hybrid_architecture ? "YES" : "NO");

    return topo;
}

} // namespace agra::discovery
