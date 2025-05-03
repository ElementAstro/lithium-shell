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

// Forward declaration
class Shell;

/**
 * @enum PluginState
 * @brief Represents the current state of a plugin
 */
enum class PluginState {
  Created,     ///< Created but not initialized
  Loaded,      ///< Loaded but not initialized
  Initialized, ///< Initialized and running
  Failed,      ///< Failed to initialize or operate
  Disabled,    ///< Disabled
  Unloaded     ///< Unloaded
};

/**
 * @enum PluginEvent
 * @brief Event types that plugins can respond to
 */
enum class PluginEvent {
  ShellStartup,       ///< When Shell starts
  ShellShutdown,      ///< When Shell shuts down
  CommandBefore,      ///< Before command execution
  CommandAfter,       ///< After command execution
  ConfigChanged,      ///< When configuration changes
  EnvironmentChanged, ///< When environment variables change
  PluginLoaded,       ///< When a plugin is loaded
  PluginUnloaded      ///< When a plugin is unloaded
};

/**
 * @struct PluginEventData
 * @brief Event data container, using std::variant to support different event
 * data types
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
 * @brief Plugin metadata
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
 * @brief Enhanced plugin interface using the latest C++ features
 */
class Plugin {
public:
  virtual ~Plugin() = default;

  // Basic lifecycle methods
  virtual bool initialize(Environment &env) = 0;
  virtual void shutdown() = 0;

  // Extended lifecycle methods
  virtual bool on_load() { return true; }
  virtual bool on_unload() { return true; }
  virtual bool on_enable() { return true; }
  virtual bool on_disable() { return true; }

  // Event handling
  virtual bool handle_event([[maybe_unused]] PluginEvent event,
                            [[maybe_unused]] const PluginEventData &data) {
    return true;
  }

  // Metadata access
  virtual PluginMetadata get_metadata() const = 0;

  // Convenience access methods
  virtual std::string get_name() const { return get_metadata().name(); }
  virtual std::string get_version() const { return get_metadata().version(); }
  virtual std::string get_description() const {
    return get_metadata().description();
  }

  // State management
  PluginState get_state() const { return state_; }
  void set_state(PluginState state) { state_ = state; }

  // Error management
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
 * @brief C++20 concept constraining plugin types
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
 * @brief Plugin creation function type
 */
using PluginCreateFunc = std::unique_ptr<Plugin> (*)();

/**
 * @class PluginManager
 * @brief Enhanced plugin manager
 */
class PluginManager {
public:
  explicit PluginManager(Environment &env);
  ~PluginManager();

  // Prevent copy and move
  PluginManager(const PluginManager &) = delete;
  PluginManager &operator=(const PluginManager &) = delete;
  PluginManager(PluginManager &&) = delete;
  PluginManager &operator=(PluginManager &&) = delete;

  // Plugin loading and management
  bool load_plugin(const std::filesystem::path &plugin_path);
  bool reload_plugin(const std::string &name);
  bool register_plugin(std::unique_ptr<Plugin> plugin);
  bool enable_plugin(const std::string &name);
  bool disable_plugin(const std::string &name);
  bool unload_plugin(const std::string &name);
  void unload_all();

  // Set Shell instance reference
  void set_shell(Shell &shell) { shell_ = &shell; }

  // Plugin discovery
  void add_plugin_directory(const std::filesystem::path &directory);
  void scan_plugin_directories();
  std::vector<std::filesystem::path> discover_plugins() const;

  // Plugin queries
  std::vector<std::string> get_loaded_plugins() const;
  std::vector<std::string> get_enabled_plugins() const;
  std::vector<std::string> get_disabled_plugins() const;
  std::optional<PluginMetadata>
  get_plugin_metadata(const std::string &name) const;
  PluginState get_plugin_state(const std::string &name) const;
  std::string get_plugin_error(const std::string &name) const;

  // Plugin event system
  void trigger_event(PluginEvent event, const PluginEventData &data = {});

  // Hot reload support
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

  // Internal implementation methods
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
 * @brief Base class for built-in plugins
 */
class BuiltinPlugin : public Plugin {
public:
  explicit BuiltinPlugin(PluginMetadata metadata)
      : metadata_(std::move(metadata)) {}

  PluginMetadata get_metadata() const override { return metadata_; }

private:
  PluginMetadata metadata_;
};

// Convenience macro for plugin definition export function
#ifdef _WIN32
#define EXPORT_PLUGIN extern "C" __declspec(dllexport)
#else
#define EXPORT_PLUGIN extern "C" __attribute__((visibility("default")))
#endif

/**
 * @brief Macro for creating and registering a standard plugin
 */
#define DECLARE_PLUGIN(PluginClass)                                            \
  EXPORT_PLUGIN std::unique_ptr<shell::Plugin> create_plugin() {               \
    return std::make_unique<PluginClass>();                                    \
  }

} // namespace shell