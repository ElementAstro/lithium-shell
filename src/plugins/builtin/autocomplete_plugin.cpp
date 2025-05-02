#include "autocomplete_plugin.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>

namespace shell {

AutoCompletePlugin::AutoCompletePlugin()
    : BuiltinPlugin(PluginMetadata(
          "autocomplete", "1.0.0", "Enhanced command line completion support",
          "Lithium Shell Team",
          "https://github.com/lithium-shell/autocomplete-plugin", {})) {
  // Constructor - initializes the plugin metadata
}

bool AutoCompletePlugin::initialize(Environment &env) {
  spdlog::info("Initializing AutoCompletePlugin...");
  env_ = &env;

  // Initialize the completion database
  spdlog::debug("Initializing command completions...");
  initialize_completions();
  spdlog::debug("Command completions initialized.");

  // Collect environment variables
  spdlog::debug("Collecting initial environment variables...");
  env_vars_.clear();
  for (const auto &[name, _] : env_->get_all_env_variables()) {
    env_vars_.push_back(name);
  }
  spdlog::debug("Collected {} environment variables.", env_vars_.size());

  // Set initial cache update time
  last_cache_update_ = std::filesystem::file_time_type::clock::now();
  spdlog::debug("Initial cache update time set.");

  // Initial check if in a Git repository and update branches
  try {
    auto git_dir = env_->get_working_directory() / ".git";
    in_git_repo_ = std::filesystem::exists(git_dir) &&
                   std::filesystem::is_directory(git_dir);
    spdlog::debug("Checking for Git repository at '{}'. In Git repo: {}",
                  git_dir.string(), in_git_repo_);
    if (in_git_repo_) {
      update_git_branches();
    }
  } catch (const std::exception &e) {
    spdlog::error("Error checking git repository during initialization: {}",
                  e.what());
    in_git_repo_ = false;
  }

  spdlog::info("Autocomplete plugin initialized successfully.");
  // std::cout << "Autocomplete plugin initialized" << std::endl; // Replaced by
  // spdlog
  return true;
}

void AutoCompletePlugin::shutdown() {
  spdlog::info("Shutting down AutoCompletePlugin...");
  // Clean up resources
  command_args_.clear();
  file_cache_.clear();
  env_vars_.clear();
  git_branches_.clear();
  env_ = nullptr;
  spdlog::info("AutoCompletePlugin resources cleaned up.");
}

bool AutoCompletePlugin::on_load() {
  spdlog::info("Autocomplete plugin loaded.");
  // std::cout << "Autocomplete plugin loaded" << std::endl; // Replaced by
  // spdlog
  return true;
}

bool AutoCompletePlugin::on_unload() {
  spdlog::info("Autocomplete plugin unloaded.");
  // std::cout << "Autocomplete plugin unloaded" << std::endl; // Replaced by
  // spdlog
  return true;
}

bool AutoCompletePlugin::handle_event(PluginEvent event,
                                      const PluginEventData &data) {
  spdlog::trace("Handling plugin event: {}", static_cast<int>(event));
  switch (event) {
  case PluginEvent::CommandBefore: {
    // Before the user enters a command, check if the cache needs updating
    auto now = std::filesystem::file_time_type::clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       now - last_cache_update_)
                       .count();
    spdlog::trace("CommandBefore event: {} seconds since last cache update.",
                  elapsed);
    // Update cache every 60 seconds
    if (now - last_cache_update_ > std::chrono::seconds(60)) {
      spdlog::debug("Cache update interval exceeded. Updating caches...");
      update_file_cache();

      if (in_git_repo_) {
        spdlog::debug("Updating Git branches cache...");
        update_git_branches();
      }

      // Update environment variable cache
      spdlog::debug("Updating environment variables cache...");
      env_vars_.clear();
      for (const auto &[name, _] : env_->get_all_env_variables()) {
        env_vars_.push_back(name);
      }
      spdlog::debug("Collected {} environment variables.", env_vars_.size());

      last_cache_update_ = now;
      spdlog::debug("Cache update complete. Timestamp updated.");
    }
    break;
  }
  case PluginEvent::EnvironmentChanged: {
    // When the working directory changes, check if entering/leaving a Git
    // repository
    if (std::holds_alternative<PluginEventData::EnvironmentData>(data.data)) {
      const auto &env_data =
          std::get<PluginEventData::EnvironmentData>(data.data);
      spdlog::trace("EnvironmentChanged event: Variable '{}' changed.",
                    env_data.name);
      if (env_data.name == "PWD") {
        try {
          auto git_dir = env_->get_working_directory() / ".git";
          bool was_in_git = in_git_repo_;
          in_git_repo_ = std::filesystem::exists(git_dir) &&
                         std::filesystem::is_directory(git_dir);
          spdlog::debug("New Git status check. In Git repo: {}", in_git_repo_);

          // If the status changed, update the git branch cache
          if (in_git_repo_ != was_in_git) {
            spdlog::info("Git repository status changed ({} -> {}).",
                         was_in_git, in_git_repo_);
            if (in_git_repo_) {
              update_git_branches();
            } else {
              spdlog::debug("Left Git repository, clearing branch cache.");
              git_branches_.clear();
            }
          } else if (in_git_repo_) {
            // If still in a git repo but changed directory, update branches too
            spdlog::debug(
                "Still in Git repository, updating branches for new path.");
            update_git_branches();
          }

          // Always update the file cache on directory change
          spdlog::debug("Updating file cache due to directory change.");
          update_file_cache();
        } catch (const std::exception &e) {
          spdlog::error("Error updating Git status on environment change: {}",
                        e.what());
        }
      }
    }
    break;
  }
  default:
    // No action for other events
    spdlog::trace("Ignoring unhandled plugin event: {}",
                  static_cast<int>(event));
    break;
  }
  return true;
}

