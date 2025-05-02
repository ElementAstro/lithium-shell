#include <filesystem>
#include <fstream>
#include <iostream> // Keep for command output
#include <readline/history.h>
#include <readline/readline.h>
#include <spdlog/spdlog.h> // Include spdlog
#include <sstream>

#ifdef _WIN32
// Windows signal handling
#include <signal.h>
#else
// POSIX signal handling
#include <signal.h>
#include <unistd.h>
#endif

#include "../plugins/plugin_manager.hpp"
#include "../utils/color.hpp" // Keep for prompt/command output colors
#include "../utils/string_utils.hpp"
#include "shell.hpp"

// Import built-in plugins
#include "../plugins/builtin/autocomplete_plugin.hpp"
#include "../plugins/builtin/file_watcher_plugin.hpp"
#include "../plugins/builtin/git_plugin.hpp"
#include "../plugins/builtin/history_plugin.hpp"
#include "../plugins/builtin/syntax_highlight_plugin.hpp"

#include "../interpreter/script_debugger.hpp"
#include "../interpreter/script_interpreter.hpp"


namespace shell {

// Static member initialization
Shell *Shell::current_instance_ = nullptr;

// Add static accessor method
Shell *Shell::get_current_instance() { return current_instance_; }

// Signal handler
void signal_handler(int signal) {
  if (Shell::get_current_instance() && signal == SIGINT) {
    // Keep std::cout for immediate user feedback on interrupt
    std::cout << std::endl;
    Shell::get_current_instance()->handle_interrupt();
  }
}

Shell::Shell()
    : tokenizer_(std::make_unique<Tokenizer>()),
      parser_(std::make_unique<Parser>()), executor_(nullptr),
      env_(std::make_unique<Environment>()),
      config_(std::make_unique<ConfigManager>()), plugin_manager_(nullptr),
      running_(false), prompt_("\033[1;32m➜ \033[1;34m{pwd}\033[0m$ "),
      history_index_(0) {
  executor_ = std::make_unique<Executor>(*env_);
  plugin_manager_ = std::make_unique<PluginManager>(*env_);
  script_interpreter_ = std::make_unique<ScriptInterpreter>(*env_);
  plugin_manager_->set_shell(*this);
  current_instance_ = this;
  spdlog::info("Shell instance created.");
}

Shell::~Shell() {
  shutdown();
  current_instance_ = nullptr;
  spdlog::info("Shell instance destroyed.");
}

bool Shell::initialize() {
  spdlog::info("Initializing shell...");
  // Setup signal handlers
  setup_signal_handlers();

  // Initialize readline completion
  initialize_completion();

  // Load default configuration
  auto default_config = ConfigManager::get_default_config_path();
  if (std::filesystem::exists(default_config)) {
    spdlog::info("Loading default configuration from: {}",
                 default_config.string());
    if (!load_config(default_config)) {
      spdlog::warn("Failed to load default configuration file: {}",
                   default_config.string());
    }
  } else {
    spdlog::info("Default configuration file not found: {}",
                 default_config.string());
  }

  // Set working directory to current directory
  env_->set_working_directory(std::filesystem::current_path());
  spdlog::info("Set working directory to: {}",
               std::filesystem::current_path().string());

  // Set up environment variables
  auto envp = environ; // Standard POSIX environment pointer
  while (*envp != nullptr) {
    std::string env_var = *envp;
    size_t equals_pos = env_var.find('=');
    if (equals_pos != std::string::npos) {
      std::string name = env_var.substr(0, equals_pos);
      std::string value = env_var.substr(equals_pos + 1);
      env_->set_env_variable(name, value);
    }
    envp++;
  }
  spdlog::debug("Loaded environment variables from system.");

  // Register built-in commands
  register_builtins();
  spdlog::info("Registered built-in commands.");

  // Register built-in plugins
  register_builtin_plugins();
  spdlog::info("Registered built-in plugins.");

// Add default plugin directory
#ifdef _WIN32
  std::filesystem::path home_dir =
      env_->get_env_variable("USERPROFILE").value_or("");
#else
  std::filesystem::path home_dir = env_->get_env_variable("HOME").value_or("");
#endif

  if (!home_dir.empty()) {
    std::filesystem::path plugin_dir = home_dir / ".lithium" / "plugins";
    add_plugin_directory(plugin_dir);
    spdlog::info("Added user plugin directory: {}", plugin_dir.string());

// Add system-level plugin directories
#ifdef _WIN32
    add_plugin_directory("C:\\Program Files\\Lithium Shell\\plugins");
    spdlog::info("Added system plugin directory: C:\\Program Files\\Lithium "
                 "Shell\\plugins");
#else
    add_plugin_directory("/usr/local/share/lithium-shell/plugins");
    spdlog::info("Added system plugin directory: "
                 "/usr/local/share/lithium-shell/plugins");
    add_plugin_directory("/usr/share/lithium-shell/plugins");
    spdlog::info(
        "Added system plugin directory: /usr/share/lithium-shell/plugins");
#endif

    // Scan and load plugins
    spdlog::info("Scanning plugin directories...");
    scan_plugin_directories();
  } else {
    spdlog::warn("Could not determine home directory, skipping user plugin "
                 "directory setup.");
  }

  // Enable hot reload by default
  set_plugin_hot_reload(true);
  spdlog::info("Plugin hot reload enabled by default.");

  // Trigger ShellStartup event
  trigger_event(PluginEvent::ShellStartup);
  spdlog::info("Shell initialization complete.");
  return true;
}

void Shell::run() {
  running_ = true;

  // Keep welcome messages as direct user output
  std::cout << Color::bold + Color::green << "Welcome to Lithium Shell"
            << Color::reset << std::endl;
  std::cout << "Type 'help' for available commands" << std::endl;

  while (running_) {
    try {
      // Check for plugin updates
      check_for_plugin_updates();

      // Print prompt (handled by read_line)
      // print_prompt(); // No longer needed here

      // Read input
      std::string input = read_line();

      // Skip empty input
      if (input.empty()) {
        continue;
      }

      spdlog::debug("Read input line: {}", input);

      // Parse command and arguments (simple split for event data)
      std::vector<std::string> args;
      std::istringstream iss(input);
      std::string token;
      while (iss >> token) {
        args.push_back(token);
      }

      if (!args.empty()) {
        // Create command event data
        PluginEventData event_data;
        PluginEventData::CommandData cmd_data;
        cmd_data.command = args[0];
        cmd_data.args = args;
        event_data.data = cmd_data;

        // Trigger CommandBefore event
        spdlog::trace("Triggering CommandBefore event for command: {}",
                      args[0]);
        trigger_event(PluginEvent::CommandBefore, event_data);
      }

      // Add to history (readline history)
      add_history(input.c_str());
      // Add to internal history (Environment)
      env_->add_to_history(input);
      spdlog::debug("Added command to history: {}", input);

      // Evaluate input
      spdlog::trace("Evaluating input: {}", input);
      std::string result = evaluate(input);
      spdlog::trace("Evaluation result: {}",
                    result.empty() ? "[empty]" : result);

      // Trigger CommandAfter event
      if (!args.empty()) {
        PluginEventData event_data;
        PluginEventData::CommandData cmd_data;
        cmd_data.command = args[0];
        cmd_data.args = args;
        event_data.data = cmd_data;

        spdlog::trace("Triggering CommandAfter event for command: {}", args[0]);
        trigger_event(PluginEvent::CommandAfter, event_data);
      }

      // Print result if not empty (Keep as direct user output)
      if (!result.empty()) {
        std::cout << result << std::endl;
      }
    } catch (const std::exception &e) {
      // Use spdlog for internal errors, keep color for user visibility
      spdlog::error("Caught exception in main loop: {}", e.what());
      std::cerr << Color::bold + Color::red << "Error: " << e.what()
                << Color::reset << std::endl;
    } catch (...) {
      spdlog::error("Caught unknown exception in main loop.");
      std::cerr << Color::bold + Color::red << "An unknown error occurred."
                << Color::reset << std::endl;
    }
  }
  spdlog::info("Shell main loop finished.");
}

std::string Shell::evaluate(const std::string &input) {
  // Tokenize input
  spdlog::trace("Tokenizing input: {}", input);
  auto tokens = tokenizer_->tokenize(input);

  // Validate syntax
  if (!tokenizer_->validate_syntax(tokens)) {
    spdlog::warn("Syntax error: {}", tokenizer_->get_error_message());
    // Return error message with color for user
    return Color::bold + Color::red +
           "Syntax error: " + tokenizer_->get_error_message() + Color::reset;
  }
  spdlog::trace("Syntax validated successfully.");

  // Parse tokens into AST
  spdlog::trace("Parsing tokens...");
  auto ast = parser_->parse(tokens);
  if (!ast) {
    spdlog::error("Parse error: {}", parser_->get_error_message());
    // Return error message with color for user
    return Color::bold + Color::red +
           "Parse error: " + parser_->get_error_message() + Color::reset;
  }
  spdlog::trace("Parsing successful.");

  // Execute AST
  ExecutionContext ctx;
  spdlog::trace("Executing AST...");
  auto result = executor_->execute(ast, ctx);
  spdlog::debug("Execution finished. Exit code: {}, Output size: {}",
                result.exit_code, result.output.length());

  // Update exit status
  env_->set_last_exit_status(result.exit_code);

  // Return output (intended for display, not logging)
  return result.output;
}

std::string Shell::evaluate_script(const std::filesystem::path &script_path) {
  spdlog::info("Evaluating script file: {}", script_path.string());
  if (!std::filesystem::exists(script_path)) {
    spdlog::error("Script file not found: {}", script_path.string());
    return Color::bold + Color::red +
           "Error: Script file not found: " + script_path.string() +
           Color::reset;
  }

  std::ifstream file(script_path);
  if (!file) {
    spdlog::error("Failed to open script file: {}", script_path.string());
    return Color::bold + Color::red +
           "Error: Failed to open script file: " + script_path.string() +
           Color::reset;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  std::string script_content = buffer.str();
  std::vector<std::string> lines = split_string(script_content, '\n');
  spdlog::debug("Read {} lines from script file.", lines.size());

  std::string result_output; // Accumulate output for return
  for (const auto &line : lines) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') {
      continue;
    }

    spdlog::trace("Evaluating script line: {}", line);
    std::string line_result = evaluate(line);
    if (!line_result.empty()) {
      // Append result to the overall script output
      result_output += line_result + "\n";
    }

    // Stop execution if last command failed and we're using set -e
    auto errexit = env_->get_variable("errexit");
    if (errexit && *errexit == "true" && env_->get_last_exit_status() != 0) {
      spdlog::warn("Script execution stopped due to error (errexit is set). "
                   "Last exit code: {}",
                   env_->get_last_exit_status());
      result_output += Color::bold + Color::red +
                       "Script execution stopped due to error" + Color::reset +
                       "\n";
      break;
    }
  }
  spdlog::info("Finished evaluating script file: {}", script_path.string());
  // Return accumulated output
  return result_output;
}

