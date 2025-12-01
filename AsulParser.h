#ifndef ASUL_PARSER_H
#define ASUL_PARSER_H

#include "AsulAST.h"
#include "AsulLexer.h"
#include <memory>
#include <string>
#include <vector>

// Parser for AsulLang - uses asul namespace types
class AsulParser {
public:
    explicit AsulParser(const std::vector<Token>& tokens, const std::string& source);
    std::vector<asul::StmtPtr> parse();
protected:
    // Source & tokens
    const std::vector<Token>& tokens;
    const std::string& source;

    // Cursor state for parsing
    size_t current {0};

    // Navigation helpers
    const Token& peek() const;
    const Token& previous() const;
    const Token& advance();
    bool isAtEnd() const;
    bool check(TokenType type) const;
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const char* message);

    // Qualified identifier helpers
    std::vector<Token> parseQualifiedIdentifiers(const char* message);
    std::string joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const;
    
    // Helper to get source line text for error messages
    std::string getLineText(int line) const;

private:
    // Declaration parsing
    asul::StmtPtr declaration();
    asul::StmtPtr importDeclaration(bool isFrom);
    asul::StmtPtr interfaceDeclaration(bool isExported = false);
    asul::StmtPtr classDeclaration(bool isExported = false);
    asul::StmtPtr extendsDeclaration();
    asul::StmtPtr functionDecl(bool isAsync, bool isExported = false);
    asul::StmtPtr varDeclaration(bool isExported = false);
    
    // Statement parsing
    asul::StmtPtr statement();
    asul::StmtPtr forStatement();
    asul::StmtPtr forEachStatement();
    asul::StmtPtr switchStatement();
    asul::StmtPtr returnStatement();
    asul::StmtPtr ifStatement();
    asul::StmtPtr whileStatement();
    asul::StmtPtr doWhileStatement();
    std::vector<asul::StmtPtr> block();
    asul::StmtPtr expressionStatement();
    
    // Expression parsing
    asul::ExprPtr expression();
    asul::ExprPtr assignment();
    asul::ExprPtr conditional();
    asul::ExprPtr logicalOr();
    asul::ExprPtr logicalAnd();
    asul::ExprPtr bitwiseOr();
    asul::ExprPtr bitwiseXor();
    asul::ExprPtr bitwiseAnd();
    asul::ExprPtr equality();
    asul::ExprPtr comparison();
    asul::ExprPtr shift();
    asul::ExprPtr term();
    asul::ExprPtr factor();
    asul::ExprPtr unary();
    asul::ExprPtr postfix();
    asul::ExprPtr finishCall(asul::ExprPtr callee);
    asul::ExprPtr call();
    asul::ExprPtr primary();
    
    // String interpolation support
    asul::ExprPtr parseInterpolatedString(const std::string& s, int line, int column, int length);
    asul::ExprPtr parseExprSnippet(const std::string& code, int line, int column, int length);
};

#endif // ASUL_PARSER_H
