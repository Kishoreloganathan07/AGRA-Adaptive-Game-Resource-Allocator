#include "test_framework.hpp"
#include "agra/game/game_profile.hpp"

#include <filesystem>

using namespace agra::game;

AGRA_TEST_CASE("GameProfile - Validation") {
    GameProfile valid_profile;
    valid_profile.profile_name = "Valorant";
    valid_profile.executable_name = "VALORANT-Win64-Shipping.exe";
    valid_profile.preferred_policy_mode = agra::core::PolicyMode::Adaptive;
    valid_profile.preferred_priority = agra::core::PriorityLevel::AboveNormal;

    AGRA_CHECK(valid_profile.validate().is_ok());

    GameProfile invalid_profile;
    invalid_profile.profile_name = ""; // Empty name
    AGRA_CHECK(invalid_profile.validate().is_err());
}

AGRA_TEST_CASE("GameProfile - Save and Load Roundtrip") {
    const std::string test_dir = "test_profiles_temp";

    GameProfile original;
    original.profile_name = "Witcher3_Test";
    original.executable_name = "witcher3.exe";
    original.preferred_policy_mode = agra::core::PolicyMode::Performance;
    original.preferred_priority = agra::core::PriorityLevel::High;
    original.custom_affinity_mask = 0x3F; // 6 cores
    original.throttle_background_processes = true;
    original.max_allowable_cpu_intervention = 90.0;
    original.notes = "Custom RPG profile";

    auto save_res = ProfileStorage::save_profile(original, test_dir);
    AGRA_CHECK(save_res.is_ok());

    auto load_res = ProfileStorage::load_profile("Witcher3_Test", test_dir);
    AGRA_CHECK(load_res.is_ok());

    const auto& loaded = load_res.value();
    AGRA_CHECK_EQ(loaded.profile_name, "Witcher3_Test");
    AGRA_CHECK_EQ(loaded.executable_name, "witcher3.exe");
    AGRA_CHECK_EQ(static_cast<int>(loaded.preferred_priority), static_cast<int>(agra::core::PriorityLevel::High));
    AGRA_CHECK_EQ(loaded.custom_affinity_mask, 0x3Fu);
    AGRA_CHECK(loaded.throttle_background_processes);

    std::filesystem::remove_all(test_dir);
}
