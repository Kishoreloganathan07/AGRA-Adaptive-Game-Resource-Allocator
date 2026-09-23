#include "agra/ui/win32_gui.hpp"
#include "agra/version.hpp"
#include "agra/discovery/process_enumerator.hpp"
#include "agra/benchmark/benchmark_runner.hpp"
#include "agra/benchmark/report_exporter.hpp"
#include "agra/simulation/workload_simulator.hpp"

#include <format>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace agra::ui {

namespace {
constexpr UINT_PTR IDT_TIMER_REFRESH = 1001;
constexpr int IDC_BTN_START     = 2001;
constexpr int IDC_BTN_STOP      = 2002;
constexpr int IDC_BTN_RESTORE   = 2003;
constexpr int IDC_BTN_BENCHMARK = 2004;
constexpr int IDC_BTN_SAFE_MODE = 2005;
constexpr int IDC_BTN_SIMULATE  = 2006;
}

Win32Gui::Win32Gui(session::SessionManager& session)
    : session_(session)
{}

Win32Gui::~Win32Gui() {
    if (hwnd_main_ != nullptr) {
        ::KillTimer(hwnd_main_, IDT_TIMER_REFRESH);
    }
}

LRESULT CALLBACK Win32Gui::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Win32Gui* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Win32Gui*>(cs->lpCreateParams);
        ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Win32Gui*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(hwnd, uMsg, wParam, lParam);
    }
    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int Win32Gui::run(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    ::InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"AGRA_MainWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);

    ::RegisterClassExW(&wc);

    std::wstring ver_w(agra::VERSION_STRING.begin(), agra::VERSION_STRING.end());
    std::wstring title = std::format(L"AGRA — Adaptive Game Resource Allocator v{}", ver_w);
    hwnd_main_ = ::CreateWindowExW(
        0,
        CLASS_NAME,
        title.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 840, 680,
        nullptr, nullptr, hInstance, this
    );

    if (hwnd_main_ == nullptr) {
        return 0;
    }

    create_controls();
    ::ShowWindow(hwnd_main_, nCmdShow);
    ::UpdateWindow(hwnd_main_);

    ::SetTimer(hwnd_main_, IDT_TIMER_REFRESH, 250, nullptr);

    MSG msg{};
    while (::GetMessage(&msg, nullptr, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

void Win32Gui::create_controls() {
    HINSTANCE hInst = ::GetModuleHandle(nullptr);

    // Title / Status Label
    hwnd_status_label_ = ::CreateWindowW(
        L"STATIC", L"Status: Ready / Idle",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 15, 450, 25, hwnd_main_, nullptr, hInst, nullptr);

    hwnd_target_label_ = ::CreateWindowW(
        L"STATIC", L"Target: None attached (Use Start or Auto-Detect)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 45, 450, 25, hwnd_main_, nullptr, hInst, nullptr);

    // Action Buttons
    ::CreateWindowW(
        L"BUTTON", L"Start Session",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        500, 15, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_START), hInst, nullptr);

    ::CreateWindowW(
        L"BUTTON", L"Stop Session",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        660, 15, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_STOP), hInst, nullptr);

    ::CreateWindowW(
        L"BUTTON", L"Restore All",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        500, 55, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_RESTORE), hInst, nullptr);

    ::CreateWindowW(
        L"BUTTON", L"Run Benchmark",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        660, 55, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_BENCHMARK), hInst, nullptr);

    ::CreateWindowW(
        L"BUTTON", L"Run Simulation",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        500, 95, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_SIMULATE), hInst, nullptr);

    ::CreateWindowW(
        L"BUTTON", L"Toggle Safe Mode",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        660, 95, 140, 32, hwnd_main_, reinterpret_cast<HMENU>(IDC_BTN_SAFE_MODE), hInst, nullptr);

    // Group box: Real-Time Allocation & System Workload
    ::CreateWindowW(
        L"BUTTON", L"Real-Time System Allocation & Demand",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 135, 780, 180, hwnd_main_, nullptr, hInst, nullptr);

    ::CreateWindowW(L"STATIC", L"Total System CPU:", WS_CHILD | WS_VISIBLE, 40, 165, 150, 20, hwnd_main_, nullptr, hInst, nullptr);
    hwnd_sys_cpu_bar_ = ::CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 200, 165, 450, 20, hwnd_main_, nullptr, hInst, nullptr);

    ::CreateWindowW(L"STATIC", L"Target Game Demand:", WS_CHILD | WS_VISIBLE, 40, 195, 150, 20, hwnd_main_, nullptr, hInst, nullptr);
    hwnd_game_cpu_bar_ = ::CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 200, 195, 450, 20, hwnd_main_, nullptr, hInst, nullptr);

    ::CreateWindowW(L"STATIC", L"Background Contention:", WS_CHILD | WS_VISIBLE, 40, 225, 150, 20, hwnd_main_, nullptr, hInst, nullptr);
    hwnd_contention_bar_ = ::CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 200, 225, 450, 20, hwnd_main_, nullptr, hInst, nullptr);

    hwnd_overhead_label_ = ::CreateWindowW(
        L"STATIC", L"AGRA Overhead: 0.0 us | 0.0% CPU | 0 MB RAM",
        WS_CHILD | WS_VISIBLE, 40, 260, 600, 20, hwnd_main_, nullptr, hInst, nullptr);

    // Group box: Decision & Explanation
    ::CreateWindowW(
        L"BUTTON", L"Bottleneck Classification & Allocation Decision",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 325, 780, 160, hwnd_main_, nullptr, hInst, nullptr);

    hwnd_bottleneck_label_ = ::CreateWindowW(
        L"STATIC", L"Bottleneck: Unknown | Confidence: 0% | Policy: Baseline",
        WS_CHILD | WS_VISIBLE, 40, 355, 740, 20, hwnd_main_, nullptr, hInst, nullptr);

    hwnd_explanation_box_ = ::CreateWindowW(
        L"EDIT", L"Awaiting target process attachment or telemetry samples...",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER,
        40, 385, 740, 80, hwnd_main_, nullptr, hInst, nullptr);

    // Running process list header
    ::CreateWindowW(
        L"BUTTON", L"Active Game Processes / Discovered Targets",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 495, 780, 130, hwnd_main_, nullptr, hInst, nullptr);

    hwnd_process_list_ = ::CreateWindowW(
        WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
        40, 520, 740, 95, hwnd_main_, nullptr, hInst, nullptr);

    ListView_SetExtendedListViewStyle(hwnd_process_list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"PID");
    col.cx = 80;
    ListView_InsertColumn(hwnd_process_list_, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"Process Name");
    col.cx = 240;
    ListView_InsertColumn(hwnd_process_list_, 1, &col);

    col.pszText = const_cast<wchar_t*>(L"Priority");
    col.cx = 120;
    ListView_InsertColumn(hwnd_process_list_, 2, &col);

    col.pszText = const_cast<wchar_t*>(L"Threads");
    col.cx = 90;
    ListView_InsertColumn(hwnd_process_list_, 3, &col);

    col.pszText = const_cast<wchar_t*>(L"Working Set (MB)");
    col.cx = 150;
    ListView_InsertColumn(hwnd_process_list_, 4, &col);

    refresh_process_list();
}

