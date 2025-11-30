#include "AsulParser.h"
#include <stdexcept>
#include <sstream>

static std::runtime_error makeParseError(const Token& t, const std::string& msg) {
    // Keep message style consistent; engine attaches stack and formatting later
    std::ostringstream oss;
    oss << "Undefined variable '" << t.lexeme << "' at line " << t.line
        << ", column " << t.column << ", length " << t.length;
    return std::runtime_error(msg.empty() ? oss.str() : msg);
}

AsulParser::AsulParser(const std::vector<Token>& t, const std::string& src)
    : tokens(t), source(src) {}

std::vector<StmtPtr> AsulParser::parse() {
    // Placeholder: actual implementation remains in ALangEngine.cpp.
    // This file exists to establish the API and allow progressive migration.
    return {};
}

const Token& AsulParser::peek() const {
    if (current >= tokens.size()) return tokens.back();
    return tokens[current];
}

const Token& AsulParser::previous() const {
    if (current == 0) return tokens[0];
    return tokens[current - 1];
}

bool AsulParser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile || current >= tokens.size();
}

bool AsulParser::check(TokenType type) const {
    return !isAtEnd() && peek().type == type;
}

bool AsulParser::match(std::initializer_list<TokenType> types) {
    for (auto t : types) {
        if (check(t)) { current++; return true; }
    }
    return false;
}

const Token& AsulParser::consume(TokenType type, const char* message) {
    if (check(type)) { current++; return previous(); }
    // Throwing here is temporary; engine will convert to its error type.
    throw makeParseError(peek(), message ? message : "parse error");
}
