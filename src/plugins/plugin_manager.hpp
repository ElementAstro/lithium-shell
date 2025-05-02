#pragma once

#include <concepts>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "../core/environment.hpp"

namespace shell {

// 前向声明
class Shell;

/**
 * @enum PluginState
 * @brief 表示插件的当前状态
 */
enum class PluginState {
  Created,     ///< 创建但未初始化
  Loaded,      ///< 已加载但未初始化
  Initialized, ///< 已初始化且运行中
  Failed,      ///< 初始化或操作失败
  Disabled,    ///< 已禁用
  Unloaded     ///< 已卸载
};

/**
 * @enum PluginEvent
 * @brief 插件可以响应的事件类型
 */
enum class PluginEvent {
  ShellStartup,       ///< Shell启动时
  ShellShutdown,      ///< Shell关闭时
  CommandBefore,      ///< 命令执行前
  CommandAfter,       ///< 命令执行后
  ConfigChanged,      ///< 配置改变时
  EnvironmentChanged, ///< 环境变量改变时
  PluginLoaded,       ///< 插件加载时
  PluginUnloaded      ///< 插件卸载时
};

/**
 * @struct PluginEventData
 * @brief 事件数据容器，使用std::variant支持不同的事件数据类型
 */
struct PluginEventData {
  struct CommandData {
    std::string command;
    std::vector<std::string> args;
  };

  struct ConfigData {
    std::string key;
    std::string old_value;
    std::string new_value;
  };

  struct EnvironmentData {
    std::string name;
    std::optional<std::string> old_value;
    std::optional<std::string> new_value;
  };

  struct PluginData {
    std::string name;
    std::string version;
  };

  std::variant<std::monostate, CommandData, ConfigData, EnvironmentData,
               PluginData>
      data;
};

/**
 * @class PluginMetadata
 * @brief 插件元数据
 */
class PluginMetadata {
public:
  PluginMetadata(std::string name, std::string version, std::string description,
                 std::string author, std::string website = "",
                 std::vector<std::string> dependencies = {})
      : name_(std::move(name)), version_(std::move(version)),
        description_(std::move(description)), author_(std::move(author)),
        website_(std::move(website)), dependencies_(std::move(dependencies)) {}

  // Getters
  const std::string &name() const { return name_; }
  const std::string &version() const { return version_; }
  const std::string &description() const { return description_; }
  const std::string &author() const { return author_; }
  const std::string &website() const { return website_; }
  const std::vector<std::string> &dependencies() const { return dependencies_; }

private:
  std::string name_;
  std::string version_;
  std::string description_;
  std::string author_;
  std::string website_;
  std::vector<std::string> dependencies_;
};

/**
 * @class Plugin
 * @brief 增强的插件接口，使用最新的C++特性
 */
class Plugin {
public:
  virtual ~Plugin() = default;

  // 基本生命周期方法
  virtual bool initialize(Environment &env) = 0;
  virtual void shutdown() = 0;

  // 扩展的生命周期方法
  virtual bool on_load() { return true; }
  virtual bool on_unload() { return true; }
  virtual bool on_enable() { return true; }
  virtual bool on_disable() { return true; }

  // 事件处理
  virtual bool handle_event([[maybe_unused]] PluginEvent event,
                            [[maybe_unused]] const PluginEventData &data) {
    return true;
  }

  // 元数据访问
  virtual PluginMetadata get_metadata() const = 0;

  // 便捷访问方法
  virtual std::string get_name() const { return get_metadata().name(); }
  virtual std::string get_version() const { return get_metadata().version(); }
  virtual std::string get_description() const {
    return get_metadata().description();
  }

  // 状态管理
  PluginState get_state() const { return state_; }
  void set_state(PluginState state) { state_ = state; }

