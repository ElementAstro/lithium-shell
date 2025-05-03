#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../config/config_manager.hpp"
#include "../interpreter/script_interpreter.hpp"
#include "../plugins/plugin_manager.hpp"
#include "environment.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "tokenizer.hpp"

namespace shell {

/**
 * @class Shell
 * @brief The main Shell class that coordinates tokenizer, parser, executor and
 * plugin_manager operations
 */
class Shell {
public:
  /**
   * @brief Default constructor
   */
  Shell();

  /**
   * @brief Destructor
   */
  ~Shell();

  /**
   * @brief Delete copy constructor to prevent copying
   */
  Shell(const Shell &) = delete;

  /**
   * @brief Delete copy assignment operator to prevent copying
   */
  Shell &operator=(const Shell &) = delete;

  /**
   * @name Initialization and Execution
   * @{
   */

  /**
   * @brief Initialize the shell
   * @return true if initialization successful, false otherwise
   */
  bool initialize();

  /**
   * @brief Run the shell main loop
   */
  void run();

  /**
   * @brief Shutdown the shell
   */
  void shutdown();

  /**
   * @brief Check if the shell is running
   * @return true if the shell is running, false otherwise
   */
  [[nodiscard]] bool is_running() const noexcept;

  /** @} */

  /**
   * @name Command Execution
   * @{
   */

  /**
   * @brief Evaluate an input command string
   * @param input Command string to evaluate
   * @return Output of the command execution
   */
  std::string evaluate(const std::string &input);

  /**
   * @brief Evaluate a script file
   * @param script_path Path to the script file
   * @return Output of the script execution
   */
  std::string evaluate_script(const std::filesystem::path &script_path);

  /**
   * @brief Evaluate script code from a string
   * @param code Script code to evaluate
   * @return Output of the script execution
   */
  std::string evaluate_script_code(const std::string &code);

  /**
   * @brief Start a script REPL (Read-Eval-Print Loop)
   */
  void start_script_repl();

  /** @} */

  /**
   * @name Variable Management
   * @{
   */

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

  /** @} */

  /**
   * @name Alias Management
   * @{
   */

  /**
   * @brief Add a command alias
   * @param alias Alias name
   * @param command Command that the alias refers to
   */
  void add_alias(const std::string &alias, const std::string &command);

  /** @} */

  /**
   * @name Configuration Management
   * @{
   */

  /**
   * @brief Load configuration from a file
   * @param config_file Path to the configuration file
   * @return true if configuration loaded successfully, false otherwise
   */
  bool load_config(const std::filesystem::path &config_file);

  /** @} */

  /**
   * @name Plugin Management
   * @{
   */

  /**
   * @brief Load a plugin from a file
   * @param plugin_path Path to the plugin file
   * @return true if plugin loaded successfully, false otherwise
   */
  bool load_plugin(const std::filesystem::path &plugin_path);

  /**
   * @brief Reload a plugin
   * @param name Name of the plugin to reload
   * @return true if plugin reloaded successfully, false otherwise
   */
  bool reload_plugin(const std::string &name);

  /**
   * @brief Enable a plugin
   * @param name Name of the plugin to enable
   * @return true if plugin enabled successfully, false otherwise
   */
  bool enable_plugin(const std::string &name);

  /**
   * @brief Disable a plugin
   * @param name Name of the plugin to disable
   * @return true if plugin disabled successfully, false otherwise
   */
  bool disable_plugin(const std::string &name);

  /**
   * @brief Register a built-in plugin
   * @param plugin Unique pointer to the plugin
   * @return true if plugin registered successfully, false otherwise
   */
  bool register_builtin_plugin(std::unique_ptr<Plugin> plugin);

  /**
   * @brief Get a list of loaded plugins
   * @return Vector of plugin names
   */
  [[nodiscard]] std::vector<std::string> get_loaded_plugins() const;

  /**
   * @brief Add a directory to search for plugins
   * @param directory Directory path to add
   */
  void add_plugin_directory(const std::filesystem::path &directory);

  /**
   * @brief Scan plugin directories for new plugins
   */
  void scan_plugin_directories();

  /**
   * @brief Enable or disable hot reloading of plugins
   * @param enabled true to enable, false to disable
   */
  void set_plugin_hot_reload(bool enabled);

  /**
   * @brief Check for plugin updates
   */
  void check_for_plugin_updates();

  /**
   * @brief Unload all plugins from the shell
   */
  void unload_all_plugins() {
    if (plugin_manager_) {
      plugin_manager_->unload_all();
    }
  }

  /**
   * @brief Get metadata for a plugin
   * @param name Plugin name
   * @return Plugin metadata if found, std::nullopt otherwise
   */
  [[nodiscard]] std::optional<PluginMetadata>
  get_plugin_metadata(const std::string &name) const;

  /**
   * @brief Trigger a plugin event
   * @param event Event to trigger
   * @param data Event data
   */
  void trigger_event(PluginEvent event, const PluginEventData &data = {});

