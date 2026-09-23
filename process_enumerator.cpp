#include "agra/discovery/process_enumerator.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>

namespace agra::discovery {

namespace {

std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

core::PriorityLevel win32_priority_to_enum(DWORD dwPriority) {
    switch (dwPriority) {
        case IDLE_PRIORITY_CLASS: return core::PriorityLevel::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS: return core::PriorityLevel::BelowNormal;
        case NORMAL_PRIORITY_CLASS: return core::PriorityLevel::Normal;
        case ABOVE_NORMAL_PRIORITY_CLASS: return core::PriorityLevel::AboveNormal;
        case HIGH_PRIORITY_CLASS:
        case REALTIME_PRIORITY_CLASS: return core::PriorityLevel::High; // Safety clamp
        default: return core::PriorityLevel::Normal;
    }
}

} // namespace

core::Result<DiscoveredProcess, core::Error> ProcessEnumerator::inspect_process(core::ProcessId pid, const core::AppConfig& config) {
    DiscoveredProcess proc{};
    proc.pid = pid;

    if (pid == 0) {
        proc.name = "System Idle Process";
        proc.is_system_protected = true;
        return proc;
    }
    if (pid == 4) {
        proc.name = "System";
        proc.is_system_protected = true;
        return proc;
    }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        // Fallback: check Toolhelp32Snapshot to identify the process name
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == pid) {
                        proc.name = wide_to_utf8(pe.szExeFile);
                        proc.thread_count = pe.cntThreads;
                        proc.is_system_protected = config.is_process_protected(proc.name);
                        proc.is_user_excluded = config.is_process_user_excluded(proc.name);
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }

        if (!proc.name.empty()) {
            return proc;
        }

        return core::Error::last_win32("OpenProcess failed for inspection");
    }

    // Query executable path
    WCHAR path_buf[MAX_PATH * 2] = {0};
    DWORD path_size = sizeof(path_buf) / sizeof(WCHAR);
    if (QueryFullProcessImageNameW(hProc, 0, path_buf, &path_size)) {
        proc.executable_path = wide_to_utf8(path_buf);

        // Extract filename
        size_t last_slash = proc.executable_path.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            proc.name = proc.executable_path.substr(last_slash + 1);
        } else {
            proc.name = proc.executable_path;
        }
    }

    // Query priority class
    DWORD prio = GetPriorityClass(hProc);
    if (prio != 0) {
        proc.priority = win32_priority_to_enum(prio);
    }

    // Query affinity
    DWORD_PTR proc_affinity = 0, sys_affinity = 0;
    if (GetProcessAffinityMask(hProc, &proc_affinity, &sys_affinity)) {
        proc.affinity_mask = static_cast<core::AffinityMask>(proc_affinity);
    }

    // Query memory working set
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(hProc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        proc.working_set_mb = pmc.WorkingSetSize / (1024 * 1024);
    }

    CloseHandle(hProc);

    // Apply classifications from config
    proc.is_system_protected = config.is_process_protected(proc.name);
    proc.is_user_excluded = config.is_process_user_excluded(proc.name);

    return proc;
}

std::vector<DiscoveredProcess> ProcessEnumerator::enumerate_all(const core::AppConfig& config) {
    std::vector<DiscoveredProcess> processes;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        AGRA_LOG_ERROR("ProcessEnumerator", "CreateToolhelp32Snapshot failed: {}", GetLastError());
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0) continue; // Skip System Idle Process

            DiscoveredProcess proc{};
            proc.pid = entry.th32ProcessID;
            proc.name = wide_to_utf8(entry.szExeFile);
            proc.thread_count = entry.cntThreads;

            proc.is_system_protected = config.is_process_protected(proc.name);
            proc.is_user_excluded = config.is_process_user_excluded(proc.name);

            // Attempt detailed inspection where permitted by Windows security
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, proc.pid);
            if (hProc) {
                WCHAR path_buf[MAX_PATH * 2] = {0};
                DWORD path_size = sizeof(path_buf) / sizeof(WCHAR);
                if (QueryFullProcessImageNameW(hProc, 0, path_buf, &path_size)) {
                    proc.executable_path = wide_to_utf8(path_buf);
                }

                DWORD prio = GetPriorityClass(hProc);
                if (prio != 0) {
                    proc.priority = win32_priority_to_enum(prio);
                }

                DWORD_PTR proc_aff = 0, sys_aff = 0;
                if (GetProcessAffinityMask(hProc, &proc_aff, &sys_aff)) {
                    proc.affinity_mask = static_cast<core::AffinityMask>(proc_aff);
                }

                PROCESS_MEMORY_COUNTERS_EX pmc{};
                if (GetProcessMemoryInfo(hProc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                    proc.working_set_mb = pmc.WorkingSetSize / (1024 * 1024);
                }

                CloseHandle(hProc);
            }

            processes.push_back(std::move(proc));
        } while (Process32NextW(hSnapshot, &entry));
    }

    CloseHandle(hSnapshot);
    return processes;
}

} // namespace agra::discovery