  // 错误管理
  void set_error(const std::string &error) { last_error_ = error; }
  std::string get_last_error() const { return last_error_; }
  bool has_error() const { return !last_error_.empty(); }
  void clear_error() { last_error_.clear(); }

private:
  PluginState state_ = PluginState::Created;
  std::string last_error_;
};

/**
 * @concept PluginConcept
 * @brief C++20 concept约束插件类型
 */
template <typename T>
concept PluginConcept =
    std::derived_from<T, Plugin> && requires(T plugin, Environment &env) {
      { plugin.initialize(env) } -> std::same_as<bool>;
      { plugin.shutdown() } -> std::same_as<void>;
      { plugin.get_metadata() } -> std::same_as<PluginMetadata>;
    };

/**
 * @typedef PluginCreateFunc
 * @brief 插件创建函数类型
 */
using PluginCreateFunc = std::unique_ptr<Plugin> (*)();

/**
 * @class PluginManager
 * @brief 增强的插件管理器
 */
class PluginManager {
public:
  explicit PluginManager(Environment &env);
  ~PluginManager();

  // 禁止复制和移动
  PluginManager(const PluginManager &) = delete;
  PluginManager &operator=(const PluginManager &) = delete;
  PluginManager(PluginManager &&) = delete;
  PluginManager &operator=(PluginManager &&) = delete;

  // 插件加载和管理
  bool load_plugin(const std::filesystem::path &plugin_path);
  bool reload_plugin(const std::string &name);
  bool register_plugin(std::unique_ptr<Plugin> plugin);
  bool enable_plugin(const std::string &name);
  bool disable_plugin(const std::string &name);
  bool unload_plugin(const std::string &name);
  void unload_all();

  // 设置Shell实例的引用
  void set_shell(Shell &shell) { shell_ = &shell; }

  // 插件发现
  void add_plugin_directory(const std::filesystem::path &directory);
  void scan_plugin_directories();
  std::vector<std::filesystem::path> discover_plugins() const;

  // 插件查询
  std::vector<std::string> get_loaded_plugins() const;
  std::vector<std::string> get_enabled_plugins() const;
  std::vector<std::string> get_disabled_plugins() const;
  std::optional<PluginMetadata>
  get_plugin_metadata(const std::string &name) const;
  PluginState get_plugin_state(const std::string &name) const;
  std::string get_plugin_error(const std::string &name) const;

  // 插件事件系统
  void trigger_event(PluginEvent event, const PluginEventData &data = {});

  // 热重载支持
  void check_for_plugin_updates();
  void set_auto_reload(bool enabled) { auto_reload_ = enabled; }
  bool get_auto_reload() const { return auto_reload_; }

private:
  struct PluginData {
    std::unique_ptr<Plugin> plugin;
    void *library_handle = nullptr;
    std::filesystem::path path;
    std::filesystem::file_time_type last_modified;
    bool enabled = true;
  };

  // 内部实现方法
  bool load_plugin_from_library(const std::filesystem::path &path,
                                PluginData &data);
  void update_plugin_timestamp(PluginData &data);
  std::filesystem::file_time_type
  get_file_last_modified(const std::filesystem::path &path) const;

  std::unordered_map<std::string, PluginData> plugins_;
  std::vector<std::filesystem::path> plugin_directories_;
  Environment &env_;
  Shell *shell_ = nullptr;
  bool auto_reload_ = false;
};

/**
 * @class BuiltinPlugin
 * @brief 内置插件的基类
 */
class BuiltinPlugin : public Plugin {
public:
  explicit BuiltinPlugin(PluginMetadata metadata)
      : metadata_(std::move(metadata)) {}

  PluginMetadata get_metadata() const override { return metadata_; }

private:
  PluginMetadata metadata_;
};

// 便捷宏，用于插件定义导出函数
#ifdef _WIN32
#define EXPORT_PLUGIN extern "C" __declspec(dllexport)
#else
#define EXPORT_PLUGIN extern "C" __attribute__((visibility("default")))
#endif

/**
 * @brief 用于创建和注册一个标准插件的宏
 */
#define DECLARE_PLUGIN(PluginClass)                                            \
  EXPORT_PLUGIN std::unique_ptr<shell::Plugin> create_plugin() {               \
    return std::make_unique<PluginClass>();                                    \
  }

} // namespace shell