  /** @} */

  /**
   * @name Environment Management
   * @{
   */

  /**
   * @brief Get the environment instance
   * @return Reference to the environment
   */
  [[nodiscard]] Environment &get_environment() noexcept { return *env_; }

  /**
   * @brief Get the script interpreter instance
   * @return Reference to the script interpreter
   */
  [[nodiscard]] ScriptInterpreter &get_script_interpreter() noexcept {
    return *script_interpreter_;
  }

  /**
   * @brief Get the plugin manager instance
   * @return Reference to the plugin manager
   */
  [[nodiscard]] PluginManager &get_plugin_manager() noexcept {
    return *plugin_manager_;
  }

  /**
   * @brief Get the environment instance (const version)
   * @return Const reference to the environment
   */
  [[nodiscard]] const Environment &get_environment() const noexcept {
    return *env_;
  }

  /** @} */

  /**
   * @name Command Registration
   * @{
   */

  /**
   * @brief Register a command with the shell
   * @tparam Func Function type
   * @param name Command name
   * @param func Command function
   * @param help_text Help text for the command
   */
  template <typename Func>
  void register_command(const std::string &name, Func &&func,
                        const std::string &help_text) {
    // Create a wrapper function that binds the command function with the
    // current Shell instance
    auto command_wrapper =
        [func = std::forward<Func>(func)](std::span<const std::string> args,
                                          Environment &env) -> std::string {
      // Get Shell instance
      Shell *shell_ptr = static_cast<Shell *>(env.get_shell());
      if (!shell_ptr) {
        return "Error: Shell instance not available";
      }

      try {
        // Call the original function, passing Shell reference and arguments
        int exit_code = func(
            *shell_ptr, std::vector<std::string>(args.begin(), args.end()));
        // Update exit status
        env.set_last_exit_status(exit_code);
        return ""; // Built-in commands output through other means, return empty
                   // string here
      } catch (const std::exception &e) {
        return std::string("Error: ") + e.what();
      }
    };

    // Register with the environment
    env_->register_command(name, command_wrapper, help_text);
  }

  /** @} */

  /**
   * @name Signal Handling
   * @{
   */

  /**
   * @brief Handle interrupt signal
   */
  void handle_interrupt();

  /** @} */

  /**
   * @name Static Instance Access
   * @{
   */

  /**
   * @brief Get the current shell instance
   * @return Pointer to the current shell instance
   */
  [[nodiscard]] static Shell *get_current_instance();

  /** @} */

private:
  /**
   * @name Core Components
   * @{
   */

  /** @brief Tokenizer component */
  std::shared_ptr<Tokenizer> tokenizer_;

  /** @brief Parser component */
  std::shared_ptr<Parser> parser_;

  /** @brief Executor component */
  std::shared_ptr<Executor> executor_;

  /** @brief Environment component */
  std::shared_ptr<Environment> env_;

  /** @brief Configuration manager component */
  std::shared_ptr<ConfigManager> config_;

  /** @brief Plugin manager component */
  std::shared_ptr<PluginManager> plugin_manager_;

  /** @brief Script interpreter component */
  std::shared_ptr<ScriptInterpreter> script_interpreter_;

  /** @} */

  /**
   * @name State Flags
   * @{
   */

  /** @brief Running state flag */
  std::atomic<bool> running_;

  /** @} */

  /**
   * @name Shell Configuration
   * @{
   */

  /** @brief Shell prompt format string */
  std::string prompt_;

  /** @brief Current history index */
  size_t history_index_;

  /** @} */

  /**
   * @name Readline Functions
   * @{
   */

  /**
   * @brief Print the shell prompt
   */
  void print_prompt();

  /**
   * @brief Read a line of input
   * @return Input line
   */
  std::string read_line();

  /**
   * @brief Initialize command completion
   */
  void initialize_completion();

  /**
   * @brief Readline completion callback
   * @param text Text to complete
   * @param start Start position of text
   * @param end End position of text
   * @return Array of possible completions
   */
  static char **completion_callback(const char *text, int start, int end);

  /**
   * @brief Command generator for completion
   * @param text Text to complete
   * @param state State of the completion
   * @return Next possible completion
   */
  static char *command_generator(const char *text, int state);

  /** @} */

  /**
   * @name Signal Handling
   * @{
   */

  /**
   * @brief Set up signal handlers
   */
  void setup_signal_handlers();

  /** @} */

  /**
   * @name Initialization Helpers
   * @{
   */

  /**
   * @brief Register built-in commands
   */
  void register_builtins();

  /**
   * @brief Register built-in plugins
   */
  void register_builtin_plugins();

  /** @} */

  /**
   * @name Singleton Instance
   * @{
   */

  /** @brief Pointer to the current shell instance */
  static Shell *current_instance_;

  /** @} */
};

} // namespace shell