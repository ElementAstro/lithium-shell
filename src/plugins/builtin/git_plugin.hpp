#pragma once

#include <filesystem>
#include <string>

#include "../../core/environment.hpp"
#include "../plugin_manager.hpp"


namespace shell {

/**
 * @class GitPlugin
 * @brief Provides Git integration features for the shell
 *
 * This plugin offers Git integration functionality, including:
 * - Displaying Git branch and status in the prompt
 * - Git command enhancements and aliases
 * - Repository status caching
 */
class GitPlugin : public BuiltinPlugin {
public:
  GitPlugin();

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

  /**
   * @brief Gets the current Git branch name
   * @return String containing the current branch name
   */
  std::string get_current_branch() const;

  /**
   * @brief Checks if the current directory is within a Git repository
   * @return True if in a Git repository, false otherwise
   */
  bool is_git_repository() const;

  /**
   * @brief Gets the formatted repository status
   * @return String containing the repository status information
   */
  std::string get_repository_status() const;

private:
  /**
   * @brief Updates the cached Git status information
   */
  void update_git_status();

  /**
   * @brief Checks if the working directory is in a Git repository
   * @return True if in a Git repository, false otherwise
   */
  bool check_if_in_git_repo();

  /**
   * @brief Registers Git aliases and enhanced commands
   */
  void register_git_commands();

  /**
   * @brief Executes a Git command and returns its output
   * @param command The Git command to execute
   * @return The command output as a string
   */
  std::string run_git_command(const std::string &command) const;

  /// Reference to the shell environment
  Environment *env_ = nullptr;

  /// Git status cache
  std::string current_branch_;

  /// Flag indicating if the repository has uncommitted changes
  bool has_changes_ = false;

  /// Flag indicating if the repository has untracked files
  bool has_untracked_ = false;

  /// Flag indicating if HEAD is in detached state
  bool is_detached_ = false;

  /// Flag indicating if current directory is in a Git repository
  bool is_git_repo_ = false;

  /// Timestamp of the last status update
  std::chrono::system_clock::time_point last_status_update_;

  /// Cached repository path
  std::filesystem::path repo_path_;

  /// Status refresh interval in seconds
  int status_refresh_interval_ = 5;
};

} // namespace shell