#include "plugin_manager.hpp"

#include <filesystem>
#include <spdlog/spdlog.h> // Include spdlog header

namespace shell {

PluginManager::PluginManager(Environment &env)
    : env_(env), auto_reload_(false) {
  // Initialize the plugin manager
  spdlog::debug("PluginManager instance created.");
}

PluginManager::~PluginManager() {
  spdlog::debug("PluginManager destructor called, unloading all plugins.");
  unload_all();
}

bool PluginManager::register_plugin(std::unique_ptr<Plugin> plugin) {
  if (!plugin) {
    spdlog::error("Failed to register plugin: null plugin pointer provided.");
    return false;
  }

  const auto &name = plugin->get_name();
  spdlog::debug("Registering plugin: '{}'", name);

  // Check if the plugin is already registered
  if (plugins_.find(name) != plugins_.end()) {
    spdlog::warn("Plugin '{}' already exists and will be replaced.", name);
    // If already enabled, unload the old one first
    if (plugins_[name].enabled) {
      spdlog::debug(
          "Unloading previous version of plugin '{}' before replacing.", name);
      unload_plugin(name);
    }
  }

  // Add to the plugin map
  PluginData plugin_data;
  plugin_data.plugin = std::move(plugin);
  plugin_data.enabled = true;
  plugins_[name] = std::move(plugin_data);

  spdlog::info("Registered plugin: {} (version {})", name,
               plugins_[name].plugin->get_version());
  return true;
}

bool PluginManager::load_plugin(const std::filesystem::path &plugin_path) {
  spdlog::info("Loading plugin from path: {}", plugin_path.string());
  // This should implement loading plugins from path
  // In this example, we only support built-in plugins, so this function won't
  // actually be called
  spdlog::warn("External plugin loading not implemented yet. Path: {}",
               plugin_path.string());
  return false;
}

bool PluginManager::reload_plugin(const std::string &name) {
  spdlog::debug("Attempting to reload plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it == plugins_.end()) {
    spdlog::error("Cannot reload plugin '{}': plugin not found.", name);
    return false;
  }

  // If plugin is enabled, disable it first
  if (it->second.enabled) {
    spdlog::debug("Disabling plugin '{}' before reload.", name);
    disable_plugin(name);
  }

  // Re-enable the plugin
  spdlog::debug("Re-enabling plugin '{}' to complete reload.", name);
  return enable_plugin(name);
}

bool PluginManager::enable_plugin(const std::string &name) {
  spdlog::debug("Attempting to enable plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it == plugins_.end()) {
    spdlog::error("Cannot enable plugin '{}': plugin not found.", name);
    return false;
  }

  if (it->second.enabled) {
    spdlog::info("Plugin '{}' is already enabled.", name);
    return true;
  }

  spdlog::info("Enabling plugin: {}", name);

  auto &plugin_data = it->second;
  auto &plugin = plugin_data.plugin;

  // Initialize the plugin
  spdlog::trace("Initializing plugin '{}'...", name);
  if (!plugin->initialize(env_)) {
    spdlog::error("Failed to initialize plugin '{}'.", name);
    return false;
  }

  // Load the plugin
  spdlog::trace("Loading plugin '{}'...", name);
  if (!plugin->on_load()) {
    spdlog::error("Failed to load plugin '{}'.", name);
    return false;
  }

  // Enable the plugin
  spdlog::trace("Running on_enable for plugin '{}'...", name);
  if (!plugin->on_enable()) {
    spdlog::warn("Failed to enable plugin '{}'. Attempting to unload.", name);
    // Try to unload
    plugin->on_unload();
    return false;
  }

  plugin_data.enabled = true;
  spdlog::debug("Plugin '{}' enabled successfully.", name);

  // Trigger plugin loaded event
  spdlog::trace("Triggering PluginLoaded event for '{}'...", name);
  PluginEventData load_data;
  PluginEventData::PluginData plugin_info;
  plugin_info.name = name;
  plugin_info.version = plugin->get_version();
  load_data.data = plugin_info;
  trigger_event(PluginEvent::PluginLoaded, load_data);

  return true;
}

bool PluginManager::disable_plugin(const std::string &name) {
  spdlog::debug("Attempting to disable plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it == plugins_.end()) {
    spdlog::error("Cannot disable plugin '{}': plugin not found.", name);
    return false;
  }

  if (!it->second.enabled) {
    spdlog::info("Plugin '{}' is already disabled.", name);
    return true;
  }

  spdlog::info("Disabling plugin: {}", name);

  auto &plugin_data = it->second;
  auto &plugin = plugin_data.plugin;

  // Trigger plugin unload event
  spdlog::trace("Triggering PluginUnloaded event for '{}'...", name);
  PluginEventData unload_data;
  PluginEventData::PluginData plugin_info;
  plugin_info.name = name;
  plugin_info.version = plugin->get_version();
  unload_data.data = plugin_info;
  trigger_event(PluginEvent::PluginUnloaded, unload_data);

  // Disable the plugin
  spdlog::trace("Running on_disable for plugin '{}'...", name);
  if (!plugin->on_disable()) {
    spdlog::warn(
        "Failed to disable plugin '{}'. Continuing with unload process.", name);
    // Continue with unload anyway
  }

  // Unload the plugin
  spdlog::trace("Running on_unload for plugin '{}'...", name);
  if (!plugin->on_unload()) {
    spdlog::error("Failed to unload plugin '{}'.", name);
    return false;
  }

  plugin_data.enabled = false;
  spdlog::debug("Plugin '{}' disabled successfully.", name);

  return true;
}

bool PluginManager::unload_plugin(const std::string &name) {
  spdlog::debug("Attempting to unload plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it == plugins_.end()) {
    spdlog::error("Cannot unload plugin '{}': plugin not found.", name);
    return false;
  }

  // If the plugin is enabled, disable it first
  if (it->second.enabled) {
    spdlog::trace("Plugin '{}' is enabled. Disabling before unload.", name);
    if (!disable_plugin(name)) {
      spdlog::error("Failed to disable plugin '{}' before unloading.", name);
      return false;
    }
  }

  // Shutdown the plugin
  spdlog::trace("Shutting down plugin '{}'...", name);
  it->second.plugin->shutdown();
  spdlog::info("Plugin '{}' unloaded successfully.", name);

  return true;
}

void PluginManager::unload_all() {
  spdlog::info("Unloading all plugins...");
  std::vector<std::string> plugin_names;

  // Collect all loaded plugin names
  for (const auto &[name, _] : plugins_) {
    plugin_names.push_back(name);
  }

  // Unload all plugins
  int unloaded_count = 0;
  for (const auto &name : plugin_names) {
    spdlog::debug("Unloading plugin '{}' as part of unload_all operation.",
                  name);
    if (unload_plugin(name)) {
      unloaded_count++;
    }
  }

  spdlog::info("Unloaded {} plugins.", unloaded_count);
}

void PluginManager::trigger_event(PluginEvent event,
                                  const PluginEventData &data) {
  spdlog::trace("Triggering event: {} for all enabled plugins.",
                static_cast<int>(event));

  int success_count = 0;
  int failure_count = 0;

  for (auto &[name, plugin_data] : plugins_) {
    if (plugin_data.enabled) {
      spdlog::trace("Sending event {} to plugin '{}'.", static_cast<int>(event),
                    name);
      if (!plugin_data.plugin->handle_event(event, data)) {
        spdlog::warn("Plugin '{}' failed to handle event {}.", name,
                     static_cast<int>(event));
        failure_count++;
      } else {
        success_count++;
      }
    }
  }

  spdlog::debug("Event {} processed by {} plugins ({} successes, {} failures).",
                static_cast<int>(event), success_count + failure_count,
                success_count, failure_count);
}

void PluginManager::add_plugin_directory(
    const std::filesystem::path &directory) {
  spdlog::debug("Adding plugin directory: {}", directory.string());

  // Check if the path is already in the list
  for (const auto &dir : plugin_directories_) {
    if (directory == dir) {
      spdlog::trace("Directory {} is already in the plugin directories list.",
                    directory.string());
      return; // Already exists
    }
  }

  plugin_directories_.push_back(directory);
  spdlog::info("Added plugin directory: {}", directory.string());
}

void PluginManager::scan_plugin_directories() {
  spdlog::info("Scanning plugin directories for external plugins...");

  // Get user home directory
#ifdef _WIN32
  auto home_env = std::getenv("USERPROFILE");
  spdlog::trace("Using USERPROFILE for Windows home directory lookup.");
#else
  auto home_env = std::getenv("HOME");
  spdlog::trace("Using HOME for Unix home directory lookup.");
#endif

  if (!home_env) {
    spdlog::warn("Could not determine user home directory. Skipping external "
                 "plugin scan.");
    return;
  }

  std::filesystem::path home_dir(home_env);
  std::filesystem::path plugins_dir = home_dir / ".lithium" / "plugins";
  spdlog::debug("Default plugins directory: {}", plugins_dir.string());

  try {
    if (!std::filesystem::exists(plugins_dir)) {
      spdlog::info("Creating default plugins directory: {}",
                   plugins_dir.string());
      std::filesystem::create_directories(plugins_dir);
    }

    // Add the default plugin directory
    add_plugin_directory(plugins_dir);

    // Scan all plugin directories
    for (const auto &dir : plugin_directories_) {
      spdlog::info("Scanning plugin directory: {}", dir.string());

      // This should actually load external plugins, but currently it's just a
      // placeholder
      // TODO: Implement dynamic plugin loading
      spdlog::debug(
          "Dynamic plugin loading not yet implemented (directory: {}).",
          dir.string());
    }
  } catch (const std::filesystem::filesystem_error &e) {
    spdlog::error("Error scanning plugin directories: {}", e.what());
  }
}

std::vector<std::filesystem::path> PluginManager::discover_plugins() const {
  spdlog::debug("Discovering plugins in registered directories...");
  std::vector<std::filesystem::path> result;

  for (const auto &dir : plugin_directories_) {
    spdlog::trace("Searching for plugins in directory: {}", dir.string());
    try {
      if (!std::filesystem::exists(dir)) {
        spdlog::warn("Plugin directory does not exist: {}", dir.string());
        continue;
      }

      for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
          continue;
        }

        const auto &path = entry.path();
        std::string ext = path.extension().string();
        spdlog::trace("Examining potential plugin file: {}", path.string());

        // Check if it's a valid plugin file
#ifdef _WIN32
        if (ext == ".dll") {
#else
        if (ext == ".so") {
#endif
          spdlog::debug("Found potential plugin file: {}", path.string());
          result.push_back(path);
        }
      }
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error("Error discovering plugins in directory {}: {}",
                    dir.string(), e.what());
    }
  }

