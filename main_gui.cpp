#include "agra/core/config.hpp"
#include "agra/core/logger.hpp"
#include "agra/session/session_manager.hpp"
#include "agra/ui/win32_gui.hpp"

#include <windows.h>

// MinGW with -mwindows expects WinMain (not wWinMain) as the entry point.
// Wide character handling is done internally via Win32 wide APIs.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {
    using namespace agra;

    core::Logger::instance().init(core::LogSeverity::Info, "agra_gui.log");

    core::ConfigManager config_mgr;
    (void)config_mgr.load_from_file("config.json");

    session::SessionManager session(config_mgr);
    ui::Win32Gui gui(session);

    int result = gui.run(hInstance, nCmdShow);

    core::Logger::instance().shutdown();
    return result;
}
