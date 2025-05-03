#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tokenizer.hpp"

namespace shell {

// Forward declarations
class Command;
class Pipeline;
class CommandSequence;

/**
 * @class ASTNode
 * @brief Base class for Abstract Syntax Tree nodes
 *
 * ASTNode serves as the base class for all nodes in the shell's abstract syntax
 * tree. It defines the common interface that all AST nodes must implement.
 */
class ASTNode {
public:
  /**
   * @brief Virtual destructor to ensure proper cleanup of derived classes
   */
  virtual ~ASTNode() = default;

  /**
   * @brief Converts the node to a string representation
   * @return String representation of the node
   */
  virtual std::string to_string() const = 0;
};

/**
 * @class Command
 * @brief Represents a simple command with arguments and redirections
 *
 * This class represents a single shell command with its name, arguments,
 * and any input/output redirections applied to it.
 */
class Command : public ASTNode {
public:
  /**
   * @brief Constructs a command with a name and arguments
   * @param name Name of the command to execute
   * @param args Vector of argument strings
   */
  Command(std::string name, std::vector<std::string> args);

  /**
   * @brief Gets the command name
   * @return Reference to the command name
   */
  const std::string &name() const { return name_; }

  /**
   * @brief Gets the command arguments
   * @return Reference to the vector of argument strings
   */
  const std::vector<std::string> &args() const { return args_; }

  /**
   * @brief Sets the input redirection file
   * @param file Filename or path to redirect input from
   */
  void set_input_redirect(std::string file);

  /**
   * @brief Sets the output redirection file
   * @param file Filename or path to redirect output to
   * @param append Whether to append to the file (true) or overwrite it (false)
   */
  void set_output_redirect(std::string file, bool append = false);

  /**
   * @brief Gets the input redirection file if set
   * @return Optional containing the input redirection file path
   */
  const std::optional<std::string> &input_redirect() const {
    return input_redirect_;
  }

  /**
   * @brief Gets the output redirection file if set
   * @return Optional containing the output redirection file path
   */
  const std::optional<std::string> &output_redirect() const {
    return output_redirect_;
  }

  /**
   * @brief Checks if output redirection is in append mode
   * @return True if appending to output file, false if overwriting
   */
  bool is_append() const { return append_; }

  /**
   * @brief Checks if the command should run in the background
   * @return True if the command is marked for background execution
   */
  bool is_background() const { return background_; }

  /**
   * @brief Sets whether the command should run in the background
   * @param background True to run in background, false for foreground
   */
  void set_background(bool background) { background_ = background; }

  /**
   * @brief Converts the command to a string representation
   * @return String representation of the command
   */
  std::string to_string() const override;

private:
  std::string name_;              ///< Name of the command to execute
  std::vector<std::string> args_; ///< Arguments to pass to the command
  std::optional<std::string>
      input_redirect_; ///< Input redirection file, if any
  std::optional<std::string>
      output_redirect_;     ///< Output redirection file, if any
  bool append_ = false;     ///< Whether output redirection should append
  bool background_ = false; ///< Whether command runs in background
};

/**
 * @class Pipeline
 * @brief Represents a pipeline of commands (cmd1 | cmd2 | cmd3)
 *
 * This class represents a sequence of commands connected by pipes,
 * where the output of each command is fed as input to the next command.
 */
class Pipeline : public ASTNode {
public:
  /**
   * @brief Constructs a pipeline with a vector of commands
   * @param commands Vector of command pointers representing the pipeline
   */
  Pipeline(std::vector<std::shared_ptr<Command>> commands);

  /**
   * @brief Gets the commands in the pipeline
   * @return Reference to vector of command pointers
   */
  const std::vector<std::shared_ptr<Command>> &commands() const {
    return commands_;
  }

  /**
   * @brief Checks if the pipeline should run in the background
   * @return True if the pipeline is marked for background execution
   */
  bool is_background() const { return background_; }

