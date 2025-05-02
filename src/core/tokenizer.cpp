#include "tokenizer.hpp"

#include <cctype>

namespace shell {

Tokenizer::Tokenizer() : position_(0) {}

std::vector<Token> Tokenizer::tokenize(std::string_view input) {
    input_ = input;
    position_ = 0;
    std::vector<Token> tokens;
    error_message_.clear();

    while (position_ < input_.size()) {
        auto token = next_token();
        if (!token)
            break;

        tokens.push_back(*token);

        if (token->type == TokenType::EndOfInput) {
            break;
        }
    }

    // Add EndOfInput token if not already added
    if (tokens.empty() || tokens.back().type != TokenType::EndOfInput) {
        tokens.push_back({TokenType::EndOfInput, "", input_.size()});
    }

    return tokens;
}

bool Tokenizer::validate_syntax(const std::vector<Token>& tokens) const {
    if (tokens.empty()) {
        return true;
    }

    // Check for empty command
    if (tokens.size() == 1 && tokens[0].type == TokenType::EndOfInput) {
        return true;
    }

    // Cannot start with these operators
    if (tokens[0].type == TokenType::Pipe || tokens[0].type == TokenType::And ||
        tokens[0].type == TokenType::Semicolon) {
        error_message_ = "Command cannot start with " + tokens[0].value;
        return false;
    }

    // Cannot end with these operators (check before EndOfInput)
    if (tokens.size() >= 2) {
        const auto& last = tokens[tokens.size() - 2];  // Last before EOI
        if (last.type == TokenType::Pipe || last.type == TokenType::And ||
            last.type == TokenType::RedirectIn ||
            last.type == TokenType::RedirectOut ||
            last.type == TokenType::RedirectAppend) {
            error_message_ = "Command cannot end with " + last.value;
            return false;
        }
    }

    // Check redirection syntax
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        const auto& token = tokens[i];

        if (token.type == TokenType::RedirectIn ||
            token.type == TokenType::RedirectOut ||
            token.type == TokenType::RedirectAppend) {
            // Must be followed by a word
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TokenType::Word &&
                 tokens[i + 1].type != TokenType::SingleQuoted &&
                 tokens[i + 1].type != TokenType::DoubleQuoted &&
                 tokens[i + 1].type != TokenType::Variable)) {
                error_message_ = "Redirection operator " + token.value +
                                 " must be followed by a filename";
                return false;
            }
        }
    }

    // Check for multiple consecutive operators
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        const auto& token = tokens[i];
        const auto& next = tokens[i + 1];

        if ((token.type == TokenType::Pipe || token.type == TokenType::And ||
             token.type == TokenType::Semicolon) &&
            (next.type == TokenType::Pipe || next.type == TokenType::And ||
             next.type == TokenType::Semicolon)) {
            error_message_ = "Invalid syntax: consecutive operators " +
                             token.value + " " + next.value;
            return false;
        }
    }

    return true;
}

std::string Tokenizer::get_error_message() const { return error_message_; }

std::optional<Token> Tokenizer::next_token() {
    skip_whitespace();

    if (is_at_end()) {
        return Token{TokenType::EndOfInput, "", position_};
    }

    // Try to match operators first
    auto op_token = try_match_operator();
    if (op_token) {
        return op_token;
    }

    // Handle quoted strings
    if (peek() == '"') {
        return read_quoted_string('"');
    }

    if (peek() == '\'') {
        return read_quoted_string('\'');
    }

    // Handle variables
    if (peek() == '$') {
        return read_variable();
    }

    // Otherwise, read a word
    return read_word();
}

void Tokenizer::skip_whitespace() {
    while (!is_at_end() && std::isspace(peek())) {
        position_++;
    }
}

std::optional<Token> Tokenizer::try_match_operator() {
    size_t start_pos = position_;

    switch (peek()) {
        case '|':
            advance();
            return Token{TokenType::Pipe, "|", start_pos};

        case '&':
            advance();
            if (peek() == '&') {
                advance();
                return Token{TokenType::And, "&&", start_pos};
            }
            return Token{TokenType::Background, "&", start_pos};

        case ';':
            advance();
            return Token{TokenType::Semicolon, ";", start_pos};

        case '<':
            advance();
            return Token{TokenType::RedirectIn, "<", start_pos};

        case '>':
            advance();
            if (peek() == '>') {
                advance();
                return Token{TokenType::RedirectAppend, ">>", start_pos};
            }
            return Token{TokenType::RedirectOut, ">", start_pos};

        case '\n':
            advance();
            return Token{TokenType::Newline, "\n", start_pos};

        default:
            return std::nullopt;
    }
}

Token Tokenizer::read_quoted_string(char quote_char) {
    size_t start_pos = position_;
    advance();  // Skip the opening quote

    std::string value;
    bool escaped = false;

    while (!is_at_end()) {
        char c = peek();

        if (escaped) {
            // Handle escape sequences
            value += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == quote_char) {
            advance();  // Skip the closing quote
            break;
        } else {
            value += c;
        }

        advance();
    }

    return Token{
        quote_char == '"' ? TokenType::DoubleQuoted : TokenType::SingleQuoted,
        value, start_pos};
}

Token Tokenizer::read_word() {
    size_t start_pos = position_;
    std::string value;

    while (!is_at_end()) {
        char c = peek();

        // Stop at whitespace or any of these special characters
        if (std::isspace(c) || c == '|' || c == '&' || c == ';' || c == '<' ||
            c == '>' || c == '"' || c == '\'' || c == '$') {
            break;
        }

        value += c;
        advance();
    }

    return Token{TokenType::Word, value, start_pos};
}

Token Tokenizer::read_variable() {
    size_t start_pos = position_;
    advance();  // Skip the $

    std::string name;

    // Handle ${var} syntax
    if (peek() == '{') {
        advance();  // Skip the {

        while (!is_at_end() && peek() != '}') {
            name += peek();
            advance();
        }

        if (peek() == '}') {
            advance();  // Skip the closing }
        }
    } else {
        // Handle $var syntax
        while (!is_at_end() && (std::isalnum(peek()) || peek() == '_')) {
            name += peek();
            advance();
        }
    }

    return Token{TokenType::Variable, name, start_pos};
}

bool Tokenizer::is_at_end() const { return position_ >= input_.size(); }

char Tokenizer::peek(size_t offset) const {
    if (position_ + offset >= input_.size()) {
        return '\0';
    }
    return input_[position_ + offset];
}

char Tokenizer::advance() {
    if (is_at_end()) {
        return '\0';
    }
    return input_[position_++];
}

}  // namespace shell