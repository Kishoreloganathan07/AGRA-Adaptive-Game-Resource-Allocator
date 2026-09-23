#include "agra/game/game_profile.hpp"
#include "agra/core/logger.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace agra::game {

namespace fs = std::filesystem;

core::Result<void, core::Error> GameProfile::validate() const {
    if (profile_name.empty()) {
        return core::Error::invalid_param("Profile name cannot be empty");
    }
    if (executable_name.empty()) {
        return core::Error::invalid_param("Executable name cannot be empty");
    }
    if (preferred_priority == core::PriorityLevel::High && preferred_policy_mode == core::PolicyMode::Conservative) {
        // High priority not permitted under conservative policy
        return core::Error::safety("High priority is restricted in Conservative policy mode");
    }
    return core::Result<void, core::Error>();
}

core::Result<void, core::Error> ProfileStorage::save_profile(const GameProfile& profile, const std::string& directory) {
    auto val_res = profile.validate();
    if (val_res.is_err()) {
        return val_res;
    }

    try {
        if (!fs::exists(directory)) {
            fs::create_directories(directory);
        }

        std::string filename = directory + "/" + profile.profile_name + ".json";
        std::ofstream out(filename);
        if (!out.is_open()) {
            return core::Error::config("Failed to open profile file for writing: " + filename);
        }

        out << "{\n";
        out << "  \"profile_name\": \"" << profile.profile_name << "\",\n";
        out << "  \"executable_name\": \"" << profile.executable_name << "\",\n";
        out << "  \"preferred_policy_mode\": \"" << core::policy_mode_to_string(profile.preferred_policy_mode) << "\",\n";
        out << "  \"preferred_priority\": \"" << core::priority_level_to_string(profile.preferred_priority) << "\",\n";
        out << "  \"custom_affinity_mask\": " << profile.custom_affinity_mask << ",\n";
        out << "  \"throttle_background_processes\": " << (profile.throttle_background_processes ? "true" : "false") << ",\n";
        out << "  \"background_reduced_priority\": \"" << core::priority_level_to_string(profile.background_reduced_priority) << "\",\n";
        out << "  \"max_allowable_cpu_intervention\": " << profile.max_allowable_cpu_intervention << ",\n";
        out << "  \"allow_automatic_rollback\": " << (profile.allow_automatic_rollback ? "true" : "false") << ",\n";
        out << "  \"notes\": \"" << profile.notes << "\"\n";
        out << "}\n";

        AGRA_LOG_INFO("Profile", "Saved profile '{}' for '{}'", profile.profile_name, profile.executable_name);
        return core::Result<void, core::Error>();
    } catch (const std::exception& e) {
        return core::Error::config(std::string("Filesystem exception: ") + e.what());
    }
}

core::Result<GameProfile, core::Error> ProfileStorage::load_profile(const std::string& profile_name, const std::string& directory) {
    std::string filename = directory + "/" + profile_name + ".json";
    std::ifstream in(filename);
    if (!in.is_open()) {
        return core::Error::not_found("Profile file not found: " + filename);
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();

    GameProfile p{};
    p.profile_name = profile_name;

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

    if (auto s = extract_string("executable_name")) p.executable_name = *s;
    if (auto s = extract_string("preferred_policy_mode")) {
        if (*s == "Default") p.preferred_policy_mode = core::PolicyMode::Default;
        else if (*s == "Conservative") p.preferred_policy_mode = core::PolicyMode::Conservative;
        else if (*s == "Adaptive") p.preferred_policy_mode = core::PolicyMode::Adaptive;
        else if (*s == "Performance") p.preferred_policy_mode = core::PolicyMode::Performance;
    }
    if (auto s = extract_string("preferred_priority")) {
        if (*s == "Normal") p.preferred_priority = core::PriorityLevel::Normal;
        else if (*s == "AboveNormal") p.preferred_priority = core::PriorityLevel::AboveNormal;
        else if (*s == "High") p.preferred_priority = core::PriorityLevel::High;
    }
    if (auto n = extract_number("custom_affinity_mask")) p.custom_affinity_mask = static_cast<core::AffinityMask>(*n);
    if (auto b = extract_bool("throttle_background_processes")) p.throttle_background_processes = *b;
    if (auto b = extract_bool("allow_automatic_rollback")) p.allow_automatic_rollback = *b;
    if (auto n = extract_number("max_allowable_cpu_intervention")) p.max_allowable_cpu_intervention = *n;
    if (auto s = extract_string("notes")) p.notes = *s;

    return p;
}

std::vector<std::string> ProfileStorage::list_saved_profiles(const std::string& directory) {
    std::vector<std::string> names;
    try {
        if (fs::exists(directory)) {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.path().extension() == ".json") {
                    names.push_back(entry.path().stem().string());
                }
            }
        }
    } catch (...) {}
    return names;
}

} // namespace agra::game
