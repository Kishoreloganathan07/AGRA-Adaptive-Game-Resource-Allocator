#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"
#include "agra/core/config.hpp"

#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace agra::discovery {

struct DiscoveredProcess {
    core::ProcessId pid{0};
    std::string name;
    std::string executable_path;
    std::uint32_t thread_count{0};
    core::PriorityLevel priority{core::PriorityLevel::Normal};
    core::AffinityMask affinity_mask{0};
    std::uint64_t working_set_mb{0};
    bool is_elevated{false};
    bool is_system_protected{false};
    bool is_user_excluded{false};
    bool is_game_candidate{false};
};

class ProcessEnumerator {
public:
    static std::vector<DiscoveredProcess> enumerate_all(const core::AppConfig& config);
    static core::Result<DiscoveredProcess, core::Error> inspect_process(core::ProcessId pid, const core::AppConfig& config);
};

} // namespace agra::discovery
