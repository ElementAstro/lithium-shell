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
 */
struct ExecutionResult {
  std::string output;
  int exit_code;
  bool success;
};

/**
 * @struct ExecutionContext
 * @brief Context for command execution
 */
struct ExecutionContext {
  std::optional<std::string> input;
  bool is_background = false;
  bool capture_output = true;
};

/**
 * @class Executor
 * @brief Executes parsed commands
 */
class Executor {
public:
  Executor(Environment &env);

  // Execute an AST node synchronously
  ExecutionResult execute(const std::shared_ptr<ASTNode> &node,
                          ExecutionContext ctx = {});

  // Execute a command asynchronously
  std::future<ExecutionResult>
  execute_async(const std::shared_ptr<ASTNode> &node,
                ExecutionContext ctx = {});

  // Execute a shell script
  ExecutionResult execute_script(const std::filesystem::path &script_path);

  // Interrupt current execution
  void interrupt();

private:
  // Execute different node types
  ExecutionResult execute_command(const std::shared_ptr<Command> &command,
                                  ExecutionContext ctx);
  ExecutionResult execute_pipeline(const std::shared_ptr<Pipeline> &pipeline,
                                   ExecutionContext ctx);
  ExecutionResult
  execute_sequence(const std::shared_ptr<CommandSequence> &sequence,
                   ExecutionContext ctx);

  // Handle I/O redirection
  struct RedirectionState {
    int original_stdin = -1;
    int original_stdout = -1;
    int input_fd = -1;
    int output_fd = -1;
  };

  RedirectionState setup_redirection(const std::shared_ptr<Command> &command);
  void restore_redirection(RedirectionState &state);

  // Create pipes for command pipeline
  std::vector<int> create_pipeline_pipes(size_t command_count);
  void close_pipeline_pipes(std::vector<int> &pipes);

  // Execute external command
  ExecutionResult
  execute_external_command(const std::shared_ptr<Command> &command,
                           ExecutionContext ctx);

  Environment &env_;
  std::atomic<bool> interrupt_requested_ = false;
};

} // namespace shell