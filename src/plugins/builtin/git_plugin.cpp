#include "git_plugin.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>          // Required for std::unique_ptr
#include <spdlog/spdlog.h> // Include spdlog header
#include <sstream>

namespace shell {

GitPlugin::GitPlugin()
    : BuiltinPlugin(PluginMetadata(
          "git_integration", "1.0.0", "Git repository integration and status",
          "Lithium Shell Team", "https://github.com/lithium-shell/git-plugin",
          {})),
      has_changes_(false), has_untracked_(false), is_detached_(false),
      is_git_repo_(false),
      last_status_update_(std::chrono::system_clock::now()),
      status_refresh_interval_(5) // Default 5 seconds
{
  spdlog::debug("GitPlugin instance created.");
}

bool GitPlugin::initialize(Environment &env) {
  spdlog::info("Initializing GitPlugin...");
  env_ = &env;

  // Check if the current directory is a Git repository
  spdlog::debug("Checking initial Git repository status...");
  is_git_repo_ = check_if_in_git_repo();
  spdlog::debug("Initial Git repository status: {}", is_git_repo_);

  // If it's a Git repository, initialize the status
  if (is_git_repo_) {
    spdlog::debug("Currently in a Git repository. Updating initial status...");
    update_git_status();

    // Append Git status to the current prompt
    spdlog::debug("Attempting to modify prompt to include Git status...");
    try {
      std::string current_prompt = env_->get_variable("prompt").value_or(
          "\\033[1;32m➜ \\033[1;34m{pwd}\\033[0m$ ");
      std::string git_status_prompt = current_prompt;

      // Replace the '$' in the prompt with '{git_status} $'
      size_t dollar_pos = git_status_prompt.find_last_of('$');
      if (dollar_pos != std::string::npos) {
        git_status_prompt.insert(dollar_pos,
                                 " \\033[0;33m{git_status}\\033[0m");
        spdlog::trace("Setting prompt variable to: {}", git_status_prompt);
        env_->set_variable("prompt", git_status_prompt);
        spdlog::debug("Prompt updated successfully.");
      } else {
        spdlog::warn("Could not find '$' in the prompt string '{}' to insert "
                     "Git status.",
                     current_prompt);
      }
    } catch (const std::exception &e) {
      spdlog::error("Error modifying prompt during initialization: {}",
                    e.what());
    }
  } else {
    spdlog::debug("Not currently in a Git repository. Prompt not modified.");
  }

  // Register Git enhanced commands and aliases
  spdlog::debug("Registering Git commands and aliases...");
  register_git_commands();
  spdlog::debug("Git commands and aliases registered.");

  // std::cout << "Git plugin initialized" << std::endl; // Replaced by spdlog
  spdlog::info("Git plugin initialized successfully.");
  return true;
}

void GitPlugin::shutdown() {
  spdlog::info("Shutting down GitPlugin...");
  env_ = nullptr; // Clear the environment pointer
  spdlog::info("GitPlugin shut down complete.");
}

bool GitPlugin::on_load() {
  spdlog::info("Git plugin loaded.");
  // std::cout << "Git plugin loaded" << std::endl; // Replaced by spdlog
  return true;
}

bool GitPlugin::on_unload() {
  spdlog::info("Git plugin unloaded.");
  // std::cout << "Git plugin unloaded" << std::endl; // Replaced by spdlog
  return true;
}

bool GitPlugin::handle_event(PluginEvent event, const PluginEventData &data) {
  spdlog::trace("Handling plugin event: {}", static_cast<int>(event));
  switch (event) {
  case PluginEvent::CommandBefore: {
    spdlog::trace("CommandBefore event received.");
    // Check if Git status needs updating
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       now - last_status_update_)
                       .count();
    spdlog::trace("{} seconds since last Git status update.", elapsed);

    if (elapsed >= status_refresh_interval_) {
      spdlog::debug(
          "Git status refresh interval ({}) exceeded. Checking status...",
          status_refresh_interval_);
      // Re-check if we are in a Git repository
      bool was_in_git = is_git_repo_;
      is_git_repo_ = check_if_in_git_repo();
      spdlog::trace("Current Git repo status: {}, Previous: {}", is_git_repo_,
                    was_in_git);

      // Update status if the state changed or if still in a Git repo
      if (is_git_repo_ != was_in_git || is_git_repo_) {
        spdlog::debug(
            "Git status needs update (state change or still in repo).");
        update_git_status();
      } else {
        spdlog::trace(
            "Not in Git repo and wasn't before. No status update needed.");
      }

      last_status_update_ = now;
      spdlog::debug("Git status update check complete. Timestamp updated.");
    } else {
      spdlog::trace("Git status refresh interval not exceeded.");
    }
    break;
  }
  case PluginEvent::EnvironmentChanged: {
    spdlog::trace("EnvironmentChanged event received.");
    if (std::holds_alternative<PluginEventData::EnvironmentData>(data.data)) {
      const auto &env_data =
          std::get<PluginEventData::EnvironmentData>(data.data);
      spdlog::trace("Environment variable '{}' changed.", env_data.name);

      // When the working directory changes, check if we are in a Git repository
      if (env_data.name == "PWD") {
        spdlog::debug("Working directory changed to '{}'. Checking Git status.",
                      env_data.new_value.value_or(""));
        bool was_in_git = is_git_repo_;
        is_git_repo_ = check_if_in_git_repo();
        spdlog::debug("New Git repo status: {}, Previous: {}", is_git_repo_,
                      was_in_git);

        if (is_git_repo_ != was_in_git) {
          spdlog::info("Git repository status changed ({} -> {}).", was_in_git,
                       is_git_repo_);
          if (is_git_repo_) {
            // Entered a Git repository
            spdlog::debug(
                "Entered Git repository. Updating status and prompt...");
            update_git_status();

            // Update the prompt to include Git status
            try {
              std::string current_prompt =
                  env_->get_variable("prompt").value_or(
                      "\\033[1;32m➜ \\033[1;34m{pwd}\\033[0m$ ");
              std::string git_status_prompt = current_prompt;

              // Avoid adding if already present (e.g., if default prompt
              // includes it)
              if (git_status_prompt.find("{git_status}") == std::string::npos) {
                size_t dollar_pos = git_status_prompt.find_last_of('$');
                if (dollar_pos != std::string::npos) {
                  git_status_prompt.insert(dollar_pos,
                                           " \\033[0;33m{git_status}\\033[0m");
                  spdlog::trace("Setting prompt variable to: {}",
                                git_status_prompt);
                  env_->set_variable("prompt", git_status_prompt);
                  spdlog::debug("Prompt updated to include Git status.");
                } else {
                  spdlog::warn(
                      "Could not find '$' in prompt '{}' to insert Git "
                      "status.",
                      current_prompt);
                }
              } else {
                spdlog::debug(
                    "Prompt already contains {git_status}. No update needed.");
              }
            } catch (const std::exception &e) {
              spdlog::error("Error updating prompt after entering Git repo: {}",
                            e.what());
            }
          } else {
            // Left a Git repository
            spdlog::debug(
                "Left Git repository. Removing Git status from prompt...");
            // Clear Git related environment variables
            env_->set_variable("git_status", "");
            env_->set_variable("git_branch", "");
            env_->set_variable("git_has_changes", "false");
            env_->set_variable("git_has_untracked", "false");
            env_->set_variable("git_is_detached", "false");

            // Remove Git status from the prompt
            try {
              std::string current_prompt =
                  env_->get_variable("prompt").value_or(
                      "\\033[1;32m➜ \\033[1;34m{pwd}\\033[0m$ ");
              std::string cleaned_prompt = current_prompt;
              size_t git_status_pos = cleaned_prompt.find("{git_status}");

              if (git_status_pos != std::string::npos) {
                // Find the start of the color code before {git_status}
                size_t color_start =
                    cleaned_prompt.rfind("\\033", git_status_pos);
                // Find the end of the color code after {git_status}
                size_t color_end = cleaned_prompt.find("\\033", git_status_pos);

                if (color_start != std::string::npos &&
                    color_end != std::string::npos &&
                    color_start < git_status_pos &&
                    color_end > git_status_pos) {
                  // Find the space before the color code
                  size_t space_before = cleaned_prompt.rfind(' ', color_start);
                  if (space_before != std::string::npos &&
                      space_before < color_start) {
                    // Remove the space and the entire colored {git_status} part
                    spdlog::trace(
                        "Removing Git status section from prompt: '{}' to '{}'",
                        space_before, color_end);
                    cleaned_prompt.erase(space_before,
                                         color_end - space_before);
                  } else {
                    // Fallback: Remove just the colored part if space not found
                    // correctly
                    spdlog::trace("Removing Git status colored section from "
                                  "prompt: '{}' to '{}'",
                                  color_start, color_end);
                    cleaned_prompt.erase(color_start, color_end - color_start);
                  }

                } else {
                  // Simpler fallback: remove just the placeholder if colors
                  // aren't found as expected
                  spdlog::trace(
                      "Removing Git status placeholder from prompt: '{}'",
                      git_status_pos);
                  cleaned_prompt.erase(git_status_pos,
                                       12); // Length of "{git_status}"
                }

                spdlog::trace("Setting prompt variable to: {}", cleaned_prompt);
                env_->set_variable("prompt", cleaned_prompt);
                spdlog::debug("Prompt updated to remove Git status.");
              } else {
                spdlog::debug(
                    "Prompt does not contain {git_status}. No removal needed.");
              }
            } catch (const std::exception &e) {
              spdlog::error("Error updating prompt after leaving Git repo: {}",
                            e.what());
            }
          }
        } else if (is_git_repo_) {
          // Still in a Git repository, just update the status
          spdlog::debug("Still in Git repository after directory change. "
                        "Updating status.");
          update_git_status();
        }
      }
    } else {
      spdlog::trace(
          "EnvironmentChanged event data type is not EnvironmentData.");
    }
    break;
  }
  default:
    spdlog::trace("Ignoring unhandled plugin event: {}",
                  static_cast<int>(event));
    break;
  }

