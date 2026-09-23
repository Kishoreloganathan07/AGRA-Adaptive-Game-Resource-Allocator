#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <vector>
#include <cstdint>

namespace agra::monitoring {

struct SystemCpuSnapshot {
    double total_cpu_percent{0.0};
    double kernel_cpu_percent{0.0};
    double user_cpu_percent{0.0};
    std::vector<double> per_core_percent;
};

class CpuSampler {
public:
    CpuSampler();

    // Computes CPU utilization delta since last call
    [[nodiscard]] SystemCpuSnapshot sample();

    void reset();

private:
    std::uint64_t prev_idle_time_{0};
    std::uint64_t prev_kernel_time_{0};
    std::uint64_t prev_user_time_{0};
    std::uint32_t logical_processor_count_{1};
    bool first_sample_{true};
};

} // namespace agra::monitoring
