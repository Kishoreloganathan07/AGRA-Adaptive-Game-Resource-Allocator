#include "test_framework.hpp"
#include "agra/core/types.hpp"

using namespace agra::core;

AGRA_TEST_CASE("CoreTypes - Policy Mode String Conversions") {
    AGRA_CHECK_EQ(policy_mode_to_string(PolicyMode::Default), "Default");
    AGRA_CHECK_EQ(policy_mode_to_string(PolicyMode::Conservative), "Conservative");
    AGRA_CHECK_EQ(policy_mode_to_string(PolicyMode::Adaptive), "Adaptive");
    AGRA_CHECK_EQ(policy_mode_to_string(PolicyMode::Performance), "Performance");
}

AGRA_TEST_CASE("CoreTypes - Priority Level String Conversions") {
    AGRA_CHECK_EQ(priority_level_to_string(PriorityLevel::Idle), "Idle");
    AGRA_CHECK_EQ(priority_level_to_string(PriorityLevel::BelowNormal), "BelowNormal");
    AGRA_CHECK_EQ(priority_level_to_string(PriorityLevel::Normal), "Normal");
    AGRA_CHECK_EQ(priority_level_to_string(PriorityLevel::AboveNormal), "AboveNormal");
    AGRA_CHECK_EQ(priority_level_to_string(PriorityLevel::High), "High");
}

AGRA_TEST_CASE("CoreTypes - Bottleneck String Conversions") {
    AGRA_CHECK_EQ(bottleneck_type_to_string(BottleneckType::CpuBound), "CPU-Bound");
    AGRA_CHECK_EQ(bottleneck_type_to_string(BottleneckType::GpuBoundOrCpuNotPrimary), "GPU-Bound or CPU Not Primary");
    AGRA_CHECK_EQ(bottleneck_type_to_string(BottleneckType::BackgroundContention), "Background Contention");
    AGRA_CHECK_EQ(bottleneck_type_to_string(BottleneckType::Unknown), "Unknown");
}

AGRA_TEST_CASE("CoreTypes - Result Value and Error Semantics") {
    Result<int, std::string> ok_res(42);
    AGRA_CHECK(ok_res.is_ok());
    AGRA_CHECK(!ok_res.is_err());
    AGRA_CHECK_EQ(ok_res.value(), 42);

    Result<int, std::string> err_res(std::string("failed"));
    AGRA_CHECK(!err_res.is_ok());
    AGRA_CHECK(err_res.is_err());
    AGRA_CHECK_EQ(err_res.error(), "failed");

    Result<void, std::string> void_ok;
    AGRA_CHECK(void_ok.is_ok());
    AGRA_CHECK(!void_ok.is_err());

    Result<void, std::string> void_err(std::string("void error"));
    AGRA_CHECK(!void_err.is_ok());
    AGRA_CHECK(void_err.is_err());
    AGRA_CHECK_EQ(void_err.error(), "void error");
}
