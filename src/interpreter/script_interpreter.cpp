#include "script_interpreter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/environment.hpp"
#include "../utils/color.hpp"
#include "script_debugger.hpp"
#include "script_value.hpp"

namespace shell {

namespace {
// 解释器实例，用于readline补全
ScriptInterpreter *current_interpreter = nullptr;

// readline补全回调
char **script_completer(const char *text, [[maybe_unused]] int start,
                        [[maybe_unused]] int end) {
  if (current_interpreter) {
    // 基于当前解释器状态提供补全
    std::vector<std::string> completions =
        current_interpreter->complete(std::string(text));

    if (completions.empty()) {
      return nullptr;
    }

    // 将补全结果转换为readline需要的格式
    char **result =
        static_cast<char **>(malloc((completions.size() + 1) * sizeof(char *)));

    for (size_t i = 0; i < completions.size(); ++i) {
      result[i] = strdup(completions[i].c_str());
    }
    result[completions.size()] = nullptr;

    return result;
  }

  return nullptr;
}
} // namespace

// ScriptError实现
std::string ScriptError::format_error() const {
  std::stringstream ss;

  // 添加错误级别和消息
  switch (level) {
  case ScriptErrorLevel::Info:
    ss << "\033[1;34m信息\033[0m: ";
    break;
  case ScriptErrorLevel::Warning:
    ss << "\033[1;33m警告\033[0m: ";
    break;
  case ScriptErrorLevel::Error:
    ss << "\033[1;31m错误\033[0m: ";
    break;
  case ScriptErrorLevel::Fatal:
    ss << "\033[1;41m严重错误\033[0m: ";
    break;
  }

  ss << message << std::endl;

  // 添加源代码位置信息
  if (!source.empty()) {
    ss << "位置: " << source;
    if (line > 0) {
      ss << ":" << line;
      if (column > 0) {
        ss << ":" << column;
      }
    }
    ss << std::endl;
  }

  // 添加代码片段（如果有）
  if (!code_snippet.empty()) {
    ss << "\n代码: \033[1m" << code_snippet << "\033[0m\n";

    // 为出错位置添加指示符
    if (column > 0) {
      ss << std::string(7 + column - 1, ' ') << "\033[1;32m^\033[0m\n";
    }
  }

  // 添加修复建议（如果有）
  if (!suggestion.empty()) {
    ss << "\n建议: \033[1;32m" << suggestion << "\033[0m\n";
  }

  return ss.str();
}

// ScriptInterpreter实现
ScriptInterpreter::ScriptInterpreter(Environment &env)
    : env_(env), running_(false) {
  // 创建全局作用域
  global_scope_ = std::make_unique<ScriptScope>();

  // 创建调试器
  debugger_ = std::make_unique<ScriptDebugger>(*this);

  // 注册标准库函数
  register_stdlib();

  // 设置readline补全
  current_interpreter = this;
  rl_attempted_completion_function = script_completer;

  spdlog::info("ScriptInterpreter instance created.");
}

ScriptInterpreter::~ScriptInterpreter() {
  // 确保解释器停止运行
  running_ = false;
  spdlog::info("ScriptInterpreter instance destroyed.");
}

void ScriptInterpreter::initialize() {
  spdlog::debug("Initializing script interpreter...");

  // 创建全局作用域
  global_scope_ = std::make_unique<ScriptScope>();

  // 创建调试器
  debugger_ = std::make_unique<ScriptDebugger>(*this);

  // 注册标准库函数
  register_stdlib();

  // 设置readline补全
  current_interpreter = this;
  rl_attempted_completion_function = script_completer;

  spdlog::debug("Script interpreter initialized successfully.");
}

void ScriptInterpreter::register_stdlib() {
  spdlog::debug("Registering standard library functions...");

  // 打印功能
  register_native_function(
      "print", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        for (size_t i = 0; i < args.size(); ++i) {
          if (i > 0)
            std::cout << " ";
          std::cout << args[i].to_debug_string();
        }
        std::cout << std::endl;
        return ScriptValue(nullptr);
      });

