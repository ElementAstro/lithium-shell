#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "environment.hpp"
#include "parser.hpp"

namespace shell {

/**
 * @struct ExecutionResult
 * @brief Result of command execution
 *
 * This structure encapsulates the output, exit code and success status
 * of a command that has been executed by the shell.
 */
struct ExecutionResult {
  std::string output; ///< Standard output and error from the command
  int exit_code; ///< Exit code returned by the command (0 typically indicates
                 ///< success)
  bool success;  ///< Flag indicating whether the command executed successfully
};

/**
 * @struct ExecutionContext
 * @brief Context for command execution
 *
 * Defines the execution environment for a command, including input stream,
 * background execution flag, and output capture settings.
 */
struct ExecutionContext {
  std::optional<std::string>
      input;                  ///< Optional input to provide to the command
  bool is_background = false; ///< Whether to run the command in the background
  bool capture_output = true; ///< Whether to capture command output
};

/**
 * @class Executor
 * @brief Executes parsed commands
 *
 * The Executor class is responsible for executing abstract syntax tree nodes
 * produced by the parser, handling command execution, pipelines, redirection,
 * and both synchronous and asynchronous operation modes.
 */
class Executor {
public:
  /**
   * @brief Constructs an executor with an environment
   * @param env Reference to the environment for execution
   */
  Executor(Environment &env);

  /**
   * @brief Execute an AST node synchronously
   * @param node Shared pointer to the AST node to execute
   * @param ctx Execution context defining how to execute the command
   * @return Result of the execution including output and exit code
   */
  ExecutionResult execute(const std::shared_ptr<ASTNode> &node,
                          ExecutionContext ctx = {});

  /**
   * @brief Execute a command asynchronously
   * @param node Shared pointer to the AST node to execute
   * @param ctx Execution context defining how to execute the command
   * @return Future containing the execution result
   */
  std::future<ExecutionResult>
  execute_async(const std::shared_ptr<ASTNode> &node,
                ExecutionContext ctx = {});

  /**
   * @brief Execute a shell script
   * @param script_path Path to the script file to execute
   * @return Result of the script execution
   */
  ExecutionResult execute_script(const std::filesystem::path &script_path);

  /**
   * @brief Interrupt current execution
   *
   * Signals the executor to stop the currently running command or pipeline.
   */
  void interrupt();

private:
  /**
   * @brief Execute a single command
   * @param command Shared pointer to the command to execute
   * @param ctx Execution context defining how to execute the command
   * @return Result of the command execution
   */
  ExecutionResult execute_command(const std::shared_ptr<Command> &command,
                                  ExecutionContext ctx);

  /**
   * @brief Execute a pipeline of commands
   * @param pipeline Shared pointer to the pipeline to execute
   * @param ctx Execution context defining how to execute the pipeline
   * @return Result of the pipeline execution
   */
  ExecutionResult execute_pipeline(const std::shared_ptr<Pipeline> &pipeline,
                                   ExecutionContext ctx);

  /**
   * @brief Execute a sequence of commands
   * @param sequence Shared pointer to the command sequence to execute
   * @param ctx Execution context defining how to execute the sequence
   * @return Result of the sequence execution
   */
  ExecutionResult
  execute_sequence(const std::shared_ptr<CommandSequence> &sequence,
                   ExecutionContext ctx);

  /**
   * @struct RedirectionState
   * @brief Stores file descriptors for I/O redirection
   *
   * This structure maintains the original and redirected file descriptors
   * for standard input and output during command execution.
   */
  struct RedirectionState {
    int original_stdin = -1;  ///< Original standard input file descriptor
    int original_stdout = -1; ///< Original standard output file descriptor
    int input_fd = -1;        ///< Input redirection file descriptor
    int output_fd = -1;       ///< Output redirection file descriptor
  };

  /**
   * @brief Set up I/O redirection for a command
   * @param command Shared pointer to the command requiring redirection
   * @return RedirectionState containing the file descriptors
   */
  RedirectionState setup_redirection(const std::shared_ptr<Command> &command);

  /**
   * @brief Restore original file descriptors after redirection
   * @param state RedirectionState with file descriptors to restore
   */
  void restore_redirection(RedirectionState &state);

  /**
   * @brief Create pipes for a pipeline of commands
   * @param command_count Number of commands in the pipeline
   * @return Vector of file descriptors for the created pipes
   */
  std::vector<int> create_pipeline_pipes(size_t command_count);

  /**
   * @brief Close all file descriptors in a pipeline
   * @param pipes Vector of pipe file descriptors to close
   */
  void close_pipeline_pipes(std::vector<int> &pipes);

  /**
   * @brief Execute an external system command
   * @param command Shared pointer to the command to execute
   * @param ctx Execution context defining how to execute the command
   * @return Result of the command execution
   */
  ExecutionResult
  execute_external_command(const std::shared_ptr<Command> &command,
                           ExecutionContext ctx);

  Environment &env_;
  std::atomic<bool> interrupt_requested_ = false;
};

} // namespace shell