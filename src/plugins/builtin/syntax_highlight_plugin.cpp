#include "syntax_highlight_plugin.hpp"
#include "utils/color.hpp"

#include <spdlog/spdlog.h> // Include spdlog header
#include <sstream>

namespace shell {

SyntaxHighlightPlugin::SyntaxHighlightPlugin()
    : BuiltinPlugin(PluginMetadata(
          "syntax_highlight", "1.0.0", "Command line syntax highlighting",
          "Lithium Shell Team",
          "https://github.com/lithium-shell/syntax-highlight-plugin", {})) {
  spdlog::debug("SyntaxHighlightPlugin instance created.");
}

bool SyntaxHighlightPlugin::initialize(Environment &env) {
  spdlog::info("Initializing SyntaxHighlightPlugin...");
  env_ = &env;

  // Initialize syntax highlighting rules
  spdlog::debug("Setting up highlighting rules...");
  initialize_highlight_rules();

  // std::cout << "Syntax highlight plugin initialized" << std::endl;
  spdlog::info("SyntaxHighlightPlugin initialized successfully.");
  return true;
}

void SyntaxHighlightPlugin::shutdown() {
  spdlog::info("Shutting down SyntaxHighlightPlugin...");
  env_ = nullptr;
  spdlog::info("SyntaxHighlightPlugin shut down complete.");
}

bool SyntaxHighlightPlugin::on_load() {
  spdlog::info("SyntaxHighlightPlugin loaded.");
  // std::cout << "Syntax highlight plugin loaded" << std::endl;
  return true;
}

bool SyntaxHighlightPlugin::on_unload() {
  spdlog::info("SyntaxHighlightPlugin unloaded.");
  // std::cout << "Syntax highlight plugin unloaded" << std::endl;
  return true;
}

bool SyntaxHighlightPlugin::handle_event(PluginEvent event,
                                         const PluginEventData &data) {
  spdlog::trace("Handling plugin event: {}", static_cast<int>(event));

  // Process command highlighting before execution (example only, actual input
  // highlighting requires readline integration)
  if (event == PluginEvent::CommandBefore &&
      std::holds_alternative<PluginEventData::CommandData>(data.data)) {
    const auto &cmd_data = std::get<PluginEventData::CommandData>(data.data);
    if (!cmd_data.command.empty()) {
      spdlog::trace("Command before event received for: {}", cmd_data.command);

      // Reconstruct full command string
      std::stringstream ss;
      for (size_t i = 0; i < cmd_data.args.size(); ++i) {
        if (i > 0)
          ss << " ";
        ss << cmd_data.args[i];
      }
      std::string full_command = ss.str();
      spdlog::trace("Full command to highlight: '{}'", full_command);

      // In a real application, this highlighted command would be displayed or
      // integrated with readline This is just an example
      std::string highlighted = highlight_command(full_command);
      spdlog::trace("Highlighted command: '{}'", highlighted);
      // std::cout << "Highlighted: " << highlighted << std::endl;
    }
  }

  return true;
}

std::string
SyntaxHighlightPlugin::highlight_command(const std::string &command) {
  spdlog::trace("Highlighting command: '{}'", command);

  if (!enabled_) {
    spdlog::trace(
        "Syntax highlighting is disabled, returning original command.");
    return command;
  }

  std::string result = command;

  // First highlight strings to prevent incorrect parsing of markers within them
  spdlog::trace("Applying string highlighting...");
  result = highlight_strings(result);

  // Split the command by spaces
  std::istringstream iss(result);
  std::vector<std::string> tokens;
  std::string token;
  bool first_token = true;

  spdlog::trace("Tokenizing and highlighting command parts...");
  while (iss >> token) {
    // First token is the command name
    if (first_token) {
      spdlog::trace("Processing command name token: '{}'", token);
      tokens.push_back(highlight_command_part(token));
      first_token = false;
    }
    // Tokens starting with - are options
    else if (token.starts_with("-")) {
      spdlog::trace("Processing option token: '{}'", token);
      tokens.push_back(highlight_options(token));
    }
    // Might be a file path if it contains / or
    else if (token.find('/') != std::string::npos ||
             token.find('\\') != std::string::npos) {
      spdlog::trace("Processing path token: '{}'", token);
      tokens.push_back(highlight_path(token));
    }
    // Other tokens
    else {
      // Check if it's a shell keyword
      if (keywords_.find(token) != keywords_.end()) {
        spdlog::trace("Processing keyword token: '{}'", token);
        tokens.push_back(Color::cyan + token + Color::reset);
      } else {
        spdlog::trace("Processing regular token: '{}'", token);
        tokens.push_back(token);
      }
    }
  }

  // Reconstruct the command
  std::ostringstream oss;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0)
      oss << " ";
    oss << tokens[i];
  }
  result = oss.str();
  spdlog::trace("Command reconstructed after token highlighting.");

  // Apply additional highlighting rules
  spdlog::trace("Applying regex-based highlighting rules...");
  for (const auto &rule : highlight_rules_) {
    result = std::regex_replace(result, rule.first, rule.second);
  }

  spdlog::debug("Command highlighting complete: '{}'", result);
  return result;
}