  // 类型检查功能
  register_native_function(
      "typeof", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        if (args.empty()) {
          return ScriptValue("undefined");
        }

        const auto &value = args[0];
        if (value.is_null())
          return ScriptValue("null");
        if (value.is_bool())
          return ScriptValue("boolean");
        if (value.is_number())
          return ScriptValue("number");
        if (value.is_string())
          return ScriptValue("string");
        if (value.is_array())
          return ScriptValue("array");
        if (value.is_object())
          return ScriptValue("object");
        if (value.is_function())
          return ScriptValue("function");

        return ScriptValue("unknown");
      });

  // 数学函数
  register_native_function(
      "abs", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        if (args.empty() || !args[0].is_number()) {
          return ScriptValue(nullptr);
        }
        return ScriptValue(std::abs(args[0].as_number()));
      });

  register_native_function(
      "sqrt", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        if (args.empty() || !args[0].is_number()) {
          return ScriptValue(nullptr);
        }
        return ScriptValue(std::sqrt(args[0].as_number()));
      });

  // 字符串函数
  register_native_function(
      "length", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        if (args.empty()) {
          return ScriptValue(0);
        }

        if (args[0].is_string()) {
          return ScriptValue(static_cast<double>(args[0].as_string().length()));
        } else if (args[0].is_array()) {
          return ScriptValue(static_cast<double>(args[0].as_array().size()));
        }

        return ScriptValue(0);
      });

  // 时间函数
  register_native_function(
      "now",
      []([[maybe_unused]] const std::vector<ScriptValue> &args) -> ScriptValue {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto value = ms.time_since_epoch().count();
        return ScriptValue(static_cast<double>(value));
      });

  // 系统函数
  register_native_function(
      "sleep", [](const std::vector<ScriptValue> &args) -> ScriptValue {
        if (args.empty() || !args[0].is_number()) {
          return ScriptValue(nullptr);
        }

        auto ms = static_cast<unsigned int>(args[0].as_number());
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return ScriptValue(nullptr);
      });

  spdlog::debug("Standard library functions registered successfully.");
}

void ScriptInterpreter::register_native_function(
    const std::string &name,
    std::function<ScriptValue(const std::vector<ScriptValue> &)> func) {

  spdlog::debug("Registering native function: '{}'", name);
  auto script_func = std::make_shared<ScriptFunction>(std::move(func), name);
  global_scope_->define_function(name, script_func);
}

std::variant<ScriptValue, ScriptError>
ScriptInterpreter::eval(const std::string &code) {
  spdlog::debug("Evaluating script code: '{}'", code);

  // 简单的表达式评估逻辑
  try {
    // 在这里添加实际代码评估逻辑
    // 这是一个简化版本，实际解释器需要实现完整的词法分析、语法分析和执行

    // TODO: 实现完整的脚本计算引擎

    // 临时的模拟实现，只支持简单的数值和字符串字面量
    std::string trimmed = code;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    // 简单的字面量解析
    if (trimmed.empty()) {
      return ScriptValue(nullptr); // 空代码返回null
    } else if (trimmed == "true") {
      return ScriptValue(true);
    } else if (trimmed == "false") {
      return ScriptValue(false);
    } else if (trimmed == "null" || trimmed == "nullptr") {
      return ScriptValue(nullptr);
    } else if (trimmed.front() == '"' && trimmed.back() == '"' &&
               trimmed.length() >= 2) {
      return ScriptValue(trimmed.substr(1, trimmed.length() - 2));
    } else if (trimmed.front() == '\'' && trimmed.back() == '\'' &&
               trimmed.length() >= 2) {
      return ScriptValue(trimmed.substr(1, trimmed.length() - 2));
    } else {
      // 尝试解析为数字
      try {
        return ScriptValue(std::stod(trimmed));
      } catch (...) {
        // 如果不是数字，则视为标识符或表达式
        // 这里需要实现完整的解析逻辑
        return ScriptError(ScriptErrorLevel::Error,
                           "未实现的表达式评估: " + trimmed, "<repl>", 1, 1);
      }
    }
  } catch (const std::exception &e) {
    return ScriptError(ScriptErrorLevel::Error,
                       std::string("评估过程中出现错误: ") + e.what(), "<repl>",
                       1, 1);
  }
}

