#include "test_framework.hpp"
#include "agra/core/logger.hpp"

using namespace agra::core;

AGRA_TEST_CASE("Logger - Severity Levels and Macros") {
    auto& logger = Logger::instance();
    logger.set_min_severity(LogSeverity::Warning);

    AGRA_CHECK_EQ(static_cast<int>(logger.min_severity()), static_cast<int>(LogSeverity::Warning));

    // Testing that macros invoke cleanly without exceptions
    AGRA_LOG_TRACE("Test", "Trace message - should be filtered");
    AGRA_LOG_DEBUG("Test", "Debug message - should be filtered");
    AGRA_LOG_WARN("Test", "Warning message: code = {}", 123);
    AGRA_LOG_ERROR("Test", "Error message: status = {}", "failed");

    logger.flush();
}
