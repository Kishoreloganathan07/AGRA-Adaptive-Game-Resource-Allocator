#include "agra/monitoring/cpu_sampler.hpp"

#include <windows.h>
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

CpuSampler::CpuSampler() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    logical_processor_count_ = si.dwNumberOfProcessors;
    if (logical_processor_count_ == 0) logical_processor_count_ = 1;
    reset();
}

void CpuSampler::reset() {
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        prev_idle_time_ = file_time_to_uint64(idle);
        prev_kernel_time_ = file_time_to_uint64(kernel);
        prev_user_time_ = file_time_to_uint64(user);
        first_sample_ = true;
    }
}

SystemCpuSnapshot CpuSampler::sample() {
    SystemCpuSnapshot snapshot{};

    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return snapshot;
    }

    std::uint64_t cur_idle = file_time_to_uint64(idle);
    std::uint64_t cur_kernel = file_time_to_uint64(kernel);
    std::uint64_t cur_user = file_time_to_uint64(user);

    if (first_sample_) {
        prev_idle_time_ = cur_idle;
        prev_kernel_time_ = cur_kernel;
        prev_user_time_ = cur_user;
        first_sample_ = false;
        return snapshot;
    }

    std::uint64_t diff_idle = (cur_idle >= prev_idle_time_) ? (cur_idle - prev_idle_time_) : 0;
    std::uint64_t diff_kernel = (cur_kernel >= prev_kernel_time_) ? (cur_kernel - prev_kernel_time_) : 0;
    std::uint64_t diff_user = (cur_user >= prev_user_time_) ? (cur_user - prev_user_time_) : 0;

    prev_idle_time_ = cur_idle;
    prev_kernel_time_ = cur_kernel;
    prev_user_time_ = cur_user;

    std::uint64_t total_sys = diff_kernel + diff_user;

    if (total_sys > 0) {
        // In Windows NT, kernel time includes idle time
        std::uint64_t active_kernel = (diff_kernel >= diff_idle) ? (diff_kernel - diff_idle) : 0;
        std::uint64_t total_active = active_kernel + diff_user;

        snapshot.total_cpu_percent = std::clamp((static_cast<double>(total_active) / static_cast<double>(total_sys)) * 100.0, 0.0, 100.0);
        snapshot.kernel_cpu_percent = std::clamp((static_cast<double>(active_kernel) / static_cast<double>(total_sys)) * 100.0, 0.0, 100.0);
        snapshot.user_cpu_percent = std::clamp((static_cast<double>(diff_user) / static_cast<double>(total_sys)) * 100.0, 0.0, 100.0);
    }

    return snapshot;
}

} // namespace agra::monitoring