void AutoCompletePlugin::initialize_completions() {
  spdlog::debug("Populating command argument completions map...");
  // Initialize arguments for common commands
  command_args_["git"] = {"add",    "commit", "push",  "pull", "checkout",
                          "branch", "status", "diff",  "log",  "clone",
                          "fetch",  "merge",  "rebase"};
  command_args_["cd"] = {"-", "..", "/"};
  command_args_["ls"] = {"-la", "-l", "-a", "--color=auto"};
  command_args_["cp"] = {"-r", "-v", "-f", "-i"};
  command_args_["mv"] = {"-v", "-f", "-i"};
  command_args_["rm"] = {"-r", "-f", "-i", "-v"};
  command_args_["grep"] = {"-i", "-r", "-n", "-v", "-e"};
  command_args_["find"] = {"-name", "-type", "-exec", "-print", "-size"};
  command_args_["ps"] = {"-aux", "-ef", "-u"};
  command_args_["ssh"] = {"-i", "-p", "-v", "-l"};
  command_args_["docker"] = {"run",  "build", "stop", "start", "ps", "images",
                             "exec", "pull",  "push", "rm",    "rmi"};
  command_args_["npm"] = {"install", "start", "run",     "test", "build",
                          "update",  "init",  "publish", "link"};
  command_args_["python"] = {"-m", "-c", "-i", "-v", "--version"};
  command_args_["gcc"] = {"-Wall", "-O2", "-c", "-o", "-I", "-L", "-l"};
  command_args_["cmake"] = {"-B", "-S", "-D", "--build", "--target"};
  command_args_["make"] = {"-j", "all", "clean", "install", "test"};
  command_args_["plugin"] = {"list",    "load", "reload", "enable",
                             "disable", "info", "scan",   "autoreload"};

  // Add more command arguments here...
  spdlog::debug("Initialized completions for {} commands.",
                command_args_.size());
}

