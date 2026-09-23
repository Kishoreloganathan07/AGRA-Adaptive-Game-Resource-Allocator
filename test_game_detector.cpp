#include "test_framework.hpp"
#include "agra/game/game_detector.hpp"
#include "agra/core/config.hpp"

#include <windows.h>

using namespace agra::game;

AGRA_TEST_CASE("GameDetector - Attach by PID") {
    agra::core::AppConfig config;
    GameDetector detector(config);

    DWORD current_pid = ::GetCurrentProcessId();
    auto attach_res = detector.attach_to_pid(current_pid);
    AGRA_CHECK(attach_res.is_ok());

    const auto& game = attach_res.value();
    AGRA_CHECK_EQ(game.pid, current_pid);
    AGRA_CHECK_EQ(game.detection_method, "ManualOverride");
    AGRA_CHECK_EQ(game.confidence, 1.0);
}

AGRA_TEST_CASE("GameDetector - Protection Rejection") {
    agra::core::AppConfig config;
    GameDetector detector(config);

    // PID 4 is Windows System process
    auto attach_system = detector.attach_to_pid(4);
    AGRA_CHECK(attach_system.is_err());
    AGRA_CHECK(attach_system.error().message().find("system-protected") != std::string::npos);
}
