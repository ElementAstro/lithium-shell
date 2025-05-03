#include "environment.hpp"

#include <cstdlib>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>


// setenv implementation for Windows environment
#ifdef _WIN32
// If this function is already declared in executor.cpp, it can be declared as
// extern to avoid duplicate definition
extern int setenv(const char *name, const char *value, int overwrite);
#endif

namespace shell {

Environment::Environment() {
  // 初始化工作目录为当前目录
  working_directory_ = std::filesystem::current_path();

  // 初始化基本环境变量
  set_variable("SHELL", "lithium-shell");
  set_variable("errexit", "false");
  set_variable("pipefail", "false");

  // 记录上次命令的退出状态
  set_variable("?", "0");

  spdlog::debug("Environment initialized");
}

void Environment::set_variable(const std::string &name,
                               const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  variables_[name] = value;

  // 特殊变量处理：退出状态
  if (name == "?") {
    try {
      last_exit_status_ = std::stoi(value);
    } catch (const std::exception &) {
      last_exit_status_ = 0;
    }
  }
}

std::optional<std::string>
Environment::get_variable(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = variables_.find(name);
  if (it != variables_.end()) {
    return it->second;
  }
  return std::nullopt;
}

const std::unordered_map<std::string, std::string> &
Environment::get_all_variables() const {
  return variables_;
}

void Environment::set_env_variable(const std::string &name,
                                   const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 设置系统环境变量
  setenv(name.c_str(), value.c_str(), 1);

  // 同时更新内部变量表
  variables_[name] = value;
}

std::optional<std::string>
Environment::get_env_variable(const std::string &name) const {
  // 首先尝试从系统获取
  const char *value = std::getenv(name.c_str());
  if (value) {
    return std::string(value);
  }

  // 然后尝试从内部变量表获取
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = variables_.find(name);
  if (it != variables_.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::unordered_map<std::string, std::string>
Environment::get_all_env_variables() const {
  std::unordered_map<std::string, std::string> result;

  // 获取系统环境变量
#ifdef _WIN32
  // Windows实现
  LPWCH envStrings = GetEnvironmentStringsW();
  if (envStrings) {
    LPWCH current = envStrings;
    while (*current) {
      std::wstring wstr(current);
      size_t pos = wstr.find(L'=');
      if (pos != std::wstring::npos) {
        std::wstring wname = wstr.substr(0, pos);
        std::wstring wvalue = wstr.substr(pos + 1);

        // 转换为UTF-8
        int nameLen = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1,
                                          nullptr, 0, nullptr, nullptr);
        int valueLen = WideCharToMultiByte(CP_UTF8, 0, wvalue.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);

        std::string name(nameLen, 0);
        std::string value(valueLen, 0);

        WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, &name[0], nameLen,
                            nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, wvalue.c_str(), -1, &value[0], valueLen,
                            nullptr, nullptr);

        // 移除末尾的null终止符
        name.pop_back();
        value.pop_back();

        result[name] = value;
      }

      // 移动到下一个环境字符串
      current += wstr.length() + 1;
    }
    FreeEnvironmentStringsW(envStrings);
  }
#else
  // POSIX实现
  extern char **environ;
  for (char **env = environ; *env; ++env) {
    std::string envStr(*env);
    size_t pos = envStr.find('=');
    if (pos != std::string::npos) {
      std::string name = envStr.substr(0, pos);
      std::string value = envStr.substr(pos + 1);
      result[name] = value;
    }
  }
#endif

  return result;
}

void Environment::register_command(const std::string &name, CommandFunc func,
                                   const std::string &help_text) {
  std::lock_guard<std::mutex> lock(mutex_);
  commands_[name] = {std::move(func), help_text};
  spdlog::debug("Registered command: {}", name);
}

bool Environment::has_command(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return commands_.find(name) != commands_.end();
}

CommandFunc Environment::get_command(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = commands_.find(name);
  if (it != commands_.end()) {
    return it->second.func;
  }

  throw std::runtime_error("Command not found: " + name);
}

std::string Environment::get_help_text(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = commands_.find(name);
  if (it != commands_.end()) {
    return it->second.help_text;
  }
  return "";
}

std::vector<std::string> Environment::get_all_commands() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(commands_.size());