std::variant<ScriptValue, ScriptError>
ScriptInterpreter::eval_multi(const std::string &code) {
  spdlog::debug("Evaluating multi-line script code, length: {}", code.length());

  // 将多行代码分解为单独的语句并逐条执行
  std::istringstream stream(code);
  std::string line;
  ScriptValue last_result;

  size_t line_number = 0;
  while (std::getline(stream, line)) {
    line_number++;

    // 跳过空行和注释
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") {
      continue;
    }

    auto result = eval(line);
    if (std::holds_alternative<ScriptError>(result)) {
      auto error = std::get<ScriptError>(result);
      // 更新错误的行号
      error.line = line_number;
      return error;
    } else {
      last_result = std::get<ScriptValue>(result);
    }
  }

  return last_result;
}

std::variant<ScriptValue, ScriptError>
ScriptInterpreter::load_and_eval(const std::filesystem::path &path) {

  spdlog::info("Loading and evaluating script from file: {}", path.string());

  if (!std::filesystem::exists(path)) {
    return ScriptError(ScriptErrorLevel::Error,
                       "脚本文件不存在: " + path.string(), path.string(), 0, 0);
  }

  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return ScriptError(ScriptErrorLevel::Error,
                         "无法打开脚本文件: " + path.string(), path.string(), 0,
                         0);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // 评估文件内容，将文件名作为源
    auto result = eval_multi(content);
    if (std::holds_alternative<ScriptError>(result)) {
      auto &error = std::get<ScriptError>(result);
      error.source = path.string();
    }

    return result;
  } catch (const std::exception &e) {
    return ScriptError(ScriptErrorLevel::Error,
                       std::string("加载或评估脚本时出错: ") + e.what(),
                       path.string(), 0, 0);
  }
}

void ScriptInterpreter::eval_async(
    const std::string &code,
    std::function<void(std::variant<ScriptValue, ScriptError>)> callback) {

  spdlog::debug("Starting async script evaluation, code length: {}",
                code.length());

  // 创建新线程执行脚本
  std::thread([this, code, callback = std::move(callback)]() {
    auto result = this->eval_multi(code);
    callback(result);
  }).detach();
}

void ScriptInterpreter::start_repl() {
  spdlog::info("Starting script REPL environment.");
  running_ = true;

  std::cout << Color::bold << Color::green << "Lithium Script 解释器 v0.1"
            << Color::reset << std::endl;
  std::cout << "输入 '.help' 获取帮助，输入 '.exit' 退出。" << std::endl;

  while (running_) {
    std::string input = read_line("script> ");

    // 处理空输入
    if (input.empty()) {
      continue;
    }

    // 处理REPL命令
    if (input[0] == '.') {
      handle_repl_command(input);
      continue;
    }

    // 添加到历史记录
    add_history(input.c_str());
    history_.push_back(input);

    // 评估输入并显示结果
    auto result = eval(input);
    if (std::holds_alternative<ScriptError>(result)) {
      std::cout << std::get<ScriptError>(result).format_error() << std::endl;
    } else {
      auto value = std::get<ScriptValue>(result);
      std::cout << Color::cyan << format_value(value) << Color::reset
                << std::endl;
    }
  }

  spdlog::info("Script REPL environment terminated.");
}