  /**
   * @brief Sets whether the pipeline should run in the background
   * @param background True to run in background, false for foreground
   */
  void set_background(bool background) { background_ = background; }

  /**
   * @brief Converts the pipeline to a string representation
   * @return String representation of the pipeline
   */
  std::string to_string() const override;

private:
  std::vector<std::shared_ptr<Command>> commands_; ///< Commands in the pipeline
  bool background_ = false; ///< Whether pipeline runs in background
};

/**
 * @class CommandSequence
 * @brief Represents a sequence of commands (cmd1 && cmd2 ; cmd3)
 *
 * This class represents a sequence of commands or pipelines that are
 * executed according to specified separators (AND or semicolon).
 */
class CommandSequence : public ASTNode {
public:
  /**
   * @enum Separator
   * @brief Types of command separators in a sequence
   */
  enum class Separator {
    And, ///< Logical AND (&&) - next command runs only if previous succeeds
    Semicolon ///< Command separator (;) - next command always runs
  };

  /**
   * @struct Element
   * @brief A command or pipeline with its following separator
   */
  struct Element {
    std::shared_ptr<ASTNode> node;      ///< The command or pipeline
    std::optional<Separator> separator; ///< The separator that follows, if any
  };

  /**
   * @brief Constructs a command sequence with elements
   * @param elements Vector of elements in the sequence
   */
  CommandSequence(std::vector<Element> elements);

  /**
   * @brief Gets the elements in the sequence
   * @return Reference to vector of elements
   */
  const std::vector<Element> &elements() const { return elements_; }

  /**
   * @brief Converts the command sequence to a string representation
   * @return String representation of the command sequence
   */
  std::string to_string() const override;

private:
  std::vector<Element> elements_; ///< Elements in the command sequence
};

/**
 * @class Parser
 * @brief Parses tokens into an abstract syntax tree
 *
 * This class converts a sequence of tokens produced by the tokenizer
 * into an abstract syntax tree (AST) representing the shell command structure.
 */
class Parser {
public:
  /**
   * @brief Default constructor
   */
  Parser();

  /**
   * @brief Parse tokens into an abstract syntax tree
   * @param tokens Vector of tokens to parse
   * @return Shared pointer to the root AST node, or nullptr if parsing fails
   */
  std::shared_ptr<ASTNode> parse(const std::vector<Token> &tokens);

  /**
   * @brief Get the error message if parsing fails
   * @return Error message string
   */
  std::string get_error_message() const;

private:
  /**
   * @brief Parse a command sequence (highest level in grammar)
   * @return Shared pointer to CommandSequence node
   */
  std::shared_ptr<CommandSequence> parse_command_sequence();

  /**
   * @brief Parse a pipeline of commands
   * @return Shared pointer to Pipeline node
   */
  std::shared_ptr<Pipeline> parse_pipeline();

  /**
   * @brief Parse a simple command
   * @return Shared pointer to Command node
   */
  std::shared_ptr<Command> parse_command();

  std::string error_message_; ///< Stores any error message from parsing

  std::vector<Token> tokens_; ///< Tokens being parsed
  size_t current_token_;      ///< Index of current token being processed

  /**
   * @brief Get the current token
   * @return Reference to the current token
   */
  const Token &current() const;

  /**
   * @brief Peek at a future token without advancing
   * @param offset Number of tokens to peek ahead
   * @return Reference to the token at current position + offset
   */
  const Token &peek(size_t offset = 1) const;

  /**
   * @brief Advance to the next token
   */
  void advance();

  /**
   * @brief Check if current token matches expected type and advance if it does
   * @param type Token type to match
   * @return True if token matched and consumed, false otherwise
   */
  bool match(TokenType type);

  /**
   * @brief Check if parser has reached the end of tokens
   * @return True if at end, false otherwise
   */
  bool is_at_end() const;
};

} // namespace shell