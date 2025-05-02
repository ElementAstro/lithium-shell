#include "file_watcher_plugin.hpp"

#include <algorithm>
#include <chrono>
#include <iostream> // Keep for potential direct output if needed, but prefer spdlog
#include <spdlog/spdlog.h> // Include spdlog header
#include <thread>


namespace shell {

FileWatcherPlugin::FileWatcherPlugin()
    : BuiltinPlugin(
          PluginMetadata("file_watcher", "1.0.0",
                         "Monitor file system changes and notify the user",
                         "Lithium Shell Team")),
      should_stop_(false), is_enabled_(false),
      watch_interval_ms_(2000) // Default check interval: 2 seconds
{
  spdlog::debug("FileWatcherPlugin instance created.");
  // Setup callback for file changes
  file_change_callback_ = [this](const std::filesystem::path &path,
                                 const std::string &event_type) {
    // Log the event first
    spdlog::info("File change detected - Type: {}, Path: {}", event_type,
                 path.string());
    if (env_) {
      // This direct output might interfere with the shell prompt.
      // Consider using a more integrated notification mechanism if available.
      std::cout << "\n[File Change] " << event_type << ": " << path.string()
                << std::endl;
      // Display prompt - This is a basic placeholder and might need adjustment
      // based on the shell's UI handling.
      std::cout << "> " << std::flush;
    } else {
      spdlog::warn("File change detected but environment pointer is null, "
                   "cannot display "
                   "prompt.");
    }
  };
}

FileWatcherPlugin::~FileWatcherPlugin() {
  spdlog::debug("FileWatcherPlugin instance destroyed.");
  shutdown(); // Ensure resources are released
}

bool FileWatcherPlugin::initialize(Environment &env) {
  spdlog::info("Initializing FileWatcherPlugin...");
  env_ = &env;

  // Register command handlers - This section needs adaptation based on the
  // actual Environment interface for command registration. The example below
  // is illustrative.

  /*
  // Example command registration structure:
  spdlog::debug("Registering 'watch' command (example)...");
  try {
      env_->register_command("watch", [this](const std::vector<std::string>&
  args) -> int { spdlog::trace("'watch' command invoked with {} arguments.",
  args.size()); if (args.size() < 1) { spdlog::error("Usage: watch [path]
  [recursive=false]"); std::cerr << "Usage: watch <path> [recursive]" <<
  std::endl; // Keep cerr for direct user feedback on usage return 1;
      }

      std::filesystem::path path_to_watch;
      try {
          path_to_watch = args[0]; // Assuming first arg is path
      } catch (const std::exception& e) {
          spdlog::error("Failed to parse path argument: {}", e.what());
          std::cerr << "Error: Invalid path specified." << std::endl;
          return 1;
      }

      bool recursive = (args.size() > 1 && (args[1] == "recursive" || args[1]
  == "true")); spdlog::debug("Attempting to add watch for path '{}', recursive:
  {}", path_to_watch.string(), recursive);

      if (add_watch(path_to_watch, recursive)) {
          std::cout << "Started watching " << path_to_watch.string();
          if (recursive) std::cout << " (recursively)";
          std::cout << std::endl;
          spdlog::info("Successfully added watch for '{}'.",
  path_to_watch.string()); return 0; } else { std::cerr << "Error: Cannot watch
  directory: " << path_to_watch.string() << std::endl; spdlog::error("Failed to
  add watch for '{}'.", path_to_watch.string()); return 1;
      }
    });
    spdlog::debug("'watch' command registered successfully (example).");
  } catch (const std::exception& e) {
      spdlog::error("Failed to register 'watch' command (example): {}",
  e.what());
      // Depending on requirements, initialization might fail here
      // return false;
  }
  */
  // Placeholder for actual command registration if needed.
  spdlog::warn("Command registration in FileWatcherPlugin::initialize is "
               "currently commented out/placeholder.");

  spdlog::info("FileWatcherPlugin initialized successfully.");
  return true;
}

void FileWatcherPlugin::shutdown() {
  spdlog::info("Shutting down FileWatcherPlugin...");
  // Stop the watcher thread
  if (watcher_thread_.joinable()) {
    spdlog::debug("Stopping watcher thread...");
    {
      std::unique_lock<std::mutex> lock(mutex_);
      should_stop_ = true;
      cv_.notify_one(); // Notify the thread to wake up and check should_stop_
      spdlog::trace("Notified watcher thread to stop.");
    }
    try {
      watcher_thread_.join();
      spdlog::debug("Watcher thread joined successfully.");
    } catch (const std::system_error &e) {
      spdlog::error("Error joining watcher thread: {}", e.what());
    }
  } else {
    spdlog::debug("Watcher thread was not running or already joined.");
  }

  // Clear state
  spdlog::debug("Clearing watched paths and file states...");
  std::unique_lock<std::mutex> lock(mutex_); // Lock for clearing shared data
  watched_paths_.clear();
  file_states_.clear();
  is_enabled_ = false;
  spdlog::debug("FileWatcherPlugin state cleared.");
  spdlog::info("FileWatcherPlugin shut down complete.");
}

bool FileWatcherPlugin::on_load() {
  spdlog::info("FileWatcherPlugin loaded.");
  // std::cout << "Loading file watcher plugin..." << std::endl; // Replaced by
  // spdlog
  return true;
}

bool FileWatcherPlugin::on_unload() {
  spdlog::info("FileWatcherPlugin unloaded.");
  // std::cout << "Unloading file watcher plugin..." << std::endl; // Replaced
  // by spdlog
  shutdown(); // Ensure cleanup when unloaded
  return true;
}

bool FileWatcherPlugin::on_enable() {
  spdlog::info("Enabling FileWatcherPlugin...");
  if (is_enabled_) {
    spdlog::warn("FileWatcherPlugin is already enabled.");
    return true;
  }

  // Setup default directories to watch
  spdlog::debug("Setting up default watches...");
  setup_default_watches();

  // Start the watcher thread
  spdlog::debug("Starting watcher thread...");
  should_stop_ = false;
  try {
    watcher_thread_ =
        std::thread(&FileWatcherPlugin::watcher_thread_func, this);
    is_enabled_ = true;
    spdlog::info("FileWatcherPlugin enabled and watcher thread started.");
  } catch (const std::system_error &e) {
    spdlog::error("Failed to start watcher thread: {}", e.what());
    is_enabled_ = false; // Ensure state reflects failure
    return false;
  }

  return true;
}

bool FileWatcherPlugin::on_disable() {
  spdlog::info("Disabling FileWatcherPlugin...");
  if (!is_enabled_) {
    spdlog::warn("FileWatcherPlugin is already disabled.");
    return true;
  }

  // Stop the watcher thread
  if (watcher_thread_.joinable()) {
    spdlog::debug("Stopping watcher thread for disabling...");
    {
      std::unique_lock<std::mutex> lock(mutex_);
      should_stop_ = true;
      cv_.notify_one();
      spdlog::trace("Notified watcher thread to stop.");
    }
    try {
      watcher_thread_.join();
      spdlog::debug("Watcher thread joined successfully during disable.");
    } catch (const std::system_error &e) {
      spdlog::error("Error joining watcher thread during disable: {}",
                    e.what());
      // Continue disabling process even if join fails
    }
  } else {
    spdlog::debug(
        "Watcher thread was not running or already joined during disable.");
  }

  is_enabled_ = false;
  spdlog::info("FileWatcherPlugin disabled.");
  return true;
}

bool FileWatcherPlugin::handle_event(PluginEvent event,
                                     const PluginEventData &data) {
  spdlog::trace("Handling plugin event: {}", static_cast<int>(event));
  // Adding data usage to avoid warning - standard practice
  (void)data; // Mark data as used if not explicitly used in all cases

  switch (event) {
  case PluginEvent::ShellStartup:
    spdlog::trace("ShellStartup event received (handled by on_enable).");
    // Typically handled by on_enable if the plugin is enabled by default or via
    // config
    break;

  case PluginEvent::ShellShutdown:
    spdlog::trace(
        "ShellShutdown event received (handled by on_disable/shutdown).");
    // Typically handled by on_disable or the destructor/shutdown method
    break;

  case PluginEvent::EnvironmentChanged:
    spdlog::trace("EnvironmentChanged event received.");
    // Example: If working directory changes, add the new directory to watch
    if (env_) {
      // Check if the change involves the working directory (PWD)
      // This requires inspecting 'data', assuming it contains variable
      // name/value Example structure (adapt based on actual PluginEventData):
      /*
      if (std::holds_alternative<PluginEventData::EnvironmentData>(data.data)) {
          const auto& env_data =
      std::get<PluginEventData::EnvironmentData>(data.data); if (env_data.name
      == "PWD") { spdlog::debug("Working directory changed to '{}'. Adding
      watch.", env_data.value); try { std::filesystem::path
      current_dir(env_data.value); add_watch(current_dir, false); // Watch
      non-recursively by default } catch (const std::exception& e) {
                  spdlog::error("Failed to add watch for new working directory:
      {}", e.what());
              }
          }
      }
      */
      spdlog::warn("EnvironmentChanged handling for PWD is currently commented "
                   "out/placeholder.");
      // Fallback or simpler logic: just get current path if PWD change isn't
      // detailed
      try {
        std::filesystem::path current_dir = std::filesystem::current_path();
        spdlog::debug(
            "Environment changed, ensuring current directory '{}' is watched.",
            current_dir.string());
        add_watch(current_dir,
                  false); // Add non-recursive watch for current dir
      } catch (const std::exception &e) {
        spdlog::error("Failed to get or watch current directory on "
                      "EnvironmentChanged: {}",
                      e.what());
      }
    } else {
      spdlog::warn(
          "EnvironmentChanged event received but environment pointer is null.");
    }
    break;

  case PluginEvent::CommandAfter:
    spdlog::trace("CommandAfter event received.");
    // Can update watched directories after specific commands here
    // For example, after 'git clone' or 'mkdir', add the new directory to watch
    // This requires inspecting 'data' to know which command was executed and
    // its arguments. Example structure (adapt based on actual PluginEventData):
    /*
    if (std::holds_alternative<PluginEventData::CommandData>(data.data)) {
        const auto& cmd_data =
    std::get<PluginEventData::CommandData>(data.data); spdlog::trace("Command
    executed: {}", cmd_data.command_name); if (cmd_data.command_name == "mkdir"
    && cmd_data.args.size() > 0) { try { std::filesystem::path new_dir =
    cmd_data.args[0]; // Assuming first arg is dir name if
    (std::filesystem::is_directory(new_dir)) { // Check if it exists and is a
    dir spdlog::info("Adding watch for newly created directory: {}",
    new_dir.string()); add_watch(new_dir, false);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to add watch after mkdir command: {}",
    e.what());
            }
        }
        // Add similar logic for 'git clone', etc.
    }
    */
    spdlog::warn("CommandAfter handling for specific commands is currently "
                 "placeholder.");
    break;

  default:
    spdlog::trace("Ignoring unhandled plugin event: {}",
                  static_cast<int>(event));
    break;
  }

  return true; // Indicate event was handled (or ignored intentionally)
}

bool FileWatcherPlugin::add_watch(const std::filesystem::path &path,
                                  bool recursive) {
  spdlog::debug("Attempting to add watch for path '{}', recursive: {}",
                path.string(), recursive);

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    spdlog::error(
        "Cannot add watch: Path '{}' does not exist or error accessing: {}",
        path.string(), ec.message());
    return false;
  }
  if (!std::filesystem::is_directory(path, ec) || ec) {
    spdlog::error(
        "Cannot add watch: Path '{}' is not a directory or error accessing: {}",
        path.string(), ec.message());
    return false;
  }

