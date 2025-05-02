#include "script_debugger.hpp"

#include <algorithm>
#include <mutex>
#include <spdlog/spdlog.h>

#include "script_interpreter.hpp"
#include "script_value.hpp"

namespace shell {

ScriptDebugger::ScriptDebugger(ScriptInterpreter &interpreter)
    : interpreter_(interpreter), state_(DebuggerState::Inactive),
      next_breakpoint_id_(1), current_stack_depth_(0), should_stop_(false) {
  spdlog::debug("ScriptDebugger instance created.");
}

ScriptDebugger::~ScriptDebugger() {
  spdlog::debug("ScriptDebugger instance destroyed.");
  stop();
}

size_t ScriptDebugger::add_breakpoint(const std::string &source, size_t line) {
  std::lock_guard<std::mutex> lock(mutex_);
  SourceLocation location{source, line, 0};
  size_t id = next_breakpoint_id_++;
  breakpoints_[id] = Breakpoint(id, location);

  spdlog::debug("Added breakpoint #{} at {}:{}", id, source, line);
  return id;
}

size_t ScriptDebugger::add_breakpoint(const std::string &source, size_t line,
                                      const std::string &condition) {
  std::lock_guard<std::mutex> lock(mutex_);
  SourceLocation location{source, line, 0};
  size_t id = next_breakpoint_id_++;
  breakpoints_[id] = Breakpoint(id, location);
  breakpoints_[id].condition = condition;

  spdlog::debug("Added conditional breakpoint #{} at {}:{} with condition: {}",
                id, source, line, condition);
  return id;
}

bool ScriptDebugger::remove_breakpoint(size_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = breakpoints_.find(id);
  if (it != breakpoints_.end()) {
    spdlog::debug("Removed breakpoint #{}", id);
    breakpoints_.erase(it);
    return true;
  }

  spdlog::debug("Failed to remove breakpoint #{}: not found", id);
  return false;
}

void ScriptDebugger::enable_breakpoint(size_t id, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = breakpoints_.find(id);
  if (it != breakpoints_.end()) {
    it->second.enabled = enabled;
    spdlog::debug("Breakpoint #{} {}", id, enabled ? "enabled" : "disabled");
  }
}

void ScriptDebugger::clear_breakpoints() {
  std::lock_guard<std::mutex> lock(mutex_);
  breakpoints_.clear();
  spdlog::debug("All breakpoints cleared");
}

std::vector<Breakpoint> ScriptDebugger::get_breakpoints() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Breakpoint> result;
  for (const auto &[id, bp] : breakpoints_) {
    result.push_back(bp);
  }
  return result;
}

void ScriptDebugger::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == DebuggerState::Inactive ||
      state_ == DebuggerState::Terminated) {
    state_ = DebuggerState::Running;
    should_stop_ = false;
    spdlog::debug("Debugger started");

    // 通知状态改变
    if (state_callback_) {
      state_callback_(state_);
    }
  }
}

void ScriptDebugger::pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == DebuggerState::Running) {
    state_ = DebuggerState::Paused;
    spdlog::debug("Debugger paused");

    // 通知状态改变
    if (state_callback_) {
      state_callback_(state_);
    }
  }
}

void ScriptDebugger::resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == DebuggerState::Paused || state_ == DebuggerState::StepInto ||
        state_ == DebuggerState::StepOver || state_ == DebuggerState::StepOut) {
      state_ = DebuggerState::Running;
      spdlog::debug("Debugger resumed");

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 唤醒等待的线程
  cv_.notify_all();
}

void ScriptDebugger::step_into() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == DebuggerState::Paused) {
      state_ = DebuggerState::StepInto;
      spdlog::debug("Debugger stepping into");

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 唤醒等待的线程
  cv_.notify_all();
}

void ScriptDebugger::step_over() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == DebuggerState::Paused) {
      state_ = DebuggerState::StepOver;
      spdlog::debug("Debugger stepping over");

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 唤醒等待的线程
  cv_.notify_all();
}

void ScriptDebugger::step_out() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == DebuggerState::Paused) {
      state_ = DebuggerState::StepOut;
      spdlog::debug("Debugger stepping out");

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 唤醒等待的线程
  cv_.notify_all();
}

void ScriptDebugger::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DebuggerState::Inactive &&
        state_ != DebuggerState::Terminated) {
      state_ = DebuggerState::Terminated;
      should_stop_ = true;
      spdlog::debug("Debugger stopped");

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 唤醒等待的线程
  cv_.notify_all();
}

ScriptValue ScriptDebugger::inspect_variable(const std::string &name) {
  // 首先在当前作用域查找变量
  for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
    ScriptScope *scope = *it;
    if (scope->has_variable(name)) {
      return scope->get_variable(name).value();
    }
  }

  // 如果在局部作用域中没有找到，尝试在全局作用域中查找
  return ScriptValue(nullptr); // 变量不存在
}

std::vector<std::pair<std::string, ScriptValue>>
ScriptDebugger::get_local_variables() {
  std::vector<std::pair<std::string, ScriptValue>> result;

  // 获取当前作用域中的变量
  if (!scope_stack_.empty()) {
    // ScriptScope* current_scope = scope_stack_.back(); // Uncomment when
    // implementing local variable retrieval
    // 这里需要从ScriptScope类获取所有变量的方法，目前暂未实现
    // TODO: 实现获取所有局部变量
  }

  return result;
}