void ScriptInterpreter::handle_repl_command(const std::string &command) {
  spdlog::trace("Handling REPL command: '{}'", command);

  if (command == ".help" || command == ".h") {
    // 显示帮助信息
    std::cout << "可用的REPL命令:" << std::endl;
    std::cout << "  .help, .h        - 显示此帮助信息" << std::endl;
    std::cout << "  .exit, .quit, .q - 退出解释器" << std::endl;
    std::cout << "  .vars            - 显示所有变量" << std::endl;
    std::cout << "  .funcs           - 显示所有函数" << std::endl;
    std::cout << "  .clear           - 清除屏幕" << std::endl;
    std::cout << "  .reset           - 重置解释器状态" << std::endl;
    std::cout << "  .load <file>     - 加载并执行脚本文件" << std::endl;
    std::cout << "  .save <file>     - 保存当前会话到文件" << std::endl;
  } else if (command == ".exit" || command == ".quit" || command == ".q") {
    // 退出REPL
    running_ = false;
  } else if (command == ".vars") {
    // 显示所有变量
    std::cout << "全局变量:" << std::endl;
    // TODO: 实现变量列表
  } else if (command == ".funcs") {
    // 显示所有函数
    std::cout << "可用函数:" << std::endl;
    // TODO: 实现函数列表
  } else if (command == ".clear") {
    // 清除屏幕
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
  } else if (command == ".reset") {
    // 重置解释器状态
    initialize();
    std::cout << "解释器状态已重置." << std::endl;
  } else if (command.substr(0, 6) == ".load ") {
    // 加载脚本文件
    std::string file_path = command.substr(6);
    auto result = load_and_eval(file_path);
    if (std::holds_alternative<ScriptError>(result)) {
      std::cout << std::get<ScriptError>(result).format_error() << std::endl;
    } else {
      std::cout << "文件执行成功." << std::endl;
    }
  } else if (command.substr(0, 6) == ".save ") {
    // 保存会话到文件
    std::string file_path = command.substr(6);
    try {
      std::ofstream file(file_path);
      if (!file.is_open()) {
        std::cout << "无法打开文件进行写入: " << file_path << std::endl;
        return;
      }

      for (const auto &line : history_) {
        file << line << std::endl;
      }

      std::cout << "会话已保存到: " << file_path << std::endl;
    } catch (const std::exception &e) {
      std::cout << "保存会话时出错: " << e.what() << std::endl;
    }
  } else {
    std::cout << "未知命令: " << command << ". 输入 '.help' 获取帮助."
              << std::endl;
  }
}

std::string ScriptInterpreter::read_line(const std::string &prompt) {
  // 使用GNU readline读取用户输入
  char *line = readline(prompt.c_str());

  if (!line) {
    // EOF (Ctrl+D)
    running_ = false;
    return ".exit";
  }

  std::string result(line);
  free(line);

  return result;
}

void ScriptInterpreter::interrupt() {
  spdlog::info("Script execution interrupted.");
  running_ = false;
}

ScriptDebugger &ScriptInterpreter::get_debugger() { return *debugger_; }

std::string ScriptInterpreter::format_value(const ScriptValue &value) {
  return value.to_debug_string();
}

std::vector<std::string>
ScriptInterpreter::complete(const std::string &partial) {
  spdlog::trace("Generating completions for: '{}'", partial);
  std::vector<std::string> result;

  // 关键字补全
  std::vector<std::string> keywords = {
      "break",    "case",    "catch",      "class", "const",    "continue",
      "debugger", "default", "delete",     "do",    "else",     "export",
      "extends",  "false",   "finally",    "for",   "function", "if",
      "import",   "in",      "instanceof", "new",   "null",     "return",
      "super",    "switch",  "this",       "throw", "true",     "try",
      "typeof",   "var",     "void",       "while", "with",     "yield"};

  // 特殊的REPL命令补全
  std::vector<std::string> repl_commands = {
      ".help",  ".h",     ".exit",  ".quit", ".q",   ".vars",
      ".funcs", ".clear", ".reset", ".load", ".save"};

  // 命名空间内置函数补全
  std::vector<std::string> stdlib_functions = {
      "print", "typeof", "abs", "sqrt", "length", "now", "sleep"};

  // 全局变量补全（从全局作用域中获取）
  std::vector<std::string> variables;
  // TODO: 实现从全局作用域获取变量名

  // 将所有可能的补全合并到一个列表中
  std::vector<std::string> all_completions;
  all_completions.insert(all_completions.end(), keywords.begin(),
                         keywords.end());
  all_completions.insert(all_completions.end(), repl_commands.begin(),
                         repl_commands.end());
  all_completions.insert(all_completions.end(), stdlib_functions.begin(),
                         stdlib_functions.end());
  all_completions.insert(all_completions.end(), variables.begin(),
                         variables.end());

  // 过滤匹配的补全项
  for (const auto &completion : all_completions) {
    if (completion.substr(0, partial.length()) == partial) {
      result.push_back(completion);
    }
  }

  // 对结果进行排序
  std::sort(result.begin(), result.end());

  spdlog::trace("Generated {} completions", result.size());
  return result;
}

} // namespace shell