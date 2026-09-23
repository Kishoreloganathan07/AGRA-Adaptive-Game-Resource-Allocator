#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace agra::game {

struct GameProfile {
    std::string profile_name;
    std::string executable_name;          // e.g. "Cyberpunk2077.exe"
    std::vector<std::string> secondary_executables; // child launchers / anti-cheat wrappers
    
    // Resource policies
    core::PolicyMode preferred_policy_mode{core::PolicyMode::Adaptive};
    core::PriorityLevel preferred_priority{core::PriorityLevel::AboveNormal};
    core::AffinityMask custom_affinity_mask{0}; // 0 = automatic / all cores
    std::vector<std::uint32_t> preferred_cpu_sets;

    // Background management
    bool throttle_background_processes{true};
    core::PriorityLevel background_reduced_priority{core::PriorityLevel::BelowNormal};

    // Safety thresholds
    double max_allowable_cpu_intervention{95.0};
    bool allow_automatic_rollback{true};

    // Custom notes / description
    std::string notes;

    [[nodiscard]] core::Result<void, core::Error> validate() const;
};

class ProfileStorage {
public:
    static core::Result<void, core::Error> save_profile(const GameProfile& profile, const std::string& directory = "profiles");
    static core::Result<GameProfile, core::Error> load_profile(const std::string& profile_name, const std::string& directory = "profiles");
    static std::vector<std::string> list_saved_profiles(const std::string& directory = "profiles");
};

} // namespace agra::game
