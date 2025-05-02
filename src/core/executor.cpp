#include "executor.hpp"
#include "../utils/color.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
// Windows specific headers
#include <direct.h> // for _chdir
#include <fcntl.h> // Ensure this header is included to resolve _O_BINARY undefined issue
#include <io.h>
#include <process.h>
#include <windows.h>

// Windows equivalents for POSIX macros and functions
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define dup _dup
#define dup2 _dup2
#define close _close
#define read _read
#define write _write
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_CREAT _O_CREAT
#define O_APPEND _O_APPEND
#define O_TRUNC _O_TRUNC
#define O_NONBLOCK 0 // Not directly supported
#define F_GETFL 0    // Not supported on Windows
#define F_SETFL 0    // Not supported on Windows

// Replacement for Unix process status macros
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0)
#define WTERMSIG(status) ((status) & 0x7f)

// Prototypes for Windows compatibility functions
inline int setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name) != nullptr)
    return 0;
  return _putenv_s(name, value);
}

inline int chdir(const char *path) { return _chdir(path); }

inline pid_t fork() {
  // Not supported directly, we'll implement a custom solution
  errno = ENOSYS;
  return -1;
}

inline int kill(pid_t pid, int sig) {
  HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (hProcess == NULL) {
    errno = ESRCH;
    return -1;
  }
  BOOL result = TerminateProcess(hProcess, 128 + sig);
  CloseHandle(hProcess);
  return (result) ? 0 : -1;
}

inline pid_t waitpid(pid_t pid, int *status, [[maybe_unused]] int options) {
  HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (hProcess == NULL) {
    errno = ESRCH;
    return -1;
  }

  DWORD result = WaitForSingleObject(hProcess, INFINITE);
  if (result == WAIT_FAILED) {
    CloseHandle(hProcess);
    errno = ECHILD;
    return -1;
  }

  DWORD exitCode;
  if (GetExitCodeProcess(hProcess, &exitCode)) {
    *status = exitCode;
  } else {
    *status = 0;
  }

  CloseHandle(hProcess);
  return pid;
}

inline int fcntl([[maybe_unused]] int fd, [[maybe_unused]] int cmd, ...) {
  // Simplified implementation: fcntl is not supported on Windows, return
  // success
  return 0;
}

