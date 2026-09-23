#pragma once

#include "agra/core/types.hpp"
#include <cstdint>
#include <chrono>

namespace agra::monitoring {

struct AgraOverheadStats {
    double agra_cpu_percent{0.0};
    std::uint64_t agra_memory_mb{0};
    std::uint64_t sampling_duration_us{0};
    std::uint64_t total_samples_collected{0};
};

class OverheadTracker {
public:
    OverheadTracker();

    void record_sample_duration(std::uint64_t duration_us);
    [[nodiscard]] AgraOverheadStats sample();

private:
    std::uint64_t prev_kernel_{0};
    std::uint64_t prev_user_{0};
    std::chrono::steady_clock::time_point prev_time_;
    std::uint32_t logical_cores_{1};
    std::uint64_t last_sample_duration_us_{0};
    std::uint64_t sample_count_{0};
    bool first_{true};
};

} // namespace agra::monitoring
