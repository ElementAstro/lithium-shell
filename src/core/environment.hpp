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

// Forward declaration
class Shell;

/**
 * @brief Function signature for shell commands
 *
 * @param args Command arguments, including the command itself
 * @param env Reference to the environment
 * @return Command output or empty string
 */
using CommandFunc = std::function<std::string(std::span<const std::string> args,
                                              class Environment &env)>;

/**
 * @brief Class for managing environment variables and commands
 */
class Environment {
public:
  /**
   * @brief Constructor that initializes the environment
   */
  Environment();

  /**
   * @brief Default destructor
   */
  ~Environment() = default;

  // Disable copying
  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;

  /**
   * @brief Set a shell variable
   * @param name Variable name
   * @param value Variable value
   */
  void set_variable(const std::string &name, const std::string &value);

  /**
   * @brief Get a shell variable
   * @param name Variable name
   * @return Variable value if it exists, std::nullopt otherwise
   */
  [[nodiscard]] std::optional<std::string>
  get_variable(const std::string &name) const;

  /**
   * @brief Get all shell variables
   * @return Reference to the map of all variables
   */
  [[nodiscard]] const std::unordered_map<std::string, std::string> &
  get_all_variables() const;

  /**
   * @brief Set a system environment variable
   * @param name Variable name
   * @param value Variable value
   */
  void set_env_variable(const std::string &name, const std::string &value);

  /**
   * @brief Get a system environment variable
   * @param name Variable name
   * @return Variable value if it exists, std::nullopt otherwise
   */
  [[nodiscard]] std::optional<std::string>
  get_env_variable(const std::string &name) const;

  /**
   * @brief Get all system environment variables
   * @return Map of all system environment variables
   */
  [[nodiscard]] std::unordered_map<std::string, std::string>
  get_all_env_variables() const;

  /**
   * @brief Register a built-in shell command
   * @param name Command name
   * @param func Command function
   * @param help_text Help text for the command
   */
  void register_command(const std::string &name, CommandFunc func,
                        const std::string &help_text);

  /**
   * @brief Check if a command exists
   * @param name Command name
   * @return true if the command exists, false otherwise
   */
  [[nodiscard]] bool has_command(const std::string &name) const;

  /**
   * @brief Get a command function
   * @param name Command name
   * @return Command function
   * @throw std::runtime_error if command doesn't exist
   */
  [[nodiscard]] CommandFunc get_command(const std::string &name) const;

  /**
   * @brief Get help text for a command
   * @param name Command name
   * @return Help text for the command
   */
  [[nodiscard]] std::string get_help_text(const std::string &name) const;

  /**
   * @brief Get names of all registered commands
   * @return Vector of command names
   */
  [[nodiscard]] std::vector<std::string> get_all_commands() const;

  /**
   * @brief Set the working directory
   * @param path New working directory path
   */
  void set_working_directory(const std::filesystem::path &path);

  /**
   * @brief Get the current working directory
   * @return Current working directory
   */
  [[nodiscard]] std::filesystem::path get_working_directory() const;

  /**
   * @brief Add a command to the history
   * @param command Command string
   */
  void add_to_history(const std::string &command);

  /**
   * @brief Get the command history
   * @return Reference to the command history vector
   */
  [[nodiscard]] const std::vector<std::string> &get_history() const;

  /**
   * @brief Clear the command history
   */
  void clear_history();

  /**
   * @brief Remove a command from history
   * @param index Index of the command to remove
   */
  void remove_from_history(size_t index);

  /**
   * @brief Add a command alias
   * @param alias Alias name
   * @param command Command that the alias refers to
   */
  void add_alias(const std::string &alias, const std::string &command);

  /**
   * @brief Resolve a command alias
   * @param name Alias name
   * @return Command that the alias refers to, or std::nullopt if not found
   */
  [[nodiscard]] std::optional<std::string>
  resolve_alias(const std::string &name) const;

  /**
   * @brief Remove a command alias
   * @param alias Alias name
   * @return true if the alias was removed, false if it didn't exist
   */
  [[nodiscard]] bool remove_alias(const std::string &alias);

  /**
   * @brief Get all command aliases
   * @return Reference to the map of aliases
   */
  [[nodiscard]] const std::unordered_map<std::string, std::string> &
  get_all_aliases() const;

  /**
   * @brief Set the last exit status
   * @param status Exit status code
   */
  void set_last_exit_status(int status);

  /**
   * @brief Get the last exit status
   * @return Last exit status code
   */
  [[nodiscard]] int get_last_exit_status() const;

  /**
   * @brief Expand variables in a string
   * @param str String containing variables to expand
   * @return String with variables expanded
   */
  [[nodiscard]] std::string expand_variables(const std::string &str) const;

  /**
   * @brief Set the shell instance
   * @param shell Pointer to the shell instance
   */
  void set_shell(void *shell);

  /**
   * @brief Get the shell instance
   * @return Pointer to the shell instance
   */
  [[nodiscard]] void *get_shell() const;

private:
  /** Map of shell variables */
  std::unordered_map<std::string, std::string> variables_;

  /** Map of command aliases */
  std::unordered_map<std::string, std::string> aliases_;

  /** Command history */
  std::vector<std::string> history_;

  /** Current working directory */
  std::filesystem::path working_directory_;

  /** Last command exit status */
  int last_exit_status_ = 0;

  /** Pointer to the shell instance */
  void *shell_instance_ = nullptr;

  /**
   * @brief Structure to store command information
   */
  struct Command {
    /** Command function */
    CommandFunc func;

    /** Command help text */
    std::string help_text;
  };

  /** Map of registered commands */
  std::unordered_map<std::string, Command> commands_;

  /** Mutex for thread safety */
  mutable std::mutex mutex_;
};

} // namespace shell