void SyntaxHighlightPlugin::initialize_highlight_rules() {
  spdlog::debug("Initializing syntax highlighting rules...");

  // Set colors for common commands
  spdlog::trace("Setting command colors...");
  command_colors_["cd"] = Color::green;
  command_colors_["ls"] = Color::green;
  command_colors_["mv"] = Color::green;
  command_colors_["cp"] = Color::green;
  command_colors_["rm"] = Color::red; // Dangerous commands in red
  command_colors_["mkdir"] = Color::green;
  command_colors_["git"] = Color::magenta;
  command_colors_["docker"] = Color::blue;
  command_colors_["ssh"] = Color::yellow;
  command_colors_["sudo"] = Color::red + Color::bold;
  command_colors_["vim"] = Color::green;
  command_colors_["grep"] = Color::cyan;
  command_colors_["find"] = Color::cyan;
  command_colors_["echo"] = Color::green;
  command_colors_["export"] = Color::yellow;
  command_colors_["source"] = Color::yellow;
  command_colors_["plugin"] = Color::cyan + Color::bold;
  spdlog::debug("Configured colors for {} common commands.",
                command_colors_.size());

  // Set shell keywords
  spdlog::trace("Setting shell keywords...");
  keywords_.insert("if");
  keywords_.insert("then");
  keywords_.insert("else");
  keywords_.insert("elif");
  keywords_.insert("fi");
  keywords_.insert("for");
  keywords_.insert("while");
  keywords_.insert("do");
  keywords_.insert("done");
  keywords_.insert("case");
  keywords_.insert("esac");
  keywords_.insert("function");
  keywords_.insert("return");
  spdlog::debug("Added {} shell keywords for highlighting.", keywords_.size());

  // Add highlighting rules
  spdlog::trace("Setting up regex-based highlighting rules...");

  // Highlight numbers
  highlight_rules_.emplace_back(std::regex("\\b\\d+\\b"),
                                Color::yellow + "$&" + Color::reset);

  // Highlight environment variable references
  highlight_rules_.emplace_back(std::regex("\\$\\w+"),
                                Color::cyan + "$&" + Color::reset);
  highlight_rules_.emplace_back(std::regex("\\$\\{\\w+\\}"),
                                Color::cyan + "$&" + Color::reset);

  // Highlight special characters
  highlight_rules_.emplace_back(std::regex("[;|><&]"),
                                Color::yellow + "$&" + Color::reset);

  // Highlight redirections
  highlight_rules_.emplace_back(std::regex("\\d*>+&?\\d*"),
                                Color::yellow + "$&" + Color::reset);
  highlight_rules_.emplace_back(std::regex("\\d*<+&?\\d*"),
                                Color::yellow + "$&" + Color::reset);

  spdlog::debug("Added {} regex-based highlighting rules.",
                highlight_rules_.size());
}

std::string
SyntaxHighlightPlugin::highlight_command_part(const std::string &command) {
  spdlog::trace("Highlighting command part: '{}'", command);

  auto it = command_colors_.find(command);
  if (it != command_colors_.end()) {
    spdlog::trace("Found custom color for command.");
    return it->second + command + Color::reset;
  }

  // Default command color
  spdlog::trace("Using default color for command.");
  return Color::green + command + Color::reset;
}

std::string SyntaxHighlightPlugin::highlight_path(const std::string &path) {
  spdlog::trace("Highlighting path: '{}'", path);
  // Simply color paths in blue
  return Color::blue + path + Color::reset;
}

std::string
SyntaxHighlightPlugin::highlight_options(const std::string &option) {
  spdlog::trace("Highlighting option: '{}'", option);
  // Color options in yellow
  return Color::yellow + option + Color::reset;
}

std::string SyntaxHighlightPlugin::highlight_strings(const std::string &input) {
  spdlog::trace("Highlighting string literals in: '{}'", input);
  std::string result = input;

  // Highlight double-quoted strings
  spdlog::trace("Processing double-quoted strings...");
  std::regex double_quotes("\"([^\"]*)\"");
  result = std::regex_replace(result, double_quotes,
                              Color::magenta + "\"$1\"" + Color::reset);

  // Highlight single-quoted strings
  spdlog::trace("Processing single-quoted strings...");
  std::regex single_quotes("'([^']*)'");
  result = std::regex_replace(result, single_quotes,
                              Color::magenta + "'$1'" + Color::reset);

  spdlog::trace("String highlighting complete: '{}'", result);
  return result;
}

} // namespace shell