void Shell::set_variable(const std::string &name, const std::string &value) {
  // Store old value for event notification
  auto old_value = env_->get_variable(name);
  spdlog::debug("Setting variable '{}' to '{}'. Old value was: '{}'", name,
                value, old_value.value_or("[not set]"));

  // Set new value
  env_->set_variable(name, value);

  // Trigger EnvironmentChanged event
  PluginEventData event_data;
  PluginEventData::EnvironmentData env_data;
  env_data.name = name;
  env_data.old_value = old_value;
  env_data.new_value = value;
  event_data.data = env_data;

  spdlog::trace("Triggering EnvironmentChanged event for variable: {}", name);
  trigger_event(PluginEvent::EnvironmentChanged, event_data);
}

std::optional<std::string> Shell::get_variable(const std::string &name) const {
  return env_->get_variable(name);
}

void Shell::add_alias(const std::string &alias, const std::string &command) {
  spdlog::debug("Adding alias: {}='{}'", alias, command);
  env_->add_alias(alias, command);
}

bool Shell::load_config(const std::filesystem::path &config_file) {
  spdlog::info("Attempting to load configuration from: {}",
               config_file.string());
  bool result = config_->load_config(config_file);

  if (result) {
    spdlog::info("Configuration loaded successfully from: {}",
                 config_file.string());
    // Trigger ConfigChanged event after successful load
    spdlog::trace("Triggering ConfigChanged event.");
    trigger_event(PluginEvent::ConfigChanged);
  } else {
    spdlog::error("Failed to load configuration from: {}",
                  config_file.string());
  }

  return result;
}

void Shell::shutdown() {
  if (running_) {
    spdlog::info("Shutting down shell...");
    // Trigger ShellShutdown event
    spdlog::trace("Triggering ShellShutdown event.");
    trigger_event(PluginEvent::ShellShutdown);

    running_ = false;

    // Unload all plugins
    if (plugin_manager_) {
      spdlog::info("Unloading all plugins...");
      plugin_manager_->unload_all();
      spdlog::info("All plugins unloaded.");
    }
    spdlog::info("Shell shutdown complete.");
  }
}

bool Shell::is_running() const { return running_; }

void Shell::handle_interrupt() {
  spdlog::debug("Handling interrupt signal (SIGINT).");
  // std::cout << std::endl; // Moved to signal handler for immediate feedback
  if (executor_) {
    executor_->interrupt();
  }
}

// Plugin system enhancement interface implementation
bool Shell::load_plugin(const std::filesystem::path &plugin_path) {
  spdlog::info("Attempting to load plugin from: {}", plugin_path.string());
  bool success = plugin_manager_->load_plugin(plugin_path);
  if (success) {
    spdlog::info("Successfully loaded plugin from: {}", plugin_path.string());
  } else {
    spdlog::error("Failed to load plugin from: {}", plugin_path.string());
  }
  return success;
}

bool Shell::reload_plugin(const std::string &name) {
  spdlog::info("Attempting to reload plugin: {}", name);
  bool success = plugin_manager_->reload_plugin(name);
  if (success) {
    spdlog::info("Successfully reloaded plugin: {}", name);
  } else {
    spdlog::error("Failed to reload plugin: {}", name);
  }
  return success;
}

bool Shell::enable_plugin(const std::string &name) {
  spdlog::info("Attempting to enable plugin: {}", name);
  bool success = plugin_manager_->enable_plugin(name);
  if (success) {
    spdlog::info("Successfully enabled plugin: {}", name);
  } else {
    spdlog::error("Failed to enable plugin: {}", name);
  }
  return success;
}

bool Shell::disable_plugin(const std::string &name) {
  spdlog::info("Attempting to disable plugin: {}", name);
  bool success = plugin_manager_->disable_plugin(name);
  if (success) {
    spdlog::info("Successfully disabled plugin: {}", name);
  } else {
    spdlog::error("Failed to disable plugin: {}", name);
  }
  return success;
}