std::vector<std::pair<std::string, ScriptValue>>
ScriptDebugger::get_global_variables() {
  std::vector<std::pair<std::string, ScriptValue>> result;

  // 获取全局作用域中的变量
  if (!scope_stack_.empty()) {
    // ScriptScope* global_scope = scope_stack_.front(); // Uncomment when
    // implementing global variable retrieval
    // 这里需要从ScriptScope类获取所有变量的方法，目前暂未实现
    // TODO: 实现获取所有全局变量
  }

  return result;
}

std::vector<std::string> ScriptDebugger::get_call_stack() {
  std::lock_guard<std::mutex> lock(mutex_);
  return call_stack_;
}

void ScriptDebugger::set_breakpoint_hit_callback(BreakpointCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  bp_callback_ = std::move(callback);
}

void ScriptDebugger::set_state_change_callback(StateChangeCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_callback_ = std::move(callback);
}

DebuggerState ScriptDebugger::get_state() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

SourceLocation ScriptDebugger::get_current_location() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_location_;
}

void ScriptDebugger::notify_location(const SourceLocation &location) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_location_ = location;
    spdlog::trace("Script execution at location: {}:{}:{}", location.source,
                  location.line, location.column);

    // 如果处于非活动状态，不触发断点
    if (state_ == DebuggerState::Inactive ||
        state_ == DebuggerState::Terminated) {
      return;
    }

    // 检查是否应该暂停
    if (should_pause(location)) {
      state_ = DebuggerState::Paused;
      spdlog::debug("Execution paused at {}:{}:{}", location.source,
                    location.line, location.column);

      // 通知状态改变
      if (state_callback_) {
        state_callback_(state_);
      }
    }
  }

  // 如果需要暂停，等待调试命令
  if (get_state() == DebuggerState::Paused) {
    wait_for_command();
  }
}

void ScriptDebugger::notify_enter_function(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 添加到调用栈
  call_stack_.push_back(name);
  current_stack_depth_++;

  spdlog::trace("Entered function: {} (stack depth: {})", name,
                current_stack_depth_);
}

void ScriptDebugger::notify_exit_function(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 从调用栈中移除
  if (!call_stack_.empty()) {
    call_stack_.pop_back();
  }

  if (current_stack_depth_ > 0) {
    current_stack_depth_--;
  }

  spdlog::trace("Exited function: {} (stack depth: {})", name,
                current_stack_depth_);
}

void ScriptDebugger::notify_exception(const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);

  spdlog::warn("Exception in script: {}", message);

  // 可以在异常点自动暂停
  if (state_ == DebuggerState::Running) {
    state_ = DebuggerState::Paused;
    spdlog::debug("Execution paused due to exception: {}", message);

    // 通知状态改变
    if (state_callback_) {
      state_callback_(state_);
    }
  }
}

void ScriptDebugger::notify_scope_created(ScriptScope *scope) {
  std::lock_guard<std::mutex> lock(mutex_);
  scope_stack_.push_back(scope);
  spdlog::trace("Scope created (stack size: {})", scope_stack_.size());
}

void ScriptDebugger::notify_scope_destroyed(ScriptScope *scope) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 查找并移除作用域
  auto it = std::find(scope_stack_.begin(), scope_stack_.end(), scope);
  if (it != scope_stack_.end()) {
    scope_stack_.erase(it);
    spdlog::trace("Scope destroyed (stack size: {})", scope_stack_.size());
  }
}

bool ScriptDebugger::check_breakpoint(const SourceLocation &location) {
  for (auto &[id, bp] : breakpoints_) {
    if (bp.enabled && bp.location.source == location.source &&
        bp.location.line == location.line) {

      // 检查条件表达式
      if (bp.condition) {
        // TODO: 评估条件表达式，如果为false则不触发断点
        // 暂时跳过条件检查，总是触发
      }

      bp.hit_count++;
      spdlog::debug("Breakpoint #{} hit (hit count: {})", id, bp.hit_count);

      // 通知断点命中
      if (bp_callback_) {
        bp_callback_(bp);
      }

      return true;
    }
  }

  return false;
}

bool ScriptDebugger::should_pause(const SourceLocation &location) {
  // 如果调试器已停止，不应暂停
  if (should_stop_) {
    return false;
  }

  // 如果已经是暂停状态，保持暂停
  if (state_ == DebuggerState::Paused) {
    return true;
  }

  // 检查断点
  if (check_breakpoint(location)) {
    return true;
  }

  // 单步执行处理
  if (state_ == DebuggerState::StepInto) {
    // 单步进入时，任何位置都应该暂停
    return true;
  } else if (state_ == DebuggerState::StepOver) {
    // 单步跳过时，当前函数内的位置或返回调用位置才暂停
    size_t prev_stack_depth = current_stack_depth_;
    if (current_stack_depth_ <= prev_stack_depth) {
      return true;
    }
  } else if (state_ == DebuggerState::StepOut) {
    // 单步跳出时，只有返回调用位置才暂停
    size_t prev_stack_depth = current_stack_depth_;
    if (current_stack_depth_ < prev_stack_depth) {
      return true;
    }
  }

  return false;
}

void ScriptDebugger::wait_for_command() {
  spdlog::debug("Waiting for debugger command...");

  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this]() {
    return state_ == DebuggerState::Running ||
           state_ == DebuggerState::StepInto ||
           state_ == DebuggerState::StepOver ||
           state_ == DebuggerState::StepOut ||
           state_ == DebuggerState::Terminated || should_stop_;
  });

  spdlog::debug("Resuming execution with state: {}", static_cast<int>(state_));
}

} // namespace shell