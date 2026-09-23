#pragma once

#include "agra/benchmark/benchmark_runner.hpp"
#include "agra/session/session_manager.hpp"
#include "agra/core/error.hpp"

#include <string>

namespace agra::benchmark {

class ReportExporter {
public:
    // Export benchmark suite results
    [[nodiscard]] static std::string to_json(const BenchmarkSuiteResult& result);
    [[nodiscard]] static std::string to_csv(const BenchmarkSuiteResult& result);

    // Export live session summary
    [[nodiscard]] static std::string session_to_json(const session::SessionSnapshot& session_snap);
    [[nodiscard]] static std::string session_to_csv(const session::SessionSnapshot& session_snap);

    // Save directly to file
    [[nodiscard]] static core::Result<void, core::Error> save_to_file(
        const std::string& filepath,
        const std::string& content);
};

} // namespace agra::benchmark
