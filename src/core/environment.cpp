#include "environment.hpp"

#include <cstdlib>
#include <regex>
#include <string>

// setenv implementation for Windows environment
#ifdef _WIN32
// If this function is already declared in executor.cpp, it can be declared as
// extern to avoid duplicate definition
inline int setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name) != nullptr)
    return 0;
  return _putenv_s(name, value);
}
#endif

namespace shell {

Environment::Environment() {
  // Initialize working directory to current directory
  working_directory_ = std::filesystem::current_path();

  // Initialize shell variables
  shell_variables_["?"] = "0"; // Last exit status
  shell_variables_["SHELL"] = "modern_shell";
  shell_variables_["errexit"] = "false";
}

void Environment::set_variable(const std::string &name,
                               const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  shell_variables_[name] = value;
}

std::optional<std::string>
Environment::get_variable(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = shell_variables_.find(name);
  if (it != shell_variables_.end()) {
    return it->second;
  }

  return std::nullopt;
}

const std::unordered_map<std::string, std::string> &
Environment::get_all_variables() const {
  return shell_variables_;
}

void Environment::set_env_variable(const std::string &name,
                                   const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  env_variables_[name] = value;

// Also set in the process environment
#ifdef _WIN32
  // Windows implementation
  setenv(name.c_str(), value.c_str(), 1);
#else
  // POSIX implementation
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

std::optional<std::string>
Environment::get_env_variable(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = env_variables_.find(name);
  if (it != env_variables_.end()) {
    return it->second;
  }

  // Try to get from process environment
  const char *value = getenv(name.c_str());
  if (value) {
    return std::string(value);
  }

  return std::nullopt;
}

std::unordered_map<std::string, std::string>
Environment::get_all_env_variables() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return env_variables_;
}

void Environment::register_command(const std::string &name, CommandFunc func,
                                   const std::string &help_text) {
  std::lock_guard<std::mutex> lock(mutex_);

  commands_[name] = std::move(func);
  command_help_[name] = help_text;
}

bool Environment::has_command(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return commands_.find(name) != commands_.end();
}

CommandFunc Environment::get_command(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = commands_.find(name);
  if (it != commands_.end()) {
    return it->second;
  }

  return nullptr;
}

std::string Environment::get_help_text(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = command_help_.find(name);
  if (it != command_help_.end()) {
    return it->second;
  }

  return "";
}

std::vector<std::string> Environment::get_all_commands() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> result;
  result.reserve(commands_.size());

  for (const auto &[name, _] : commands_) {
    result.push_back(name);
  }

  return result;
}

void Environment::set_working_directory(const std::filesystem::path &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  working_directory_ = path;

  // Also update PWD environment variable
  set_env_variable("PWD", path.string());
}

std::filesystem::path Environment::get_working_directory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return working_directory_;
}

void Environment::add_to_history(const std::string &command) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Don't add empty commands
  if (command.empty()) {
    return;
  }

  // Don't add duplicate of the last command
  if (!history_.empty() && history_.back() == command) {
    return;
  }

  history_.push_back(command);

  // Limit history size
  const size_t max_history = 1000;
  if (history_.size() > max_history) {
    history_.erase(history_.begin());
  }
}

const std::vector<std::string> &Environment::get_history() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return history_;
}

void Environment::clear_history() {
  std::lock_guard<std::mutex> lock(mutex_);
  history_.clear();
}

void Environment::remove_from_history(size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < history_.size()) {
    history_.erase(history_.begin() + index);
  }
}

void Environment::add_alias(const std::string &alias,
                            const std::string &command) {
  std::lock_guard<std::mutex> lock(mutex_);
  aliases_[alias] = command;
}

bool Environment::remove_alias(const std::string &alias) {
  std::lock_guard<std::mutex> lock(mutex_);
  return aliases_.erase(alias) > 0;
}

std::optional<std::string>
Environment::resolve_alias(const std::string &alias) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = aliases_.find(alias);
  if (it != aliases_.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::unordered_map<std::string, std::string>
Environment::get_all_aliases() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return aliases_;
}

std::string Environment::expand_variables(const std::string &input) const {
  std::string result = input;

  // Regex for matching ${VAR} format
  std::regex var_braces_regex("\\$\\{([^}]+)\\}");

  // Regex for matching $VAR format (must be at word boundary)
  std::regex var_regex("\\$([a-zA-Z_][a-zA-Z0-9_]*)");

  // Helper function to replace variables
  auto replace_var = [this](const std::string &var_name) -> std::string {
    // First check shell variables
    auto shell_var = this->get_variable(var_name);
    if (shell_var) {
      return *shell_var;
    }

    // Then check environment variables
    auto env_var = this->get_env_variable(var_name);
    if (env_var) {
      return *env_var;
    }

    // If not found, expand to empty string
    return std::string();
  };

  // Replace ${VAR} with variable value
  {
    std::string temp;
    std::sregex_iterator it(result.begin(), result.end(), var_braces_regex);
    std::sregex_iterator end;

    size_t last_pos = 0;
    for (; it != end; ++it) {
      std::smatch match = *it;
      // Add text before the match
      temp.append(result, last_pos, match.position() - last_pos);
      // Add replacement
      temp.append(replace_var(match[1].str()));
      last_pos = match.position() + match.length();
    }
    // Add the remaining text
    temp.append(result, last_pos, result.size() - last_pos);
    result = std::move(temp);
  }

  // Replace $VAR with variable value
  {
    std::string temp;
    std::sregex_iterator it(result.begin(), result.end(), var_regex);
    std::sregex_iterator end;

    size_t last_pos = 0;
    for (; it != end; ++it) {
      std::smatch match = *it;
      // Add text before the match
      temp.append(result, last_pos, match.position() - last_pos);
      // Add replacement
      temp.append(replace_var(match[1].str()));
      last_pos = match.position() + match.length();
    }
    // Add the remaining text
    temp.append(result, last_pos, result.size() - last_pos);
    result = std::move(temp);
  }

  return result;
}

int Environment::get_last_exit_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_exit_status_;
}

void Environment::set_last_exit_status(int status) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_exit_status_ = status;

  // Also update the ? variable
  shell_variables_["?"] = std::to_string(status);
}

} // namespace shell