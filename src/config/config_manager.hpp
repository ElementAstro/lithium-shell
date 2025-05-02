#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>


namespace shell {

/**
 * @class ConfigManager
 * @brief Manages shell configuration
 */
class ConfigManager {
public:
    ConfigManager();

    // Load configuration from file
    bool load_config(const std::filesystem::path& config_path);

    // Save configuration to file
    bool save_config(const std::filesystem::path& config_path) const;

    // Get/set configuration values
    template <typename T>
    void set_value(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::stringstream ss;
        ss << value;
        config_values_[key] = ss.str();
    }

    template <typename T>
    std::optional<T> get_value(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = config_values_.find(key);
        if (it == config_values_.end()) {
            return std::nullopt;
        }

        T value;
        std::stringstream ss(it->second);

        if (ss >> value) {
            return value;
        }

        return std::nullopt;
    }

    // Check if a key exists
    bool has_key(const std::string& key) const;

    // Get all configuration values
    const std::unordered_map<std::string, std::string>& get_all_values() const;

    // Get default config file path
    static std::filesystem::path get_default_config_path();

private:
    std::unordered_map<std::string, std::string> config_values_;

    // Parse config file
    bool parse_config_file(const std::filesystem::path& path);

    // Serialize config values
    std::string serialize_config() const;

    // Thread safety
    mutable std::mutex mutex_;
};

// Specialization for string to avoid stringstream
template <>
inline void ConfigManager::set_value<std::string>(const std::string& key,
                                                  const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_values_[key] = value;
}

template <>
inline std::optional<std::string> ConfigManager::get_value<std::string>(
    const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = config_values_.find(key);
    if (it == config_values_.end()) {
        return std::nullopt;
    }

    return it->second;
}

}  // namespace shell