#else
// Unix/POSIX specific headers
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace shell {

// Define process creation function for Windows environment
#ifdef _WIN32
bool create_process_windows(const std::string &cmd,
                            const std::vector<std::string> &args,
                            const Environment &env, HANDLE hStdIn,
                            HANDLE hStdOut, HANDLE hStdErr,
                            PROCESS_INFORMATION &procInfo) {
  // Build command line
  std::string cmdLine = cmd;
  for (size_t i = 1; i < args.size(); ++i) {
    cmdLine += " " + args[i];
  }

  // Set process startup information - fix initialization issue
  STARTUPINFOA startInfo;
  ZeroMemory(&startInfo, sizeof(STARTUPINFOA));
  startInfo.cb = sizeof(STARTUPINFOA);
  startInfo.dwFlags = STARTF_USESTDHANDLES;
  startInfo.hStdInput = hStdIn;
  startInfo.hStdOutput = hStdOut;
  startInfo.hStdError = hStdErr;

  // Create process
  ZeroMemory(&procInfo, sizeof(PROCESS_INFORMATION));
  if (!CreateProcessA(
          NULL,                                // Application name
          const_cast<char *>(cmdLine.c_str()), // Command line
          NULL,                                // Process security attributes
          NULL,                                // Thread security attributes
          TRUE,                                // Inherit handles
          0,                                   // Creation flags
          NULL,                                // Environment block
          env.get_working_directory().string().c_str(), // Current directory
          &startInfo,                                   // Startup info
          &procInfo                                     // Process info
          )) {
    return false;
  }
  return true;
}
#endif

Executor::Executor(Environment &env) : env_(env), interrupt_requested_(false) {}

ExecutionResult Executor::execute(const std::shared_ptr<ASTNode> &node,
                                  ExecutionContext ctx) {
  interrupt_requested_ = false;

  if (!node) {
    return {"", 0, true};
  }

  if (auto command = std::dynamic_pointer_cast<Command>(node)) {
    return execute_command(command, ctx);
  } else if (auto pipeline = std::dynamic_pointer_cast<Pipeline>(node)) {
    return execute_pipeline(pipeline, ctx);
  } else if (auto sequence = std::dynamic_pointer_cast<CommandSequence>(node)) {
    return execute_sequence(sequence, ctx);
  }

  return {"Error: Unknown node type", 1, false};
}

std::future<ExecutionResult>
Executor::execute_async(const std::shared_ptr<ASTNode> &node,
                        ExecutionContext ctx) {
  return std::async(std::launch::async,
                    [this, node, ctx]() { return execute(node, ctx); });
}

ExecutionResult
Executor::execute_script(const std::filesystem::path &script_path) {
  if (!std::filesystem::exists(script_path)) {
    return {"Error: Script file not found: " + script_path.string(), 1, false};
  }

  std::ifstream file(script_path);
  if (!file) {
    return {"Error: Failed to open script file: " + script_path.string(), 1,
            false};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  std::string script_content = buffer.str();
  std::istringstream iss(script_content);

  std::string line;
  std::string output;
  int last_exit_code = 0;

  while (std::getline(iss, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // Tokenize and parse the line
    Tokenizer tokenizer;
    auto tokens = tokenizer.tokenize(line);

    if (!tokenizer.validate_syntax(tokens)) {
      output += "Syntax error: " + tokenizer.get_error_message() + "\n";
      last_exit_code = 2;
      break;
    }

    Parser parser;
    auto ast = parser.parse(tokens);

    if (!ast) {
      output += "Parse error: " + parser.get_error_message() + "\n";
      last_exit_code = 2;
      break;
    }

    // Execute the AST
    ExecutionContext ctx;
    auto result = execute(ast, ctx);

    last_exit_code = result.exit_code;

    if (!result.output.empty()) {
      output += result.output + "\n";
    }

    // Stop execution if last command failed and we're using set -e
    auto errexit = env_.get_variable("errexit");
    if (errexit && *errexit == "true" && last_exit_code != 0) {
      output += "Script execution stopped due to error\n";
      break;
    }
  }

  return {output, last_exit_code, last_exit_code == 0};
}

ExecutionResult
Executor::execute_command(const std::shared_ptr<Command> &command,
                          ExecutionContext ctx) {
  // Check for interruption
  if (interrupt_requested_) {
    return {"Command execution interrupted", 130, false};
  }

  // Get command name with variable expansion
  std::string cmd_name = env_.expand_variables(command->name());

  // Check for alias
  auto alias = env_.resolve_alias(cmd_name);
  if (alias) {
    // Tokenize and parse the alias
    Tokenizer tokenizer;
    Parser parser;

    auto tokens = tokenizer.tokenize(*alias);
    auto ast = parser.parse(tokens);

    if (!ast) {
      return {"Error: Invalid alias definition: " + parser.get_error_message(),
              1, false};
    }

    // Execute the alias
    return execute(ast, ctx);
  }

  // Check for built-in command
  if (env_.has_command(cmd_name)) {
    auto cmd_func = env_.get_command(cmd_name);

    // Expand variables in arguments
    std::vector<std::string> expanded_args;
    expanded_args.reserve(command->args().size());

    for (const auto &arg : command->args()) {
      expanded_args.push_back(env_.expand_variables(arg));
    }

    // Setup redirection
    RedirectionState redir_state;

    if (command->input_redirect() || command->output_redirect()) {
      redir_state = setup_redirection(command);
    }

    // Execute the command
    std::string result;
    int exit_code = 0;

    try {
      // Create a span of args
      std::span<const std::string> args_span(expanded_args);
      result = cmd_func(args_span, env_);
    } catch (const std::exception &e) {
      result = "Error executing command: " + std::string(e.what());
      exit_code = 1;
    }

    // Restore redirection
    if (command->input_redirect() || command->output_redirect()) {
      restore_redirection(redir_state);
    }

    return {result, exit_code, exit_code == 0};
  }

  // External command execution
  return execute_external_command(command, ctx);
}

ExecutionResult
Executor::execute_external_command(const std::shared_ptr<Command> &command,
                                   ExecutionContext ctx) {
  // Get command name and arguments
  std::string cmd_name = env_.expand_variables(command->name());
  std::vector<std::string> expanded_args;
  expanded_args.reserve(command->args().size());

  for (const auto &arg : command->args()) {
    expanded_args.push_back(env_.expand_variables(arg));
  }

#ifdef _WIN32
  // Windows implementation
  HANDLE hStdOutRead = NULL, hStdOutWrite = NULL;
  HANDLE hStdErrRead = NULL, hStdErrWrite = NULL;
  HANDLE hStdInRead = NULL, hStdInWrite = NULL;

  // Fix security attributes initialization
  SECURITY_ATTRIBUTES saAttr;
  ZeroMemory(&saAttr, sizeof(SECURITY_ATTRIBUTES));
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  // Create pipes
  if (ctx.capture_output) {
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0) ||
        !CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0)) {
      return {"Error: Failed to create pipes", 1, false};
    }

    // Ensure parent process handles are not inherited by the child process
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
  }

  // If there is input, create an input pipe
  if (ctx.input) {
    if (!CreatePipe(&hStdInRead, &hStdInWrite, &saAttr, 0)) {
      if (ctx.capture_output) {
        CloseHandle(hStdOutRead);
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrRead);
        CloseHandle(hStdErrWrite);
      }
      return {"Error: Failed to create input pipe", 1, false};
    }

    // Ensure parent process handle is not inherited by the child process
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

    // Write input data
    DWORD bytesWritten;
    WriteFile(hStdInWrite, ctx.input->c_str(), ctx.input->size(), &bytesWritten,
              NULL);
    CloseHandle(hStdInWrite); // Close the write end
  }

  // Create process
  PROCESS_INFORMATION procInfo = {};
  bool success = create_process_windows(
      cmd_name, expanded_args, env_,
      ctx.input ? hStdInRead : GetStdHandle(STD_INPUT_HANDLE),
      ctx.capture_output ? hStdOutWrite : GetStdHandle(STD_OUTPUT_HANDLE),
      ctx.capture_output ? hStdErrWrite : GetStdHandle(STD_ERROR_HANDLE),
      procInfo);

  // Close unnecessary handles
  if (ctx.input)
    CloseHandle(hStdInRead);
  if (ctx.capture_output) {
    CloseHandle(hStdOutWrite);
    CloseHandle(hStdErrWrite);
  }

  if (!success) {
    DWORD error = GetLastError();
    if (ctx.capture_output) {
      CloseHandle(hStdOutRead);
      CloseHandle(hStdErrRead);
    }
    return {"Error: Failed to create process: " + std::to_string(error), 1,
            false};
  }

  // Handle background execution
  if (command->is_background() || ctx.is_background) {
    if (ctx.capture_output) {
      CloseHandle(hStdOutRead);
      CloseHandle(hStdErrRead);
    }
    CloseHandle(procInfo.hThread);
    CloseHandle(procInfo.hProcess);
    return {"Running in background [" + std::to_string(procInfo.dwProcessId) +
                "]",
            0, true};
  }

  // Read output
  std::string stdout_output;
  std::string stderr_output;

  if (ctx.capture_output) {
    const int buffer_size = 4096;
    char buffer[buffer_size];
    DWORD bytesRead;
    bool stdout_done = false;
    bool stderr_done = false;

    while (!interrupt_requested_ && (!stdout_done || !stderr_done)) {
      // Read standard output
      if (!stdout_done) {
        if (ReadFile(hStdOutRead, buffer, buffer_size - 1, &bytesRead, NULL) &&
            bytesRead > 0) {
          buffer[bytesRead] = '\0';
          stdout_output += buffer;
        } else {
          stdout_done = true;
        }
      }

      // Read standard error
      if (!stderr_done) {
        if (ReadFile(hStdErrRead, buffer, buffer_size - 1, &bytesRead, NULL) &&
            bytesRead > 0) {
          buffer[bytesRead] = '\0';
          stderr_output += buffer;
        } else {
          stderr_done = true;
        }
      }

      if (!stdout_done || !stderr_done) {
        // Avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    CloseHandle(hStdOutRead);
    CloseHandle(hStdErrRead);
  }

  // Wait for process to end
  if (interrupt_requested_) {
    TerminateProcess(procInfo.hProcess, 1);
  }

  WaitForSingleObject(procInfo.hProcess, INFINITE);

  // Get exit code
  DWORD exitCode = 0;
  GetExitCodeProcess(procInfo.hProcess, &exitCode);

  // Clean up handles
  CloseHandle(procInfo.hThread);
  CloseHandle(procInfo.hProcess);

  // Merge output
  std::string output = stdout_output;
  if (!stderr_output.empty()) {
    if (!output.empty() && !output.ends_with("\n")) {
      output += "\n";
    }
    output += Color::bold + Color::red + stderr_output + Color::reset;
  }

  return {output, static_cast<int>(exitCode), exitCode == 0};

#else
  // Unix implementation remains the same
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};

  if (ctx.capture_output) {
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
      return {"Error: Failed to create pipes: " + std::string(strerror(errno)),
              1, false};
    }

    // Make the read ends non-blocking
    fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL) | O_NONBLOCK);
  }

  // Set up execvp arguments
  std::vector<char *> c_args;
  c_args.reserve(expanded_args.size() + 1);

  for (const auto &arg : expanded_args) {
    c_args.push_back(const_cast<char *>(arg.c_str()));
  }

  c_args.push_back(nullptr);

  // Fork child process
  pid_t pid = fork();

  if (pid == -1) {
    // Fork failed
    if (ctx.capture_output) {
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
    }

    return {"Error: Failed to fork process: " + std::string(strerror(errno)), 1,
            false};
  } else if (pid == 0) {
    // Child process

    // Set up redirection
    if (command->input_redirect()) {
      std::string expanded_input =
          env_.expand_variables(*command->input_redirect());
      int fd = open(expanded_input.c_str(), O_RDONLY);

      if (fd == -1) {
        std::cerr << "Error: Failed to open input file: " << strerror(errno)
                  << std::endl;
        exit(1);
      }

      dup2(fd, STDIN_FILENO);
      close(fd);
    } else if (ctx.input) {
      // Use provided input
      int input_pipe[2];

      if (pipe(input_pipe) == -1) {
        std::cerr << "Error: Failed to create input pipe: " << strerror(errno)
                  << std::endl;
        exit(1);
      }

      // Write input to the pipe
      write(input_pipe[1], ctx.input->c_str(), ctx.input->size());
      close(input_pipe[1]);

      // Redirect standard input
      dup2(input_pipe[0], STDIN_FILENO);
      close(input_pipe[0]);
    }

    if (command->output_redirect()) {
      std::string expanded_output =
          env_.expand_variables(*command->output_redirect());
      int flags = O_WRONLY | O_CREAT;

      if (command->is_append()) {
        flags |= O_APPEND;
      } else {
        flags |= O_TRUNC;
      }

      int fd = open(expanded_output.c_str(), flags, 0666);

      if (fd == -1) {
        std::cerr << "Error: Failed to open output file: " << strerror(errno)
                  << std::endl;
        exit(1);
      }

      dup2(fd, STDOUT_FILENO);
      close(fd);
    } else if (ctx.capture_output) {
      // Redirect standard output and standard error to pipes
      dup2(stdout_pipe[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);

      // Close pipe file descriptors
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
    }

    // Set environment variables
    for (const auto &[name, value] : env_.get_all_env_variables()) {
      setenv(name.c_str(), value.c_str(), 1);
    }

    // Change working directory
#ifdef _WIN32
    chdir(env_.get_working_directory().string().c_str());
#else
    chdir(env_.get_working_directory().c_str());
#endif

    // Execute command
    execvp(cmd_name.c_str(), c_args.data());

    // If execvp returns, it means an error occurred
    std::cerr << "Error: Failed to execute command: " << strerror(errno)
              << std::endl;
    exit(127); // Command not found
  }

  // Parent process

  // Close pipe write ends
  if (ctx.capture_output) {
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
  }

  // If requested to run in background
  if (command->is_background() || ctx.is_background) {
    if (ctx.capture_output) {
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
    }

    return {"Running in background [" + std::to_string(pid) + "]", 0, true};
  }

  // Read output from pipes
  std::string stdout_output;
  std::string stderr_output;

  if (ctx.capture_output) {
    const int buffer_size = 4096;
    char buffer[buffer_size];

    // Loop reading to avoid blocking
    bool stdout_done = false;
    bool stderr_done = false;

    while (!interrupt_requested_ && (!stdout_done || !stderr_done)) {
      // Check standard output
      if (!stdout_done) {
        ssize_t bytes_read = read(stdout_pipe[0], buffer, buffer_size - 1);

        if (bytes_read > 0) {
          buffer[bytes_read] = '\0';
          stdout_output += buffer;
        } else if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN)) {
          stdout_done = true;
        }
      }

      // Check standard error
      if (!stderr_done) {
        ssize_t bytes_read = read(stderr_pipe[0], buffer, buffer_size - 1);

        if (bytes_read > 0) {
          buffer[bytes_read] = '\0';
          stderr_output += buffer;
        } else if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN)) {
          stderr_done = true;
        }
      }

      if (!stdout_done || !stderr_done) {
        // Sleep a bit to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // Close read ends of pipes
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
  }

  // Wait for child process
  int status;
  pid_t wait_result;

  if (interrupt_requested_) {
    // Send interrupt signal to child
    kill(pid, SIGINT);
  }

  do {
    wait_result = waitpid(pid, &status, 0);
  } while (wait_result == -1 && errno == EINTR);

  if (wait_result == -1) {
    return {stderr_output + "\nError: Failed to wait for process: " +
                std::string(strerror(errno)),
            1, false};
  }

  int exit_code;

  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exit_code = 128 + WTERMSIG(status);
  } else {
    exit_code = 1;
  }

  // Combine stdout and stderr
  std::string output = stdout_output;
  if (!stderr_output.empty()) {
    if (!output.empty() && !output.ends_with("\n")) {
      output += "\n";
    }
    output += Color::bold + Color::red + stderr_output + Color::reset;
  }

  return {output, exit_code, exit_code == 0};
