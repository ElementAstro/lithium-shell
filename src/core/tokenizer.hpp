#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shell {

/**
 * @enum TokenType
 * @brief Types of tokens recognized by the shell parser
 */
enum class TokenType {
  Word,           // Regular word/command/argument
  Pipe,           // |
  And,            // &&
  Semicolon,      // ;
  RedirectIn,     // <
  RedirectOut,    // >
  RedirectAppend, // >>
  DoubleQuoted,   // "text"
  SingleQuoted,   // 'text'
  Variable,       // $var or ${var}
  Background,     // &
  Newline,        // \n
  EndOfInput      // End of input
};

/**
 * @struct Token
 * @brief Represents a lexical token in the input
 */
struct Token {
  TokenType type;
  std::string value;
  size_t position;

  // Equality operator for testing
  bool operator==(const Token &other) const {
    return type == other.type && value == other.value;
  }
};

/**
 * @class Tokenizer
 * @brief Breaks input strings into tokens for parsing
 */
class Tokenizer {
public:
  Tokenizer();

  // Tokenize a command string
  std::vector<Token> tokenize(std::string_view input);

  // Validate syntax before execution
  bool validate_syntax(const std::vector<Token> &tokens) const;

  // Get error message if validation fails
  std::string get_error_message() const;

private:
  // Helper methods for tokenization
  std::optional<Token> next_token();
  void skip_whitespace();
  std::optional<Token> try_match_operator();
  Token read_quoted_string(char quote_char);
  Token read_word();
  Token read_variable();
  bool is_at_end() const;
  char peek(size_t offset = 0) const;
  char advance();

  // Error handling
  mutable std::string error_message_;

  // Current position in the input
  std::string_view input_;
  size_t position_;
};

} // namespace shell