  spdlog::info("Discovered {} potential plugin files.", result.size());
  return result;
}

std::vector<std::string> PluginManager::get_loaded_plugins() const {
  std::vector<std::string> result;

  for (const auto &[name, plugin_data] : plugins_) {
    result.push_back(name);
  }

  spdlog::trace("Retrieved list of {} loaded plugins.", result.size());
  return result;
}

std::vector<std::string> PluginManager::get_enabled_plugins() const {
  std::vector<std::string> result;

  for (const auto &[name, plugin_data] : plugins_) {
    if (plugin_data.enabled) {
      result.push_back(name);
    }
  }

  spdlog::trace("Retrieved list of {} enabled plugins.", result.size());
  return result;
}

std::vector<std::string> PluginManager::get_disabled_plugins() const {
  std::vector<std::string> result;

  for (const auto &[name, plugin_data] : plugins_) {
    if (!plugin_data.enabled) {
      result.push_back(name);
    }
  }

  spdlog::trace("Retrieved list of {} disabled plugins.", result.size());
  return result;
}

std::optional<PluginMetadata>
PluginManager::get_plugin_metadata(const std::string &name) const {
  spdlog::trace("Getting metadata for plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it != plugins_.end()) {
    spdlog::trace("Found metadata for plugin '{}'", name);
    return it->second.plugin->get_metadata();
  }

  spdlog::debug("No metadata found for plugin '{}'", name);
  return std::nullopt;
}

