#ifndef ASUL_PARSER_H
#define ASUL_PARSER_H

#include "AsulAst.h"
#include "AsulLexer.h"

#include <memory>
#include <string>
#include <vector>

namespace asul {

struct ParseError {
	int line;
	int column;
	int length;
	std::string message;
};

// ----------- Parser -----------
class Parser {
public:
	explicit Parser(const std::vector<Token>& t, const std::string& src);
	std::vector<StmtPtr> parse();
	const std::vector<ParseError>& getErrors() const { return errors; }

private:
	const std::vector<Token>& tokens;
	size_t current{0};
	const std::string& source;
	std::vector<ParseError> errors;

	void synchronize();

	bool isAtEnd() const;
	const Token& peek() const;
	const Token& previous() const;
	const Token& advance();
	bool check(TokenType type) const;
	bool match(std::initializer_list<TokenType> types);
	const Token& consume(TokenType type, const char* message);
	[[noreturn]] void error(const char* message);
	[[noreturn]] void error(const Token& token, const char* message);

	std::string getLineText(int line) const;
	std::vector<Token> parseQualifiedIdentifiers(const char* message);
	std::string joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const;

	PatternPtr parsePattern();
	PatternPtr parseArrayPattern();
	PatternPtr parseObjectPattern();

	StmtPtr declaration();
	StmtPtr importDeclaration(bool isFrom);
	StmtPtr interfaceDeclaration(bool isExported = false);
	StmtPtr classDeclaration(bool isExported = false);
	StmtPtr extendsDeclaration();
	StmtPtr functionDecl(bool isAsync, bool isExported = false);
	StmtPtr varDeclaration(bool isExported = false);
	StmtPtr statement();
	StmtPtr forStatement();
	StmtPtr forEachStatement();
	StmtPtr switchStatement();
	StmtPtr matchStatement();
	StmtPtr returnStatement();
	StmtPtr ifStatement();
	StmtPtr whileStatement();
	StmtPtr doWhileStatement();
	std::vector<StmtPtr> block();
	StmtPtr expressionStatement();

	ExprPtr expression();
	ExprPtr assignment();
	ExprPtr conditional();
	ExprPtr nullishCoalescing();
	ExprPtr logicalOr();
	ExprPtr logicalAnd();
	ExprPtr bitwiseOr();
	ExprPtr bitwiseXor();
	ExprPtr bitwiseAnd();
	ExprPtr equality();
	ExprPtr comparison();
	ExprPtr shift();
	ExprPtr term();
	ExprPtr factor();
	ExprPtr unary();
	ExprPtr postfix();
	ExprPtr finishCall(ExprPtr callee);
	ExprPtr call();
	ExprPtr primary();

	ExprPtr parseInterpolatedString(const std::string& s, int line, int column, int length);
	ExprPtr parseExprSnippet(const std::string& code, int line, int column, int length);
};

} // namespace asul

#endif // ASUL_PARSER_H
