#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "script_value.hpp"

namespace shell {

// Forward declarations
class ScriptInterpreter;
class ScriptScope;

/**
 * @struct SourceLocation
 * @brief Represents a location in source code
 */
struct SourceLocation {
  std::string source; ///< Filename or "<repl>"
  size_t line;        ///< Line number
  size_t column;      ///< Column number

  /**
   * @brief Equality comparison operator for locations
   * @param other The location to compare with
   * @return True if locations are equal, false otherwise
   */
  bool operator==(const SourceLocation &other) const {
    return source == other.source && line == other.line &&
           column == other.column;
  }

  /**
   * @brief Less than operator for ordering locations
   * @param other The location to compare with
   * @return True if this location is less than the other, false otherwise
   */
  bool operator<(const SourceLocation &other) const {
    if (source != other.source)
      return source < other.source;
    if (line != other.line)
      return line < other.line;
    return column < other.column;
  }
};

/**
 * @struct Breakpoint
 * @brief Represents a debugging breakpoint
 */
struct Breakpoint {
  size_t id;                            ///< Breakpoint identifier
  SourceLocation location;              ///< Breakpoint location
  std::optional<std::string> condition; ///< Optional condition expression
  bool enabled;                         ///< Whether the breakpoint is enabled
  size_t hit_count; ///< Number of times the breakpoint was hit

  /**
   * @brief Constructor for creating breakpoints
   * @param id Breakpoint identifier
   * @param loc Source location for the breakpoint
   */
  Breakpoint(size_t id, SourceLocation loc)
      : id(id), location(std::move(loc)), enabled(true), hit_count(0) {}
};

/**
 * @enum DebuggerState
 * @brief Defines the possible states of the debugger
 */
enum class DebuggerState {
  Inactive,  ///< Debugger is not active
  Running,   ///< Code is executing normally
  Paused,    ///< Execution is paused at a breakpoint
  StepInto,  ///< Stepping into function calls
  StepOver,  ///< Stepping over function calls
  StepOut,   ///< Stepping out of current function
  Terminated ///< Debugging session has ended
};

/**
 * @class ScriptDebugger
 * @brief Script debugger supporting breakpoints, stepping and variable
 * inspection
 *
 * Provides comprehensive debugging capabilities for the script interpreter,
 * including breakpoint management, execution control, variable inspection,
 * and call stack tracking.
 */
class ScriptDebugger {
public:
  /**
   * @brief Constructor for the script debugger
   * @param interpreter Reference to the script interpreter being debugged
   */
  explicit ScriptDebugger(ScriptInterpreter &interpreter);

  /**
   * @brief Destructor for the script debugger
   */
  ~ScriptDebugger();

  // Prevent copy and move
  ScriptDebugger(const ScriptDebugger &) = delete;
  ScriptDebugger &operator=(const ScriptDebugger &) = delete;
  ScriptDebugger(ScriptDebugger &&) = delete;
  ScriptDebugger &operator=(ScriptDebugger &&) = delete;

  // Breakpoint management
  /**
   * @brief Adds a breakpoint at specified location
   * @param source Source file name or "<repl>" for interactive input
   * @param line Line number for the breakpoint
   * @return ID of the created breakpoint
   */
  size_t add_breakpoint(const std::string &source, size_t line);

  /**
   * @brief Adds a conditional breakpoint
   * @param source Source file name or "<repl>" for interactive input
   * @param line Line number for the breakpoint
   * @param condition Expression that must evaluate to true to trigger the
   * breakpoint
   * @return ID of the created breakpoint
   */
  size_t add_breakpoint(const std::string &source, size_t line,
                        const std::string &condition);

  /**
   * @brief Removes a breakpoint by ID
   * @param id Breakpoint identifier
   * @return True if breakpoint was found and removed, false otherwise
   */
  bool remove_breakpoint(size_t id);

  /**
   * @brief Enables or disables a breakpoint
   * @param id Breakpoint identifier
   * @param enabled Whether to enable (true) or disable (false) the breakpoint
   */
  void enable_breakpoint(size_t id, bool enabled = true);

  /**
   * @brief Removes all breakpoints
   */
  void clear_breakpoints();

  /**
   * @brief Gets all breakpoints
   * @return Vector of all breakpoints
   */
  std::vector<Breakpoint> get_breakpoints();

  // Execution control
  /**
   * @brief Starts the debugging session
   */
  void start();

  /**
   * @brief Pauses script execution
   */
  void pause();

  /**
   * @brief Resumes script execution
   */
  void resume();

