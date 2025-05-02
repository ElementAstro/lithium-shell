#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../core/environment.hpp"
#include "../plugin_manager.hpp"

namespace shell {

/**
 * @class AutoCompletePlugin
 * @brief Enhanced auto-completion functionality for the shell
 *
 * This plugin provides advanced completion features, including:
 * - Command argument completion (based on common command arguments)
 * - Git branch completion
 * - Environment variable name completion
 * - Smarter file path completion
 */
class AutoCompletePlugin : public BuiltinPlugin {
public:
  AutoCompletePlugin();

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
   * @brief Initialize completion data
   */
  void initialize_completions();

  /**
   * @brief Update Git branch cache
   */
  void update_git_branches();

  /**
   * @brief Scan current directory and update file cache
   */
  void update_file_cache();

  /**
   * @brief Command completion function
   * @param command The command string
   * @param current_word The current word being completed
   * @return List of possible completions
   */
  std::vector<std::string> complete_command(const std::string &command,
                                            const std::string &current_word);

  /// Command argument completion data
  std::unordered_map<std::string, std::vector<std::string>> command_args_;

  /// File cache by directory
  std::unordered_map<std::string, std::vector<std::string>> file_cache_;

  /// Environment variable cache
  std::vector<std::string> env_vars_;

  /// Git branch cache
  std::vector<std::string> git_branches_;

  /// Reference to the environment
  Environment *env_ = nullptr;

  /// Flag indicating if current directory is in a Git repository
  bool in_git_repo_ = false;

  /// Timestamp of last cache update
  std::filesystem::file_time_type last_cache_update_;
};

} // namespace shell