#endif
}

ExecutionResult
Executor::execute_pipeline(const std::shared_ptr<Pipeline> &pipeline,
                           ExecutionContext ctx) {
  const auto &commands = pipeline->commands();

  if (commands.empty()) {
    return {"", 0, true};
  }

  if (commands.size() == 1) {
    // Single command - no need for pipes
    auto result = execute_command(commands[0], ctx);

    // If running in background, propagate that
    if (pipeline->is_background()) {
      ctx.is_background = true;
    }

    return result;
  }

  // Create pipes for the pipeline
  std::vector<int> pipes = create_pipeline_pipes(commands.size());

  if (pipes.empty()) {
    return {"Error: Failed to create pipes: " + std::string(strerror(errno)), 1,
            false};
  }

  // Vectors to store child PIDs and output
  std::vector<pid_t> pids;
  pids.reserve(commands.size());

  std::string final_output;
  int final_exit_code = 0;

  // Create a pipe for capturing final output
  int final_pipe[2] = {-1, -1};

  if (pipe(final_pipe) == -1) {
    close_pipeline_pipes(pipes);

    return {"Error: Failed to create final pipe: " +
                std::string(strerror(errno)),
            1, false};
  }

  // Fork processes for each command in the pipeline
  for (size_t i = 0; i < commands.size(); ++i) {
    pid_t pid = fork();

    if (pid == -1) {
      // Fork failed
      for (pid_t child_pid : pids) {
        kill(child_pid, SIGTERM);
      }

      close_pipeline_pipes(pipes);
      close(final_pipe[0]);
      close(final_pipe[1]);

      return {"Error: Failed to fork process: " + std::string(strerror(errno)),
              1, false};
    } else if (pid == 0) {
      // Child process

      // Setup stdin from previous command's output
      if (i > 0) {
        dup2(pipes[(i - 1) * 2], STDIN_FILENO);
      } else if (ctx.input) {
        // Use provided input for first command
        int input_pipe[2];

        if (pipe(input_pipe) == -1) {
          std::cerr << "Error: Failed to create input pipe: " << strerror(errno)
                    << std::endl;
          exit(1);
        }

        // Write input to pipe
        write(input_pipe[1], ctx.input->c_str(), ctx.input->size());
        close(input_pipe[1]);

        // Redirect stdin from pipe
        dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]);
      }

      // Setup stdout to next command's input or final output
      if (i < commands.size() - 1) {
        dup2(pipes[i * 2 + 1], STDOUT_FILENO);
      } else {
        // Last command outputs to final pipe
        dup2(final_pipe[1], STDOUT_FILENO);
      }

      // Close all pipe file descriptors
      close_pipeline_pipes(pipes);
      close(final_pipe[0]);
      close(final_pipe[1]);

      // Apply command-specific redirections
      RedirectionState redir_state;

      if (commands[i]->input_redirect() || commands[i]->output_redirect()) {
        // This will override pipeline stdin/stdout if specified
        redir_state = setup_redirection(commands[i]);
      }

      // Execute the command
      ExecutionContext cmd_ctx;
      cmd_ctx.capture_output = false; // We're using pipes

      std::string cmd_name = env_.expand_variables(commands[i]->name());

      // Check if it's a built-in command
      if (env_.has_command(cmd_name)) {
        auto cmd_func = env_.get_command(cmd_name);

        // Expand variables in arguments
        std::vector<std::string> expanded_args;
        expanded_args.reserve(commands[i]->args().size());

        for (const auto &arg : commands[i]->args()) {
          expanded_args.push_back(env_.expand_variables(arg));
        }

        // Execute the command and write result to stdout
        try {
          std::span<const std::string> args_span(expanded_args);
          std::string result = cmd_func(args_span, env_);

          if (!result.empty()) {
            std::cout << result;
          }

          exit(0);
        } catch (const std::exception &e) {
          std::cerr << "Error executing command: " << e.what() << std::endl;
          exit(1);
        }
      } else {
        // External command

        // Expand variables in arguments
        std::vector<std::string> expanded_args;
        expanded_args.reserve(commands[i]->args().size());

        for (const auto &arg : commands[i]->args()) {
          expanded_args.push_back(env_.expand_variables(arg));
        }

        // Set up arguments for execvp
        std::vector<char *> c_args;
        c_args.reserve(expanded_args.size() + 1);

        for (const auto &arg : expanded_args) {
          c_args.push_back(const_cast<char *>(arg.c_str()));
        }

        c_args.push_back(nullptr);

        // Set up environment variables
        for (const auto &[name, value] : env_.get_all_env_variables()) {
          setenv(name.c_str(), value.c_str(), 1);
        }

        // Change to working directory
#ifdef _WIN32
        chdir(env_.get_working_directory().string().c_str());
#else
        chdir(env_.get_working_directory().c_str());
#endif

        // Execute the command
        execvp(cmd_name.c_str(), c_args.data());

        // If execvp returns, there was an error
        std::cerr << "Error: Failed to execute command: " << strerror(errno)
                  << std::endl;
        exit(127); // Command not found
      }
    } else {
      // Parent process
      pids.push_back(pid);
    }
  }

  // Close all pipe file descriptors in parent
  close_pipeline_pipes(pipes);
  close(final_pipe[1]); // Close write end

  // Run in background if requested
  if (pipeline->is_background() || ctx.is_background) {
    close(final_pipe[0]);

    return {"Running pipeline in background [" + std::to_string(pids[0]) +
                " ... " + std::to_string(pids.back()) + "]",
            0, true};
  }

  // Read final output from last command
  std::string output;

  if (ctx.capture_output) {
    const int buffer_size = 4096;
    char buffer[buffer_size];

    while (!interrupt_requested_) {
      ssize_t bytes_read = read(final_pipe[0], buffer, buffer_size - 1);

      if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
      } else if (bytes_read <= 0) {
        break;
      }
    }
  }

  close(final_pipe[0]); // Close read end

  // Wait for all processes to complete
  for (size_t i = 0; i < pids.size(); ++i) {
    int status;

    if (interrupt_requested_) {
      // Send interrupt signal to child
      kill(pids[i], SIGINT);
    }

    waitpid(pids[i], &status, 0);

    // For pipeline, we care about the exit status of the last command
    if (i == pids.size() - 1) {
      if (WIFEXITED(status)) {
        final_exit_code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        final_exit_code = 128 + WTERMSIG(status);
      } else {
        final_exit_code = 1;
      }
    }
  }

  return {output, final_exit_code, final_exit_code == 0};
}

