#pragma once

#include "agra/game/game_profile.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace agra::game {

struct KnownGameEntry {
    std::string title;
    std::string executable_name;
    core::PolicyMode recommended_mode;
    core::PriorityLevel recommended_priority;
    bool is_cpu_heavy;
    std::string genre;
};

class GameDatabase {
public:
    static const std::vector<KnownGameEntry>& get_known_games();
    static std::optional<KnownGameEntry> find_by_executable(std::string_view exe_name);
    static bool is_known_game(std::string_view exe_name);
};

} // namespace agra::game
