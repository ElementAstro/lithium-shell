#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


#include "../config/config_manager.hpp"
#include "../interpreter/script_interpreter.hpp"
#include "../plugins/plugin_manager.hpp"
#include "environment.hpp"
#include "executor.hpp"
#include "parser.hpp"
#include "tokenizer.hpp"

namespace shell {

/**
 * @class Shell
 * @brief Shell主类，负责协调tokenizer、parser、executor和plugin_manager的操作
 */
class Shell {
public:
  Shell();
  ~Shell();

  // 禁止复制和移动
  Shell(const Shell &) = delete;
  Shell &operator=(const Shell &) = delete;
  Shell(Shell &&) = delete;
  Shell &operator=(Shell &&) = delete;

  // Variable and alias management
  void set_variable(const std::string &name, const std::string &value);
  std::optional<std::string> get_variable(const std::string &name) const;
  void add_alias(const std::string &alias, const std::string &command);
  
  // Configuration management
  bool load_config(const std::filesystem::path &config_file);

  // Shell state
  bool is_running() const;

  // 初始化shell
  // 初始化shell，返回是否成功初始化
  bool initialize();

  // 处理中断信号
  void handle_interrupt();

  // Plugin management functions
  bool load_plugin(const std::filesystem::path &plugin_path);
  bool reload_plugin(const std::string &name);
  bool enable_plugin(const std::string &name);
  bool disable_plugin(const std::string &name);
  bool register_builtin_plugin(std::unique_ptr<Plugin> plugin);
  std::vector<std::string> get_loaded_plugins() const;
  void add_plugin_directory(const std::filesystem::path &directory);
  void scan_plugin_directories();
  void set_plugin_hot_reload(bool enabled);
  void check_for_plugin_updates();
  std::optional<PluginMetadata> get_plugin_metadata(const std::string& name) const;
  void trigger_event(PluginEvent event, const PluginEventData& data = {});

  // 关闭shell
  void shutdown();

  // 运行交互式shell
  void run();

  // 脚本解释器功能
  void start_script_repl();
  std::string evaluate_script_code(const std::string& code);
  ScriptInterpreter& get_script_interpreter() { return *script_interpreter_; }

  // 评估命令字符串并返回输出
  std::string evaluate(const std::string &command);

  // 评估脚本文件并返回输出
  std::string evaluate_script(const std::filesystem::path &script_path);

  // 获取环境
  Environment &get_environment() {
    if (!env_) {
      throw std::runtime_error("Environment not initialized");
    }
    return *env_;
  }

  // 获取插件管理器
  PluginManager &get_plugin_manager() {
    if (!plugin_manager_) {
      throw std::runtime_error("PluginManager not initialized");
    }
    return *plugin_manager_;
  }

  // 设置提示符格式化函数
  void set_prompt_formatter(
      std::function<std::string(const Environment &)> formatter);

  // 执行内置命令
  bool execute_builtin(const std::string &command,
                       const std::vector<std::string> &args);
  // Alias for add_builtin_command - same functionality, different name
  void register_command(const std::string &name,
                       std::function<int(Shell &, const std::vector<std::string> &)> handler,
                       const std::string &help = "") {
    add_builtin_command(name, handler, help);
  }

  // 添加内置命令
  void add_builtin_command(
      const std::string &name,
      std::function<int(Shell &, const std::vector<std::string> &)> handler,
      const std::string &help = "");

  // 处理信号
  void handle_signal(int sig);

  // 获取当前实例
  static Shell *get_current_instance();

private:
  // 初始化基本环境
  void setup_environment();

  // 初始化内置命令
  void register_builtin_commands();

  // 获取格式化的提示符
  std::string get_prompt();

  // Read a line of input with completion and history
  std::string read_line();

  // Setup signal handlers
  void setup_signal_handlers();

  // Initialize completion
  void initialize_completion();

  // Print colorized prompt
  void print_prompt();

  // Register built-in commands
  void register_builtins();

  // 注册内置插件
  void register_builtin_plugins();

  // Components
  std::unique_ptr<Tokenizer> tokenizer_;
  std::unique_ptr<Parser> parser_;
  std::unique_ptr<Executor> executor_;
  std::unique_ptr<Environment> env_;
  std::unique_ptr<ConfigManager> config_;
  std::unique_ptr<PluginManager> plugin_manager_;
  std::unique_ptr<ScriptInterpreter> script_interpreter_;

  // State
  std::atomic<bool> running_;
  std::string prompt_;
  size_t history_index_;

  // 提示符格式化器
  std::function<std::string(const Environment &)> prompt_formatter_;

  // 内置命令处理器映射
  std::unordered_map<
      std::string,
      std::function<int(Shell &, const std::vector<std::string> &)>>
      builtin_commands_;

  // 内置命令帮助文本映射
  std::unordered_map<std::string, std::string> builtin_help_;

  // Tab completion
  std::vector<std::string> complete(const std::string &partial);
  static char **completion_callback(const char *text, int start, int end);
  static char *command_generator(const char *text, int state);
  static Shell *current_instance_;
};

} // namespace shell