  /**
   * @brief Steps into function calls during debugging
   */
  void step_into();

  /**
   * @brief Steps over function calls during debugging
   */
  void step_over();

  /**
   * @brief Steps out of current function during debugging
   */
  void step_out();

  /**
   * @brief Stops the debugging session
   */
  void stop();

  // Variable inspection
  /**
   * @brief Inspects a variable by name
   * @param name Name of the variable to inspect
   * @return Value of the variable
   */
  ScriptValue inspect_variable(const std::string &name);

  /**
   * @brief Gets all variables in the current local scope
   * @return Vector of variable name-value pairs
   */
  std::vector<std::pair<std::string, ScriptValue>> get_local_variables();

  /**
   * @brief Gets all variables in the global scope
   * @return Vector of variable name-value pairs
   */
  std::vector<std::pair<std::string, ScriptValue>> get_global_variables();

  /**
   * @brief Gets the current call stack
   * @return Vector of function names representing the call stack
   */
  std::vector<std::string> get_call_stack();

  // Callback management
  /**
   * @typedef BreakpointCallback
   * @brief Function signature for breakpoint hit callbacks
   */
  using BreakpointCallback = std::function<void(const Breakpoint &)>;

  /**
   * @typedef StateChangeCallback
   * @brief Function signature for debugger state change callbacks
   */
  using StateChangeCallback = std::function<void(DebuggerState)>;

  /**
   * @brief Sets callback for breakpoint hit events
   * @param callback Function to call when a breakpoint is hit
   */
  void set_breakpoint_hit_callback(BreakpointCallback callback);

  /**
   * @brief Sets callback for debugger state changes
   * @param callback Function to call when debugger state changes
   */
  void set_state_change_callback(StateChangeCallback callback);

  // State queries
  /**
   * @brief Gets the current debugger state
   * @return Current state of the debugger
   */
  DebuggerState get_state();

  /**
   * @brief Gets the current execution location
   * @return Current source location
   */
  SourceLocation get_current_location();

  // Internal methods (called by the interpreter)
  /**
   * @brief Notifies the debugger of a location change
   * @param location New source location
   */
  void notify_location(const SourceLocation &location);

  /**
   * @brief Notifies the debugger when entering a function
   * @param name Name of the function being entered
   */
  void notify_enter_function(const std::string &name);

  /**
   * @brief Notifies the debugger when exiting a function
   * @param name Name of the function being exited
   */
  void notify_exit_function(const std::string &name);

  /**
   * @brief Notifies the debugger of an exception
   * @param message Exception message
   */
  void notify_exception(const std::string &message);

  /**
   * @brief Notifies the debugger when a new scope is created
   * @param scope Pointer to the new scope
   */
  void notify_scope_created(ScriptScope *scope);

  /**
   * @brief Notifies the debugger when a scope is destroyed
   * @param scope Pointer to the scope being destroyed
   */
  void notify_scope_destroyed(ScriptScope *scope);

private:
  /**
   * @brief Checks if a breakpoint is triggered at the given location
   * @param location Location to check
   * @return True if a breakpoint is triggered, false otherwise
   */
  bool check_breakpoint(const SourceLocation &location);

  /**
   * @brief Determines if execution should pause at the given location
   * @param location Current location in the code
   * @return True if execution should pause, false otherwise
   */
  bool should_pause(const SourceLocation &location);

  /**
   * @brief Blocks execution until a debugging command is received
   */
  void wait_for_command();

private:
  ScriptInterpreter
      &interpreter_; ///< Reference to the interpreter being debugged
  std::map<size_t, Breakpoint>
      breakpoints_; ///< Map of breakpoint IDs to breakpoint objects
  std::vector<std::string> call_stack_;    ///< Current function call stack
  std::vector<ScriptScope *> scope_stack_; ///< Current scope stack

  SourceLocation current_location_; ///< Current execution location
  DebuggerState state_;             ///< Current debugger state
  size_t next_breakpoint_id_;       ///< ID to use for the next breakpoint
  size_t current_stack_depth_;      ///< Current function call depth

  // Debug thread synchronization
  std::mutex mutex_; ///< Mutex for thread synchronization
  std::condition_variable
      cv_; ///< Condition variable for thread synchronization
  std::atomic<bool> should_stop_; ///< Flag indicating if debugging should stop

  // Callbacks
  BreakpointCallback bp_callback_;     ///< Callback for breakpoint hit events
  StateChangeCallback state_callback_; ///< Callback for state change events
};

} // namespace shell