#include "parser.hpp"

#include <sstream>

namespace shell {

Command::Command(std::string name, std::vector<std::string> args)
    : name_(std::move(name)), args_(std::move(args)) {
  // Add the command name as the first argument
  args_.insert(args_.begin(), name_);
}

void Command::set_input_redirect(std::string file) {
  input_redirect_ = std::move(file);
}

void Command::set_output_redirect(std::string file, bool append) {
  output_redirect_ = std::move(file);
  append_ = append;
}

std::string Command::to_string() const {
  std::stringstream ss;

  ss << name_;

  for (size_t i = 1; i < args_.size(); ++i) {
    ss << " " << args_[i];
  }

  if (input_redirect_) {
    ss << " < " << *input_redirect_;
  }

  if (output_redirect_) {
    if (append_) {
      ss << " >> " << *output_redirect_;
    } else {
      ss << " > " << *output_redirect_;
    }
  }

  if (background_) {
    ss << " &";
  }

  return ss.str();
}

Pipeline::Pipeline(std::vector<std::shared_ptr<Command>> commands)
    : commands_(std::move(commands)) {}

std::string Pipeline::to_string() const {
  std::stringstream ss;

  for (size_t i = 0; i < commands_.size(); ++i) {
    if (i > 0) {
      ss << " | ";
    }
    ss << commands_[i]->to_string();
  }

  if (background_) {
    ss << " &";
  }

  return ss.str();
}

CommandSequence::CommandSequence(std::vector<Element> elements)
    : elements_(std::move(elements)) {}

std::string CommandSequence::to_string() const {
  std::stringstream ss;

  for (size_t i = 0; i < elements_.size(); ++i) {
    const auto &element = elements_[i];

    ss << element.node->to_string();

    if (element.separator) {
      switch (*element.separator) {
      case Separator::And:
        ss << " && ";
        break;
      case Separator::Semicolon:
        ss << "; ";
        break;
      }
    }
  }

  return ss.str();
}

Parser::Parser() : current_token_(0) {}

std::shared_ptr<ASTNode> Parser::parse(const std::vector<Token> &tokens) {
  tokens_ = tokens;
  current_token_ = 0;
  error_message_.clear();

  try {
    // Empty input
    if (tokens_.empty() || tokens_[0].type == TokenType::EndOfInput) {
      return nullptr;
    }

    auto result = parse_command_sequence();

    // Check if we consumed all tokens
    if (!is_at_end() && current().type != TokenType::EndOfInput) {
      error_message_ = "Unexpected token: " + current().value;
      return nullptr;
    }

    return result;
  } catch (const std::exception &e) {
    error_message_ = std::string("Parser error: ") + e.what();
    return nullptr;
  }
}

std::string Parser::get_error_message() const { return error_message_; }

std::shared_ptr<CommandSequence> Parser::parse_command_sequence() {
  std::vector<CommandSequence::Element> elements;

  // Parse the first pipeline or command
  std::shared_ptr<ASTNode> node;

  // Look ahead to see if we have a pipeline or just a command
  bool is_pipeline = false;
  for (size_t i = current_token_; i < tokens_.size(); ++i) {
    if (tokens_[i].type == TokenType::Pipe) {
      is_pipeline = true;
      break;
    } else if (tokens_[i].type == TokenType::And ||
               tokens_[i].type == TokenType::Semicolon) {
      break;
    }
  }

  if (is_pipeline) {
    node = parse_pipeline();
  } else {
    node = parse_command();
  }

  if (!node) {
    return nullptr;
  }

  elements.push_back({node, std::nullopt});

  // Parse additional commands in the sequence
  while (!is_at_end() && (current().type == TokenType::And ||
                          current().type == TokenType::Semicolon)) {
    CommandSequence::Separator separator;

    if (current().type == TokenType::And) {
      separator = CommandSequence::Separator::And;
    } else {
      separator = CommandSequence::Separator::Semicolon;
    }

    advance(); // Consume the separator

    // Parse the next pipeline or command
    is_pipeline = false;
    for (size_t i = current_token_; i < tokens_.size(); ++i) {
      if (tokens_[i].type == TokenType::Pipe) {
        is_pipeline = true;
        break;
      } else if (tokens_[i].type == TokenType::And ||
                 tokens_[i].type == TokenType::Semicolon) {
        break;
      }
    }

    if (is_pipeline) {
      node = parse_pipeline();
    } else {
      node = parse_command();
    }

    if (!node) {
      return nullptr;
    }

    // Update the separator for the previous element
    elements.back().separator = separator;

    // Add the new element
    elements.push_back({node, std::nullopt});
  }

  return std::make_shared<CommandSequence>(elements);
}

std::shared_ptr<Pipeline> Parser::parse_pipeline() {
  std::vector<std::shared_ptr<Command>> commands;

  // Parse the first command
  auto command = parse_command();
  if (!command) {
    return nullptr;
  }

  commands.push_back(command);

  // Parse additional commands in the pipeline
  while (match(TokenType::Pipe)) {
    // Parse the next command
    command = parse_command();
    if (!command) {
      return nullptr;
    }

    commands.push_back(command);
  }

  // Create the pipeline
  auto pipeline = std::make_shared<Pipeline>(commands);

  // Check for background execution
  if (match(TokenType::Background)) {
    pipeline->set_background(true);
  }

  return pipeline;
}

std::shared_ptr<Command> Parser::parse_command() {
  // Command must start with a word
  if (current().type != TokenType::Word &&
      current().type != TokenType::DoubleQuoted &&
      current().type != TokenType::SingleQuoted &&
      current().type != TokenType::Variable) {
    error_message_ = "Expected command name, got " + current().value;
    return nullptr;
  }

  // Get the command name
  std::string name = current().value;
  advance();

  // Parse arguments
  std::vector<std::string> args;

  while (!is_at_end() && current().type != TokenType::Pipe &&
         current().type != TokenType::And &&
         current().type != TokenType::Semicolon &&
         current().type != TokenType::RedirectIn &&
         current().type != TokenType::RedirectOut &&
         current().type != TokenType::RedirectAppend &&
         current().type != TokenType::Background &&
         current().type != TokenType::EndOfInput) {
    args.push_back(current().value);
    advance();
  }

  // Create the command
  auto command = std::make_shared<Command>(name, args);

  // Parse redirections
  while (!is_at_end() && (current().type == TokenType::RedirectIn ||
                          current().type == TokenType::RedirectOut ||
                          current().type == TokenType::RedirectAppend)) {
    if (current().type == TokenType::RedirectIn) {
      advance();

      if (is_at_end() || (current().type != TokenType::Word &&
                          current().type != TokenType::DoubleQuoted &&
                          current().type != TokenType::SingleQuoted &&
                          current().type != TokenType::Variable)) {
        error_message_ = "Expected filename after <";
        return nullptr;
      }

      command->set_input_redirect(current().value);
      advance();
    } else if (current().type == TokenType::RedirectOut) {
      advance();

      if (is_at_end() || (current().type != TokenType::Word &&
                          current().type != TokenType::DoubleQuoted &&
                          current().type != TokenType::SingleQuoted &&
                          current().type != TokenType::Variable)) {
        error_message_ = "Expected filename after >";
        return nullptr;
      }

      command->set_output_redirect(current().value, false);
      advance();
    } else if (current().type == TokenType::RedirectAppend) {
      advance();

      if (is_at_end() || (current().type != TokenType::Word &&
                          current().type != TokenType::DoubleQuoted &&
                          current().type != TokenType::SingleQuoted &&
                          current().type != TokenType::Variable)) {
        error_message_ = "Expected filename after >>";
        return nullptr;
      }

      command->set_output_redirect(current().value, true);
      advance();
    }
  }

  // Check for background execution
  if (match(TokenType::Background)) {
    command->set_background(true);
  }

  return command;
}

const Token &Parser::current() const {
  if (is_at_end()) {
    static Token eof_token{TokenType::EndOfInput, "", tokens_.size()};
    return eof_token;
  }

  return tokens_[current_token_];
}

const Token &Parser::peek(size_t offset) const {
  if (current_token_ + offset >= tokens_.size()) {
    static Token eof_token{TokenType::EndOfInput, "", tokens_.size()};
    return eof_token;
  }

  return tokens_[current_token_ + offset];
}

void Parser::advance() {
  if (!is_at_end()) {
    current_token_++;
  }
}

bool Parser::match(TokenType type) {
  if (is_at_end() || current().type != type) {
    return false;
  }

  advance();
  return true;
}

bool Parser::is_at_end() const { return current_token_ >= tokens_.size(); }

} // namespace shell