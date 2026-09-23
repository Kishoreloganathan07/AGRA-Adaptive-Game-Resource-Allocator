#pragma once

#include "agra/core/types.hpp"
#include "agra/core/error.hpp"

#include <cstdint>
#include <chrono>

namespace agra::monitoring {

struct ProcessSampleData {
    core::ProcessId pid{0};
    double cpu_percent{0.0};
    std::uint64_t working_set_mb{0};
    std::uint64_t io_read_bytes_sec{0};
    std::uint64_t io_write_bytes_sec{0};
    std::uint32_t thread_count{0};
    bool is_alive{false};
};

class ProcessSampler {
public:
    ProcessSampler();
    ~ProcessSampler();

    core::Result<void, core::Error> attach(core::ProcessId pid);
    void detach();

    [[nodiscard]] ProcessSampleData sample();
    [[nodiscard]] bool is_attached() const noexcept { return target_pid_ != 0; }
    [[nodiscard]] core::ProcessId attached_pid() const noexcept { return target_pid_; }

private:
    core::ProcessId target_pid_{0};
    void* process_handle_{nullptr}; // HANDLE
    std::uint32_t logical_cores_{1};

    std::uint64_t prev_kernel_time_{0};
    std::uint64_t prev_user_time_{0};
    std::uint64_t prev_io_read_bytes_{0};
    std::uint64_t prev_io_write_bytes_{0};
    std::chrono::steady_clock::time_point prev_sample_time_;
    bool first_sample_{true};
};

} // namespace agra::monitoring
