#include "test_framework.hpp"
#include "agra/core/logger.hpp"

int main() {
    // Disable file logging during unit tests, keep console quiet or minimal
    agra::core::Logger::instance().init(agra::core::LogSeverity::Warning, "");

    int result = agra::test::TestRegistry::instance().run_all();

    agra::core::Logger::instance().shutdown();
    return result;
}