  std::unique_lock<std::mutex> lock(mutex_);

  // Check if we're already watching this directory
  for (const auto &[watched_path, is_recursive] : watched_paths_) {
    if (watched_path == path) {
      // If already watching, potentially update the recursive flag if needed
      if (is_recursive != recursive) {
        // This scenario might require more complex handling (e.g., removing and
        // re-adding) For simplicity, we just log and maybe update the flag if
        // structure allows. Current structure doesn't easily allow updating, so
        // just log.
        spdlog::warn("Path '{}' already watched. Recursive flag mismatch "
                     "(current: {}, requested: {}). Keeping current.",
                     path.string(), is_recursive, recursive);
      } else {
        spdlog::debug("Path '{}' is already being watched.", path.string());
      }
      return true; // Indicate success as it's already watched
    }
  }

  // Add to watched directories list
  spdlog::debug("Adding '{}' to watched paths list.", path.string());
  watched_paths_.push_back({path, recursive});

  // Save initial file state for the new watch
  spdlog::debug("Saving initial file state for '{}'.", path.string());
  // Unlock mutex temporarily to avoid holding it during potentially long I/O
  lock.unlock();
  save_file_state(path, recursive);
  lock.lock(); // Re-lock if needed, though not strictly necessary here

  spdlog::info("Successfully added watch for '{}', recursive: {}.",
               path.string(), recursive);
  return true;
}