PluginState PluginManager::get_plugin_state(const std::string &name) const {
  spdlog::trace("Getting state for plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it != plugins_.end()) {
    PluginState state = it->second.plugin->get_state();
    spdlog::trace("Plugin '{}' state: {}", name, static_cast<int>(state));
    return state;
  }

  spdlog::debug("Plugin '{}' not found, returning Unloaded state.", name);
  return PluginState::Unloaded;
}

std::string PluginManager::get_plugin_error(const std::string &name) const {
  spdlog::trace("Getting last error for plugin: '{}'", name);

  auto it = plugins_.find(name);
  if (it != plugins_.end()) {
    std::string error = it->second.plugin->get_last_error();
    spdlog::trace("Plugin '{}' last error: '{}'", name, error);
    return error;
  }

  spdlog::debug("Plugin '{}' not found, returning generic error message.",
                name);
  return "Plugin not found";
}

void PluginManager::check_for_plugin_updates() {
  spdlog::trace("Checking for plugin updates...");

  if (!auto_reload_) {
    spdlog::trace("Auto-reload is disabled, skipping update check.");
    return; // Auto-reload is disabled
  }

  // Check if plugin files have been updated
  for (auto &[name, plugin_data] : plugins_) {
    if (plugin_data.path.empty()) {
      spdlog::trace("Plugin '{}' has no file path (built-in plugin), skipping "
                    "update check.",
                    name);
      continue; // Built-in plugin with no file path
    }

    auto current_time = get_file_last_modified(plugin_data.path);
    if (current_time != plugin_data.last_modified) {
      spdlog::info("Plugin '{}' has been updated, reloading...", name);
      reload_plugin(name);
      update_plugin_timestamp(plugin_data);
    }
  }
}

