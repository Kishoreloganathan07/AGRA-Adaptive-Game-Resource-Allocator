#pragma once

#include "agra/session/session_manager.hpp"

#include <windows.h>
#include <commctrl.h>

namespace agra::ui {

class Win32Gui {
public:
    explicit Win32Gui(session::SessionManager& session);
    ~Win32Gui();

    // Initialize and run the Win32 GUI event loop
    int run(HINSTANCE hInstance, int nCmdShow);

private:
    session::SessionManager& session_;
    HWND hwnd_main_{nullptr};
    HWND hwnd_tab_{nullptr};
    HWND hwnd_status_label_{nullptr};
    HWND hwnd_target_label_{nullptr};
    HWND hwnd_sys_cpu_bar_{nullptr};
    HWND hwnd_game_cpu_bar_{nullptr};
    HWND hwnd_contention_bar_{nullptr};
    HWND hwnd_bottleneck_label_{nullptr};
    HWND hwnd_explanation_box_{nullptr};
    HWND hwnd_overhead_label_{nullptr};
    HWND hwnd_process_list_{nullptr};

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void create_controls();
    void update_telemetry();
    void refresh_process_list();
};

} // namespace agra::ui
