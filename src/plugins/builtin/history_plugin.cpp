#include "history_plugin.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h> // Include spdlog header
#include <sstream>


namespace shell {

HistoryPlugin::HistoryPlugin()
    : BuiltinPlugin(PluginMetadata(
          "history", "1.0.0", "Enhanced shell command history management",
          "Lithium Shell Team",
          "https://github.com/lithium-shell/history-plugin", {})),
      last_save_time_(std::chrono::system_clock::now()),
      history_changed_(false) {
  spdlog::debug("HistoryPlugin instance created.");
}

bool HistoryPlugin::initialize(Environment &env) {
  spdlog::info("Initializing HistoryPlugin...");
  env_ = &env;

  // Set history file path
#ifdef _WIN32
  std::filesystem::path home_dir =
      env_->get_env_variable("USERPROFILE").value_or("");
  spdlog::debug(
      "Windows environment detected, using USERPROFILE for home directory.");
#else
  std::filesystem::path home_dir = env_->get_env_variable("HOME").value_or("");
  spdlog::debug("Unix environment detected, using HOME for home directory.");
#endif

  if (home_dir.empty()) {
    spdlog::error("Could not determine home directory for history file.");
    // std::cerr << "Error: Could not determine home directory for history file"
    //           << std::endl;
    return false;
  }
  spdlog::trace("Home directory: {}", home_dir.string());

  // Create .lithium directory if it doesn't exist
  std::filesystem::path config_dir = home_dir / ".lithium";
  spdlog::debug("Config directory path: {}", config_dir.string());
  if (!std::filesystem::exists(config_dir)) {
    spdlog::debug("Config directory does not exist. Creating directory...");
    try {
      std::filesystem::create_directories(config_dir);
      spdlog::debug("Config directory created successfully.");
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error("Failed to create config directory: {}", e.what());
      return false;
    }
  }

  history_file_ = config_dir / "history";
  spdlog::debug("History file path: {}", history_file_.string());

  // Load history
  if (!load_history()) {
    spdlog::warn("Failed to load history file from {}.",
                 history_file_.string());
    // std::cerr << "Warning: Failed to load history file" << std::endl;
    // Continue even if loading fails, as it's not a fatal error
  } else {
    spdlog::info("History loaded successfully from {}.",
                 history_file_.string());
  }

  // Set other parameters
  auto_save_interval_ = 60;     // Auto-save every 60 seconds
  max_history_size_ = 10000;    // Maximum 10000 entries
  enable_deduplication_ = true; // Enable deduplication
  spdlog::debug("History parameters set: auto_save_interval={}s, "
                "max_history_size={}, enable_deduplication={}",
                auto_save_interval_, max_history_size_, enable_deduplication_);

  // std::cout << "History plugin initialized" << std::endl;
  spdlog::info("History plugin initialized successfully.");
  return true;
}

void HistoryPlugin::shutdown() {
  spdlog::info("Shutting down HistoryPlugin...");
  // Save history
  if (history_changed_) {
    spdlog::debug(
        "History has changed since last save. Saving before shutdown.");
    if (save_history()) {
      spdlog::debug("History saved successfully during shutdown.");
    } else {
      spdlog::error("Failed to save history during shutdown.");
    }
  } else {
    spdlog::debug(
        "History unchanged since last save. No save needed during shutdown.");
  }

  env_ = nullptr;
  spdlog::info("HistoryPlugin shut down complete.");
}

bool HistoryPlugin::on_load() {
  spdlog::info("History plugin loaded.");
  // std::cout << "History plugin loaded" << std::endl;
  return true;
}

bool HistoryPlugin::on_unload() {
  spdlog::info("Unloading History plugin...");
  // Ensure history is saved
  if (history_changed_) {
    spdlog::debug("History has changed since last save. Saving before unload.");
    if (save_history()) {
      spdlog::debug("History saved successfully during unload.");
    } else {
      spdlog::error("Failed to save history during unload.");
    }
  } else {
    spdlog::debug(
        "History unchanged since last save. No save needed during unload.");
  }

  // std::cout << "History plugin unloaded" << std::endl;
  spdlog::info("History plugin unloaded.");
  return true;
}

bool HistoryPlugin::handle_event(PluginEvent event,
                                 const PluginEventData &data) {
  spdlog::trace("Handling plugin event: {}", static_cast<int>(event));
  switch (event) {
  case PluginEvent::CommandAfter:
    spdlog::trace("CommandAfter event received.");
    if (std::holds_alternative<PluginEventData::CommandData>(data.data)) {
      const auto &cmd_data = std::get<PluginEventData::CommandData>(data.data);
      spdlog::trace("Command executed: {}", cmd_data.command);

      // Only process non-empty commands
      if (!cmd_data.command.empty()) {
        // Reconstruct the full command string
        std::stringstream ss;
        ss << cmd_data.command;
        for (size_t i = 1; i < cmd_data.args.size(); ++i) {
          ss << " " << cmd_data.args[i];
        }
        std::string full_command = ss.str();
        spdlog::debug("Adding command to history: '{}'", full_command);

        // Add to history
        add_history_entry(full_command);

        // Update command statistics
        update_statistics(cmd_data.command);
        spdlog::trace("Updated command frequency statistics for '{}'",
                      cmd_data.command);

        // Check if auto-save is needed
        auto_save_history();
      } else {
        spdlog::trace("Empty command ignored for history.");
      }
    } else {
      spdlog::trace("CommandAfter event data is not CommandData type.");
    }
    break;

  case PluginEvent::ShellShutdown:
    spdlog::debug("ShellShutdown event received.");
    // Save history
    if (history_changed_) {
      spdlog::info("Saving history before shell shutdown.");
      if (save_history()) {
        spdlog::debug("History saved successfully on shell shutdown.");
      } else {
        spdlog::error("Failed to save history on shell shutdown.");
      }
    } else {
      spdlog::debug("History unchanged since last save. No save needed on "
                    "shell shutdown.");
    }
    break;

  default:
    spdlog::trace("Ignoring unhandled plugin event: {}",
                  static_cast<int>(event));
    break;
  }

  return true;
}

bool HistoryPlugin::save_history() {
  spdlog::debug("Attempting to save history to file: {}",
                history_file_.string());

  if (!env_) {
    spdlog::error("Cannot save history: Environment pointer is null.");
    return false;
  }

  try {
    // Ensure the directory exists
    std::filesystem::create_directories(history_file_.parent_path());
    spdlog::trace("Ensured parent directory exists: {}",
                  history_file_.parent_path().string());

    std::ofstream file(history_file_, std::ios::out);
    if (!file) {
      spdlog::error("Failed to open history file for writing: {}",
                    history_file_.string());
      // std::cerr << "Error: Failed to open history file for writing: "
      //           << history_file_ << std::endl;
      return false;
    }
    spdlog::trace("History file opened successfully for writing.");

    // Get history
    const auto &history = env_->get_history();
    spdlog::debug("Writing {} history entries to file.", history.size());

    // Write history entries
    int entries_written = 0;
    for (const auto &entry : history) {
      file << entry << std::endl;
      entries_written++;
    }

    // Reset state
    history_changed_ = false;
    last_save_time_ = std::chrono::system_clock::now();

    spdlog::info("History saved successfully: {} entries written to {}",
                 entries_written, history_file_.string());
    return true;
  } catch (const std::exception &e) {
    spdlog::error("Error saving history: {}", e.what());
    // std::cerr << "Error saving history: " << e.what() << std::endl;
    return false;
  }
}

bool HistoryPlugin::load_history() {
  spdlog::debug("Attempting to load history from file: {}",
                history_file_.string());

  if (!env_) {
    spdlog::error("Cannot load history: Environment pointer is null.");
    return false;
  }

  // If the history file doesn't exist, return success directly
  if (!std::filesystem::exists(history_file_)) {
    spdlog::info("History file does not exist. Starting with empty history.");
    return true;
  }

  try {
    std::ifstream file(history_file_);
    if (!file) {
      spdlog::error("Failed to open history file for reading: {}",
                    history_file_.string());
      // std::cerr << "Error: Failed to open history file for reading: "
      //           << history_file_ << std::endl;
      return false;
    }
    spdlog::trace("History file opened successfully for reading.");

    // Clear existing history
    size_t existing_entries = env_->get_history().size();
    if (existing_entries > 0) {
      spdlog::debug("Clearing existing history ({} entries).",
                    existing_entries);
      while (env_->get_history().size() > 0) {
        env_->remove_from_history(0);
      }
    }

    // Read history entries
    int entries_loaded = 0;
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty()) {
        env_->add_to_history(line);
        entries_loaded++;

        // Also update statistics
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (!cmd.empty()) {
          update_statistics(cmd);
          spdlog::trace("Updated statistics for command: '{}'", cmd);
        }
      }
    }