void AutoCompletePlugin::update_git_branches() {
  if (!env_) {
    spdlog::warn("Cannot update Git branches: Environment not set.");
    return;
  }
  if (!in_git_repo_) {
    spdlog::trace("Not in a Git repository, skipping branch update.");
    git_branches_.clear(); // Ensure it's clear if we somehow left the repo
    return;
  }

  spdlog::debug("Updating Git branches cache for directory '{}'...",
                env_->get_working_directory().string());
  git_branches_.clear();

  try {
    // Try to open .git/refs/heads directory to get local branches
    std::filesystem::path git_refs =
        env_->get_working_directory() / ".git" / "refs" / "heads";
    spdlog::trace("Scanning Git refs directory: {}", git_refs.string());
    if (std::filesystem::exists(git_refs) &&
        std::filesystem::is_directory(git_refs)) {
      std::function<void(const std::filesystem::path &, const std::string &)>
          scan_branches;
      scan_branches = [&](const std::filesystem::path &path,
                          const std::string &prefix) {
        spdlog::trace("Scanning Git refs path: '{}', prefix: '{}'",
                      path.string(), prefix);
        for (const auto &entry : std::filesystem::directory_iterator(path)) {
          if (entry.is_directory()) {
            scan_branches(entry.path(),
                          prefix + entry.path().filename().string() + "/");
          } else if (entry.is_regular_file()) {
            std::string branch_name = prefix + entry.path().filename().string();
            spdlog::trace("Found Git branch: {}", branch_name);
            git_branches_.push_back(branch_name);
          }
        }
      };

      scan_branches(git_refs, "");
      spdlog::debug("Found {} potential branches in refs/heads.",
                    git_branches_.size());
    } else {
      spdlog::warn("Git refs directory not found or not a directory: {}",
                   git_refs.string());
    }

    // Try to open .git/HEAD to get the current branch
    std::filesystem::path head_file =
        env_->get_working_directory() / ".git" / "HEAD";
    spdlog::trace("Reading Git HEAD file: {}", head_file.string());
    if (std::filesystem::exists(head_file)) {
      std::ifstream head(head_file);
      std::string head_content;
      if (std::getline(head, head_content)) {
        spdlog::trace("HEAD content: '{}'", head_content);
        // Format is usually "ref: refs/heads/main" or a commit hash (detached
        // HEAD)
        const std::string prefix = "ref: refs/heads/";
        if (head_content.starts_with(prefix)) {
          std::string current_branch = head_content.substr(prefix.length());
          spdlog::debug("Current Git branch detected: {}", current_branch);
          // Ensure the current branch is at the beginning of the list
          auto it = std::find(git_branches_.begin(), git_branches_.end(),
                              current_branch);
          if (it != git_branches_.end()) {
            spdlog::trace("Moving current branch '{}' to the front.",
                          current_branch);
            git_branches_.erase(it);
          } else {
            spdlog::warn(
                "Current branch '{}' from HEAD not found in refs/heads.",
                current_branch);
          }
          git_branches_.insert(git_branches_.begin(), current_branch);
        } else {
          spdlog::debug("HEAD does not point to a local branch (detached HEAD "
                        "or invalid format).");
        }
      } else {
        spdlog::warn("Failed to read content from HEAD file: {}",
                     head_file.string());
      }
    } else {
      spdlog::warn("Git HEAD file not found: {}", head_file.string());
    }
  } catch (const std::exception &e) {
    spdlog::error("Error updating git branches: {}", e.what());
    git_branches_.clear(); // Clear potentially partial results on error
  }
  spdlog::debug("Git branch cache update finished. {} branches cached.",
                git_branches_.size());
}

