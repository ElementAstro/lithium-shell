#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "../../core/environment.hpp"
#include "../plugin_manager.hpp"

namespace shell {

/**
 * @class HistoryPlugin
 * @brief Enhanced shell history functionality
 *
 * This plugin provides advanced history management features, including:
 * - Command history persistence
 * - Enhanced history search
 * - History statistics and analysis
 * - History deduplication and optimization
 */
class HistoryPlugin : public BuiltinPlugin {
public:
  HistoryPlugin();

  bool initialize(Environment &env) override;
  void shutdown() override;

  /**
   * @brief Lifecycle methods
   */
  bool on_load() override;
  bool on_unload() override;

  /**
   * @brief Handles plugin events
   * @param event The event type
   * @param data Associated event data
   * @return True if event was handled successfully
   */
  bool handle_event(PluginEvent event, const PluginEventData &data) override;

private:
  /**
   * @brief Save history to file
   * @return True if history was saved successfully
   */
  bool save_history();

  /**
   * @brief Load history from file
   * @return True if history was loaded successfully
   */
  bool load_history();

  /**
   * @brief Add entry to history with deduplication
   * @param command The command string to add
   */
  void add_history_entry(const std::string &command);

  /**
   * @brief Update command usage statistics
   * @param command The command to update statistics for
   */
  void update_statistics(const std::string &command);

  /**
   * @brief Periodically save history to file
   */
  void auto_save_history();

  /// Reference to the environment
  Environment *env_ = nullptr;

  /// History file path
  std::filesystem::path history_file_;

  /// Command frequency statistics
  std::unordered_map<std::string, size_t> command_frequency_;

  /// Most frequently used commands cache
  std::vector<std::string> most_used_commands_;

  /// Timestamp of last history save
  std::chrono::system_clock::time_point last_save_time_;

  /// Flag indicating if history has changed
  bool history_changed_ = false;

  /// Automatic save interval in seconds
  int auto_save_interval_ = 60;

  /// Maximum history size
  size_t max_history_size_ = 10000;

  /// Flag controlling deduplication feature
  bool enable_deduplication_ = true;
};

} // namespace shell