#include "test_framework.hpp"
#include "agra/core/config.hpp"

#include <cstdio>

using namespace agra::core;

AGRA_TEST_CASE("Config - Default Validation") {
    AppConfig config;
    auto val_res = config.validate();
    AGRA_CHECK(val_res.is_ok());
}

AGRA_TEST_CASE("Config - Invalid Values Caught by Validator") {
    AppConfig invalid_interval;
    invalid_interval.sampling_interval_ms = 10; // Too low (< 50)
    AGRA_CHECK(invalid_interval.validate().is_err());

    AppConfig invalid_thresholds;
    invalid_thresholds.low_demand_cpu_threshold = 80.0;
    invalid_thresholds.high_demand_cpu_threshold = 70.0; // Inverted
    AGRA_CHECK(invalid_thresholds.validate().is_err());

    AppConfig invalid_confidence;
    invalid_confidence.min_confidence_threshold = 1.5; // > 1.0
    AGRA_CHECK(invalid_confidence.validate().is_err());
}

AGRA_TEST_CASE("Config - Protected Process Identification") {
    AppConfig config;
    AGRA_CHECK(config.is_process_protected("csrss.exe"));
    AGRA_CHECK(config.is_process_protected("CSRSS.EXE")); // Case insensitivity
    AGRA_CHECK(config.is_process_protected("explorer.exe"));
    AGRA_CHECK(config.is_process_protected("lsass.exe"));
    AGRA_CHECK(config.is_process_protected("services.exe"));
    AGRA_CHECK(!config.is_process_protected("MyCustomGame.exe"));
}

AGRA_TEST_CASE("Config - JSON Save and Reload Roundtrip") {
    const std::string test_file = "test_config_temp.json";

    ConfigManager mgr;
    AppConfig cfg = mgr.get();
    cfg.sampling_interval_ms = 350;
    cfg.policy_mode = PolicyMode::Performance;
    cfg.high_demand_cpu_threshold = 85.0;
    cfg.low_demand_cpu_threshold = 45.0;
    mgr.set(cfg);

    auto save_res = mgr.save_to_file(test_file);
    AGRA_CHECK(save_res.is_ok());

    ConfigManager mgr2;
    auto load_res = mgr2.load_from_file(test_file);
    AGRA_CHECK(load_res.is_ok());

    AGRA_CHECK_EQ(mgr2.get().sampling_interval_ms, 350u);
    AGRA_CHECK_EQ(static_cast<int>(mgr2.get().policy_mode), static_cast<int>(PolicyMode::Performance));
    AGRA_CHECK_EQ(mgr2.get().high_demand_cpu_threshold, 85.0);
    AGRA_CHECK_EQ(mgr2.get().low_demand_cpu_threshold, 45.0);

    std::remove(test_file.c_str());
}
