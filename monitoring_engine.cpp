#include "agra/monitoring/monitoring_engine.hpp"
#include "agra/core/event_bus.hpp"
#include "agra/core/logger.hpp"

#include <chrono>
#include <algorithm>

namespace agra::monitoring {

MonitoringEngine::MonitoringEngine(const core::AppConfig& config)
    : config_(config) {}

MonitoringEngine::~MonitoringEngine() {
    stop();
}

core::Result<void, core::Error> MonitoringEngine::start(core::ProcessId target_pid) {
    if (is_running_.load()) {
        return core::Result<void, core::Error>(); // Already running
    }

    target_pid_.store(target_pid);
    if (target_pid != 0) {
        auto attach_res = process_sampler_.attach(target_pid);
        if (attach_res.is_err()) {
            AGRA_LOG_WARN("MonitoringEngine", "Could not attach to PID {}: {}", target_pid, attach_res.error().message());
        }
    }

    cpu_sampler_.reset();
    is_running_.store(true);
    worker_thread_ = std::thread(&MonitoringEngine::monitoring_loop, this);

    AGRA_LOG_INFO("MonitoringEngine", "Monitoring engine started (interval: {} ms, target PID: {})",
                  config_.sampling_interval_ms, target_pid);

    return core::Result<void, core::Error>();
}

void MonitoringEngine::stop() {
    if (!is_running_.load()) {
        return;
    }

    is_running_.store(false);
    cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    process_sampler_.detach();
    AGRA_LOG_INFO("MonitoringEngine", "Monitoring engine stopped.");
}

void MonitoringEngine::set_target_process(core::ProcessId pid) {
    target_pid_.store(pid);
    if (pid != 0) {
        process_sampler_.attach(pid);
    } else {
        process_sampler_.detach();
    }
}

core::WorkloadMetrics MonitoringEngine::get_latest_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return latest_metrics_;
}

AgraOverheadStats MonitoringEngine::get_overhead_stats() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return latest_overhead_;
}

void MonitoringEngine::monitoring_loop() {
    using namespace std::chrono;

    while (is_running_.load()) {
        auto sample_start = steady_clock::now();

        // 1. Sample System CPU
        auto sys_cpu = cpu_sampler_.sample();

        // 2. Sample Process metrics if attached
        core::ProcessId pid = target_pid_.load();
        ProcessSampleData proc_data{};
        if (pid != 0) {
            proc_data = process_sampler_.sample();
            if (!proc_data.is_alive && process_sampler_.is_attached()) {
                AGRA_LOG_WARN("MonitoringEngine", "Target process PID {} terminated or exited.", pid);
                core::GameExitedEvent exit_ev;
                exit_ev.pid = pid;
                core::EventBus::instance().publish(exit_ev);
                target_pid_.store(0);
            }
        }

        // 3. Compute Metrics
        core::WorkloadMetrics metrics{};
        metrics.timestamp = sample_start;
        metrics.system_cpu_percent = sys_cpu.total_cpu_percent;
        metrics.game_cpu_percent = proc_data.cpu_percent;
        metrics.background_cpu_percent = (std::max)(0.0, sys_cpu.total_cpu_percent - proc_data.cpu_percent);
        metrics.working_set_mb = proc_data.working_set_mb;
        metrics.io_read_bytes_sec = proc_data.io_read_bytes_sec;
        metrics.io_write_bytes_sec = proc_data.io_write_bytes_sec;

        // Contention Index Calculation:
        // C = Background CPU / (Available capacity for background)
        // Scaled to [0.0, 1.0]
        double remaining_cpu = (std::max)(1.0, 100.0 - metrics.game_cpu_percent);
        metrics.contention_index = std::clamp(metrics.background_cpu_percent / remaining_cpu, 0.0, 1.0);

        // 4. Sample AGRA Overhead
        auto sample_end = steady_clock::now();
        auto sample_duration_us = duration_cast<microseconds>(sample_end - sample_start).count();
        overhead_tracker_.record_sample_duration(static_cast<std::uint64_t>(sample_duration_us));
        auto overhead = overhead_tracker_.sample();

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            latest_metrics_ = metrics;
            latest_overhead_ = overhead;
        }

        // 5. Emit Event
        core::WorkloadSampleEvent event{};
        event.pid = pid;
        event.metrics = metrics;
        core::EventBus::instance().publish(event);

        // 6. Sleep for remaining sampling interval
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, milliseconds(config_.sampling_interval_ms), [this]() {
            return !is_running_.load();
        });
    }
}

} // namespace agra::monitoring
