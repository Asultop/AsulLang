#include "AsulParser.h"
#include "AsulParser.h"
#include <stdexcept>
#include <sstream>

AsulParser::AsulParser(const std::vector<Token>& t, const std::string& src)
    : tokens(t), source(src) {}

std::vector<StmtPtr> AsulParser::parse() {
    // Placeholder: actual implementation remains in ALangEngine.cpp for now.
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
    const Token& tok = peek();
    std::ostringstream oss;
    oss << "[Parse] " << (message ? message : "") << " at line " << tok.line << ", column " << tok.column << "\n";
    oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
    throw std::runtime_error(oss.str());
}

const Token& AsulParser::advance() {
    if (!isAtEnd()) { current++; }
    return previous();
}

std::string AsulParser::getLineText(int line) const {
    if (line <= 0) return std::string();
    int curLine = 1;
    size_t i = 0, startIdx = 0;
    for (; i < source.size(); ++i) {
        if (curLine == line) { startIdx = i; break; }
        if (source[i] == '\n') curLine++;
    }
    if (curLine != line) return std::string();
    size_t j = startIdx;
    while (j < source.size() && source[j] != '\n' && source[j] != '\r') j++;
    return source.substr(startIdx, j - startIdx);
}

std::vector<Token> AsulParser::parseQualifiedIdentifiers(const char* message) {
    auto first = consume(TokenType::Identifier, message);
    std::vector<Token> parts{ first };
    while (peek().type == TokenType::Dot) {
        size_t saved = current;
        advance();
        if (check(TokenType::Identifier)) {
            parts.push_back(advance());
        } else {
            current = saved;
            break;
        }
    }
    return parts;
}

std::string AsulParser::joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const {
    std::string res;
    for (size_t i = begin; i < end && i < parts.size(); ++i) {
        if (i > begin) res.push_back('.');
        res += parts[i].lexeme;
    }
    return res;
}

// importDeclaration remains implemented in ALangEngine.cpp for now; AsulParser
// provides helpers used by that implementation (getLineText, parseQualifiedIdentifiers, joinIdentifiers).