bool Shell::register_builtin_plugin(std::unique_ptr<Plugin> plugin) {
  if (!plugin) {
    spdlog::error("Attempted to register a null built-in plugin.");
    return false;
  }
  std::string name = plugin->get_metadata().name();
  spdlog::info("Registering built-in plugin: {}", name);
  bool success = plugin_manager_->register_plugin(std::move(plugin));
  if (!success) {
    spdlog::error("Failed to register built-in plugin: {}", name);
  }
  return success;
}

std::vector<std::string> Shell::get_loaded_plugins() const {
  return plugin_manager_->get_loaded_plugins();
}

void Shell::add_plugin_directory(const std::filesystem::path &directory) {
  spdlog::debug("Adding plugin directory to scan list: {}", directory.string());
  plugin_manager_->add_plugin_directory(directory);
}

void Shell::scan_plugin_directories() {
  spdlog::info("Scanning registered plugin directories for new plugins...");
  plugin_manager_->scan_plugin_directories();
  spdlog::info("Finished scanning plugin directories.");
}

void Shell::set_plugin_hot_reload(bool enabled) {
  spdlog::info("Setting plugin hot reload to: {}",
               enabled ? "enabled" : "disabled");
  plugin_manager_->set_auto_reload(enabled);
}

void Shell::check_for_plugin_updates() {
  spdlog::trace("Checking for plugin updates (if hot reload enabled)...");
  plugin_manager_->check_for_plugin_updates();
}

std::optional<PluginMetadata>
Shell::get_plugin_metadata(const std::string &name) const {
  return plugin_manager_->get_plugin_metadata(name);
}

void Shell::trigger_event(PluginEvent event, const PluginEventData &data) {
  // Logging for specific events can be noisy, use trace level
  spdlog::trace("Triggering plugin event: {}", static_cast<int>(event));
  plugin_manager_->trigger_event(event, data);
}

std::string Shell::read_line() {
  // Generate prompt string with replacements
  std::string prompt_str = prompt_;
  std::string pwd = env_->get_working_directory().string();
  std::string home_dir = env_->get_env_variable("HOME").value_or("");

  // Replace {pwd} with current directory (shortened if in home)
  if (!home_dir.empty() && pwd.starts_with(home_dir)) {
    pwd = "~" + pwd.substr(home_dir.length());
  }

  // Replace placeholders in prompt
  prompt_str = replace_all(prompt_str, "{pwd}", pwd);

  // Get exit status for prompt
  int status = env_->get_last_exit_status();
  std::string status_str = std::to_string(status);
  prompt_str = replace_all(prompt_str, "{status}", status_str);

  // Read line with readline
  char *line = readline(prompt_str.c_str());

  if (!line) {
    // Handle EOF (Ctrl+D) - Keep direct output for user feedback
    std::cout << std::endl;
    spdlog::info("EOF detected (Ctrl+D), shutting down.");
    shutdown();
    return "";
  }

  std::string result(line);
  free(line); // Free memory allocated by readline

  // Trim leading/trailing whitespace
  result = trim(result);

  return result;
}

void Shell::setup_signal_handlers() {
  spdlog::debug("Setting up signal handlers...");
#ifdef _WIN32
  // Windows signal handling
  if (signal(SIGINT, signal_handler) == SIG_ERR) {
    spdlog::critical("Failed to set up SIGINT handler on Windows.");
    throw std::runtime_error("Failed to set up signal handler");
  }
#else
  // POSIX signal handling
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sa.sa_flags = 0; // No SA_RESTART, interrupt system calls
  sigemptyset(&sa.sa_mask);

  if (::sigaction(SIGINT, &sa, nullptr) == -1) {
    spdlog::critical("Failed to set up SIGINT handler using sigaction: {}",
                     strerror(errno));
    throw std::runtime_error("Failed to set up signal handler");
  }
#endif
  spdlog::debug("Signal handlers set up successfully.");
}

void Shell::initialize_completion() {
  spdlog::debug("Initializing readline completion functions...");
  // Configure readline
  rl_attempted_completion_function = Shell::completion_callback;
  // Use default word break characters initially, can be customized
  // rl_completer_word_break_characters = const_cast<char*>("
  // \t\n\"\\'`@$><=;|&{(");
  spdlog::debug("Readline completion initialized.");
}

void Shell::print_prompt() {
  // Prompt is now printed by readline in read_line()
  spdlog::trace("print_prompt() called (handled by readline).");
}

// Readline completion callback
char **Shell::completion_callback(const char *text, int start, int end) {
  // Suppress unused parameter warnings
  (void)start;
  (void)end;

  spdlog::trace("Readline completion callback invoked for text: '{}'", text);
  rl_attempted_completion_over =
      1; // Prevent default file completion by readline
  return rl_completion_matches(text, command_generator);
}

// Readline command generator for completion
char *Shell::command_generator(const char *text, int state) {
  static std::vector<std::string> matches;
  static size_t match_index = 0;

  if (state == 0) {
    // Start a new completion cycle
    spdlog::trace("Starting new completion cycle for: '{}'", text);
    matches.clear();
    match_index = 0;

    if (current_instance_ && current_instance_->env_) {
      std::string prefix(text);
      const Environment &env = *current_instance_->env_;

      // 1. Complete built-in commands and external commands found in PATH
      auto commands =
          env.get_all_commands(); // Includes builtins and PATH executables
      for (const auto &cmd : commands) {
        if (cmd.starts_with(prefix)) {
          matches.push_back(cmd);
        }
      }
      spdlog::trace("Found {} command matches.", matches.size());

      // 2. Complete aliases
      auto aliases = env.get_all_aliases();
      size_t alias_start_count = matches.size();
      for (const auto &[alias, _] : aliases) {
        if (alias.starts_with(prefix)) {
          // Avoid adding duplicate if alias name matches a command
          if (std::find(matches.begin(), matches.end(), alias) ==
              matches.end()) {
            matches.push_back(alias);
          }
        }
      }
      spdlog::trace("Found {} alias matches.",
                    matches.size() - alias_start_count);

      // 3. Complete file/directory paths
      // Check if the text looks like a path or if we are completing an argument
      bool complete_path =
          (prefix.find('/') != std::string::npos ||
           prefix.find('\\') != std::string::npos || // Windows path separator
           prefix.starts_with("./") || prefix.starts_with(".\\") ||
           prefix.starts_with("../") || prefix.starts_with("..\\") ||
           prefix == "~" || prefix.starts_with("~/") ||
           (rl_point > 0 && rl_line_buffer[rl_point - 1] ==
                                ' ')); // After a space likely expects arg/path

      if (complete_path) {
        spdlog::trace("Attempting path completion for: '{}'", prefix);
        std::filesystem::path current_dir = env.get_working_directory();
        std::string search_prefix = prefix;
        std::filesystem::path base_dir = current_dir;
        std::string filename_prefix;

        // Handle tilde expansion
        // 修复 home 路径变量的问题
        if (search_prefix.starts_with("~/")) {
          auto home = env.get_env_variable("HOME");
          if (home) {
            search_prefix = home.value() + "/" + search_prefix.substr(2);
          }
        } else if (search_prefix == "~") {
          auto home = env.get_env_variable("HOME");
          if (home) {
            search_prefix = home.value() + "/"; // 确保添加尾部斜杠用于目录列表
          }
        }

        // Extract directory part and filename prefix
        size_t last_sep = search_prefix.find_last_of("/\\");
        if (last_sep != std::string::npos) {
          base_dir = search_prefix.substr(0, last_sep);
          filename_prefix = search_prefix.substr(last_sep + 1);
          // Handle root directory case
          if (base_dir.empty() &&
              (search_prefix[0] == '/' || search_prefix[0] == '\\')) {
#ifdef _WIN32
            // Need to handle drive letters C:\ etc.
            if (search_prefix.length() >= 2 && search_prefix[1] == ':') {
              base_dir = search_prefix.substr(0, 2) + "\\"; // e.g., C:
            } else {
              base_dir = "\\"; // UNC path? Or relative to current drive root?
                               // Let filesystem handle relative.
            }
#else
            base_dir = "/";
#endif
          }
        } else {
          filename_prefix = search_prefix;
          base_dir =
              current_dir; // Search in current directory if no path specified
        }

        // Resolve base_dir relative to current directory if needed
        if (base_dir.is_relative()) {
          base_dir = current_dir / base_dir;
        }

        try {
          if (std::filesystem::exists(base_dir) &&
              std::filesystem::is_directory(base_dir)) {
            size_t path_start_count = matches.size();
            for (const auto &entry :
                 std::filesystem::directory_iterator(base_dir)) {
              std::string filename = entry.path().filename().string();
              if (filename.starts_with(filename_prefix)) {
                std::string match_path_str;
                // Reconstruct the full path string for the match
                std::filesystem::path match_fs_path = entry.path();
                match_path_str = match_fs_path.string();

                // Add trailing separator for directories for better UX
                if (entry.is_directory()) {
                  match_path_str += std::filesystem::path::preferred_separator;
                }

                // Add the match if not already present (e.g., command name
                // matches file)
                if (std::find(matches.begin(), matches.end(), match_path_str) ==
                    matches.end()) {
                  matches.push_back(match_path_str);
                }
              }
            }
            spdlog::trace("Found {} path matches in directory: {}",
                          matches.size() - path_start_count, base_dir.string());
          } else {
            spdlog::trace("Base directory for path completion does not exist "
                          "or is not a directory: {}",
                          base_dir.string());
          }
        } catch (const std::filesystem::filesystem_error &e) {
          spdlog::warn("Filesystem error during path completion for {}: {}",
                       base_dir.string(), e.what());
          // Ignore filesystem errors during completion, don't crash
        }
      }

      // Sort matches for consistency
      std::sort(matches.begin(), matches.end());
      spdlog::trace("Total completion matches found: {}", matches.size());
    }
  }

  // Return next match
  if (match_index < matches.size()) {
    spdlog::trace("Returning match #{}: {}", match_index, matches[match_index]);
    // Need to return a C-style string allocated with malloc, readline will free
    // it
    return strdup(matches[match_index++].c_str());
  }

  // No more matches
  spdlog::trace("No more matches for this completion cycle.");
  return nullptr;
}

