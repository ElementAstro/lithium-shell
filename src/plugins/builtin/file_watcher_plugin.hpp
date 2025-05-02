#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../core/environment.hpp"
#include "../plugin_manager.hpp"

namespace shell {

/**
 * @class FileWatcherPlugin
 * @brief Monitors file system changes and notifies the user
 *
 * This plugin provides file monitoring functionality, including:
 * - Watching for file changes in specified directories
 * - Automatically refreshing file lists
 * - Providing notifications when files change
 */
class FileWatcherPlugin : public BuiltinPlugin {
public:
  FileWatcherPlugin();
  ~FileWatcherPlugin();

  bool initialize(Environment &env) override;
  void shutdown() override;

  /**
   * @brief Lifecycle methods
   */
  bool on_load() override;
  bool on_unload() override;
  bool on_enable() override;
  bool on_disable() override;

  /**
   * @brief Handles plugin events
   * @param event The event type
   * @param data Associated event data
   * @return True if event was handled successfully
   */
  bool handle_event(PluginEvent event, const PluginEventData &data) override;

  /**
   * @brief Adds a directory to watch for changes
   * @param path The directory path to monitor
   * @param recursive Whether to monitor subdirectories recursively
   * @return True if the directory was successfully added
   */
  bool add_watch(const std::filesystem::path &path, bool recursive = false);

  /**
   * @brief Removes a watched directory
   * @param path The directory path to stop monitoring
   * @return True if the directory was successfully removed
   */
  bool remove_watch(const std::filesystem::path &path);

  /**
   * @brief Gets all currently watched directories
   * @return Vector of paths being monitored
   */
  std::vector<std::filesystem::path> get_watched_paths() const;

  /**
   * @brief Sets the watch interval in milliseconds
   * @param milliseconds The interval between file system checks
   */
  void set_watch_interval(int milliseconds);

private:
  /// File change callback function type
  using FileChangeCallback =
      std::function<void(const std::filesystem::path &, const std::string &)>;

  /**
   * @brief Thread function that monitors for file changes
   */
  void watcher_thread_func();

  /**
   * @brief Checks for changes in the specified directory
   * @param dir The directory to check
   * @param recursive Whether to check subdirectories recursively
   */
  void check_directory_changes(const std::filesystem::path &dir,
                               bool recursive);

  /**
   * @brief Saves the current file state for the specified directory
   * @param dir The directory to save state for
   * @param recursive Whether to include subdirectories recursively
   */
  void save_file_state(const std::filesystem::path &dir, bool recursive);

  /**
   * @brief Sets up default directories to watch
   */
  void setup_default_watches();

  /// Reference to the environment
  Environment *env_ = nullptr;

  /// File system monitoring thread
  std::thread watcher_thread_;

  /// Thread synchronization
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> should_stop_;

  /// Whether the plugin is enabled
  std::atomic<bool> is_enabled_;

  /// Monitoring interval in milliseconds
  int watch_interval_ms_;

  /// Watched directories (path and recursive flag)
  std::vector<std::pair<std::filesystem::path, bool>> watched_paths_;

  /// File state cache <file path, last modification time>
  std::unordered_map<std::string, std::filesystem::file_time_type> file_states_;

  /// File change callback
/**
   * @brief Helper function to save file state from directory iterator
   * @tparam DirectoryIterator Type of directory iterator (recursive or non-recursive)
   * @param dir_iterator The directory iterator to process
   */
  template<typename DirectoryIterator>
  void save_file_state_from_iterator(DirectoryIterator &dir_iterator);

  /**
   * @brief Process a single directory entry for file changes
   * @param entry The directory entry to process
   */
  void process_directory_entry(const std::filesystem::directory_entry &entry);
  
  FileChangeCallback file_change_callback_;
};

} // namespace shell