  return true;
}

std::string GitPlugin::get_current_branch() const {
  spdlog::trace("Getting current branch: {}", current_branch_);
  return current_branch_;
}

bool GitPlugin::is_git_repository() const {
  spdlog::trace("Getting Git repository status: {}", is_git_repo_);
  return is_git_repo_;
}

std::string GitPlugin::get_repository_status() const {
  spdlog::trace("Generating repository status string...");
  if (!is_git_repo_) {
    spdlog::trace("Not in a Git repository, returning empty status.");
    return "";
  }

  std::string status;

  if (is_detached_) {
    status = "detached:" + current_branch_;
    spdlog::trace("Status: detached head at {}", current_branch_);
  } else {
    status = current_branch_;
    spdlog::trace("Status: on branch {}", current_branch_);
  }

  if (has_changes_) {
    status += "*"; // Mark changes
    spdlog::trace("Status: has uncommitted changes (*)");
  }

  if (has_untracked_) {
    status += "+"; // Mark untracked files
    spdlog::trace("Status: has untracked files (+)");
  }

  spdlog::debug("Generated repository status string: '{}'", status);
  return status;
}

void GitPlugin::update_git_status() {
  spdlog::debug("Updating Git status...");
  if (!is_git_repo_) {
    spdlog::debug("Not in a Git repository, skipping status update.");
    // Ensure variables are cleared if we somehow thought we were in a repo
    // before
    current_branch_ = "";
    has_changes_ = false;
    has_untracked_ = false;
    is_detached_ = false;
    env_->set_variable("git_status", "");
    env_->set_variable("git_branch", "");
    env_->set_variable("git_has_changes", "false");
    env_->set_variable("git_has_untracked", "false");
    env_->set_variable("git_is_detached", "false");
    return;
  }
  if (!env_) {
    spdlog::error("Cannot update Git status: Environment pointer is null.");
    return;
  }

  try {
    // Get current branch name
    spdlog::trace("Running 'git rev-parse --abbrev-ref HEAD'...");
    std::string branch_output =
        run_git_command("git rev-parse --abbrev-ref HEAD");
    // Trim newline characters
    current_branch_ =
        branch_output.substr(0, branch_output.find_last_not_of("\r\n") + 1);
    spdlog::trace("Raw branch output: '{}', Trimmed branch: '{}'",
                  branch_output, current_branch_);

    // Check if in detached HEAD state
    is_detached_ = (current_branch_ == "HEAD");
    spdlog::trace("Detached HEAD state: {}", is_detached_);
    if (is_detached_) {
      // Get the current commit hash (short)
      spdlog::trace(
          "In detached HEAD state. Running 'git rev-parse --short HEAD'...");
      std::string hash_output = run_git_command("git rev-parse --short HEAD");
      current_branch_ =
          hash_output.substr(0, hash_output.find_last_not_of("\r\n") + 1);
      spdlog::trace("Raw hash output: '{}', Trimmed hash: '{}'", hash_output,
                    current_branch_);
    }

    // Check for uncommitted changes and untracked files
    spdlog::trace("Running 'git status --porcelain'...");
    std::string status_output = run_git_command("git status --porcelain");
    spdlog::trace("Raw status output:\n{}", status_output);
    has_changes_ = false;
    has_untracked_ = false;

    std::istringstream status_stream(status_output);
    std::string line;
    while (std::getline(status_stream, line)) {
      if (line.length() >= 2) {
        char index_status = line[0];
        char work_tree_status = line[1];
        spdlog::trace(
            "Processing status line: '{}' (Index: '{}', WorkTree: '{}')", line,
            index_status, work_tree_status);
        if (index_status == '?' && work_tree_status == '?') {
          has_untracked_ = true;
          spdlog::trace("Detected untracked file.");
        } else {
          // Any other non-space status indicates changes (staged or unstaged)
          if (index_status != ' ' || work_tree_status != ' ') {
            has_changes_ = true;
            spdlog::trace("Detected staged or unstaged change.");
          }
        }
      }
      // Optimization: if both flags are true, no need to check further lines
      if (has_changes_ && has_untracked_)
        break;
    }
    spdlog::trace("Final status flags - has_changes: {}, has_untracked: {}",
                  has_changes_, has_untracked_);

    // Update environment variables
    spdlog::trace(
        "Setting environment variables: git_branch='{}', git_has_changes={}, "
        "git_has_untracked={}, git_is_detached={}",
        current_branch_, has_changes_, has_untracked_, is_detached_);
    env_->set_variable("git_branch", current_branch_);
    env_->set_variable("git_has_changes", has_changes_ ? "true" : "false");
    env_->set_variable("git_has_untracked", has_untracked_ ? "true" : "false");
    env_->set_variable("git_is_detached", is_detached_ ? "true" : "false");

    // Update the combined git status variable used in the prompt
    std::string status_string = get_repository_status();
    spdlog::trace("Setting environment variable: git_status='{}'",
                  status_string);
    env_->set_variable("git_status", status_string);

    spdlog::debug("Git status update successful.");

  } catch (const std::exception &e) {
    spdlog::error("Error updating git status: {}", e.what());
    // Reset state on error
    current_branch_ = "";
    has_changes_ = false;
    has_untracked_ = false;
    is_detached_ = false;
    env_->set_variable("git_status", "");
    env_->set_variable("git_branch", "");
    env_->set_variable("git_has_changes", "false");
    env_->set_variable("git_has_untracked", "false");
    env_->set_variable("git_is_detached", "false");
  }
}