void Win32Gui::refresh_process_list() {
    ListView_DeleteAllItems(hwnd_process_list_);
    auto procs = discovery::ProcessEnumerator::enumerate_all(session_.config());

    int idx = 0;
    for (const auto& p : procs) {
        if (p.is_system_protected) continue;

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        std::wstring pid_str = std::to_wstring(p.pid);
        item.pszText = const_cast<wchar_t*>(pid_str.c_str());
        ListView_InsertItem(hwnd_process_list_, &item);

        std::wstring name_str(p.name.begin(), p.name.end());
        ListView_SetItemText(hwnd_process_list_, idx, 1, const_cast<wchar_t*>(name_str.c_str()));

        std::string prio_str(core::priority_level_to_string(p.priority));
        std::wstring prio_wstr(prio_str.begin(), prio_str.end());
        ListView_SetItemText(hwnd_process_list_, idx, 2, const_cast<wchar_t*>(prio_wstr.c_str()));

        std::wstring th_str = std::to_wstring(p.thread_count);
        ListView_SetItemText(hwnd_process_list_, idx, 3, const_cast<wchar_t*>(th_str.c_str()));

        std::wstring ram_str = std::to_wstring(p.working_set_mb);
        ListView_SetItemText(hwnd_process_list_, idx, 4, const_cast<wchar_t*>(ram_str.c_str()));

        ++idx;
        if (idx >= 15) break; // Display top 15 candidates
    }
}

