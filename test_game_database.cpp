#include "test_framework.hpp"
#include "agra/game/game_database.hpp"

using namespace agra::game;

AGRA_TEST_CASE("GameDatabase - Query Known Games") {
    const auto& list = GameDatabase::get_known_games();
    AGRA_CHECK(list.size() >= 10);

    AGRA_CHECK(GameDatabase::is_known_game("Cyberpunk2077.exe"));
    AGRA_CHECK(GameDatabase::is_known_game("cyberpunk2077.exe")); // Case insensitivity
    AGRA_CHECK(GameDatabase::is_known_game("cs2.exe"));
    AGRA_CHECK(!GameDatabase::is_known_game("notepad.exe"));

    auto cp_opt = GameDatabase::find_by_executable("Cyberpunk2077.exe");
    AGRA_CHECK(cp_opt.has_value());
    AGRA_CHECK_EQ(cp_opt->title, "Cyberpunk 2077");
    AGRA_CHECK(cp_opt->is_cpu_heavy);
}
