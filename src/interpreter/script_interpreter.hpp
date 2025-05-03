#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "../core/environment.hpp"

namespace shell {

// Forward declarations
class ScriptDebugger;
class ScriptValue;
class ScriptFunction;

/**
 * @enum ScriptErrorLevel
 * @brief Defines the severity levels of script errors
 */
enum class ScriptErrorLevel {
  Info,    ///< Informational message, no actual error
  Warning, ///< Warning, potential issue but does not affect execution
  Error,   ///< Error, prevents current statement/expression execution
  Fatal    ///< Critical error, aborts the entire script/program execution
};

/**
 * @struct ScriptError
 * @brief Represents an error that occurred during script execution
 */
struct ScriptError {
  ScriptErrorLevel level;   ///< Error severity level
  std::string message;      ///< Error description message
  std::string source;       ///< Error source (filename or "<repl>")
  size_t line;              ///< Line number where the error occurred
  size_t column;            ///< Column number where the error occurred
  std::string code_snippet; ///< Code snippet containing the error
  std::string suggestion;   ///< Suggested fix for the error

  /**
   * @brief Constructor for creating error objects
   * @param lvl Error severity level
   * @param msg Error description message
   * @param src Error source (defaults to "<repl>")
   * @param ln Line number (defaults to 0)
   * @param col Column number (defaults to 0)
   * @param code Code snippet containing the error (defaults to empty)
   * @param suggest Suggested fix (defaults to empty)
   */
  ScriptError(ScriptErrorLevel lvl, std::string msg, std::string src = "<repl>",
              size_t ln = 0, size_t col = 0, std::string code = "",
              std::string suggest = "")
      : level(lvl), message(std::move(msg)), source(std::move(src)), line(ln),
        column(col), code_snippet(std::move(code)),
        suggestion(std::move(suggest)) {}

  /**
   * @brief Formats the error into a human-readable message
   * @return Formatted error message string
   */
  std::string format_error() const;
};

/**
 * @class ScriptScope
 * @brief Represents a script scope, managing variables and functions
 *
 * Handles variable and function scoping, allowing for nested scopes
 * and proper name resolution according to scope hierarchy.
 */
class ScriptScope {
public:
  /**
   * @brief Constructor for creating a scope
   * @param parent Parent scope (nullptr for global scope)
   */
  ScriptScope(ScriptScope *parent = nullptr) : parent_(parent) {}

  /**
   * @brief Sets or creates a variable in the current scope
   * @param name Variable name
   * @param value Variable value
   */
  void set_variable(const std::string &name, ScriptValue value);

  /**
   * @brief Gets a variable value from current scope or parent scopes
   * @param name Variable name to retrieve
   * @return Optional containing the value if found, empty optional otherwise
   */
  std::optional<ScriptValue> get_variable(const std::string &name) const;

  /**
   * @brief Checks if a variable exists in the current scope or parent scopes
   * @param name Variable name to check
   * @return True if the variable exists, false otherwise
   */
  bool has_variable(const std::string &name) const;

  /**
   * @brief Defines a function in the current scope
   * @param name Function name
   * @param func Function implementation
   */
  void define_function(const std::string &name,
                       std::shared_ptr<ScriptFunction> func);

  /**
   * @brief Gets a function from current scope or parent scopes
   * @param name Function name to retrieve
   * @return Function implementation if found, nullptr otherwise
   */
  std::shared_ptr<ScriptFunction> get_function(const std::string &name) const;

  /**
   * @brief Checks if a function exists in the current scope or parent scopes
   * @param name Function name to check
   * @return True if the function exists, false otherwise
   */
  bool has_function(const std::string &name) const;

  /**
   * @brief Gets the parent scope
   * @return Pointer to parent scope, or nullptr if this is the global scope
   */
  ScriptScope *get_parent() const { return parent_; }

private:
  ScriptScope *parent_; ///< Parent scope (nullptr for global scope)
  std::unordered_map<std::string, ScriptValue>
      variables_; ///< Variables in this scope
  std::unordered_map<std::string, std::shared_ptr<ScriptFunction>>
      functions_; ///< Functions in this scope
};

/**
 * @class ScriptInterpreter
 * @brief C++ script interpreter providing script execution capabilities
 *
 * A modern C++ implementation of a script interpreter with support for REPL
 * environment, debugging features, and asynchronous execution.
 */
class ScriptInterpreter {
public:
  /**
   * @brief Constructor for the script interpreter
   * @param env Reference to the environment
   */
  ScriptInterpreter(Environment &env);

  /**
   * @brief Destructor for the script interpreter
   */
  ~ScriptInterpreter();

  // Prevent copying and moving
  ScriptInterpreter(const ScriptInterpreter &) = delete;
  ScriptInterpreter &operator=(const ScriptInterpreter &) = delete;
  ScriptInterpreter(ScriptInterpreter &&) = delete;
  ScriptInterpreter &operator=(ScriptInterpreter &&) = delete;

  /**
   * @brief Evaluates a single line of script code
   * @param code Code to execute
   * @return Either the execution result or an error
   */
  std::variant<ScriptValue, ScriptError> eval(const std::string &code);

  /**
   * @brief Evaluates multiple lines of script code
   * @param code Multi-line code to execute
   * @return Either the execution result or an error
   */
  std::variant<ScriptValue, ScriptError> eval_multi(const std::string &code);

  /**
   * @brief Loads and executes a script from a file
   * @param path Path to the script file
   * @return Either the execution result or an error
   */
  std::variant<ScriptValue, ScriptError>
  load_and_eval(const std::filesystem::path &path);

  /**
   * @brief Asynchronously executes script code
   * @param code Code to execute
   * @param callback Function to call upon completion
   */
  void eval_async(
      const std::string &code,
      std::function<void(std::variant<ScriptValue, ScriptError>)> callback);

  /**
   * @brief Starts the Read-Eval-Print Loop (REPL) environment
   */
  void start_repl();

  /**
   * @brief Interrupts the current execution
   */
  void interrupt();

  /**
   * @brief Gets the debugger interface
   * @return Reference to the script debugger
   */
  ScriptDebugger &get_debugger();

  /**
   * @brief Gets the current environment
   * @return Reference to the environment
   */
  Environment &get_environment() { return env_; }

  /**
   * @brief Handles a special REPL command
   * @param command Command string to process
   */
  void handle_repl_command(const std::string &command);

  /**
   * @brief Registers a native C++ function in the interpreter
   * @param name Function name
   * @param func Function implementation
   */
  void register_native_function(
      const std::string &name,
      std::function<ScriptValue(const std::vector<ScriptValue> &)> func);

  /**
   * @brief Provides auto-completion suggestions for partial inputs
   * @param partial Partial input string to complete
   * @return Vector of possible completions
   */
  std::vector<std::string> complete(const std::string &partial);

private:
  /**
   * @brief Initializes the interpreter
   */
  void initialize();

  /**
   * @brief Registers standard library functions
   */
  void register_stdlib();

  /**
   * @brief Reads a line of input with history and completion support
   * @param prompt The prompt to display
   * @return Input string
   */
  std::string read_line(const std::string &prompt);

  /**
   * @brief Formats a script value as a string
   * @param value Value to format
   * @return Formatted string representation
   */
  std::string format_value(const ScriptValue &value);

private:
  Environment &env_;                          ///< Reference to the environment
  std::unique_ptr<ScriptScope> global_scope_; ///< Global script scope
  std::unique_ptr<ScriptDebugger> debugger_;  ///< Script debugger
  std::atomic<bool> running_; ///< Flag indicating if interpreter is running
  std::vector<std::string> history_; ///< Command history
};

} // namespace shell