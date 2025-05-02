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
 */
class ASTNode {
public:
  virtual ~ASTNode() = default;
  virtual std::string to_string() const = 0;
};

/**
 * @class Command
 * @brief Represents a simple command with arguments and redirections
 */
class Command : public ASTNode {
public:
  Command(std::string name, std::vector<std::string> args);

  const std::string &name() const { return name_; }
  const std::vector<std::string> &args() const { return args_; }

  void set_input_redirect(std::string file);
  void set_output_redirect(std::string file, bool append = false);

  const std::optional<std::string> &input_redirect() const {
    return input_redirect_;
  }
  const std::optional<std::string> &output_redirect() const {
    return output_redirect_;
  }
  bool is_append() const { return append_; }

  bool is_background() const { return background_; }
  void set_background(bool background) { background_ = background; }

  std::string to_string() const override;

private:
  std::string name_;
  std::vector<std::string> args_;
  std::optional<std::string> input_redirect_;
  std::optional<std::string> output_redirect_;
  bool append_ = false;
  bool background_ = false;
};

/**
 * @class Pipeline
 * @brief Represents a pipeline of commands (cmd1 | cmd2 | cmd3)
 */
class Pipeline : public ASTNode {
public:
  Pipeline(std::vector<std::shared_ptr<Command>> commands);

  const std::vector<std::shared_ptr<Command>> &commands() const {
    return commands_;
  }

  bool is_background() const { return background_; }
  void set_background(bool background) { background_ = background; }

  std::string to_string() const override;

private:
  std::vector<std::shared_ptr<Command>> commands_;
  bool background_ = false;
};

/**
 * @class CommandSequence
 * @brief Represents a sequence of commands (cmd1 && cmd2 ; cmd3)
 */
class CommandSequence : public ASTNode {
public:
  enum class Separator {
    And,      // &&
    Semicolon // ;
  };

  struct Element {
    std::shared_ptr<ASTNode> node;
    std::optional<Separator> separator;
  };

  CommandSequence(std::vector<Element> elements);

  const std::vector<Element> &elements() const { return elements_; }

  std::string to_string() const override;

private:
  std::vector<Element> elements_;
};

/**
 * @class Parser
 * @brief Parses tokens into an abstract syntax tree
 */
class Parser {
public:
  Parser();

  // Parse tokens into an AST
  std::shared_ptr<ASTNode> parse(const std::vector<Token> &tokens);

  // Get error message if parsing fails
  std::string get_error_message() const;

private:
  // Helper methods for recursive descent parsing
  std::shared_ptr<CommandSequence> parse_command_sequence();
  std::shared_ptr<Pipeline> parse_pipeline();
  std::shared_ptr<Command> parse_command();

  // Error handling
  std::string error_message_;

  // Token stream state
  std::vector<Token> tokens_;
  size_t current_token_;

  // Token navigation
  const Token &current() const;
  const Token &peek(size_t offset = 1) const;
  void advance();
  bool match(TokenType type);
  bool is_at_end() const;
};

} // namespace shell