bool FileWatcherPlugin::remove_watch(const std::filesystem::path &path) {
  spdlog::debug("Attempting to remove watch for path '{}'.", path.string());
  std::unique_lock<std::mutex> lock(mutex_);

  auto it =
      std::find_if(watched_paths_.begin(), watched_paths_.end(),
                   [&path](const auto &pair) { return pair.first == path; });

  if (it == watched_paths_.end()) {
    spdlog::warn("Cannot remove watch: Path '{}' is not currently watched.",
                 path.string());
    return false; // Not watching this directory
  }

  // Remove from watched directories list
  spdlog::debug("Removing '{}' from watched paths list.", path.string());
  watched_paths_.erase(it);

  // Remove related file states from the cache
  spdlog::debug("Removing file states associated with '{}'.", path.string());
  auto prefix = path.string();
  auto state_it = file_states_.begin();
  int removed_count = 0;
  while (state_it != file_states_.end()) {
    // Check if the file state path starts with the directory being removed
    // Need to handle path separators carefully if paths aren't normalized
    if (state_it->first.rfind(prefix, 0) == 0) {
      spdlog::trace("Removing state for file: {}", state_it->first);
      state_it = file_states_.erase(state_it);
      removed_count++;
    } else {
      ++state_it;
    }
  }
  spdlog::debug("Removed {} file states for path '{}'.", removed_count,
                path.string());

  spdlog::info("Successfully removed watch for '{}'.", path.string());
  return true;
}