void Win32Gui::update_telemetry() {
    auto s = session_.snapshot();

    std::wstring status_text = std::format(
        L"Status: {} | Mode: {}",
        s.is_active ? (s.safe_mode ? L"ACTIVE [SAFE MODE]" : L"ACTIVE [OPTIMIZING]") : L"READY / IDLE",
        std::wstring(core::policy_mode_to_string(s.active_mode).begin(), core::policy_mode_to_string(s.active_mode).end()));
    ::SetWindowTextW(hwnd_status_label_, status_text.c_str());

    std::wstring target_text = std::format(
        L"Target: {} (PID: {}) | Policy: {}",
        s.target_name.empty() ? L"(None)" : std::wstring(s.target_name.begin(), s.target_name.end()),
        s.target_pid,
        s.latest_policy.is_game_boosted ? L"ELEVATED" : L"BASELINE");
    ::SetWindowTextW(hwnd_target_label_, target_text.c_str());

    // Update Progress Bars (range 0 - 100)
    ::SendMessage(hwnd_sys_cpu_bar_, PBM_SETPOS, static_cast<WPARAM>(s.latest_metrics.system_cpu_percent), 0);
    ::SendMessage(hwnd_game_cpu_bar_, PBM_SETPOS, static_cast<WPARAM>(s.latest_metrics.game_cpu_percent), 0);
    ::SendMessage(hwnd_contention_bar_, PBM_SETPOS, static_cast<WPARAM>(s.latest_metrics.contention_index * 100.0), 0);

    // Overhead
    std::wstring ov_text = std::format(
        L"AGRA Overhead: {:.1f} us sampling | {:.2f}% CPU | {} MB Working Set",
        static_cast<double>(s.latest_overhead.sampling_duration_us),
        s.latest_overhead.agra_cpu_percent,
        s.latest_overhead.agra_memory_mb);
    ::SetWindowTextW(hwnd_overhead_label_, ov_text.c_str());

    // Bottleneck & Decision
    std::wstring bn_text = std::format(
        L"Bottleneck: {} | Confidence: {:.0f}% | Score: {:.1f} / 100 | Priority: {}",
        std::wstring(core::bottleneck_type_to_string(s.latest_bottleneck.type).begin(), core::bottleneck_type_to_string(s.latest_bottleneck.type).end()),
        s.latest_bottleneck.confidence * 100.0,
        s.latest_decision.allocation_score,
        std::wstring(core::priority_level_to_string(s.latest_decision.target_priority).begin(), core::priority_level_to_string(s.latest_decision.target_priority).end()));
    ::SetWindowTextW(hwnd_bottleneck_label_, bn_text.c_str());

    std::string exp = s.latest_decision.reason.empty() ? s.latest_bottleneck.explanation : s.latest_decision.reason;
    if (exp.empty()) exp = "Awaiting target process attachment or telemetry samples...";
    std::wstring exp_w(exp.begin(), exp.end());
    ::SetWindowTextW(hwnd_explanation_box_, exp_w.c_str());
}

LRESULT Win32Gui::handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == IDT_TIMER_REFRESH) {
                update_telemetry();
            }
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_BTN_START: {
                    auto res = session_.auto_detect_and_start();
                    if (res.is_ok() && res.value()) {
                        ::MessageBoxW(hwnd, L"Successfully auto-detected game and started AGRA session!", L"AGRA Session", MB_OK | MB_ICONINFORMATION);
                    } else {
                        ::MessageBoxW(hwnd, L"No active game target detected. Select a process from list or launch a game.", L"AGRA Detection", MB_OK | MB_ICONWARNING);
                    }
                    break;
                }
                case IDC_BTN_STOP:
                    session_.stop_session();
                    ::MessageBoxW(hwnd, L"Session stopped and all policies reverted to original baseline.", L"AGRA Session", MB_OK | MB_ICONINFORMATION);
                    break;
                case IDC_BTN_RESTORE:
                    session_.restore_all();
                    ::MessageBoxW(hwnd, L"Emergency Restore All executed successfully.", L"AGRA Safety", MB_OK | MB_ICONINFORMATION);
                    break;
                case IDC_BTN_BENCHMARK: {
                    benchmark::BenchmarkRunner runner(session_);
                    auto report = runner.run_simulated_benchmark(15);
                    std::string json_str = benchmark::ReportExporter::to_json(report);
                    (void)benchmark::ReportExporter::save_to_file("benchmark_report.json", json_str);
                    (void)benchmark::ReportExporter::save_to_file("benchmark_report.csv", benchmark::ReportExporter::to_csv(report));
                    ::MessageBoxW(hwnd, L"Benchmark suite completed! Exported to benchmark_report.json and benchmark_report.csv", L"AGRA Benchmark", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                case IDC_BTN_SIMULATE: {
                    simulation::WorkloadSimulator sim(session_.config());
                    auto sim_report = sim.run_simulation();
                    (void)benchmark::ReportExporter::save_to_file("simulation_report.json", simulation::WorkloadSimulator::to_json(sim_report));
                    (void)benchmark::ReportExporter::save_to_file("simulation_report.csv", simulation::WorkloadSimulator::to_csv(sim_report));
                    ::MessageBoxW(hwnd, L"Controlled Workload Simulation completed! Exported to simulation_report.json and simulation_report.csv", L"AGRA Simulation", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                case IDC_BTN_SAFE_MODE: {
                    auto snap = session_.snapshot();
                    session_.set_safe_mode(!snap.safe_mode);
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY:
            session_.stop_session();
            session_.restore_all();
            ::PostQuitMessage(0);
            return 0;

        default:
            return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

} // namespace agra::ui
