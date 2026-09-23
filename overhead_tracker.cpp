#include "agra/monitoring/overhead_tracker.hpp"

#include <windows.h>
#include <psapi.h>
#include <algorithm>

namespace agra::monitoring {

namespace {

std::uint64_t file_time_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

} // namespace

OverheadTracker::OverheadTracker() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    logical_cores_ = si.dwNumberOfProcessors;
    if (logical_cores_ == 0) logical_cores_ = 1;
    prev_time_ = std::chrono::steady_clock::now();
}

void OverheadTracker::record_sample_duration(std::uint64_t duration_us) {
    last_sample_duration_us_ = duration_us;
    sample_count_++;
}

AgraOverheadStats OverheadTracker::sample() {
    AgraOverheadStats stats{};
    stats.sampling_duration_us = last_sample_duration_us_;
    stats.total_samples_collected = sample_count_;

    HANDLE hProc = GetCurrentProcess();

    // Memory footprint
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(hProc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        stats.agra_memory_mb = pmc.WorkingSetSize / (1024 * 1024);
    }

    // CPU usage
    FILETIME creation, exit, kernel, user;
    if (GetProcessTimes(hProc, &creation, &exit, &kernel, &user)) {
        std::uint64_t cur_kernel = file_time_to_uint64(kernel);
        std::uint64_t cur_user = file_time_to_uint64(user);
        auto now = std::chrono::steady_clock::now();

        if (first_) {
            prev_kernel_ = cur_kernel;
            prev_user_ = cur_user;
            prev_time_ = now;
            first_ = false;
            return stats;
        }

        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - prev_time_).count();
        if (elapsed_us > 0) {
            std::uint64_t delta_k = (cur_kernel >= prev_kernel_) ? (cur_kernel - prev_kernel_) : 0;
            std::uint64_t delta_u = (cur_user >= prev_user_) ? (cur_user - prev_user_) : 0;
            double cpu_us = static_cast<double>(delta_k + delta_u) / 10.0;
            double pct = (cpu_us / (static_cast<double>(elapsed_us) * logical_cores_)) * 100.0;
            stats.agra_cpu_percent = std::clamp(pct, 0.0, 100.0);
        }

        prev_kernel_ = cur_kernel;
        prev_user_ = cur_user;
        prev_time_ = now;
    }

    return stats;
}

} // namespace agra::monitoring
