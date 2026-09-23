#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/core/error.hpp"
#include "agra/game/game_profile.hpp"
#include "agra/discovery/process_enumerator.hpp"

#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace agra::game {

struct DetectedGame {
    core::ProcessId pid{0};
    std::string title;
    std::string executable_name;
    std::string executable_path;
    bool is_fullscreen{false};
    bool is_in_database{false};
    double confidence{0.0};
    std::string detection_method;
    GameProfile active_profile;
};

class GameDetector {
public:
    explicit GameDetector(const core::AppConfig& config);

    // Primary detection probe
    [[nodiscard]] std::vector<DetectedGame> scan_for_games();
    [[nodiscard]] std::optional<DetectedGame> get_primary_active_game();

    // Manual selection override
    [[nodiscard]] core::Result<DetectedGame, core::Error> attach_to_pid(core::ProcessId pid);
    [[nodiscard]] core::Result<DetectedGame, core::Error> attach_by_executable_name(std::string_view exe_name);

private:
    const core::AppConfig& config_;
    bool is_window_fullscreen(void* hwnd);
};

} // namespace agra::game
