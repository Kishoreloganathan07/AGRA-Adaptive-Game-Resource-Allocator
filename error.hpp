#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace agra::core {

enum class ErrorCode {
    Success = 0,
    Win32Error,
    ProcessNotFound,
    AccessDenied,
    InvalidParameter,
    ConfigParseError,
    ConfigValidationError,
    SafetyViolation,
    UnsupportedPlatformFeature,
    MonitoringFailure,
    RollbackFailure,
    UnknownError
};

class Error {
public:
    Error() noexcept = default;
    Error(ErrorCode code, std::string message, std::uint32_t win32_code = 0, std::string details = "")
        : code_(code), win32_code_(win32_code), message_(std::move(message)), details_(std::move(details)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] std::uint32_t win32_code() const noexcept { return win32_code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& details() const noexcept { return details_; }

    [[nodiscard]] std::string to_string() const;

    // Factory methods
    static Error ok() noexcept { return Error(ErrorCode::Success, "Success"); }
    static Error win32(std::uint32_t native_err, std::string_view context);
    static Error last_win32(std::string_view context);
    static Error not_found(std::string_view context);
    static Error access_denied(std::string_view context);
    static Error invalid_param(std::string_view context);
    static Error config(std::string_view msg);
    static Error safety(std::string_view msg);
    static Error unsupported(std::string_view msg);
    static Error monitoring(std::string_view msg);
    static Error rollback(std::string_view msg);

private:
    ErrorCode code_{ErrorCode::Success};
    std::uint32_t win32_code_{0};
    std::string message_{"Success"};
    std::string details_;
};

} // namespace agra::core
