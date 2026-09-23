#include "agra/core/config.hpp"
#include "agra/core/logger.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace agra::core {

namespace {

std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}



} // namespace

Result<void, Error> AppConfig::validate() const {
    if (sampling_interval_ms < 50 || sampling_interval_ms > 5000) {
        return Error::config("sampling_interval_ms must be between 50 and 5000 ms");
    }
    if (min_confidence_threshold < 0.0 || min_confidence_threshold > 1.0) {
        return Error::config("min_confidence_threshold must be between 0.0 and 1.0");
    }
    if (low_demand_cpu_threshold >= high_demand_cpu_threshold) {
        return Error::config("low_demand_cpu_threshold must be strictly less than high_demand_cpu_threshold");
    }
    if (high_demand_cpu_threshold > 100.0 || low_demand_cpu_threshold < 0.0) {
        return Error::config("CPU thresholds must be within 0.0% to 100.0%");
    }
    if (contention_threshold < 0.0 || contention_threshold > 1.0) {
        return Error::config("contention_threshold must be between 0.0 and 1.0");
    }
    if (hysteresis_enter_samples == 0 || hysteresis_exit_samples == 0) {
        return Error::config("Hysteresis sample counts must be greater than zero");
    }
    return Result<void, Error>();
}

bool AppConfig::is_process_protected(std::string_view process_name) const {
    std::string lower_target = to_lower(process_name);
    for (const auto& proc : protected_processes) {
        if (to_lower(proc) == lower_target) {
            return true;
        }
    }
    return false;
}

bool AppConfig::is_process_user_excluded(std::string_view process_name) const {
    std::string lower_target = to_lower(process_name);
    for (const auto& proc : user_exclusions) {
        if (to_lower(proc) == lower_target) {
            return true;
        }
    }
    return false;
}

void ConfigManager::reset_to_defaults() {
    config_ = AppConfig{};
}