    history_changed_ = false;
    spdlog::info("History loaded successfully: {} entries from {}",
                 entries_loaded, history_file_.string());
    return true;
  } catch (const std::exception &e) {
    spdlog::error("Error loading history: {}", e.what());
    // std::cerr << "Error loading history: " << e.what() << std::endl;
    return false;
  }
}

void HistoryPlugin::add_history_entry(const std::string &command) {
  spdlog::trace("Adding history entry: '{}'", command);

  if (!env_) {
    spdlog::error("Cannot add history entry: Environment pointer is null.");
    return;
  }

  // Check if deduplication is enabled
  if (enable_deduplication_) {
    const auto &history = env_->get_history();

    // Check if it duplicates the last entry
    if (!history.empty() && history.back() == command) {
      spdlog::trace("Skipping duplicate of last command in history.");
      return; // Skip duplicate entry
    }

    // Find and remove older identical commands (optional)
    for (int i = static_cast<int>(history.size()) - 2; i >= 0; --i) {
      if (history[i] == command) {
        spdlog::debug("Removing older duplicate command at position {} for "
                      "deduplication.",
                      i);
        env_->remove_from_history(i);
        break;
      }
    }
  }

  // Add new history entry
  env_->add_to_history(command);
  spdlog::trace("Command added to history.");

  // Limit history size
  const auto &history = env_->get_history();
  if (history.size() > max_history_size_) {
    int to_remove = static_cast<int>(history.size()) - max_history_size_;
    spdlog::debug(
        "History size ({}) exceeds maximum ({}). Removing {} oldest entries.",
        history.size(), max_history_size_, to_remove);

    for (int i = 0; i < to_remove; i++) {
      env_->remove_from_history(0);
    }
  }

  history_changed_ = true;
}

