#include "agra/ui/terminal_dashboard.hpp"
#include "agra/version.hpp"

#include <iostream>
#include <format>
#include <conio.h>
#include <windows.h>
#include <thread>
#include <chrono>

namespace agra::ui {

TerminalDashboard::TerminalDashboard(session::SessionManager& session)
    : session_(session)
{}

std::string TerminalDashboard::render_bar(double percent, int width) {
    if (width <= 0) width = 20;
    double clamped = (std::clamp)(percent, 0.0, 100.0);
    int filled = static_cast<int>((clamped / 100.0) * width);
    filled = (std::clamp)(filled, 0, width);
    int empty = width - filled;

    std::string bar = "[";
    for (int i = 0; i < filled; ++i) bar += "#";
    for (int i = 0; i < empty; ++i)  bar += "-";
    bar += std::format("] {:5.1f}%", clamped);
    return bar;
}

void TerminalDashboard::render_frame() const {
    auto s = session_.snapshot();

    // Position cursor at top-left
    std::cout << "\033[H";

    std::cout << "===============================================================================\n";
    std::cout << "  AGRA — Adaptive Game Resource Allocator v" << agra::VERSION_STRING << " | Real-Time Dashboard\n";
    std::cout << "===============================================================================\n";

    std::string status_str = s.is_active ? (s.safe_mode ? "ACTIVE [SAFE/READ-ONLY]" : "ACTIVE [OPTIMIZING]") : "IDLE / READY";
    std::cout << std::format(" Status:      {:<24} Target: {:<20} (PID {})\n",
                             status_str,
                             s.target_name.empty() ? "(None)" : s.target_name.substr(0, 19),
                             s.target_pid);
    std::cout << std::format(" Mode:        {:<24} Policy: {:<20} (Priority: {})\n",
                             core::policy_mode_to_string(s.active_mode),
                             s.latest_policy.is_game_boosted ? "ELEVATED" : "BASELINE",
                             core::priority_level_to_string(s.latest_policy.game_priority));
    std::cout << "-------------------------------------------------------------------------------\n";
    std::cout << " SYSTEM CPU & WORKLOAD DEMAND\n\n";

    std::cout << " Total System:      " << render_bar(s.latest_metrics.system_cpu_percent, 28) << "\n";
    std::cout << " Game Demand:       " << render_bar(s.latest_metrics.game_cpu_percent, 28) << "\n";
    std::cout << " Background Demand: " << render_bar(s.latest_metrics.background_cpu_percent, 28) << "\n";
    std::cout << " Contention Index:  " << render_bar(s.latest_metrics.contention_index * 100.0, 28) << "\n";
    std::cout << "-------------------------------------------------------------------------------\n";
    std::cout << " ADAPTIVE ANALYSIS & BOTTLENECK CLASSIFICATION\n\n";

    std::cout << std::format(" Bottleneck:  {:<26} Confidence: {:.0f}%\n",
                             core::bottleneck_type_to_string(s.latest_bottleneck.type),
                             s.latest_bottleneck.confidence * 100.0);
    std::cout << std::format(" Urgency:     {:<26} Score:      {:.1f} / 100\n",
                             core::allocation_urgency_to_string(s.latest_decision.urgency),
                             s.latest_decision.allocation_score);
    std::cout << std::format(" Stability:   {:<26} Workload:   {}\n",
                             std::format("{:.0f}%", s.latest_analysis.stability_score * 100.0),
                             s.latest_analysis.is_sustained_heavy ? "SUSTAINED HEAVY" : (s.latest_analysis.is_transient_spike ? "TRANSIENT SPIKE" : "NORMAL"));

    std::cout << "\n Explanation:\n";
    std::string exp = s.latest_decision.reason.empty() ? s.latest_bottleneck.explanation : s.latest_decision.reason;
    if (exp.empty()) exp = "Awaiting telemetry samples for workload convergence...";
    // Wrap to 76 chars
    for (size_t i = 0; i < exp.length(); i += 76) {
        std::cout << "   " << exp.substr(i, 76) << "\n";
    }

    std::cout << "-------------------------------------------------------------------------------\n";
    std::cout << std::format(" AGRA Overhead: {:.1f} us sampling | {:.2f}% CPU | {} MB Working Set\n",
                             static_cast<double>(s.latest_overhead.sampling_duration_us),
                             s.latest_overhead.agra_cpu_percent,
                             s.latest_overhead.agra_memory_mb);
    std::cout << "===============================================================================\n";
    std::cout << " Controls: [S] Start Session | [X] Stop Session | [R] Restore All | [Q] Quit\n";
    std::cout << "===============================================================================\n" << std::flush;
}

void TerminalDashboard::run_interactive() {
    // Enable ANSI escape sequences on Windows console
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (::GetConsoleMode(hOut, &dwMode)) {
        ::SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // Clear screen initially
    std::cout << "\033[2J\033[H";

    bool running = true;
    while (running) {
        render_frame();

        // Check for non-blocking key presses
        if (_kbhit()) {
            int ch = _getch();
            switch (ch) {
                case 'q':
                case 'Q':
                case 27: // ESC
                    running = false;
                    break;
                case 's':
                case 'S':
                    (void)session_.auto_detect_and_start();
                    break;
                case 'x':
                case 'X':
                    session_.stop_session();
                    break;
                case 'r':
                case 'R':
                    session_.restore_all();
                    break;
                default:
                    break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::cout << "\nExited AGRA Real-Time Dashboard.\n";
}

} // namespace agra::ui