ExecutionResult
Executor::execute_sequence(const std::shared_ptr<CommandSequence> &sequence,
                           ExecutionContext ctx) {
  const auto &elements = sequence->elements();

  if (elements.empty()) {
    return {"", 0, true};
  }

  std::string output;
  int exit_code = 0;
  bool success = true;

  for (size_t i = 0; i < elements.size(); ++i) {
    const auto &element = elements[i];

    // If this is not the first command and uses && separator,
    // check if previous command succeeded
    if (i > 0 && elements[i - 1].separator) {
      if (*elements[i - 1].separator == CommandSequence::Separator::And &&
          !success) {
        // Skip this command if previous failed and we're using &&
        continue;
      }
    }

    // Execute the command
    auto result = execute(element.node, ctx);

    // Append output if not empty
    if (!result.output.empty()) {
      if (!output.empty() && !output.ends_with("\n")) {
        output += "\n";
      }
      output += result.output;
    }

    // Update exit code and success
    exit_code = result.exit_code;
    success = result.success;

    // Stop on interrupt
    if (interrupt_requested_) {
      break;
    }
  }

  return {output, exit_code, success};
}

Executor::RedirectionState
Executor::setup_redirection(const std::shared_ptr<Command> &command) {
  RedirectionState state;

  // Handle input redirection
  if (command->input_redirect()) {
    // Save original stdin
    state.original_stdin = dup(STDIN_FILENO);

    // Open input file
    std::string expanded_input =
        env_.expand_variables(*command->input_redirect());
#ifdef _WIN32
    state.input_fd = _open(expanded_input.c_str(), _O_RDONLY);
#else
    state.input_fd = open(expanded_input.c_str(), O_RDONLY);
#endif

    if (state.input_fd == -1) {
      throw std::runtime_error("Failed to open input file: " +
                               std::string(strerror(errno)));
    }

    // Redirect stdin to input file
    if (dup2(state.input_fd, STDIN_FILENO) == -1) {
      close(state.input_fd);
      throw std::runtime_error("Failed to redirect stdin: " +
                               std::string(strerror(errno)));
    }
  }

  // Handle output redirection
  if (command->output_redirect()) {
    // Save original stdout
    state.original_stdout = dup(STDOUT_FILENO);

    // Open output file
    std::string expanded_output =
        env_.expand_variables(*command->output_redirect());

#ifdef _WIN32
    int flags = _O_WRONLY | _O_CREAT;
    if (command->is_append()) {
      flags |= _O_APPEND;
    } else {
      flags |= _O_TRUNC;
    }
    state.output_fd = _open(expanded_output.c_str(), flags, 0666);
#else
    int flags = O_WRONLY | O_CREAT;
    if (command->is_append()) {
      flags |= O_APPEND;
    } else {
      flags |= O_TRUNC;
    }
    state.output_fd = open(expanded_output.c_str(), flags, 0666);
#endif

    if (state.output_fd == -1) {
      if (state.original_stdin != -1) {
        dup2(state.original_stdin, STDIN_FILENO);
        close(state.original_stdin);
      }
      if (state.input_fd != -1) {
        close(state.input_fd);
      }

      throw std::runtime_error("Failed to open output file: " +
                               std::string(strerror(errno)));
    }

    // Redirect stdout to output file
    if (dup2(state.output_fd, STDOUT_FILENO) == -1) {
      if (state.original_stdin != -1) {
        dup2(state.original_stdin, STDIN_FILENO);
        close(state.original_stdin);
      }
      if (state.input_fd != -1) {
        close(state.input_fd);
      }
      close(state.output_fd);

      throw std::runtime_error("Failed to redirect stdout: " +
                               std::string(strerror(errno)));
    }
  }

  return state;
}

