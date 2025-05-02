#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../core/environment.hpp"
#include "../plugin_manager.hpp"


namespace shell {

/**
 * @class SyntaxHighlightPlugin
 * @brief Provides syntax highlighting functionality for the shell
 *
 * This plugin adds command line syntax highlighting features including:
 * - Command highlighting
 * - Arguments and options highlighting
 * - File path highlighting
 * - String highlighting
 * - Special character highlighting
 */
class SyntaxHighlightPlugin : public BuiltinPlugin {
public:
  SyntaxHighlightPlugin();

  bool initialize(Environment &env) override;
  void shutdown() override;

  // Lifecycle methods
  bool on_load() override;
  bool on_unload() override;

  // Event handling
  bool handle_event(PluginEvent event, const PluginEventData &data) override;

  /**
   * @brief Applies syntax highlighting to a command string
   * @param command The command string to highlight
   * @return The highlighted command string with ANSI color codes
   */
  std::string highlight_command(const std::string &command);

private:
  /**
   * @brief Initialize syntax highlighting rules
   */
  void initialize_highlight_rules();

  /**
   * @brief Highlight the command part of the input
   * @param command The command to highlight
   * @return The highlighted command with color codes
   */
  std::string highlight_command_part(const std::string &command);

  /**
   * @brief Highlight file paths in the command
   * @param path The path to highlight
   * @return The highlighted path with color codes
   */
  std::string highlight_path(const std::string &path);

  /**
   * @brief Highlight command options and arguments
   * @param option The option string to highlight
   * @return The highlighted option with color codes
   */
  std::string highlight_options(const std::string &option);

  /**
   * @brief Highlight quoted strings in the input
   * @param input The input containing strings to highlight
   * @return The input with highlighted strings
   */
  std::string highlight_strings(const std::string &input);

  /// Typedef for a highlighting rule (regex pattern and color)
  using HighlightRule = std::pair<std::regex, std::string>;

  /// Collection of highlighting rules
  std::vector<HighlightRule> highlight_rules_;

  /// Mapping of commands to their highlight colors
  std::unordered_map<std::string, std::string> command_colors_;

  /// Set of keywords to highlight
  std::unordered_set<std::string> keywords_;

  /// Reference to the shell environment
  Environment *env_ = nullptr;

  /// Flag indicating whether highlighting is enabled
  bool enabled_ = true;
};

} // namespace shell