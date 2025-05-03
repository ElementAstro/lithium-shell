#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shell {

/**
 * @enum TokenType
 * @brief Types of tokens recognized by the shell parser
 *
 * Defines all possible token types that can be identified during lexical
 * analysis of shell commands and scripts.
 */
enum class TokenType {
  Word,           ///< Regular word/command/argument
  Pipe,           ///< | (Pipe operator for connecting command output to input)
  And,            ///< && (Logical AND operator for conditional execution)
  Semicolon,      ///< ; (Command separator)
  RedirectIn,     ///< < (Input redirection operator)
  RedirectOut,    ///< > (Output redirection operator)
  RedirectAppend, ///< >> (Output redirection with append operator)
  DoubleQuoted,   ///< "text" (Text enclosed in double quotes)
  SingleQuoted,   ///< 'text' (Text enclosed in single quotes)
  Variable,       ///< $var or ${var} (Variable reference)
  Background,     ///< & (Background execution operator)
  Newline,        ///< \n (Line terminator)
  EndOfInput      ///< End of input marker
};

/**
 * @struct Token
 * @brief Represents a lexical token in the input
 *
 * Contains the token type, its textual value, and position information
 * for error reporting and syntax highlighting.
 */
struct Token {
  TokenType type;    ///< Type of the token
  std::string value; ///< Actual text content of the token
  size_t position;   ///< Starting position in the input string

  /**
   * @brief Equality operator for comparing tokens
   * @param other The token to compare with
   * @return True if tokens have same type and value, false otherwise
   */
  bool operator==(const Token &other) const {
    return type == other.type && value == other.value;
  }
};

/**
 * @class Tokenizer
 * @brief Breaks input strings into tokens for parsing
 *
 * Implements a lexical analyzer that converts command strings into a sequence
 * of tokens that can be processed by the shell parser. Handles various shell
 * syntax elements including quotes, variables, and redirection operators.
 */
class Tokenizer {
public:
  /**
   * @brief Default constructor
   */
  Tokenizer();

  /**
   * @brief Tokenizes a command string into individual tokens
   * @param input The command string to tokenize
   * @return Vector of tokens extracted from the input
   */
  std::vector<Token> tokenize(std::string_view input);

  /**
   * @brief Validates the syntax of a token sequence
   * @param tokens The sequence of tokens to validate
   * @return True if syntax is valid, false otherwise
   */
  bool validate_syntax(const std::vector<Token> &tokens) const;

  /**
   * @brief Returns the error message from the last validation attempt
   * @return Error message string if validation failed
   */
  std::string get_error_message() const;

private:
  /**
   * @brief Extracts the next token from the input
   * @return The next token if available, or nullopt if at end of input
   */
  std::optional<Token> next_token();

  /**
   * @brief Advances the position past any whitespace characters
   */
  void skip_whitespace();

  /**
   * @brief Attempts to match an operator token at the current position
   * @return Token if an operator was found, nullopt otherwise
   */
  std::optional<Token> try_match_operator();

  /**
   * @brief Reads a quoted string (either single or double quotes)
   * @param quote_char The quote character (single or double)
   * @return Token containing the quoted string
   */
  Token read_quoted_string(char quote_char);

  /**
   * @brief Reads a word token from the input
   * @return The word token
   */
  Token read_word();

  /**
   * @brief Reads a variable reference token
   * @return The variable token
   */
  Token read_variable();

  /**
   * @brief Checks if tokenization has reached the end of input
   * @return True if at end of input, false otherwise
   */
  bool is_at_end() const;

  /**
   * @brief Looks ahead at characters without advancing position
   * @param offset Number of characters to look ahead
   * @return The character at current position + offset, or '\0' if out of
   * bounds
   */
  char peek(size_t offset = 0) const;

  /**
   * @brief Gets the current character and advances position
   * @return The current character
   */
  char advance();

  mutable std::string error_message_; ///< Stores error message from validation
  std::string_view input_; ///< Reference to the input being tokenized
  size_t position_;        ///< Current position in the input
};

} // namespace shell