void AutoCompletePlugin::update_file_cache() {
  if (!env_) {
    spdlog::warn("Cannot update file cache: Environment not set.");
    return;
  }

  try {
    const auto &pwd = env_->get_working_directory();
    spdlog::debug("Updating file cache for directory '{}'...", pwd.string());

    // Clear old cache
    file_cache_.clear();
    spdlog::trace("File cache cleared.");

    // Recursion depth limit
    int max_depth = 2; // Limit scanning depth for performance
    std::function<void(const std::filesystem::path &, int)> scan_files;
    scan_files = [&](const std::filesystem::path &dir, int depth) {
      spdlog::trace("Scanning directory '{}' at depth {}.", dir.string(),
                    depth);
      std::vector<std::string> files;
      std::vector<std::string> dirs;

      try {
        // First, add files and subdirectories of the current directory
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
          std::string filename = entry.path().filename().string();
          if (entry.is_directory()) {
            spdlog::trace("Found directory: {}", filename);
            dirs.push_back(filename + "/"); // Append '/' to indicate directory
            // If depth allows, scan recursively
            if (depth < max_depth) {
              scan_files(entry.path(), depth + 1);
            } else {
              spdlog::trace("Max depth reached for directory '{}'.",
                            entry.path().string());
            }
          } else {
            spdlog::trace("Found file: {}", filename);
            files.push_back(filename);
          }
        }

        // For a directory, list subdirectories first, then files
        std::vector<std::string> entries;
        entries.reserve(dirs.size() + files.size());
        // Sort directories and files alphabetically before inserting
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        entries.insert(entries.end(), dirs.begin(), dirs.end());
        entries.insert(entries.end(), files.begin(), files.end());

        // Store in cache
        spdlog::trace("Caching {} entries for directory '{}'.", entries.size(),
                      dir.string());
        file_cache_[dir.string()] = std::move(entries);
      } catch (const std::filesystem::filesystem_error &fs_err) {
        // Log filesystem errors (e.g., permission denied) but continue
        spdlog::warn("Filesystem error scanning directory '{}': {}",
                     dir.string(), fs_err.what());
      } catch (const std::exception &e) {
        // Log other potential errors
        spdlog::error("Error scanning directory '{}': {}", dir.string(),
                      e.what());
      }
    };

    // Start scanning from the current working directory
    scan_files(pwd, 0);
    spdlog::debug(
        "File cache update finished. Cached entries for {} directories.",
        file_cache_.size());

  } catch (const std::exception &e) {
    spdlog::error("Error updating file cache: {}", e.what());
    file_cache_.clear(); // Clear cache on major error
  }
}

std::vector<std::string>
AutoCompletePlugin::complete_command(const std::string &command,
                                     const std::string &current_word) {
  spdlog::trace("Attempting completion for command '{}', current word '{}'.",
                command, current_word);
  std::vector<std::string> suggestions;

  // Find completion options for the given command
  auto it = command_args_.find(command);
  if (it != command_args_.end()) {
    spdlog::trace("Found argument completions for command '{}'.", command);
    for (const auto &arg : it->second) {
      if (arg.starts_with(current_word)) {
        spdlog::trace("Adding suggestion: '{}'", arg);
        suggestions.push_back(arg);
      }
    }
  } else {
    spdlog::trace("No specific argument completions found for command '{}'.",
                  command);
  }

  // Git branch completion (specific case for 'git checkout', 'git branch',
  // etc.) This basic example adds branches if the command is 'git' and word is
  // empty. A more robust solution would check the *previous* word (e.g.,
  // 'checkout').
  if (command == "git" && current_word.empty() && in_git_repo_) {
    spdlog::trace("Attempting Git branch completion for 'git' command.");
    if (git_branches_.empty()) {
      spdlog::debug(
          "Git branch cache is empty, attempting update before completion.");
      update_git_branches(); // Ensure cache is up-to-date if needed
    }

    spdlog::trace("Adding {} cached Git branches as suggestions.",
                  git_branches_.size());
    for (const auto &branch : git_branches_) {
      // No need to check starts_with here as current_word is empty
      suggestions.push_back(branch);
    }
  }

  // TODO: Add file/directory path completion based on file_cache_
  // TODO: Add environment variable completion (e.g., after '$')

  spdlog::debug("Generated {} suggestions for '{}' with current word '{}'.",
                suggestions.size(), command, current_word);
  return suggestions;
}

} // namespace shell