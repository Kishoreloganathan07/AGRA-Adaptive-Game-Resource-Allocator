#include "agra/game/game_detector.hpp"
#include "agra/game/game_database.hpp"
#include "agra/core/logger.hpp"

#include <windows.h>
#include <algorithm>

namespace agra::game {

namespace {

std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

GameDetector::GameDetector(const core::AppConfig& config) : config_(config) {}

bool GameDetector::is_window_fullscreen(void* hwnd_ptr) {
    auto hwnd = static_cast<HWND>(hwnd_ptr);
    if (!hwnd || !IsWindow(hwnd)) return false;

    RECT app_bounds{};
    if (!GetWindowRect(hwnd, &app_bounds)) return false;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    return (app_bounds.left <= 0 && app_bounds.top <= 0 &&
            app_bounds.right >= screen_w && app_bounds.bottom >= screen_h);
}

std::vector<DetectedGame> GameDetector::scan_for_games() {
    std::vector<DetectedGame> candidates;
    auto processes = discovery::ProcessEnumerator::enumerate_all(config_);

    // Check foreground window
    HWND fg_hwnd = GetForegroundWindow();
    DWORD fg_pid = 0;
    std::string fg_title;
    bool fg_fullscreen = false;

    if (fg_hwnd && IsWindow(fg_hwnd)) {
        GetWindowThreadProcessId(fg_hwnd, &fg_pid);
        WCHAR title_buf[256] = {0};
        GetWindowTextW(fg_hwnd, title_buf, 256);
        fg_title = wide_to_utf8(title_buf);
        fg_fullscreen = is_window_fullscreen(fg_hwnd);
    }

    for (const auto& proc : processes) {
        if (proc.is_system_protected || proc.is_user_excluded) continue;

        // Check 1: Known Game Database
        auto db_entry = GameDatabase::find_by_executable(proc.name);
        if (db_entry.has_value()) {
            DetectedGame game{};
            game.pid = proc.pid;
            game.title = db_entry->title;
            game.executable_name = proc.name;
            game.executable_path = proc.executable_path;
            game.is_in_database = true;
            game.is_fullscreen = (proc.pid == fg_pid) && fg_fullscreen;
            game.detection_method = "DatabaseMatch";
            game.confidence = 0.95;

            // Load or construct profile
            auto loaded_profile = ProfileStorage::load_profile(db_entry->title);
            if (loaded_profile.is_ok()) {
                game.active_profile = loaded_profile.value();
            } else {
                game.active_profile.profile_name = db_entry->title;
                game.active_profile.executable_name = proc.name;
                game.active_profile.preferred_policy_mode = db_entry->recommended_mode;
                game.active_profile.preferred_priority = db_entry->recommended_priority;
            }

            candidates.push_back(std::move(game));
            continue;
        }

        // Check 2: Foreground Fullscreen Heuristic
        if (proc.pid == fg_pid && fg_fullscreen && proc.thread_count >= 8 && proc.working_set_mb > 150) {
            DetectedGame game{};
            game.pid = proc.pid;
            game.title = fg_title.empty() ? proc.name : fg_title;
            game.executable_name = proc.name;
            game.executable_path = proc.executable_path;
            game.is_in_database = false;
            game.is_fullscreen = true;
            game.detection_method = "ForegroundFullscreenHeuristic";
            game.confidence = 0.75;

            game.active_profile.profile_name = game.title;
            game.active_profile.executable_name = proc.name;
            game.active_profile.preferred_policy_mode = core::PolicyMode::Adaptive;
            game.active_profile.preferred_priority = core::PriorityLevel::AboveNormal;

            candidates.push_back(std::move(game));
        }
    }

    return candidates;
}

std::optional<DetectedGame> GameDetector::get_primary_active_game() {
    auto list = scan_for_games();
    if (list.empty()) return std::nullopt;

    // Prioritize fullscreen or highest confidence
    auto best = std::max_element(list.begin(), list.end(), [](const DetectedGame& a, const DetectedGame& b) {
        if (a.is_fullscreen != b.is_fullscreen) return !a.is_fullscreen;
        return a.confidence < b.confidence;
    });

    return *best;
}

core::Result<DetectedGame, core::Error> GameDetector::attach_to_pid(core::ProcessId pid) {
    auto inspect_res = discovery::ProcessEnumerator::inspect_process(pid, config_);
    if (inspect_res.is_err()) {
        return inspect_res.error();
    }

    const auto& proc = inspect_res.value();
    if (proc.is_system_protected) {
        return core::Error::safety("Cannot attach to system-protected critical process: " + proc.name);
    }

    DetectedGame game{};
    game.pid = pid;
    game.executable_name = proc.name;
    game.executable_path = proc.executable_path;
    game.detection_method = "ManualOverride";
    game.confidence = 1.0;

    auto db_entry = GameDatabase::find_by_executable(proc.name);
    if (db_entry.has_value()) {
        game.title = db_entry->title;
        game.is_in_database = true;
        game.active_profile.profile_name = db_entry->title;
        game.active_profile.executable_name = proc.name;
        game.active_profile.preferred_policy_mode = db_entry->recommended_mode;
        game.active_profile.preferred_priority = db_entry->recommended_priority;
    } else {
        game.title = proc.name;
        game.is_in_database = false;
        game.active_profile.profile_name = proc.name;
        game.active_profile.executable_name = proc.name;
        game.active_profile.preferred_policy_mode = core::PolicyMode::Adaptive;
        game.active_profile.preferred_priority = core::PriorityLevel::AboveNormal;
    }

    AGRA_LOG_INFO("GameDetector", "Manually attached to PID {} ('{}')", pid, game.title);
    return game;
}

core::Result<DetectedGame, core::Error> GameDetector::attach_by_executable_name(std::string_view exe_name) {
    auto processes = discovery::ProcessEnumerator::enumerate_all(config_);
    for (const auto& proc : processes) {
        if (proc.name == exe_name) {
            return attach_to_pid(proc.pid);
        }
    }
    return core::Error::not_found(std::string("No running process matched executable: ") + std::string(exe_name));
}

} // namespace agra::game
