#ifndef ASUL_PARSER_H
#define ASUL_PARSER_H

#include "AsulLexer.h"
#include <memory>
#include <string>
#include <vector>

// Forward declarations for AST used by Parser interface
struct Stmt; struct Expr; using StmtPtr = std::shared_ptr<Stmt>; using ExprPtr = std::shared_ptr<Expr>;

// Parser interface extracted from engine; concrete AST definitions remain
// in the engine until full migration. This header provides a stable surface.
class AsulParser {
public:
    explicit AsulParser(const std::vector<Token>& tokens, const std::string& source);
    std::vector<StmtPtr> parse();
protected:
    // Source & tokens
    const std::vector<Token>& tokens;
    const std::string& source;

    // Cursor state for parsing
    size_t current {0};

    // Navigation helpers (mirrors engine's parser utilities)
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    bool check(TokenType type) const;
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const char* message);

    // Qualified identifier helpers
    std::vector<Token> parseQualifiedIdentifiers(const char* message);
    std::string joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const;
};

#endif // ASUL_PARSER_H
