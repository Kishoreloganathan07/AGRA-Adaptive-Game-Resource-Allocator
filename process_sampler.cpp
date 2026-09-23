#include "agra/monitoring/process_sampler.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>

namespace agra::monitoring {

namespace {

std::uint64_t file_time_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

std::uint32_t count_process_threads(DWORD pid) {
    std::uint32_t count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid) {
                    count++;
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
    }
    return count;
}

} // namespace

ProcessSampler::ProcessSampler() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    logical_cores_ = si.dwNumberOfProcessors;
    if (logical_cores_ == 0) logical_cores_ = 1;
}

ProcessSampler::~ProcessSampler() {
    detach();
}

core::Result<void, core::Error> ProcessSampler::attach(core::ProcessId pid) {
    detach();

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return core::Error::last_win32("Failed to open process for monitoring");
    }

    target_pid_ = pid;
    process_handle_ = h;
    first_sample_ = true;
    prev_sample_time_ = std::chrono::steady_clock::now();

    AGRA_LOG_INFO("ProcessSampler", "Attached monitoring to PID {}", pid);
    return core::Result<void, core::Error>();
}

void ProcessSampler::detach() {
    if (process_handle_) {
        CloseHandle(static_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }
    target_pid_ = 0;
    first_sample_ = true;
}

ProcessSampleData ProcessSampler::sample() {
    ProcessSampleData data{};
    data.pid = target_pid_;

    if (!process_handle_ || target_pid_ == 0) {
        return data;
    }

    HANDLE h = static_cast<HANDLE>(process_handle_);

    // Check if process still alive
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(h, &exit_code) || exit_code != STILL_ACTIVE) {
        data.is_alive = false;
        detach();
        return data;
    }
    data.is_alive = true;

    auto now = std::chrono::steady_clock::now();
    FILETIME creation, exit, kernel, user;

    if (!GetProcessTimes(h, &creation, &exit, &kernel, &user)) {
        return data;
    }

    std::uint64_t cur_kernel = file_time_to_uint64(kernel);
    std::uint64_t cur_user = file_time_to_uint64(user);

    // Memory info
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        data.working_set_mb = pmc.WorkingSetSize / (1024 * 1024);
    }

    // IO counters
    IO_COUNTERS io{};
    std::uint64_t cur_io_read = 0;
    std::uint64_t cur_io_write = 0;
    if (GetProcessIoCounters(h, &io)) {
        cur_io_read = io.ReadTransferCount;
        cur_io_write = io.WriteTransferCount;
    }

    data.thread_count = count_process_threads(target_pid_);

    if (first_sample_) {
        prev_kernel_time_ = cur_kernel;
        prev_user_time_ = cur_user;
        prev_io_read_bytes_ = cur_io_read;
        prev_io_write_bytes_ = cur_io_write;
        prev_sample_time_ = now;
        first_sample_ = false;
        return data;
    }

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - prev_sample_time_).count();
    if (elapsed_us <= 0) return data;

    std::uint64_t delta_kernel = (cur_kernel >= prev_kernel_time_) ? (cur_kernel - prev_kernel_time_) : 0;
    std::uint64_t delta_user = (cur_user >= prev_user_time_) ? (cur_user - prev_user_time_) : 0;
    // FILETIME units are 100-nanoseconds (0.1 us)
    double cpu_time_us = static_cast<double>(delta_kernel + delta_user) / 10.0;

    double cpu_usage = (cpu_time_us / (static_cast<double>(elapsed_us) * logical_cores_)) * 100.0;
    data.cpu_percent = std::clamp(cpu_usage, 0.0, 100.0);

    double elapsed_sec = static_cast<double>(elapsed_us) / 1'000'000.0;
    if (elapsed_sec > 0.0) {
        if (cur_io_read >= prev_io_read_bytes_) {
            data.io_read_bytes_sec = static_cast<std::uint64_t>((cur_io_read - prev_io_read_bytes_) / elapsed_sec);
        }
        if (cur_io_write >= prev_io_write_bytes_) {
            data.io_write_bytes_sec = static_cast<std::uint64_t>((cur_io_write - prev_io_write_bytes_) / elapsed_sec);
        }
    }

    prev_kernel_time_ = cur_kernel;
    prev_user_time_ = cur_user;
    prev_io_read_bytes_ = cur_io_read;
    prev_io_write_bytes_ = cur_io_write;
    prev_sample_time_ = now;

    return data;
}

} // namespace agra::monitoring
