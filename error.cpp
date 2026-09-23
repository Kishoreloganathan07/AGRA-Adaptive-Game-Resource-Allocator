#include "agra/core/error.hpp"

#include <windows.h>
#include <format>
#include <sstream>

namespace agra::core {

namespace {

std::string format_win32_message(std::uint32_t error_code) {
    if (error_code == 0) {
        return "No error";
    }

    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    if (size == 0 || buffer == nullptr) {
        return std::format("Win32 Error 0x{:08X}", error_code);
    }

    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), nullptr, 0, nullptr, nullptr);
    std::string result(utf8_size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), result.data(), utf8_size, nullptr, nullptr);

    LocalFree(buffer);

    // Strip trailing newlines
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }

    return result;
}

} // namespace

std::string Error::to_string() const {
    if (code_ == ErrorCode::Success) {
        return "Success";
    }

    std::ostringstream ss;
    switch (code_) {
        case ErrorCode::Win32Error: ss << "[Win32Error]"; break;
        case ErrorCode::ProcessNotFound: ss << "[ProcessNotFound]"; break;
        case ErrorCode::AccessDenied: ss << "[AccessDenied]"; break;
        case ErrorCode::InvalidParameter: ss << "[InvalidParameter]"; break;
        case ErrorCode::ConfigParseError: ss << "[ConfigParseError]"; break;
        case ErrorCode::ConfigValidationError: ss << "[ConfigValidationError]"; break;
        case ErrorCode::SafetyViolation: ss << "[SafetyViolation]"; break;
        case ErrorCode::UnsupportedPlatformFeature: ss << "[UnsupportedPlatformFeature]"; break;
        case ErrorCode::MonitoringFailure: ss << "[MonitoringFailure]"; break;
        case ErrorCode::RollbackFailure: ss << "[RollbackFailure]"; break;
        default: ss << "[UnknownError]"; break;
    }

    ss << " " << message_;
    if (win32_code_ != 0) {
        ss << " (Native Win32 Code: " << win32_code_ << " - " << format_win32_message(win32_code_) << ")";
    }
    if (!details_.empty()) {
        ss << " Details: " << details_;
    }
    return ss.str();
}

Error Error::win32(std::uint32_t native_err, std::string_view context) {
    std::string details = format_win32_message(native_err);
    return Error(
        ErrorCode::Win32Error,
        std::string(context),
        native_err,
        std::move(details)
    );
}

Error Error::last_win32(std::string_view context) {
    DWORD err = ::GetLastError();
    return win32(static_cast<std::uint32_t>(err), context);
}

Error Error::not_found(std::string_view context) {
    return Error(ErrorCode::ProcessNotFound, std::string(context));
}

Error Error::access_denied(std::string_view context) {
    return Error(ErrorCode::AccessDenied, std::string(context), ERROR_ACCESS_DENIED, "Access Denied by OS security or anti-cheat");
}

Error Error::invalid_param(std::string_view context) {
    return Error(ErrorCode::InvalidParameter, std::string(context));
}

Error Error::config(std::string_view msg) {
    return Error(ErrorCode::ConfigParseError, std::string(msg));
}

Error Error::safety(std::string_view msg) {
    return Error(ErrorCode::SafetyViolation, std::string(msg));
}

Error Error::unsupported(std::string_view msg) {
    return Error(ErrorCode::UnsupportedPlatformFeature, std::string(msg));
}

Error Error::monitoring(std::string_view msg) {
    return Error(ErrorCode::MonitoringFailure, std::string(msg));
}

Error Error::rollback(std::string_view msg) {
    return Error(ErrorCode::RollbackFailure, std::string(msg));
}

} // namespace agra::core