void Executor::restore_redirection(RedirectionState &state) {
  // Restore stdin
  if (state.original_stdin != -1) {
    dup2(state.original_stdin, STDIN_FILENO);
    close(state.original_stdin);
    state.original_stdin = -1;
  }

  // Close input file
  if (state.input_fd != -1) {
    close(state.input_fd);
    state.input_fd = -1;
  }

  // Restore stdout
  if (state.original_stdout != -1) {
    dup2(state.original_stdout, STDOUT_FILENO);
    close(state.original_stdout);
    state.original_stdout = -1;
  }

  // Close output file
  if (state.output_fd != -1) {
    close(state.output_fd);
    state.output_fd = -1;
  }
}

std::vector<int> Executor::create_pipeline_pipes(size_t command_count) {
  if (command_count <= 1) {
    return {};
  }

  // Need (command_count - 1) pipes, each with 2 file descriptors
  std::vector<int> pipes((command_count - 1) * 2);

  for (size_t i = 0; i < command_count - 1; ++i) {
#ifdef _WIN32
    // Ensure _O_BINARY is defined
    if (_pipe(&pipes[i * 2], 4096, _O_BINARY) == -1) {
#else
    if (pipe(&pipes[i * 2]) == -1) {
#endif
      // Close already created pipes
      for (size_t j = 0; j < i * 2; ++j) {
        close(pipes[j]);
      }

      return {};
    }
  }

  return pipes;
}

void Executor::close_pipeline_pipes(std::vector<int> &pipes) {
  for (int fd : pipes) {
    if (fd != -1) {
      close(fd);
    }
  }

  pipes.clear();
}

void Executor::interrupt() { interrupt_requested_ = true; }

} // namespace shell