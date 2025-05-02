#pragma once

#include <string>

namespace shell {

/**
 * @namespace Color
 * @brief ANSI color codes for shell output
 */
namespace Color {
inline const std::string reset = "\033[0m";
inline const std::string bold = "\033[1m";
inline const std::string dim = "\033[2m";
inline const std::string italic = "\033[3m";
inline const std::string underline = "\033[4m";

inline const std::string black = "\033[30m";
inline const std::string red = "\033[31m";
inline const std::string green = "\033[32m";
inline const std::string yellow = "\033[33m";
inline const std::string blue = "\033[34m";
inline const std::string magenta = "\033[35m";
inline const std::string cyan = "\033[36m";
inline const std::string white = "\033[37m";

inline const std::string bg_black = "\033[40m";
inline const std::string bg_red = "\033[41m";
inline const std::string bg_green = "\033[42m";
inline const std::string bg_yellow = "\033[43m";
inline const std::string bg_blue = "\033[44m";
inline const std::string bg_magenta = "\033[45m";
inline const std::string bg_cyan = "\033[46m";
inline const std::string bg_white = "\033[47m";

/**
 * @brief Check if terminal supports color
 * @return true if terminal supports color, false otherwise
 */
inline bool supports_color() {
  const char *term = std::getenv("TERM");
  if (!term) {
    return false;
  }

  static const char *color_terms[] = {
      "xterm",           "xterm-color", "xterm-256color", "screen",
      "screen-256color", "tmux",        "tmux-256color",  "rxvt",
      "rxvt-unicode",    "linux",       "cygwin",         "ansi",
      "vt100",           "konsole"};

  std::string term_str(term);
  for (const char *color_term : color_terms) {
    if (term_str == color_term) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Disable color output
 */
inline void disable_color() {
  const_cast<std::string &>(reset) = "";
  const_cast<std::string &>(bold) = "";
  const_cast<std::string &>(dim) = "";
  const_cast<std::string &>(italic) = "";
  const_cast<std::string &>(underline) = "";

  const_cast<std::string &>(black) = "";
  const_cast<std::string &>(red) = "";
  const_cast<std::string &>(green) = "";
  const_cast<std::string &>(yellow) = "";
  const_cast<std::string &>(blue) = "";
  const_cast<std::string &>(magenta) = "";
  const_cast<std::string &>(cyan) = "";
  const_cast<std::string &>(white) = "";

  const_cast<std::string &>(bg_black) = "";
  const_cast<std::string &>(bg_red) = "";
  const_cast<std::string &>(bg_green) = "";
  const_cast<std::string &>(bg_yellow) = "";
  const_cast<std::string &>(bg_blue) = "";
  const_cast<std::string &>(bg_magenta) = "";
  const_cast<std::string &>(bg_cyan) = "";
  const_cast<std::string &>(bg_white) = "";
}
} // namespace Color

} // namespace shell