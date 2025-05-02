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

// 前向声明
class ScriptDebugger;
class ScriptValue;
class ScriptFunction;

/**
 * @enum ScriptErrorLevel
 * @brief 定义脚本错误的严重程度
 */
enum class ScriptErrorLevel {
  Info,    // 信息性消息
  Warning, // 警告，可能存在问题但不影响执行
  Error,   // 错误，阻止当前语句/表达式执行
  Fatal    // 严重错误，中止整个脚本/程序执行
};

/**
 * @struct ScriptError
 * @brief 表示脚本执行过程中的错误
 */
struct ScriptError {
  ScriptErrorLevel level;   // 错误级别
  std::string message;      // 错误消息
  std::string source;       // 错误来源(文件名或"<repl>")
  size_t line;              // 行号
  size_t column;            // 列号
  std::string code_snippet; // 代码片段
  std::string suggestion;   // 修复建议

  // 构造函数，方便创建错误对象
  ScriptError(ScriptErrorLevel lvl, std::string msg, std::string src = "<repl>",
              size_t ln = 0, size_t col = 0, std::string code = "",
              std::string suggest = "")
      : level(lvl), message(std::move(msg)), source(std::move(src)), line(ln),
        column(col), code_snippet(std::move(code)),
        suggestion(std::move(suggest)) {}

  // 格式化为可读错误消息
  std::string format_error() const;
};

/**
 * @class ScriptScope
 * @brief 表示脚本的作用域，管理变量和函数
 */
class ScriptScope {
public:
  ScriptScope(ScriptScope *parent = nullptr) : parent_(parent) {}

  // 变量操作
  void set_variable(const std::string &name, ScriptValue value);
  std::optional<ScriptValue> get_variable(const std::string &name) const;
  bool has_variable(const std::string &name) const;

  // 函数操作
  void define_function(const std::string &name,
                       std::shared_ptr<ScriptFunction> func);
  std::shared_ptr<ScriptFunction> get_function(const std::string &name) const;
  bool has_function(const std::string &name) const;

  // 作用域查询
  ScriptScope *get_parent() const { return parent_; }

private:
  ScriptScope *parent_;
  std::unordered_map<std::string, ScriptValue> variables_;
  std::unordered_map<std::string, std::shared_ptr<ScriptFunction>> functions_;
};

/**
 * @class ScriptInterpreter
 * @brief C++脚本解释器，提供解释执行C++脚本的能力
 *
 * 使用现代C++特性实现的脚本解释器，支持REPL环境、调试功能和异步执行。
 */
class ScriptInterpreter {
public:
  // 构造函数
  ScriptInterpreter(Environment &env);
  ~ScriptInterpreter();

  // 禁止复制和移动
  ScriptInterpreter(const ScriptInterpreter &) = delete;
  ScriptInterpreter &operator=(const ScriptInterpreter &) = delete;
  ScriptInterpreter(ScriptInterpreter &&) = delete;
  ScriptInterpreter &operator=(ScriptInterpreter &&) = delete;

  /**
   * @brief 评估单行脚本代码
   * @param code 要执行的代码
   * @return 执行结果或错误
   */
  std::variant<ScriptValue, ScriptError> eval(const std::string &code);

  /**
   * @brief 评估多行脚本代码
   * @param code 要执行的多行代码
   * @return 执行结果或错误
   */
  std::variant<ScriptValue, ScriptError> eval_multi(const std::string &code);

  /**
   * @brief 从文件中加载并执行脚本
   * @param path 脚本文件路径
   * @return 执行结果或错误
   */
  std::variant<ScriptValue, ScriptError>
  load_and_eval(const std::filesystem::path &path);

  /**
   * @brief 异步执行脚本代码
   * @param code 要执行的代码
   * @param callback 完成回调函数
   */
  void eval_async(
      const std::string &code,
      std::function<void(std::variant<ScriptValue, ScriptError>)> callback);

  /**
   * @brief 启动REPL环境
   */
  void start_repl();

  /**
   * @brief 中断当前执行
   */
  void interrupt();

  /**
   * @brief 获取调试器接口
   */
  ScriptDebugger &get_debugger();

  /**
   * @brief 获取当前环境
   */
  Environment &get_environment() { return env_; }

  void handle_repl_command(const std::string &command);

  /**
   * @brief 注册内置函数
   * @param name 函数名
   * @param func 函数实现
   */
  void register_native_function(
      const std::string &name,
      std::function<ScriptValue(const std::vector<ScriptValue> &)> func);

  std::vector<std::string> complete(const std::string &partial);

private:
  // 初始化解释器
  void initialize();

  // 注册标准库函数
  void register_stdlib();

  // 获取完成提示

  // 读取一行输入，支持历史和补全
  std::string read_line(const std::string &prompt);

  // 格式化脚本值为字符串
  std::string format_value(const ScriptValue &value);

private:
  Environment &env_;
  std::unique_ptr<ScriptScope> global_scope_;
  std::unique_ptr<ScriptDebugger> debugger_;
  std::atomic<bool> running_;
  std::vector<std::string> history_;
};

} // namespace shell