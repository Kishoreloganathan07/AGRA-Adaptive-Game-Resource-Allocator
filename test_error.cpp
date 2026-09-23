#include "test_framework.hpp"
#include "agra/core/error.hpp"

using namespace agra::core;

AGRA_TEST_CASE("Error - Factory Methods and String Representation") {
    auto ok_err = Error::ok();
    AGRA_CHECK_EQ(ok_err.code(), ErrorCode::Success);
    AGRA_CHECK_EQ(ok_err.to_string(), "Success");

    auto not_found_err = Error::not_found("Game.exe not found");
    AGRA_CHECK_EQ(not_found_err.code(), ErrorCode::ProcessNotFound);
    AGRA_CHECK(not_found_err.to_string().find("ProcessNotFound") != std::string::npos);
    AGRA_CHECK(not_found_err.to_string().find("Game.exe not found") != std::string::npos);

    auto win32_err = Error::win32(5, "OpenProcess failed"); // 5 = ERROR_ACCESS_DENIED
    AGRA_CHECK_EQ(win32_err.code(), ErrorCode::Win32Error);
    AGRA_CHECK_EQ(win32_err.win32_code(), 5u);
    AGRA_CHECK(win32_err.to_string().find("OpenProcess failed") != std::string::npos);
}
