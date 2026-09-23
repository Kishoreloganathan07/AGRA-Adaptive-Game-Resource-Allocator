#pragma once

#include <string_view>

namespace agra {

inline constexpr int VERSION_MAJOR = 1;
inline constexpr int VERSION_MINOR = 0;
inline constexpr int VERSION_PATCH = 0;

inline constexpr std::string_view VERSION_STRING = "1.0.0";
inline constexpr std::string_view APP_NAME = "AGRA";
inline constexpr std::string_view APP_FULL_NAME = "Adaptive Game Resource Allocator";
inline constexpr std::string_view APP_DESCRIPTION = "Workload-aware Windows CPU resource allocation system for games";

} // namespace agra