std::filesystem::file_time_type
PluginManager::get_file_last_modified(const std::filesystem::path &path) const {
  spdlog::trace("Getting last modified time for file: {}", path.string());

  try {
    if (std::filesystem::exists(path)) {
      auto time = std::filesystem::last_write_time(path);
      spdlog::trace("File '{}' last modified time retrieved successfully.",
                    path.string());
      return time;
    }
  } catch (const std::filesystem::filesystem_error &e) {
    spdlog::error("Failed to get file modification time for '{}': {}",
                  path.string(), e.what());
  }

  spdlog::debug("Returning minimum time for file '{}' (file may not exist).",
                path.string());
  return std::filesystem::file_time_type::min();
}

void PluginManager::update_plugin_timestamp(PluginData &data) {
  if (!data.path.empty()) {
    spdlog::trace("Updating timestamp for plugin at path: {}",
                  data.path.string());
    data.last_modified = get_file_last_modified(data.path);
  }
}

bool PluginManager::load_plugin_from_library(const std::filesystem::path &path,
                                             PluginData &data) {
  spdlog::info("Loading plugin from library: {}", path.string());
  void *handle = nullptr;

  // Load the dynamic library
#ifdef _WIN32
  spdlog::trace("Using LoadLibrary for Windows dynamic library loading.");
  handle = LoadLibrary(path.string().c_str());
#else
  spdlog::trace("Using dlopen for Unix dynamic library loading.");
  handle = dlopen(path.string().c_str(), RTLD_LAZY);
#endif

  if (!handle) {
#ifdef _WIN32
    spdlog::error("Failed to load dynamic library: error code {}",
                  GetLastError());
#else
    spdlog::error("Failed to load dynamic library: {}", dlerror());
#endif
    return false;
  }

  // Get the plugin creation function
  spdlog::trace("Looking for create_plugin function in library.");
  PluginCreateFunc create_func = nullptr;

#ifdef _WIN32
  void *func_ptr = reinterpret_cast<void *>(
      GetProcAddress(static_cast<HMODULE>(handle), "create_plugin"));
  create_func = reinterpret_cast<PluginCreateFunc>(func_ptr);
#else
  create_func =
      reinterpret_cast<PluginCreateFunc>(dlsym(handle, "create_plugin"));
#endif

  if (!create_func) {
#ifdef _WIN32
    spdlog::error("Failed to get plugin creation function: error code {}",
                  GetLastError());
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    spdlog::error("Failed to get plugin creation function: {}", dlerror());
    dlclose(handle);
#endif
    return false;
  }

  // Create the plugin instance
  spdlog::trace("Creating plugin instance using create_plugin function.");
  auto plugin = create_func();
  if (!plugin) {
    spdlog::error("Failed to create plugin instance.");
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
    return false;
  }

  // Set up plugin data
  spdlog::debug("Plugin created successfully from library: {}", path.string());
  data.plugin = std::move(plugin);
  data.library_handle = handle;
  data.path = path;
  update_plugin_timestamp(data);
  data.enabled = false;

  return true;
}

} // namespace shell