bool GitPlugin::check_if_in_git_repo() {
  spdlog::trace("Checking if current path is inside a Git repository...");
  if (!env_) {
    spdlog::warn("Cannot check Git repo status: Environment pointer is null.");
    return false;
  }

  try {
    auto current_dir = env_->get_working_directory();
    spdlog::trace("Current working directory: {}", current_dir.string());

    // Method 1: Check for a .git directory/file in the current path or parents
    // (More robust check than just current_dir / ".git")
    std::filesystem::path check_path = current_dir;
    std::filesystem::path root_path = check_path.root_path();
    bool found_git = false;
    while (true) {
      std::filesystem::path git_path = check_path / ".git";
      std::error_code ec;
      if (std::filesystem::exists(git_path, ec) && !ec) {
        // Found .git, could be a directory (repo root) or file
        // (submodule/worktree)
        spdlog::trace("Found '.git' at path: {}", git_path.string());
        found_git = true;
        break;
      }
      // Stop if we reached the root or the parent is the same (e.g., at root)
      if (check_path == root_path || check_path.parent_path() == check_path) {
        break;
      }
      check_path = check_path.parent_path();
    }

    if (found_git) {
      spdlog::trace("Found .git via filesystem traversal.");
      // Optionally, confirm with 'git rev-parse --is-inside-work-tree' for
      // robustness
      try {
        std::string result =
            run_git_command("git rev-parse --is-inside-work-tree");
        spdlog::trace("Result of 'git rev-parse --is-inside-work-tree': {}",
                      result);
        if (result.find("true") != std::string::npos) {
          // Get the repository root directory
          std::string root_dir_output =
              run_git_command("git rev-parse --show-toplevel");
          if (!root_dir_output.empty()) {
            repo_path_ = root_dir_output.substr(
                0, root_dir_output.find_last_not_of("\r\n") + 1);
            spdlog::debug("Confirmed inside Git work tree. Repo root: {}",
                          repo_path_.string());
          } else {
            spdlog::warn("Could not determine Git repository root using "
                         "--show-toplevel.");
            // Fallback: use the path where .git was found if it was a directory
            if (std::filesystem::is_directory(check_path / ".git")) {
              repo_path_ = check_path;
              spdlog::debug(
                  "Using path where .git directory was found as repo path: {}",
                  repo_path_.string());
            } else {
              repo_path_.clear(); // Unsure about the root
            }
          }
          return true;
        } else {
          spdlog::warn("Found .git via filesystem, but 'git rev-parse "
                       "--is-inside-work-tree' returned false.");
          repo_path_.clear();
          return false; // Git command disagrees
        }
      } catch (const std::exception &git_cmd_ex) {
        spdlog::warn("Error running git command during repo check: {}. "
                     "Assuming not in repo.",
                     git_cmd_ex.what());
        repo_path_.clear();
        return false;
      }
    } else {
      spdlog::trace("Did not find .git via filesystem traversal.");
      repo_path_.clear();
      return false;
    }

  } catch (const std::exception &e) {
    spdlog::error("Exception during Git repository check: {}", e.what());
    repo_path_.clear();
    return false; // Assume not in a Git repository on error
  }
}