std::vector<std::filesystem::path>
FileWatcherPlugin::get_watched_paths() const {
  spdlog::trace("Getting list of watched paths.");
  std::vector<std::filesystem::path> paths;
  std::unique_lock lock(mutex_); // Lock for reading shared data
  paths.reserve(watched_paths_.size());
  for (const auto &[path, _] : watched_paths_) {
    paths.push_back(path);
  }
  spdlog::trace("Returning {} watched paths.", paths.size());
  return paths;
}

void FileWatcherPlugin::set_watch_interval(int milliseconds) {
  spdlog::debug("Attempting to set watch interval to {} ms.", milliseconds);
  if (milliseconds >= 100) { // Enforce a minimum interval
    watch_interval_ms_ = milliseconds;
    spdlog::info("Watch interval set to {} ms.", milliseconds);
    // Optionally notify the watcher thread to potentially restart its wait
    // timer cv_.notify_one(); // This might cause more frequent checks
    // initially
  } else {
    spdlog::warn("Invalid watch interval {} ms. Must be >= 100 ms. Keeping "
                 "current value ({} ms).",
                 milliseconds, watch_interval_ms_);
  }
}

void FileWatcherPlugin::watcher_thread_func() {
  spdlog::info("Watcher thread started.");
  while (true) {
    // Check stop condition immediately after waking up or starting
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (should_stop_) {
        spdlog::debug("Stop signal received in watcher thread. Exiting.");
        break; // Exit the loop if stop is requested
      }
    }

    spdlog::trace("Watcher thread iteration started.");
    // Copy the watched directories list to avoid holding the lock during checks
    std::vector<std::pair<std::filesystem::path, bool>> paths_to_check;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      paths_to_check = watched_paths_;
      spdlog::trace("Copied {} paths to check this iteration.",
                    paths_to_check.size());
    }

    // Check changes for each watched directory
    for (const auto &[path, recursive] : paths_to_check) {
      // Re-check stop condition periodically within the loop if checks are long
      if (should_stop_)
        break;

      std::error_code ec;
      if (std::filesystem::exists(path, ec) && !ec &&
          std::filesystem::is_directory(path, ec) && !ec) {
        spdlog::trace("Checking directory '{}' (recursive: {})...",
                      path.string(), recursive);
        check_directory_changes(path, recursive);
      } else {
        spdlog::warn(
            "Watched path '{}' no longer exists or is not a directory. "
            "Error: {}",
            path.string(), ec.message());
        // Optionally remove the watch automatically here
        // remove_watch(path); // Be careful with locking if called here
      }
    }

    // Wait for the next check interval or until notified to stop
    spdlog::trace("Watcher thread waiting for {} ms or stop signal...",
                  watch_interval_ms_);
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Wait using the condition variable
      cv_.wait_for(lock, std::chrono::milliseconds(watch_interval_ms_),
                   [this]() -> bool { return should_stop_; });
      // Loop will check should_stop_ again at the beginning
    }
  }
  spdlog::info("Watcher thread finished.");
}