void Shell::register_builtin_plugins() {
  spdlog::info("Registering built-in plugins...");
  // Register built-in plugins, these are implemented directly in the code, not
  // dynamically loaded Register autocomplete enhancement plugin
  register_builtin_plugin(std::make_unique<AutoCompletePlugin>());

  // Register history enhancement plugin
  register_builtin_plugin(std::make_unique<HistoryPlugin>());

  // Register syntax highlighting plugin
  register_builtin_plugin(std::make_unique<SyntaxHighlightPlugin>());

  // Register Git integration plugin
  register_builtin_plugin(std::make_unique<GitPlugin>());

  // Register file watcher plugin
  register_builtin_plugin(std::make_unique<FileWatcherPlugin>());
  spdlog::info("Finished registering built-in plugins.");
}

void Shell::register_builtins() {
  spdlog::info("Registering built-in commands...");

  // cd command
  register_command(
      "cd",
      [](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        std::filesystem::path target_path;

        if (args.size() < 2 || args[1] == "~" || args[1].starts_with("~/")) {
          // Default to home directory or handle tilde explicitly
          auto home = env.get_env_variable("HOME");
          if (!home) {
            spdlog::error("Builtin 'cd': HOME environment variable not set.");
            std::cerr << Color::red
                      << "Error: HOME environment variable not set"
                      << Color::reset << std::endl; // User feedback
            return 1;
          }
          target_path = *home;
          if (args.size() >= 2 && args[1].starts_with("~/")) {
            target_path /= args[1].substr(2);
          }
        } else if (args[1] == "-") {
          // Change to previous directory (OLDPWD)
          auto oldpwd = env.get_env_variable("OLDPWD");
          if (!oldpwd) {
            spdlog::error("Builtin 'cd': OLDPWD environment variable not set.");
            std::cerr << Color::red << "Error: OLDPWD not set" << Color::reset
                      << std::endl; // User feedback
            return 1;
          }
          target_path = *oldpwd;
          // Print the directory being changed to when using 'cd -'
          std::cout << target_path.string() << std::endl;
        } else {
          target_path = args[1];
        }

        // Store current directory before changing
        std::filesystem::path current_path_str = env.get_working_directory();

        // Resolve relative paths against the current working directory
        std::filesystem::path final_path;
        if (target_path.is_relative()) {
          final_path = env.get_working_directory() / target_path;
        } else {
          final_path = target_path;
        }

        // Canonical path cleans up '..' etc.
        try {
          final_path = std::filesystem::weakly_canonical(final_path);

          if (!std::filesystem::exists(final_path)) {
            spdlog::error("Builtin 'cd': Target directory does not exist: {}",
                          final_path.string());
            std::cerr << Color::red << "Error: Directory does not exist: "
                      << final_path.string() << Color::reset
                      << std::endl; // User feedback
            return 1;
          }

          if (!std::filesystem::is_directory(final_path)) {
            spdlog::error("Builtin 'cd': Target path is not a directory: {}",
                          final_path.string());
            std::cerr << Color::red
                      << "Error: Not a directory: " << final_path.string()
                      << Color::reset << std::endl; // User feedback
            return 1;
          }

          // Update OLDPWD
          env.set_env_variable("OLDPWD", current_path_str.string());
          // Set new working directory
          env.set_working_directory(final_path);
          spdlog::debug("Changed directory to: {}", final_path.string());
          return 0;
        } catch (const std::filesystem::filesystem_error &e) {
          spdlog::error(
              "Builtin 'cd': Filesystem error changing directory to {}: {}",
              final_path.string(), e.what());
          std::cerr << Color::red << "Error changing directory: " << e.what()
                    << Color::reset << std::endl; // User feedback
          return 1;
        }
      },
      "Change the current directory. `cd -` switches to the previous "
      "directory.");

  // pwd command
  register_command(
      "pwd",
      [](Shell &shell,
         const std::vector<std::string> &args [[maybe_unused]]) -> int {
        auto &env = shell.get_environment();
        // pwd output is essential command output, not logging
        std::cout << env.get_working_directory().string() << std::endl;
        return 0;
      },
      "Print the current working directory.");

  // echo command
  register_command(
      "echo",
      []([[maybe_unused]] Shell &shell,
         const std::vector<std::string> &args) -> int {
        // echo output is essential command output, not logging
        for (size_t i = 1; i < args.size(); ++i) {
          if (i > 1)
            std::cout << " ";
          std::cout << args[i];
        }
        std::cout << std::endl;
        return 0;
      },
      "Display a line of text.");

  // exit command
  register_command(
      "exit",
      [this](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        int status = 0;
        if (args.size() > 1) {
          try {
            status = std::stoi(args[1]);
            spdlog::debug("Exit command called with status: {}", status);
          } catch (const std::invalid_argument &) {
            spdlog::error("Builtin 'exit': Invalid exit status argument '{}'.",
                          args[1]);
            std::cerr << Color::red << "Error: Invalid exit status '" << args[1]
                      << "'" << Color::reset << std::endl; // User feedback
            return 1; // Indicate error in parsing status
          } catch (const std::out_of_range &) {
            spdlog::error(
                "Builtin 'exit': Exit status argument '{}' out of range.",
                args[1]);
            std::cerr << Color::red << "Error: Exit status '" << args[1]
                      << "' out of range" << Color::reset
                      << std::endl; // User feedback
            return 1;               // Indicate error in parsing status
          }
        } else {
          spdlog::debug("Exit command called with default status 0.");
        }

        env.set_last_exit_status(status);
        shutdown(); // Initiate shell shutdown
        // The return value here might not be used if shutdown is immediate,
        // but set it correctly anyway.
        return status;
      },
      "Exit the shell. `exit [n]` exits with status n.");

  // help command
  register_command(
      "help",
      [](Shell &shell,
         const std::vector<std::string> &args [[maybe_unused]]) -> int {
        auto &env = shell.get_environment();
        // help output is essential command output, not logging
        std::cout << Color::bold << Color::blue
                  << "Available commands:" << Color::reset << std::endl;

        auto commands = env.get_all_commands();
        std::sort(commands.begin(), commands.end());

        size_t max_len = 0;
        for (const auto &cmd : commands) {
          max_len = std::max(max_len, cmd.length());
        }

        for (const auto &cmd : commands) {
          std::cout << "  " << Color::bold << Color::green << cmd
                    << Color::reset;

          // Pad with spaces for alignment
          std::cout << std::string(max_len - cmd.length() + 2, ' ');

          // Add help text
          std::string help_text = env.get_help_text(cmd);
          if (!help_text.empty()) {
            std::cout << help_text;
          }

          std::cout << std::endl;
        }

        return 0;
      },
      "Display help for available commands.");

  // export command
  register_command(
      "export",
      [](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        if (args.size() < 2) {
          // List all environment variables (essential command output)
          std::cout << "Environment variables:" << std::endl;
          // Sort for consistent output
          std::vector<std::pair<std::string, std::string>> vars(
              env.get_all_env_variables().begin(),
              env.get_all_env_variables().end());
          std::sort(vars.begin(), vars.end());
          for (const auto &[name, value] : vars) {
            std::cout << name << "=" << value << std::endl;
          }
          return 0;
        }

        int ret_code = 0;
        for (size_t i = 1; i < args.size(); ++i) {
          std::string arg = args[i];
          size_t equals_pos = arg.find('=');

          if (equals_pos != std::string::npos) {
            std::string name = arg.substr(0, equals_pos);
            std::string value = arg.substr(equals_pos + 1);
            // Basic validation for variable name (optional but good practice)
            // 修复逻辑表达式优先级问题
            if (name.empty() || (!isalpha(name[0]) && name[0] != '_')) {
              spdlog::error("Builtin 'export': Invalid variable name '{}'. "
                            "Must start with a letter or underscore.",
                            name);
              std::cerr << Color::red
                        << "Error: Invalid variable name: " << name
                        << Color::reset << std::endl; // User feedback
              ret_code = 1;
              continue;
            }
            spdlog::debug("Exporting environment variable: {}={}", name, value);
            env.set_env_variable(name, value);
          } else {
            // Allow exporting existing shell variables to environment
            auto shell_var = env.get_variable(arg);
            if (shell_var) {
              spdlog::debug(
                  "Exporting existing shell variable to environment: {}={}",
                  arg, *shell_var);
              env.set_env_variable(arg, *shell_var);
            } else {
              spdlog::error("Builtin 'export': Invalid syntax '{}'. Use "
                            "'export NAME=VALUE' or 'export EXISTING_VAR'.",
                            arg);
              std::cerr << Color::red << "Error: Invalid export syntax: " << arg
                        << ". Use 'export NAME=VALUE' or 'export EXISTING_VAR'"
                        << Color::reset << std::endl; // User feedback
              ret_code = 1;
            }
          }
        }

        return ret_code;
      },
      "Set environment variables. `export NAME=VALUE` or `export "
      "EXISTING_VAR`.");

  // set command
  register_command(
      "set",
      [](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        if (args.size() < 2) {
          // List all shell variables (essential command output)
          std::cout << "Shell variables:" << std::endl;
          // Sort for consistent output
          std::vector<std::pair<std::string, std::string>> vars(
              env.get_all_variables().begin(), env.get_all_variables().end());
          std::sort(vars.begin(), vars.end());
          for (const auto &[name, value] : vars) {
            std::cout << name << "=" << value << std::endl;
          }
          // Also list shell options like errexit
          std::cout << "Shell options:" << std::endl;
          auto errexit = env.get_variable("errexit");
          std::cout << "  errexit: "
                    << (errexit && *errexit == "true" ? "on" : "off")
                    << std::endl;

          return 0;
        }

        // Handle shell options
        if (args[1] == "-e") {
          spdlog::debug("Setting shell option: errexit=true");
          env.set_variable("errexit", "true");
          return 0;
        } else if (args[1] == "+e") {
          spdlog::debug("Setting shell option: errexit=false");
          env.set_variable("errexit", "false");
          return 0;
        }
        // Add other options like -o/+o pipefail etc. here if implemented

        // Regular variable setting
        int ret_code = 0;
        for (size_t i = 1; i < args.size(); ++i) {
          std::string arg = args[i];
          size_t equals_pos = arg.find('=');

          if (equals_pos != std::string::npos) {
            std::string name = arg.substr(0, equals_pos);
            std::string value = arg.substr(equals_pos + 1);
            // Basic validation for variable name (optional but good practice)
            if (name.empty() || (!isalpha(name[0]) && name[0] != '_')) {
              spdlog::error("Builtin 'set': Invalid variable name '{}'. Must "
                            "start with a letter or underscore.",
                            name);
              std::cerr << Color::red
                        << "Error: Invalid variable name: " << name
                        << Color::reset << std::endl; // User feedback
              ret_code = 1;
              continue;
            }
            spdlog::debug("Setting shell variable: {}={}", name, value);
            shell.set_variable(name,
                               value); // Use shell's method to trigger events
          } else {
            spdlog::error("Builtin 'set': Invalid set syntax '{}'. Use 'set "
                          "NAME=VALUE' or options like '+e' / '-e'.",
                          arg);
            std::cerr << Color::red << "Error: Invalid set syntax: " << arg
                      << ". Use 'set NAME=VALUE' or options like '+e' / '-e'"
                      << Color::reset << std::endl; // User feedback
            ret_code = 1;
          }
        }

        return ret_code;
      },
      "Set shell options and shell variables. `set -e` / `set +e` control exit "
      "on error.");

  // alias command
  register_command(
      "alias",
      [](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        if (args.size() < 2) {
          // List all aliases (essential command output)
          std::cout << "Aliases:" << std::endl;
          // Sort for consistent output
          std::vector<std::pair<std::string, std::string>> aliases(
              env.get_all_aliases().begin(), env.get_all_aliases().end());
          std::sort(aliases.begin(), aliases.end());
          for (const auto &[name, value] : aliases) {
            // Quote the value properly for display
            std::string quoted_value =
                "'" + replace_all(value, "'", "'\\''") + "'";
            std::cout << "alias " << name << "=" << quoted_value << std::endl;
          }
          return 0;
        }

        int ret_code = 0;
        for (size_t i = 1; i < args.size(); ++i) {
          std::string arg = args[i];
          size_t equals_pos = arg.find('=');

          if (equals_pos != std::string::npos) {
            std::string name = arg.substr(0, equals_pos);
            std::string value = arg.substr(equals_pos + 1);

            // Basic validation for alias name (optional)
            if (name.empty()) {
              spdlog::error("Builtin 'alias': Invalid empty alias name.");
              std::cerr << Color::red << "Error: Invalid empty alias name."
                        << Color::reset << std::endl; // User feedback
              ret_code = 1;
              continue;
            }

            // Remove surrounding quotes if present (simple check)
            if (value.length() >= 2 &&
                ((value.front() == '\'' && value.back() == '\'') ||
                 (value.front() == '"' && value.back() == '"'))) {
              value = value.substr(1, value.length() - 2);
            }

            spdlog::debug("Defining alias: {}='{}'", name, value);
            env.add_alias(name, value);
          } else {
            // Check if it's a request to display a specific alias
            auto alias_value = env.resolve_alias(arg);
            if (alias_value) {
              // Quote the value properly for display
              std::string quoted_value =
                  "'" + replace_all(*alias_value, "'", "'\\''") + "'";
              std::cout << "alias " << arg << "=" << quoted_value
                        << std::endl; // Essential output
            } else {
              spdlog::error("Builtin 'alias': Alias '{}' not found and syntax "
                            "invalid for definition.",
                            arg);
              std::cerr << Color::red << "Error: Alias not found: " << arg
                        << ". Use 'alias NAME=VALUE' to define." << Color::reset
                        << std::endl; // User feedback
              ret_code = 1;
            }
          }
        }

        return ret_code;
      },
      "Define or display aliases. `alias NAME=VALUE`.");

  // unalias command
  register_command(
      "unalias",
      [](Shell &shell, const std::vector<std::string> &args) -> int {
        auto &env = shell.get_environment();
        if (args.size() < 2) {
          spdlog::error("Builtin 'unalias': Missing alias name argument.");
          std::cerr
              << Color::red
              << "Error: Missing alias name. Usage: unalias NAME [NAME...]"
              << Color::reset << std::endl; // User feedback
          return 1;
        }

        int ret_code = 0;
        for (size_t i = 1; i < args.size(); ++i) {
          spdlog::debug("Removing alias: {}", args[i]);
          // 修复 remove_alias 函数调用
          if (!env.remove_alias(args[i])) {
            spdlog::warn("Builtin 'unalias': Alias not found: {}", args[i]);
          }
        }

        return ret_code; // Typically returns 0 even if alias wasn't found
      },
      "Remove alias definitions.");

  // source or . command
  auto source_func = [](Shell &shell,
                        const std::vector<std::string> &args) -> int {
    auto &env = shell.get_environment();
    if (args.size() < 2) {
      spdlog::error("Builtin 'source': Missing script file argument.");
      std::cerr << Color::red
                << "Error: Missing script file. Usage: source FILE"
                << Color::reset << std::endl; // User feedback
      return 1;
    }

    std::filesystem::path script_path_arg = args[1];
    std::filesystem::path script_path;

    // Handle ~
    if (script_path_arg.string().starts_with("~")) {
      auto home = env.get_env_variable("HOME");
      if (!home) {
        spdlog::error("Builtin 'source': HOME environment variable not set for "
                      "tilde expansion.");
        std::cerr << Color::red << "Error: HOME environment variable not set"
                  << Color::reset << std::endl; // User feedback
        return 1;
      }
      script_path = *home;
      if (script_path_arg.string().length() > 1) { // Handle "~/" vs just "~"
        script_path /= script_path_arg.string().substr(1);
      }
    } else {
      script_path = script_path_arg;
    }

    // Handle relative paths - resolve against current dir *at time of sourcing*
    if (script_path.is_relative()) {
      script_path = env.get_working_directory() / script_path;
    }

    // Canonical path cleans up '..' etc.
    try {
      script_path = std::filesystem::weakly_canonical(script_path);
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error(
          "Builtin 'source': Filesystem error resolving script path {}: {}",
          script_path_arg.string(), e.what());
      std::cerr << Color::red << "Error resolving script path: " << e.what()
                << Color::reset << std::endl; // User feedback
      return 1;
    }

    spdlog::info("Sourcing script file: {}", script_path.string());
    if (!std::filesystem::exists(script_path)) {
      spdlog::error("Builtin 'source': Script file not found: {}",
                    script_path.string());
      std::cerr << Color::red
                << "Error: Script file not found: " << script_path.string()
                << Color::reset << std::endl; // User feedback
      return 1;
    }
    if (!std::filesystem::is_regular_file(script_path)) {
      spdlog::error("Builtin 'source': Path is not a regular file: {}",
                    script_path.string());
      std::cerr << Color::red
                << "Error: Not a regular file: " << script_path.string()
                << Color::reset << std::endl; // User feedback
      return 1;
    }

    std::ifstream file(script_path);
    if (!file) {
      spdlog::error("Builtin 'source': Failed to open script file: {}",
                    script_path.string());
      std::cerr << Color::red
                << "Error: Failed to open script file: " << script_path.string()
                << Color::reset << std::endl; // User feedback
      return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close(); // Close the file handle

    std::string script_content = buffer.str();
    std::vector<std::string> lines = split_string(script_content, '\n');
    spdlog::debug("Read {} lines from script file {}.", lines.size(),
                  script_path.string());

    int last_exit_code = 0;
    for (const auto &line : lines) {
      // Skip comments and empty lines
      std::string trimmed_line = trim(line);
      if (trimmed_line.empty() || trimmed_line[0] == '#') {
        continue;
      }

      // Evaluate the line in the current shell context
      spdlog::trace("Sourcing line: {}", trimmed_line);
      std::string line_result =
          shell.evaluate(trimmed_line); // Use shell's evaluate

      // Update exit status from the evaluated line
      last_exit_code = env.get_last_exit_status();

      // Print output from the sourced command if any (essential output)
      if (!line_result.empty()) {
        std::cout << line_result << std::endl;
      }

      // Stop execution if last command failed and 'errexit' is set
      auto errexit = env.get_variable("errexit");
      if (errexit && *errexit == "true" && last_exit_code != 0) {
        spdlog::warn("Builtin 'source': Script execution stopped due to error "
                     "(errexit is set). Last exit code: {}",
                     last_exit_code);
        std::cerr << Color::red << "Script execution stopped due to error"
                  << Color::reset << std::endl; // User feedback
        break;
      }
    }
    spdlog::info("Finished sourcing script file: {}", script_path.string());
    return last_exit_code; // Return the exit status of the last command
                           // executed in the script
  };

  register_command(
      "source", source_func,
      "Execute commands from a file in the current shell context.");
  register_command(".", source_func,
                   "Execute commands from a file in the current shell context "
                   "(POSIX alias for source).");

  // history command
  register_command(
      "history",
      [](Shell &shell,
         const std::vector<std::string> &args [[maybe_unused]]) -> int {
        auto &env = shell.get_environment();
        const auto &history = env.get_history();

        // history output is essential command output, not logging
        for (size_t i = 0; i < history.size(); ++i) {
          // Standard history format: index (starting from 1) followed by
          // command
          std::cout << " " << (i + 1) << "\t" << history[i] << std::endl;
        }

        return 0;
      },
      "Display command history.");

  // plugin command - Manage plugins
  register_command(
      "plugin",
      [this](Shell &shell, const std::vector<std::string> &args) -> int {
        [[maybe_unused]] auto &env =
            shell.get_environment(); // Keep env for potential future use
        if (args.size() < 2) {
          // Show plugin help (essential command output)
          std::cout
              << "Usage: plugin <command> [args]" << std::endl
              << "Commands:" << std::endl
              << "  list           - List all loaded plugins and their status"
              << std::endl
              << "  load <path>    - Load a plugin from the specified path"
              << std::endl
              << "  reload <name>  - Reload the specified plugin" << std::endl
              << "  enable <name>  - Enable the specified plugin" << std::endl
              << "  disable <name> - Disable the specified plugin" << std::endl
              << "  info <name>    - Show detailed information about a plugin"
              << std::endl
              << "  scan           - Scan plugin directories and load new "
                 "plugins"
              << std::endl
              << "  autoreload <on|off> - Enable or disable plugin hot "
                 "reloading"
              << std::endl;
          return 0;
        }

        std::string subcmd = args[1];
        spdlog::debug("Plugin command received: {}", subcmd);

        if (subcmd == "list") {
          // List plugins (essential command output)
          std::cout << Color::bold << "Loaded Plugins:" << Color::reset
                    << std::endl;

          auto plugins = get_loaded_plugins();
          if (plugins.empty()) {
            std::cout << "  No plugins loaded." << std::endl;
            return 0;
          }

          // Sort for consistent output
          std::sort(plugins.begin(), plugins.end());

          for (const auto &name : plugins) {
            auto state = plugin_manager_->get_plugin_state(name);
            std::string state_str;

            switch (state) {
            case PluginState::Initialized:
              state_str = Color::green + "Enabled" + Color::reset;
              break;
            case PluginState::Disabled:
              state_str = Color::yellow + "Disabled" + Color::reset;
              break;
            case PluginState::Failed:
              state_str = Color::red + "Failed" + Color::reset;
              break;
            default:
              state_str = Color::dim + "Unknown" + Color::reset;
            }

            auto metadata = get_plugin_metadata(name);
            std::cout << "  " << Color::bold << name << Color::reset;
            if (metadata) {
              std::cout << " (v" << metadata->version() << ")";
            }
            std::cout << " - " << state_str << std::endl;
            // Optionally show error message if failed
            if (state == PluginState::Failed) {
              std::string error = plugin_manager_->get_plugin_error(name);
              if (!error.empty()) {
                std::cout << "    " << Color::red << "Error: " << error
                          << Color::reset << std::endl;
              }
            }
          }
          return 0;
        }

        else if (subcmd == "load" && args.size() >= 3) {
          std::filesystem::path plugin_path_arg = args[2];
          std::filesystem::path plugin_path;

          // Handle ~
          if (plugin_path_arg.string().starts_with("~")) {
            auto home = env_->get_env_variable("HOME");
            if (!home) {
              spdlog::error("Builtin 'plugin load': HOME environment variable "
                            "not set for tilde expansion.");
              std::cerr << Color::red
                        << "Error: HOME environment variable not set"
                        << Color::reset << std::endl; // User feedback
              return 1;
            }
            plugin_path = *home;
            if (plugin_path_arg.string().length() >
                1) { // Handle "~/" vs just "~"
              plugin_path /= plugin_path_arg.string().substr(1);
            }
          } else {
            plugin_path = plugin_path_arg;
          }

          // Handle relative paths
          if (plugin_path.is_relative()) {
            plugin_path = env_->get_working_directory() / plugin_path;
          }

          // Canonical path cleans up '..' etc. and verifies existence
          // implicitly somewhat
          try {
            plugin_path = std::filesystem::weakly_canonical(plugin_path);
          } catch (const std::filesystem::filesystem_error &e) {
            spdlog::error("Builtin 'plugin load': Filesystem error resolving "
                          "plugin path {}: {}",
                          plugin_path_arg.string(), e.what());
            std::cerr << Color::red
                      << "Error resolving plugin path: " << e.what()
                      << Color::reset << std::endl; // User feedback
            return 1;
          }

          if (!std::filesystem::exists(plugin_path)) {
            spdlog::error("Builtin 'plugin load': Plugin file not found: {}",
                          plugin_path.string());
            std::cerr << Color::red << "Error: Plugin file not found: "
                      << plugin_path.string() << Color::reset
                      << std::endl; // User feedback
            return 1;
          }

          // load_plugin already logs success/failure internally
          bool success = load_plugin(plugin_path);
          if (success) {
            std::cout << Color::green << "Plugin loaded successfully from: "
                      << plugin_path.string() << Color::reset
                      << std::endl; // User feedback
            return 0;
          } else {
            std::cerr << Color::red
                      << "Failed to load plugin from: " << plugin_path.string()
                      << Color::reset << std::endl; // User feedback
            // More detailed error might be in the main log via load_plugin
            return 1;
          }
        }

        else if (subcmd == "reload" && args.size() >= 3) {
          std::string name = args[2];
          // reload_plugin already logs success/failure internally
          bool success = reload_plugin(name);
          if (success) {
            std::cout << Color::green << "Plugin '" << name
                      << "' reloaded successfully." << Color::reset
                      << std::endl; // User feedback
            return 0;
          } else {
            std::cerr << Color::red << "Failed to reload plugin '" << name
                      << "'." << Color::reset
                      << std::endl; // User feedback
                                    // More detailed error might be in the main
                                    // log via reload_plugin
            return 1;
          }
        }

        else if (subcmd == "enable" && args.size() >= 3) {
          std::string name = args[2];
          // enable_plugin already logs success/failure internally
          bool success = enable_plugin(name);
          if (success) {
            std::cout << Color::green << "Plugin '" << name << "' enabled."
                      << Color::reset << std::endl; // User feedback
            return 0;
          } else {
            std::cerr << Color::red << "Failed to enable plugin '" << name
                      << "'." << Color::reset
                      << std::endl; // User feedback
                                    // More detailed error might be in the main
                                    // log via enable_plugin
            return 1;
          }
        }

        else if (subcmd == "disable" && args.size() >= 3) {
          std::string name = args[2];
          // disable_plugin already logs success/failure internally
          bool success = disable_plugin(name);
          if (success) {
            std::cout << Color::green << "Plugin '" << name << "' disabled."
                      << Color::reset << std::endl; // User feedback
            return 0;
          } else {
            std::cerr << Color::red << "Failed to disable plugin '" << name
                      << "'." << Color::reset
                      << std::endl; // User feedback
                                    // More detailed error might be in the main
                                    // log via disable_plugin
            return 1;
          }
        }

        else if (subcmd == "info" && args.size() >= 3) {
          std::string name = args[2];
          auto metadata = get_plugin_metadata(name);

          if (!metadata) {
            spdlog::warn("Builtin 'plugin info': Plugin '{}' not found.", name);
            std::cerr << Color::red << "Plugin '" << name << "' not found."
                      << Color::reset << std::endl; // User feedback
            return 1;
          }

          // Display info (essential command output)
          auto state = plugin_manager_->get_plugin_state(name);
          std::string state_str;
          switch (state) {
          case PluginState::Initialized:
            state_str = Color::green + "Enabled" + Color::reset;
            break;
          case PluginState::Disabled:
            state_str = Color::yellow + "Disabled" + Color::reset;
            break;
          case PluginState::Failed:
            state_str = Color::red + "Failed" + Color::reset;
            break;
          default:
            state_str = "Unknown";
          }

          std::cout << Color::bold << "Plugin Info: " << name << Color::reset
                    << std::endl
                    << "  Version:     " << metadata->version() << std::endl
                    << "  Author:      " << metadata->author() << std::endl
                    << "  Status:      " << state_str << std::endl
                    << "  Description: " << metadata->description()
                    << std::endl;

          if (!metadata->website().empty()) {
            std::cout << "  Website:     " << metadata->website() << std::endl;
          }

          if (!metadata->dependencies().empty()) {
            std::cout << "  Dependencies:" << std::endl;
            for (const auto &dep : metadata->dependencies()) {
              std::cout << "    - " << dep << std::endl;
            }
          }

          // Show error message if failed
          if (state == PluginState::Failed) {
            std::string error = plugin_manager_->get_plugin_error(name);
            if (!error.empty()) {
              std::cout << "  " << Color::red << "Error: " << error
                        << Color::reset << std::endl;
            }
          }

          return 0;
        }

        else if (subcmd == "scan") {
          // scan_plugin_directories already logs internally
          scan_plugin_directories();
          std::cout << Color::green << "Plugin directories scanned."
                    << Color::reset << std::endl; // User feedback
          return 0;
        }

        else if (subcmd == "autoreload" && args.size() >= 3) {
          std::string setting = args[2];
          // set_plugin_hot_reload already logs internally
          if (setting == "on") {
            set_plugin_hot_reload(true);
            std::cout << Color::green << "Plugin hot reload enabled."
                      << Color::reset << std::endl; // User feedback
            return 0;
          } else if (setting == "off") {
            set_plugin_hot_reload(false);
            std::cout << Color::green << "Plugin hot reload disabled."
                      << Color::reset << std::endl; // User feedback
            return 0;
          } else {
            spdlog::error("Builtin 'plugin autoreload': Invalid option '{}'. "
                          "Use 'on' or 'off'.",
                          setting);
            std::cerr << Color::red << "Invalid option: " << setting
                      << ". Use 'on' or 'off'." << Color::reset
                      << std::endl; // User feedback
            return 1;
          }
        }

        // Unknown subcommand or wrong number of arguments
        spdlog::error(
            "Builtin 'plugin': Unknown subcommand '{}' or incorrect arguments.",
            subcmd);
        std::cerr << Color::red
                  << "Unknown plugin command or missing/incorrect arguments: "
                  << subcmd << ". Try 'plugin' for usage help." << Color::reset
                  << std::endl; // User feedback
        return 1;
      },
      "Manage shell plugins (`plugin list`, `load`, `reload`, `enable`, "
      "`disable`, `info`, `scan`, `autoreload`).");

  // script command - 脚本解释器交互
  register_command(
      "script",
      [this]([[maybe_unused]] Shell &shell,
             const std::vector<std::string> &args) -> int {
        if (args.size() < 2) {
          std::cout << "用法: script <命令> [参数...]" << std::endl
                    << "命令:" << std::endl
                    << "  run <文件> - 运行指定脚本文件" << std::endl
                    << "  eval <代码> - 执行单行脚本代码" << std::endl
                    << "  repl       - 启动交互式脚本环境" << std::endl
                    << "  debug <文件> - 以调试模式运行脚本" << std::endl;
          return 0;
        }

        std::string subcmd = args[1];
        if (subcmd == "run" && args.size() >= 3) {
          std::filesystem::path script_path = args[2];
          if (script_path.is_relative()) {
            script_path = env_->get_working_directory() / script_path;
          }

          try {
            auto result = script_interpreter_->load_and_eval(script_path);
            if (std::holds_alternative<ScriptError>(result)) {
              std::cout << std::get<ScriptError>(result).format_error()
                        << std::endl;
              return 1;
            } else {
              // 脚本执行成功
              return 0;
            }
          } catch (const std::exception &e) {
            std::cerr << "运行脚本时出错: " << e.what() << std::endl;
            return 1;
          }
        } else if (subcmd == "eval" && args.size() >= 3) {
          // 组合剩余的参数为一个字符串
          std::string code;
          for (size_t i = 2; i < args.size(); ++i) {
            if (i > 2)
              code += " ";
            code += args[i];
          }

          try {
            auto result = script_interpreter_->eval(code);
            if (std::holds_alternative<ScriptError>(result)) {
              std::cout << std::get<ScriptError>(result).format_error()
                        << std::endl;
              return 1;
            } else {
              // 显示执行结果
              ScriptValue value = std::get<ScriptValue>(result);
              std::cout << value.to_debug_string() << std::endl;
              return 0;
            }
          } catch (const std::exception &e) {
            std::cerr << "执行代码时出错: " << e.what() << std::endl;
            return 1;
          }
        } else if (subcmd == "repl") {
          // 启动REPL环境
          start_script_repl();
          return 0;
        } else if (subcmd == "debug" && args.size() >= 3) {
          std::filesystem::path script_path = args[2];
          if (script_path.is_relative()) {
            script_path = env_->get_working_directory() / script_path;
          }

          // 设置断点在第一行
          auto &debugger = script_interpreter_->get_debugger();
          debugger.add_breakpoint(script_path.string(), 1);
          debugger.start();

          std::cout << "在" << script_path.string()
                    << "的第1行设置断点并启动调试..." << std::endl;

          try {
            auto result = script_interpreter_->load_and_eval(script_path);
            if (std::holds_alternative<ScriptError>(result)) {
              std::cout << std::get<ScriptError>(result).format_error()
                        << std::endl;
              return 1;
            } else {
              // 调试执行完成
              std::cout << "调试执行完成。" << std::endl;
              return 0;
            }
          } catch (const std::exception &e) {
            std::cerr << "调试脚本时出错: " << e.what() << std::endl;
            return 1;
          }
        } else {
          std::cerr << "未知的脚本命令: " << subcmd << std::endl;
          return 1;
        }

        return 1;
      },
      "执行脚本代码或启动交互式脚本环境。");

  spdlog::info("Finished registering built-in commands.");
}

void Shell::start_script_repl() {
  spdlog::info("Starting script REPL environment");

  // 保存当前Shell状态
  bool original_running = running_;

  try {
    // 启动调试器
    script_interpreter_->get_debugger().start();
    
    // 设置调试器回调
    script_interpreter_->get_debugger().set_breakpoint_hit_callback(
      [](const Breakpoint& bp) {
        spdlog::debug("断点触发: #{} at {}:{}", bp.id, bp.location.source, bp.location.line);
        std::cout << "\033[1;33m断点触发\033[0m: " << bp.location.source << ":" 
                  << bp.location.line << " (命中次数: " << bp.hit_count << ")" << std::endl;
      }
    );
    
    script_interpreter_->get_debugger().set_state_change_callback(
      [](DebuggerState state) {
        spdlog::debug("调试器状态改变: {}", static_cast<int>(state));
        
        // 根据状态变化输出不同提示
        switch (state) {
          case DebuggerState::Running:
            std::cout << "\033[1;32m调试器运行中...\033[0m" << std::endl;
            break;
          case DebuggerState::Paused:
            std::cout << "\033[1;33m调试器已暂停\033[0m" << std::endl;
            break;
          case DebuggerState::Terminated:
            std::cout << "\033[1;31m调试器已终止\033[0m" << std::endl;
            break;
          default:
            break;
        }
      }
    );
    
    // 启动脚本解释器的REPL环境
    script_interpreter_->start_repl();

    // 停止调试器
    script_interpreter_->get_debugger().stop();
    
    // REPL结束后恢复Shell状态
    running_ = original_running;
  } catch (const std::exception &e) {
    spdlog::error("Error in script REPL: {}", e.what());
    std::cerr << "脚本REPL环境出错: " << e.what() << std::endl;
    running_ = original_running;
  }

  spdlog::info("Script REPL environment terminated");
}

std::string Shell::evaluate_script_code(const std::string &code) {
  spdlog::debug("Evaluating script code: '{}'", code);

  try {
    auto result = script_interpreter_->eval(code);
    if (std::holds_alternative<ScriptError>(result)) {
      const auto &error = std::get<ScriptError>(result);
      return error.format_error();
    } else {
      const auto &value = std::get<ScriptValue>(result);
      return value.to_debug_string();
    }
  } catch (const std::exception &e) {
    spdlog::error("Exception in evaluate_script_code: {}", e.what());
    return std::string("Error: ") + e.what();
  }
}

} // namespace shell