void HistoryPlugin::update_statistics(const std::string &command) {
  spdlog::trace("Updating statistics for command: '{}'", command);

  // Update command frequency statistics
  command_frequency_[command]++;
  spdlog::trace("Command '{}' frequency updated to {}", command,
                command_frequency_[command]);

  // Update the most used commands cache
  // This is a simple implementation; a more efficient one might use a priority
  // queue First, check if the command is already in the list
  auto it = std::find(most_used_commands_.begin(), most_used_commands_.end(),
                      command);
  if (it != most_used_commands_.end()) {
    // It's already in the list, need to adjust position based on new frequency
    spdlog::trace("Command '{}' already in most-used list. Adjusting position.",
                  command);
    most_used_commands_.erase(it);
  }

  // Find the insertion position
  auto freq = command_frequency_[command];
  auto pos = most_used_commands_.begin();
  while (pos != most_used_commands_.end() && command_frequency_[*pos] >= freq) {
    ++pos;
  }

  // Insert the command
  most_used_commands_.insert(pos, command);
  spdlog::trace("Command '{}' inserted in most-used list at position {}",
                command, std::distance(most_used_commands_.begin(), pos));

  // Limit the list size to the top 10
  if (most_used_commands_.size() > 10) {
    spdlog::trace("Most-used list exceeded 10 entries. Trimming.");
    most_used_commands_.resize(10);
  }

  // Log the updated top commands for debugging
  if (spdlog::should_log(spdlog::level::debug)) {
    std::stringstream ss;
    for (const auto &cmd : most_used_commands_) {
      ss << cmd << "(" << command_frequency_[cmd] << ") ";
    }
    spdlog::debug("Updated most used commands: {}", ss.str());
  }
}

void HistoryPlugin::auto_save_history() {
  if (!history_changed_) {
    spdlog::trace(
        "Auto-save check: History unchanged since last save. No save needed.");
    return;
  }

  auto now = std::chrono::system_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time_)
          .count();

  spdlog::trace(
      "Auto-save check: {} seconds elapsed since last save (interval: {}s)",
      elapsed, auto_save_interval_);

  if (elapsed >= auto_save_interval_) {
    spdlog::debug("Auto-save interval reached. Saving history.");
    if (save_history()) {
      spdlog::debug("Auto-save completed successfully.");
    } else {
      spdlog::error("Auto-save failed.");
    }
  }
}

} // namespace shell