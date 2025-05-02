#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace shell {

/**
 * @typedef CommandFunc
 * @brief Function type for shell commands
 */
using CommandFunc = std::function<std::string(std::span<const std::string>,
                                              class Environment &)>;

/**
 * @class Environment
 * @brief Manages shell environment variables, functions, and state
 */
class Environment {
public:
  Environment();

  // Get/set shell variables
  void set_variable(const std::string &name, const std::string &value);
  std::optional<std::string> get_variable(const std::string &name) const;
  const std::unordered_map<std::string, std::string> &get_all_variables() const;

  // Get/set environment variables
  void set_env_variable(const std::string &name, const std::string &value);
  std::optional<std::string> get_env_variable(const std::string &name) const;
  std::unordered_map<std::string, std::string> get_all_env_variables() const;

  // Command registration
  void register_command(const std::string &name, CommandFunc func,
                        const std::string &help_text = "");
  bool has_command(const std::string &name) const;
  CommandFunc get_command(const std::string &name) const;
  std::string get_help_text(const std::string &name) const;
  std::vector<std::string> get_all_commands() const;

  // Working directory
  void set_working_directory(const std::filesystem::path &path);
  std::filesystem::path get_working_directory() const;

  // Command history
  void add_to_history(const std::string &command);
  const std::vector<std::string> &get_history() const;
  void clear_history();
  void remove_from_history(size_t index); // 新增：删除指定索引的历史记录

  // Command aliases
  void add_alias(const std::string &alias, const std::string &command);
  std::optional<std::string> resolve_alias(const std::string &alias) const;
  bool remove_alias(const std::string &alias);
  std::unordered_map<std::string, std::string> get_all_aliases() const;

  // Variable expansion
  std::string expand_variables(const std::string &input) const;

  // Shell exit status
  int get_last_exit_status() const;
  void set_last_exit_status(int status);

private:
  std::unordered_map<std::string, std::string> shell_variables_;
  std::unordered_map<std::string, std::string> env_variables_;
  std::unordered_map<std::string, CommandFunc> commands_;
  std::unordered_map<std::string, std::string> command_help_;
  std::unordered_map<std::string, std::string> aliases_;
  std::filesystem::path working_directory_;
  std::vector<std::string> history_;
  int last_exit_status_ = 0;

  // Thread safety
  mutable std::mutex mutex_;
};

} // namespace shell