  for (const auto &pair : commands_) {
    result.push_back(pair.first);
  }

  return result;
}

void Environment::set_working_directory(const std::filesystem::path &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  working_directory_ = path;

  // 同时更新环境变量PWD
  set_env_variable("PWD", working_directory_.string());
}

std::filesystem::path Environment::get_working_directory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return working_directory_;
}

void Environment::add_to_history(const std::string &command) {
  std::lock_guard<std::mutex> lock(mutex_);
  history_.push_back(command);
}

const std::vector<std::string> &Environment::get_history() const {
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

std::optional<std::string>
Environment::resolve_alias(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = aliases_.find(name);
  if (it != aliases_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool Environment::remove_alias(const std::string &alias) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = aliases_.find(alias);
  if (it != aliases_.end()) {
    aliases_.erase(it);
    return true;
  }
  return false;
}

const std::unordered_map<std::string, std::string> &
Environment::get_all_aliases() const {
  return aliases_;
}

void Environment::set_last_exit_status(int status) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_exit_status_ = status;

  // 同时更新shell变量?
  set_variable("?", std::to_string(status));
}

int Environment::get_last_exit_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_exit_status_;
}

std::string Environment::expand_variables(const std::string &str) const {
  std::string result = str;

  // 处理$VAR形式的变量
  std::regex var_regex("\\$(\\w+|\\?|\\$)");
  auto var_begin = std::sregex_iterator(str.begin(), str.end(), var_regex);
  auto var_end = std::sregex_iterator();

  // 需要从后往前替换，避免位置偏移
  std::vector<std::pair<size_t, size_t>> positions;
  std::vector<std::string> replacements;

  for (auto i = var_begin; i != var_end; ++i) {
    std::smatch match = *i;
    std::string var_name = match[1].str();

    // 特殊变量
    if (var_name == "$") {
      // $$ 展开为进程ID
      replacements.push_back(std::to_string(getpid()));
    } else if (var_name == "?") {
      // $? 展开为上一条命令的退出状态
      replacements.push_back(std::to_string(get_last_exit_status()));
    } else {
      // 常规变量
      auto value = get_variable(var_name);
      replacements.push_back(value.value_or(""));
    }

    positions.emplace_back(match.position(), match.length());
  }

  // 从后往前替换，避免位置偏移
  for (size_t i = positions.size(); i > 0; --i) {
    auto [pos, len] = positions[i - 1];
    result.replace(pos, len, replacements[i - 1]);
  }

  // 处理${VAR}形式的变量
  std::regex brace_var_regex("\\$\\{(\\w+|\\?|\\$)\\}");
  auto brace_var_begin =
      std::sregex_iterator(result.begin(), result.end(), brace_var_regex);
  auto brace_var_end = std::sregex_iterator();

  positions.clear();
  replacements.clear();

  for (auto i = brace_var_begin; i != brace_var_end; ++i) {
    std::smatch match = *i;
    std::string var_name = match[1].str();

    // 特殊变量
    if (var_name == "$") {
      replacements.push_back(std::to_string(getpid()));
    } else if (var_name == "?") {
      replacements.push_back(std::to_string(get_last_exit_status()));
    } else {
      auto value = get_variable(var_name);
      replacements.push_back(value.value_or(""));
    }

    positions.emplace_back(match.position(), match.length());
  }

  // 从后往前替换
  for (size_t i = positions.size(); i > 0; --i) {
    auto [pos, len] = positions[i - 1];
    result.replace(pos, len, replacements[i - 1]);
  }

  // 处理~展开为用户主目录
  if (result == "~" || result.starts_with("~/") || result.starts_with("~\\")) {
    auto home = get_env_variable("HOME");
#ifdef _WIN32
    if (!home) {
      home = get_env_variable("USERPROFILE");
    }
#endif
    if (home) {
      if (result == "~") {
        result = *home;
      } else {
        result.replace(0, 1, *home);
      }
    }
  }

  return result;
}

void Environment::set_shell(void *shell) { shell_instance_ = shell; }

void *Environment::get_shell() const { return shell_instance_; }

} // namespace shell