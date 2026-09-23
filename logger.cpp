#include "agra/core/logger.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace agra::core {

namespace {

void enable_virtual_terminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}

std::string get_iso8601_timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto itt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &itt);
#else
    localtime_r(&itt, &tm_buf);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

const char* get_severity_color(LogSeverity sev) {
    switch (sev) {
        case LogSeverity::Trace:    return "\033[90m"; // Bright Black / Gray
        case LogSeverity::Debug:    return "\033[36m"; // Cyan
        case LogSeverity::Info:     return "\033[32m"; // Green
        case LogSeverity::Warning:  return "\033[33m"; // Yellow
        case LogSeverity::Error:    return "\033[31m"; // Red
        case LogSeverity::Critical: return "\033[41;97m"; // White on Red background
        default:                    return "\033[0m";
    }
}

const char* RESET_COLOR = "\033[0m";

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(LogSeverity min_severity, const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_severity_ = min_severity;
    enable_virtual_terminal();

    if (!log_file_path.empty()) {
        file_stream_.open(log_file_path, std::ios::out | std::ios::app);
    }
    initialized_ = true;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
    initialized_ = false;
}

void Logger::log(LogSeverity severity, std::string_view tag, std::string_view message) {
    if (severity < min_severity_) return;

    std::string timestamp = get_iso8601_timestamp();
    std::string_view sev_str = log_severity_to_string(severity);
    DWORD thread_id = ::GetCurrentThreadId();

    std::lock_guard<std::mutex> lock(mutex_);

    // Console output with colors
    if (console_enabled_) {
        const char* color = get_severity_color(severity);
        std::cout << color << "[" << timestamp << "]"
                  << " [" << sev_str << "]"
                  << " [T:" << thread_id << "]"
                  << " [" << tag << "] "
                  << message << RESET_COLOR << "\n";
    }

    // File output (clean without ANSI color codes)
    if (file_stream_.is_open()) {
        file_stream_ << "[" << timestamp << "]"
                     << " [" << sev_str << "]"
                     << " [T:" << thread_id << "]"
                     << " [" << tag << "] "
                     << message << "\n";
    }
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout.flush();
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
}

} // namespace agra::core
