#pragma once

#include "agra/core/types.hpp"
#include "agra/core/config.hpp"
#include "agra/core/error.hpp"
#include "agra/monitoring/cpu_sampler.hpp"
#include "agra/monitoring/process_sampler.hpp"
#include "agra/monitoring/overhead_tracker.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace agra::monitoring {

class MonitoringEngine {
public:
    explicit MonitoringEngine(const core::AppConfig& config);
    ~MonitoringEngine();

    core::Result<void, core::Error> start(core::ProcessId target_pid = 0);
    void stop();

    void set_target_process(core::ProcessId pid);
    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }
    [[nodiscard]] core::ProcessId current_target_pid() const noexcept { return target_pid_.load(); }

    [[nodiscard]] core::WorkloadMetrics get_latest_metrics() const;
    [[nodiscard]] AgraOverheadStats get_overhead_stats() const;

private:
    void monitoring_loop();

    const core::AppConfig& config_;
    std::atomic<bool> is_running_{false};
    std::atomic<core::ProcessId> target_pid_{0};

    std::thread worker_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    mutable std::mutex metrics_mutex_;
    core::WorkloadMetrics latest_metrics_;
    AgraOverheadStats latest_overhead_;

    CpuSampler cpu_sampler_;
    ProcessSampler process_sampler_;
    OverheadTracker overhead_tracker_;
};

} // namespace agra::monitoring
