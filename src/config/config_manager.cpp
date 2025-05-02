#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>


#include "../utils/string_utils.hpp"
#include "config_manager.hpp"


namespace shell {

ConfigManager::ConfigManager() {
    // Set default configuration values
    config_values_["prompt"] = "\\033[1;32m➜ \\033[1;34m{pwd}\\033[0m$ ";
    config_values_["history_size"] = "1000";
    config_values_["tab_completion"] = "true";
    config_values_["color_output"] = "true";
}

bool ConfigManager::load_config(const std::filesystem::path& config_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!std::filesystem::exists(config_path)) {
        return false;
    }

    return parse_config_file(config_path);
}

bool ConfigManager::save_config(
    const std::filesystem::path& config_path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        std::filesystem::create_directories(config_path.parent_path());

        std::ofstream file(config_path);
        if (!file) {
            return false;
        }

        file << serialize_config();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::has_key(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_values_.find(key) != config_values_.end();
}

const std::unordered_map<std::string, std::string>&
ConfigManager::get_all_values() const {
    return config_values_;
}

std::filesystem::path ConfigManager::get_default_config_path() {
    const char* home_dir = getenv("HOME");
    if (!home_dir) {
        return std::filesystem::current_path() / ".modernshellrc";
    }

    return std::filesystem::path(home_dir) / ".modernshellrc";
}

bool ConfigManager::parse_config_file(const std::filesystem::path& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Trim whitespace
            line = trim(line);

            // Parse key=value
            size_t equals_pos = line.find('=');
            if (equals_pos != std::string::npos) {
                std::string key = trim(line.substr(0, equals_pos));
                std::string value = trim(line.substr(equals_pos + 1));

                // Remove quotes if present
                if ((value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\'')) {
                    value = value.substr(1, value.length() - 2);
                }

                // Unescape special characters
                value = unescape_string(value);

                config_values_[key] = value;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
}

std::string ConfigManager::serialize_config() const {
    std::stringstream ss;

    ss << "# Modern Shell Configuration File\n\n";

    for (const auto& [key, value] : config_values_) {
        // Escape special characters
        std::string escaped_value = escape_string(value);

        // Add quotes if value contains spaces
        if (escaped_value.find(' ') != std::string::npos) {
            escaped_value = "\"" + escaped_value + "\"";
        }

        ss << key << " = " << escaped_value << "\n";
    }

    return ss.str();
}

}  // namespace shell