void FileWatcherPlugin::check_directory_changes(
    const std::filesystem::path &dir, bool recursive) {
  spdlog::trace("Checking changes in directory: {}", dir.string());
  std::unordered_map<std::string, std::filesystem::file_time_type>
      current_states;
  std::error_code ec;

  try {
    // Iterate through the directory (recursively or not) and collect current
    // states
    auto iterator_options =
        std::filesystem::directory_options::skip_permission_denied;

    if (recursive) {
      std::filesystem::recursive_directory_iterator dir_iterator(
          dir, iterator_options, ec);
      if (ec) {
        spdlog::error("Error creating recursive iterator for '{}': {}",
                      dir.string(), ec.message());
        return;
      }
      for (const auto &entry : dir_iterator) {
        if (should_stop_)
          return; // Check stop condition during iteration
        try {
          const auto &path = entry.path();
          // Basic check for hidden files/dirs (might need refinement for
          // cross-platform)
          if (path.filename().string().starts_with('.')) {
            if (entry.is_directory()) {
              dir_iterator.disable_recursion_pending(); // Don't recurse into
                                                        // hidden dirs
            }
            continue; // Skip hidden entries
          }
          if (entry.is_regular_file(ec) &&
              !ec) { // Only process regular files for simplicity
            auto mod_time = std::filesystem::last_write_time(path, ec);
            if (!ec) {
              current_states[path.string()] = mod_time;
            } else {
              spdlog::trace("Could not get mod time for '{}': {}",
                            path.string(), ec.message());
            }
          }
        } catch (const std::exception &e) {
          spdlog::warn(
              "Error processing entry during recursive scan of '{}': {}",
              dir.string(), e.what());
        }
      }
    } else {
      std::filesystem::directory_iterator dir_iterator(dir, iterator_options,
                                                       ec);
      if (ec) {
        spdlog::error("Error creating directory iterator for '{}': {}",
                      dir.string(), ec.message());
        return;
      }
      for (const auto &entry : dir_iterator) {
        if (should_stop_)
          return; // Check stop condition during iteration
        try {
          const auto &path = entry.path();
          if (path.filename().string().starts_with('.')) {
            continue; // Skip hidden entries
          }
          if (entry.is_regular_file(ec) && !ec) {
            auto mod_time = std::filesystem::last_write_time(path, ec);
            if (!ec) {
              current_states[path.string()] = mod_time;
            } else {
              spdlog::trace("Could not get mod time for '{}': {}",
                            path.string(), ec.message());
            }
          }
        } catch (const std::exception &e) {
          spdlog::warn(
              "Error processing entry during non-recursive scan of '{}': {}",
              dir.string(), e.what());
        }
      }
    }

    // Compare current states with cached states under lock
    std::unique_lock<std::mutex> lock(mutex_);
    spdlog::trace(
        "Comparing current states ({}) with cached states ({}) for '{}'.",
        current_states.size(), file_states_.size(), dir.string());

    // Check for new or modified files
    for (const auto &[path_str, current_mod_time] : current_states) {
      if (should_stop_)
        return; // Check stop condition
      auto it = file_states_.find(path_str);
      if (it == file_states_.end()) {
        // New file found
        spdlog::trace("New file detected: {}", path_str);
        file_states_[path_str] = current_mod_time; // Add to cache
        lock.unlock(); // Unlock before calling callback
        file_change_callback_(std::filesystem::path(path_str), "Added");
        lock.lock(); // Re-lock
      } else if (it->second != current_mod_time) {
        // File modified
        spdlog::trace("Modified file detected: {}", path_str);
        it->second = current_mod_time; // Update cache
        lock.unlock();                 // Unlock before calling callback
        file_change_callback_(std::filesystem::path(path_str), "Modified");
        lock.lock(); // Re-lock
      }
      // If times match, no change, do nothing.
    }

    // Check for deleted files (files in cache but not in current scan)
    auto state_it = file_states_.begin();
    while (state_it != file_states_.end()) {
      if (should_stop_)
        return; // Check stop condition
      const std::string &cached_path_str = state_it->first;

      // Only check files belonging to the directory being scanned
      // Ensure path comparison is robust (e.g., normalization or careful prefix
      // check)
      if (cached_path_str.rfind(dir.string(), 0) == 0) {
        if (current_states.find(cached_path_str) == current_states.end()) {
          // File deleted
          spdlog::trace("Deleted file detected: {}", cached_path_str);
          std::filesystem::path deleted_path(cached_path_str);
          state_it = file_states_.erase(state_it); // Remove from cache
          lock.unlock(); // Unlock before calling callback
          file_change_callback_(deleted_path, "Deleted");
          lock.lock(); // Re-lock
          // Continue loop with the iterator returned by erase
          continue;
        }
      }
      ++state_it; // Move to next cached file
    }
    spdlog::trace("Finished checking changes in directory: {}", dir.string());

  } catch (const std::exception &e) {
    spdlog::error("Exception during directory change check for '{}': {}",
                  dir.string(), e.what());
  }
}