Result<void, Error> ConfigManager::save_to_file(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) {
        return Error::config("Failed to open configuration file for writing: " + filepath);
    }

    out << "{\n";
    out << "  \"sampling_interval_ms\": " << config_.sampling_interval_ms << ",\n";
    out << "  \"policy_mode\": \"" << policy_mode_to_string(config_.policy_mode) << "\",\n";
    out << "  \"min_confidence_threshold\": " << config_.min_confidence_threshold << ",\n";
    out << "  \"high_demand_cpu_threshold\": " << config_.high_demand_cpu_threshold << ",\n";
    out << "  \"low_demand_cpu_threshold\": " << config_.low_demand_cpu_threshold << ",\n";
    out << "  \"contention_threshold\": " << config_.contention_threshold << ",\n";
    out << "  \"hysteresis_enter_samples\": " << config_.hysteresis_enter_samples << ",\n";
    out << "  \"hysteresis_exit_samples\": " << config_.hysteresis_exit_samples << ",\n";
    out << "  \"cooldown_period_ms\": " << config_.cooldown_period_ms << ",\n";
    out << "  \"allow_background_throttling\": " << (config_.allow_background_throttling ? "true" : "false") << ",\n";
    out << "  \"prevent_starvation\": " << (config_.prevent_starvation ? "true" : "false") << ",\n";
    out << "  \"log_level\": \"" << log_severity_to_string(config_.log_level) << "\",\n";
    out << "  \"log_file_path\": \"" << config_.log_file_path << "\",\n";

    // Protected processes array
    out << "  \"protected_processes\": [\n";
    for (size_t i = 0; i < config_.protected_processes.size(); ++i) {
        out << "    \"" << config_.protected_processes[i] << "\"";
        if (i + 1 < config_.protected_processes.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // User exclusions array
    out << "  \"user_exclusions\": [\n";
    for (size_t i = 0; i < config_.user_exclusions.size(); ++i) {
        out << "    \"" << config_.user_exclusions[i] << "\"";
        if (i + 1 < config_.user_exclusions.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    return Result<void, Error>();
}

Result<void, Error> ConfigManager::load_from_file(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        AGRA_LOG_WARN("Config", "Configuration file '{}' not found, using safe defaults.", filepath);
        reset_to_defaults();
        return Error::not_found("Configuration file not found, loaded defaults");
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();

    AppConfig loaded = config_; // start from current/default

    // Lightweight parsing of key JSON fields
    auto extract_string = [&](const std::string& key) -> std::optional<std::string> {
        std::string search_key = "\"" + key + "\"";
        auto pos = content.find(search_key);
        if (pos == std::string::npos) return std::nullopt;
        auto colon = content.find(':', pos + search_key.length());
        if (colon == std::string::npos) return std::nullopt;
        auto quote1 = content.find('\"', colon + 1);
        if (quote1 == std::string::npos) return std::nullopt;
        auto quote2 = content.find('\"', quote1 + 1);
        if (quote2 == std::string::npos) return std::nullopt;
        return content.substr(quote1 + 1, quote2 - quote1 - 1);
    };

    auto extract_number = [&](const std::string& key) -> std::optional<double> {
        std::string search_key = "\"" + key + "\"";
        auto pos = content.find(search_key);
        if (pos == std::string::npos) return std::nullopt;
        auto colon = content.find(':', pos + search_key.length());
        if (colon == std::string::npos) return std::nullopt;
        size_t start = content.find_first_of("-0123456789.", colon + 1);
        if (start == std::string::npos) return std::nullopt;
        size_t end = content.find_first_not_of("-0123456789.", start);
        std::string num_str = content.substr(start, end - start);
        try {
            return std::stod(num_str);
        } catch (...) {
            return std::nullopt;
        }
    };

    auto extract_bool = [&](const std::string& key) -> std::optional<bool> {
        std::string search_key = "\"" + key + "\"";
        auto pos = content.find(search_key);
        if (pos == std::string::npos) return std::nullopt;
        auto colon = content.find(':', pos + search_key.length());
        if (colon == std::string::npos) return std::nullopt;
        auto val_pos = content.find_first_not_of(" \t\r\n", colon + 1);
        if (val_pos == std::string::npos) return std::nullopt;
        if (content.compare(val_pos, 4, "true") == 0) return true;
        if (content.compare(val_pos, 5, "false") == 0) return false;
        return std::nullopt;
    };

    if (auto v = extract_number("sampling_interval_ms")) loaded.sampling_interval_ms = static_cast<std::uint32_t>(*v);
    if (auto v = extract_number("min_confidence_threshold")) loaded.min_confidence_threshold = *v;
    if (auto v = extract_number("high_demand_cpu_threshold")) loaded.high_demand_cpu_threshold = *v;
    if (auto v = extract_number("low_demand_cpu_threshold")) loaded.low_demand_cpu_threshold = *v;
    if (auto v = extract_number("contention_threshold")) loaded.contention_threshold = *v;
    if (auto v = extract_number("hysteresis_enter_samples")) loaded.hysteresis_enter_samples = static_cast<std::uint32_t>(*v);
    if (auto v = extract_number("hysteresis_exit_samples")) loaded.hysteresis_exit_samples = static_cast<std::uint32_t>(*v);
    if (auto v = extract_number("cooldown_period_ms")) loaded.cooldown_period_ms = static_cast<std::uint32_t>(*v);
    if (auto v = extract_bool("allow_background_throttling")) loaded.allow_background_throttling = *v;
    if (auto v = extract_bool("prevent_starvation")) loaded.prevent_starvation = *v;

    if (auto s = extract_string("policy_mode")) {
        if (*s == "Default") loaded.policy_mode = PolicyMode::Default;
        else if (*s == "Conservative") loaded.policy_mode = PolicyMode::Conservative;
        else if (*s == "Adaptive") loaded.policy_mode = PolicyMode::Adaptive;
        else if (*s == "Performance") loaded.policy_mode = PolicyMode::Performance;
    }

    if (auto s = extract_string("log_level")) {
        if (*s == "TRACE") loaded.log_level = LogSeverity::Trace;
        else if (*s == "DEBUG") loaded.log_level = LogSeverity::Debug;
        else if (*s == "INFO") loaded.log_level = LogSeverity::Info;
        else if (*s == "WARN") loaded.log_level = LogSeverity::Warning;
        else if (*s == "ERROR") loaded.log_level = LogSeverity::Error;
        else if (*s == "CRIT") loaded.log_level = LogSeverity::Critical;
    }

    if (auto s = extract_string("log_file_path")) loaded.log_file_path = *s;

    // Validate loaded config
    auto val_res = loaded.validate();
    if (val_res.is_err()) {
        AGRA_LOG_ERROR("Config", "Loaded configuration failed validation: {}. Reverting to defaults.", val_res.error().message());
        reset_to_defaults();
        return val_res;
    }

    config_ = loaded;
    AGRA_LOG_INFO("Config", "Successfully loaded and validated configuration from '{}'", filepath);
    return Result<void, Error>();
}

} // namespace agra::core
