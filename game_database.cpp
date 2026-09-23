#include "agra/game/game_database.hpp"

#include <algorithm>
#include <cctype>

namespace agra::game {

namespace {

std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

const std::vector<KnownGameEntry> KNOWN_GAMES = {
    {"Cyberpunk 2077", "Cyberpunk2077.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Open World RPG"},
    {"Valorant", "VALORANT-Win64-Shipping.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Tactical Shooter"},
    {"Counter-Strike 2", "cs2.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Competitive FPS"},
    {"Fortnite", "FortniteClient-Win64-Shipping.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Battle Royale"},
    {"Overwatch 2", "Overwatch.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Hero Shooter"},
    {"Apex Legends", "r5apex.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Battle Royale"},
    {"Elden Ring", "eldenring.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, false, "Action RPG"},
    {"Starfield", "Starfield.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Space RPG"},
    {"Grand Theft Auto V", "GTA5.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Open World"},
    {"The Witcher 3", "witcher3.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "RPG"},
    {"Dota 2", "dota2.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "MOBA"},
    {"League of Legends", "League of Legends.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, false, "MOBA"},
    {"Black Myth: Wukong", "b1-Win64-Shipping.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Action RPG"},
    {"Baldur's Gate 3", "bg3.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "CRPG"},
    {"Baldur's Gate 3 (DX11)", "bg3_dx11.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "CRPG"},
    {"Call of Duty: Modern Warfare", "cod.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Shooter"},
    {"Hogwarts Legacy", "HogwartsLegacy.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Action RPG"},
    {"Rust", "RustClient.exe", core::PolicyMode::Adaptive, core::PriorityLevel::AboveNormal, true, "Survival"}
};

} // namespace

const std::vector<KnownGameEntry>& GameDatabase::get_known_games() {
    return KNOWN_GAMES;
}

std::optional<KnownGameEntry> GameDatabase::find_by_executable(std::string_view exe_name) {
    std::string lower_target = to_lower(exe_name);
    for (const auto& game : KNOWN_GAMES) {
        if (to_lower(game.executable_name) == lower_target) {
            return game;
        }
    }
    return std::nullopt;
}

bool GameDatabase::is_known_game(std::string_view exe_name) {
    return find_by_executable(exe_name).has_value();
}

} // namespace agra::game