// This helper method is now integrated into check_directory_changes
// void FileWatcherPlugin::process_directory_entry(...) { ... }

void FileWatcherPlugin::save_file_state(const std::filesystem::path &dir,
                                        bool recursive) {
  spdlog::debug("Saving initial file state for directory '{}', recursive: {}",
                dir.string(), recursive);
  std::error_code ec;
  int saved_count = 0;
  try {
    auto iterator_options =
        std::filesystem::directory_options::skip_permission_denied;

    // Use the appropriate iterator based on the recursive flag
    if (recursive) {
      std::filesystem::recursive_directory_iterator dir_iterator(
          dir, iterator_options, ec);
      if (ec) {
        spdlog::error(
            "Error creating recursive iterator for saving state '{}': {}",
            dir.string(), ec.message());
        return;
      }
      save_file_state_from_iterator(dir_iterator);
    } else {
      std::filesystem::directory_iterator dir_iterator(dir, iterator_options,
                                                       ec);
      if (ec) {
        spdlog::error(
            "Error creating directory iterator for saving state '{}': {}",
            dir.string(), ec.message());
        return;
      }
      save_file_state_from_iterator(dir_iterator);
    }
    spdlog::debug("Saved initial state for {} files in '{}'.", saved_count,
                  dir.string());

  } catch (const std::exception &e) {
    spdlog::error("Exception during initial file state saving for '{}': {}",
                  dir.string(), e.what());
  }
}

// Helper template method to save file state from any directory iterator type
template <typename DirectoryIterator>
void FileWatcherPlugin::save_file_state_from_iterator(
    DirectoryIterator &dir_iterator) {
  std::unique_lock<std::mutex> lock(mutex_); // Lock for modifying file_states_

  for (const auto &entry : dir_iterator) {
    if (should_stop_)
      break; // Check stop condition during iteration
    try {
      const auto &path = entry.path();
      std::error_code ec;

      // Ignore hidden files/directories (basic check)
      if (path.filename().string().starts_with('.')) {
        // If recursive and it's a directory, stop recursion into it
        if constexpr (std::is_same_v<
                          DirectoryIterator,
                          std::filesystem::recursive_directory_iterator>) {
          if (entry.is_directory(ec) && !ec) {
            spdlog::trace("Skipping recursion into hidden directory: {}",
                          path.string());
            dir_iterator.disable_recursion_pending();
          } else {
            spdlog::trace("Skipping hidden file: {}", path.string());
          }
        } else {
          spdlog::trace("Skipping hidden entry: {}", path.string());
        }
        continue; // Skip this hidden entry
      }

      // Save file modification time only for regular files
      if (entry.is_regular_file(ec) && !ec) {
        auto mod_time = std::filesystem::last_write_time(path, ec);
        if (!ec) {
          spdlog::trace("Saving state for file: {}", path.string());
          file_states_[path.string()] = mod_time;
        } else {
          spdlog::trace("Could not get mod time for '{}' during save: {}",
                        path.string(), ec.message());
        }
      }
    } catch (const std::exception &e) {
      // Log errors for individual files but continue scanning
      spdlog::warn("Error processing entry during state save: {}", e.what());
      continue;
    }
  }
}

void FileWatcherPlugin::setup_default_watches() {
  spdlog::debug("Setting up default watches...");
  // Watch current working directory by default (non-recursively)
  if (env_) {
    try {
      std::filesystem::path current_dir = std::filesystem::current_path();
      spdlog::debug("Adding default watch for current directory: {}",
                    current_dir.string());
      add_watch(current_dir, false); // Add non-recursive watch
    } catch (const std::exception &e) {
      spdlog::error(
          "Failed to get or watch current directory for default watch: {}",
          e.what());
    }
  } else {
    spdlog::warn(
        "Cannot setup default watch for current directory: Environment pointer "
        "is null.");
  }
  // Add other default watches here if needed (e.g., home directory)
}

} // namespace shell