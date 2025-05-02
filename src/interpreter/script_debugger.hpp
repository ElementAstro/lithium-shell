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

// 前向声明
class ScriptInterpreter;
class ScriptScope;

/**
 * @struct SourceLocation
 * @brief 表示源代码中的位置
 */
struct SourceLocation {
  std::string source; // 文件名或"<repl>"
  size_t line;        // 行号
  size_t column;      // 列号

  // 比较运算符，用于排序和查找
  bool operator==(const SourceLocation &other) const {
    return source == other.source && line == other.line &&
           column == other.column;
  }

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
 * @brief 表示调试断点
 */
struct Breakpoint {
  size_t id;                            // 断点ID
  SourceLocation location;              // 断点位置
  std::optional<std::string> condition; // 条件表达式 (可选)
  bool enabled;                         // 是否启用
  size_t hit_count;                     // 命中次数

  Breakpoint(size_t id, SourceLocation loc)
      : id(id), location(std::move(loc)), enabled(true), hit_count(0) {}
};

/**
 * @enum DebuggerState
 * @brief 调试器状态
 */
enum class DebuggerState {
  Inactive,  // 未活动
  Running,   // 运行中
  Paused,    // 已暂停
  StepInto,  // 单步执行 (进入函数)
  StepOver,  // 单步执行 (跳过函数)
  StepOut,   // 单步执行 (跳出函数)
  Terminated // 已终止
};

/**
 * @class ScriptDebugger
 * @brief 脚本调试器，支持断点、单步执行和变量检查
 */
class ScriptDebugger {
public:
  explicit ScriptDebugger(ScriptInterpreter &interpreter);
  ~ScriptDebugger();

  // 禁止复制和移动
  ScriptDebugger(const ScriptDebugger &) = delete;
  ScriptDebugger &operator=(const ScriptDebugger &) = delete;
  ScriptDebugger(ScriptDebugger &&) = delete;
  ScriptDebugger &operator=(ScriptDebugger &&) = delete;

  // 设置断点
  size_t add_breakpoint(const std::string &source, size_t line);
  size_t add_breakpoint(const std::string &source, size_t line,
                        const std::string &condition);
  bool remove_breakpoint(size_t id);
  void enable_breakpoint(size_t id, bool enabled = true);
  void clear_breakpoints();
  std::vector<Breakpoint> get_breakpoints();

  // 调试控制
  void start();     // 启动调试
  void pause();     // 暂停执行
  void resume();    // 继续执行
  void step_into(); // 单步进入
  void step_over(); // 单步跳过
  void step_out();  // 单步跳出
  void stop();      // 停止调试

  // 变量检查
  ScriptValue inspect_variable(const std::string &name);
  std::vector<std::pair<std::string, ScriptValue>> get_local_variables();
  std::vector<std::pair<std::string, ScriptValue>> get_global_variables();
  std::vector<std::string> get_call_stack();

  // 设置回调
  using BreakpointCallback = std::function<void(const Breakpoint &)>;
  using StateChangeCallback = std::function<void(DebuggerState)>;

  void set_breakpoint_hit_callback(BreakpointCallback callback);
  void set_state_change_callback(StateChangeCallback callback);

  // 状态查询
  DebuggerState get_state();
  SourceLocation get_current_location();

  // 内部方法 (由解释器调用)
  void notify_location(const SourceLocation &location);
  void notify_enter_function(const std::string &name);
  void notify_exit_function(const std::string &name);
  void notify_exception(const std::string &message);
  void notify_scope_created(ScriptScope *scope);
  void notify_scope_destroyed(ScriptScope *scope);

private:
  // 检查断点是否触发
  bool check_breakpoint(const SourceLocation &location);

  // 检查是否应该暂停执行
  bool should_pause(const SourceLocation &location);

  // 等待调试命令
  void wait_for_command();

private:
  ScriptInterpreter &interpreter_;
  std::map<size_t, Breakpoint> breakpoints_;
  std::vector<std::string> call_stack_;
  std::vector<ScriptScope *> scope_stack_;

  SourceLocation current_location_;
  DebuggerState state_;
  size_t next_breakpoint_id_;
  size_t current_stack_depth_;

  // 调试线程同步
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> should_stop_;

  // 回调
  BreakpointCallback bp_callback_;
  StateChangeCallback state_callback_;
};

} // namespace shell