#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace shell {

/**
 * @brief Trim whitespace from the beginning of a string
 * @param s String to trim
 * @return Trimmed string
 */
inline std::string ltrim(const std::string &s) {
  auto start = s.begin();
  while (start != s.end() && std::isspace(*start)) {
    ++start;
  }
  return std::string(start, s.end());
}

/**
 * @brief Trim whitespace from the end of a string
 * @param s String to trim
 * @return Trimmed string
 */
inline std::string rtrim(const std::string &s) {
  auto end = s.end();
  while (end != s.begin() && std::isspace(*(end - 1))) {
    --end;
  }
  return std::string(s.begin(), end);
}

/**
 * @brief Trim whitespace from both ends of a string
 * @param s String to trim
 * @return Trimmed string
 */
inline std::string trim(const std::string &s) { return rtrim(ltrim(s)); }

/**
 * @brief Split a string by a delimiter
 * @param s String to split
 * @param delimiter Character to split by
 * @return Vector of split strings
 */
inline std::vector<std::string> split_string(const std::string &s,
                                             char delimiter) {
  std::vector<std::string> tokens;
  std::stringstream ss(s);
  std::string token;

  while (std::getline(ss, token, delimiter)) {
    tokens.push_back(token);
  }

  return tokens;
}

/**
 * @brief Replace all occurrences of a substring
 * @param s Source string
 * @param from Substring to replace
 * @param to Replacement string
 * @return Modified string
 */
inline std::string replace_all(const std::string &s, const std::string &from,
                               const std::string &to) {
  std::string result = s;
  size_t pos = 0;

  while ((pos = result.find(from, pos)) != std::string::npos) {
    result.replace(pos, from.length(), to);
    pos += to.length();
  }

  return result;
}

/**
 * @brief Escape special characters in a string
 * @param s String to escape
 * @return Escaped string
 */
inline std::string escape_string(const std::string &s) {
  std::string result;
  result.reserve(s.size());

  for (char c : s) {
    switch (c) {
    case '\\':
      result += "\\\\";
      break;
    case '\"':
      result += "\\\"";
      break;
    case '\'':
      result += "\\\'";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
    }
  }

  return result;
}

/**
 * @brief Unescape special characters in a string
 * @param s String to unescape
 * @return Unescaped string
 */
inline std::string unescape_string(const std::string &s) {
  std::string result;
  result.reserve(s.size());

  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
      case '\\':
        result += '\\';
        break;
      case '\"':
        result += '\"';
        break;
      case '\'':
        result += '\'';
        break;
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      default:
        result += s[i + 1];
      }
      ++i;
    } else {
      result += s[i];
    }
  }

  return result;
}

} // namespace shell