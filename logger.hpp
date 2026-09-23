#pragma once

#include "agra/core/types.hpp"

#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <fstream>
#include <format>
#include <chrono>

namespace agra::core {

class Logger {
public:
    static Logger& instance();

    void init(LogSeverity min_severity = LogSeverity::Info, const std::string& log_file_path = "agra.log");
    void shutdown();

    void set_min_severity(LogSeverity severity) noexcept {
        min_severity_ = severity;
    }

    [[nodiscard]] LogSeverity min_severity() const noexcept {
        return min_severity_;
    }

    void log(LogSeverity severity, std::string_view tag, std::string_view message);

    template <typename... Args>
    void log_format(LogSeverity severity, std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        if (severity < min_severity_) return;
        std::string formatted = std::format(fmt, std::forward<Args>(args)...);
        log(severity, tag, formatted);
    }

    void flush();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogSeverity min_severity_{LogSeverity::Info};
    std::mutex mutex_;
    std::ofstream file_stream_;
    bool initialized_{false};
    bool console_enabled_{true};
};

} // namespace agra::core

// Convenience logging macros
#define AGRA_LOG_TRACE(tag, ...) ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Trace, tag, __VA_ARGS__)
#define AGRA_LOG_DEBUG(tag, ...) ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Debug, tag, __VA_ARGS__)
#define AGRA_LOG_INFO(tag, ...)  ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Info,  tag, __VA_ARGS__)
#define AGRA_LOG_WARN(tag, ...)  ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Warning, tag, __VA_ARGS__)
#define AGRA_LOG_ERROR(tag, ...) ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Error, tag, __VA_ARGS__)
#define AGRA_LOG_CRIT(tag, ...)  ::agra::core::Logger::instance().log_format(::agra::core::LogSeverity::Critical, tag, __VA_ARGS__)