void GitPlugin::register_git_commands() {
  spdlog::trace("Registering Git aliases...");
  if (!env_) {
    spdlog::warn("Cannot register Git aliases: Environment pointer is null.");
    return;
  }

  try {
    // Add Git command aliases
    env_->add_alias("gs", "git status");
    env_->add_alias("gc", "git commit");
    env_->add_alias("gco", "git checkout");
    env_->add_alias("gl", "git log --oneline --graph --decorate");
    env_->add_alias("gp", "git pull");
    env_->add_alias("gpu", "git push");
    env_->add_alias("ga", "git add");
    spdlog::debug(
        "Registered standard Git aliases (gs, gc, gco, gl, gp, gpu, ga).");
    // Add more aliases as needed
  } catch (const std::exception &e) {
    spdlog::error("Failed to register Git aliases: {}", e.what());
  }
}

std::string GitPlugin::run_git_command(const std::string &command) const {
  spdlog::trace("Executing Git command: '{}'", command);
  if (!env_) {
    spdlog::error("Cannot run Git command: Environment pointer is null.");
    return "";
  }

  // Ensure the command runs within the repository context if possible
  // This might involve setting the working directory for the command execution,
  // depending on how the underlying shell execution works.
  // For popen, it typically inherits the current working directory.

  std::string result;
  std::array<char, 256> buffer;

  // Redirect stderr to stdout to capture errors
  std::string redirected_command = command + " 2>&1";
  spdlog::trace("Executing redirected command: '{}'", redirected_command);

#ifdef _WIN32
  // Windows implementation using _popen
  std::unique_ptr<FILE, decltype(&_pclose)> pipe(
      _popen(redirected_command.c_str(), "r"), _pclose);
#else
  // POSIX implementation using popen
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen(redirected_command.c_str(), "r"), pclose);
#endif

  if (!pipe) {
    spdlog::error("Failed to execute _popen/popen for command: {}", command);
    throw std::runtime_error("Failed to execute git command: " + command);
  }

  spdlog::trace("Reading output from command pipe...");
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  spdlog::trace("Finished reading pipe. Raw result:\n{}", result);

  // Check the exit status (specific to popen/pclose, _popen/_pclose)
  // Note: Getting the exact exit status from popen is tricky and
  // platform-dependent. pclose returns the status, but _pclose returns -1 on
  // error, 0 otherwise. We might infer failure if the output contains common
  // Git error messages.
  if (result.find("fatal:") != std::string::npos ||
      result.find("error:") != std::string::npos) {
    spdlog::warn("Git command '{}' might have failed. Output: {}", command,
                 result);
    // Depending on the command, we might want to throw or handle this
    // differently.
  }

  return result;
}

} // namespace shell