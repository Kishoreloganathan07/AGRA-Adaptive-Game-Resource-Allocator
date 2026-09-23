#pragma once

#include "agra/session/session_manager.hpp"

namespace agra::ui {

class TerminalDashboard {
public:
    explicit TerminalDashboard(session::SessionManager& session);

    // Render a single complete snapshot frame of the dashboard
    void render_frame() const;

    // Run interactive real-time dashboard loop until user exits (presses 'q')
    void run_interactive();

    // Helper to generate ASCII progress bar: [████████░░░░] 65%
    [[nodiscard]] static std::string render_bar(double percent, int width = 24);

private:
    session::SessionManager& session_;
};

} // namespace agra::ui
