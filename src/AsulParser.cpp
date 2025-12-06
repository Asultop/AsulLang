#include "AsulParser.h"

#include <sstream>
#include <stdexcept>

namespace asul {

Parser::Parser(const std::vector<Token>& t, const std::string& src)
	: tokens(t), source(src) {}

std::vector<StmtPtr> Parser::parse() {
	std::vector<StmtPtr> stmts;
	while (!isAtEnd()) stmts.push_back(declaration());
	return stmts;
}

bool Parser::isAtEnd() const { return peek().type == TokenType::EndOfFile; }
const Token& Parser::peek() const { return tokens[current]; }
const Token& Parser::previous() const { return tokens[current-1]; }
const Token& Parser::advance() { if (!isAtEnd()) current++; return previous(); }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }

bool Parser::match(std::initializer_list<TokenType> types) {
	for (auto t : types) if (check(t)) { advance(); return true; }
	return false;
}

const Token& Parser::consume(TokenType type, const char* message) {
	if (check(type)) return advance();
	const Token& tok = peek();
	std::ostringstream oss;
	oss << "[Parse] " << message << " at line " << tok.line << ", column " << tok.column << "\n";
	oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
	throw std::runtime_error(oss.str());
}

std::string Parser::getLineText(int line) const {
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

std::vector<Token> Parser::parseQualifiedIdentifiers(const char* message) {
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

std::string Parser::joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const {
	std::string res;
	for (size_t i = begin; i < end && i < parts.size(); ++i) {
		if (i > begin) res.push_back('.');
		res += parts[i].lexeme;
	}
	return res;
}

StmtPtr Parser::declaration() {
	bool isExported = false;
	if (match({TokenType::Export})) {
		isExported = true;
	}
	if (match({TokenType::Async})) { consume(TokenType::Function, "Expect 'function' after 'async'"); return functionDecl(true, isExported); }
	if (match({TokenType::Function})) return functionDecl(false, isExported);
	if (match({TokenType::Class})) return classDeclaration(isExported);
	if (match({TokenType::Extends})) return extendsDeclaration(); // extends cannot be exported
	if (match({TokenType::Interface})) return interfaceDeclaration(isExported);
	if (match({TokenType::Import})) return importDeclaration(false);
	if (match({TokenType::From})) return importDeclaration(true);
	if (match({TokenType::Let, TokenType::Var, TokenType::Const})) return varDeclaration(isExported);
	if (isExported) throw std::runtime_error("Unexpected 'export' before statement");
	return statement();
}

StmtPtr Parser::importDeclaration(bool isFrom) {
	auto imp = std::make_shared<ImportStmt>();
	if (isFrom) {
		// Support both: from Package import name | from "file" import name
		if (match({TokenType::String})) {
			// from "file" import symbol | from "file" import (a b ...)
			Token t = previous();
			auto filePath = t.lexeme;
			consume(TokenType::Import, "Expect 'import' after file path");
			if (match({TokenType::LeftParen})) {
				while (!check(TokenType::RightParen) && !isAtEnd()) {
					auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
					ImportStmt::Entry e; e.isFile = true; e.filePath = filePath; e.symbol = nameTok.lexeme; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
					if (match({TokenType::As})) {
						e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
					}
					imp->entries.push_back(e);
					(void)match({TokenType::Comma});
				}
				consume(TokenType::RightParen, "Expect ')' after import list");
				consume(TokenType::Semicolon, "Expect ';' after import statement");
				return imp;
			} else {
				auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
				ImportStmt::Entry e; e.isFile = true; e.filePath = filePath; e.symbol = nameTok.lexeme; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
				}
				imp->entries.push_back(e);
				consume(TokenType::Semicolon, "Expect ';' after import statement");
				return imp;
			}
		}
		// from Package import name | from Package import (name1 name2 ...)
		// fallthrough to existing package handling
		auto pkgParts = parseQualifiedIdentifiers("Expect package name after 'from'");
		auto pkg = joinIdentifiers(pkgParts, 0, pkgParts.size());
		consume(TokenType::Import, "Expect 'import' after package name");
		if (match({TokenType::LeftParen})) {
			while (!check(TokenType::RightParen) && !isAtEnd()) {
				auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
				ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
				}
				imp->entries.push_back(e);
				(void)match({TokenType::Comma});
			}
			consume(TokenType::RightParen, "Expect ')' after import list");
		} else {
			auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
			ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
			if (match({TokenType::As})) {
				e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
			}
			imp->entries.push_back(e);
		}
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	}

	// import Package.* | import Package.(a b ...) | import (Pkg.a Pkg.b ...) | import "file" | import ("f1" "f2" ...)
	if (match({TokenType::LeftParen})) {
		// import (Pkg.a Pkg.b ...) or ("file1" "file2" ...)
		while (!check(TokenType::RightParen) && !isAtEnd()) {
			if (match({TokenType::String})) {
				// file import entry from string literal
				Token t = previous();
				ImportStmt::Entry e; e.isFile = true; e.filePath = t.lexeme; e.line = t.line; e.column = t.column; e.length = t.length; imp->entries.push_back(e);
				} else {
					auto parts = parseQualifiedIdentifiers("Expect package symbol");
					if (parts.size() < 2) throw std::runtime_error("import list entries must reference package.symbol");
					auto symTok = parts.back();
					auto pkg = joinIdentifiers(parts, 0, parts.size()-1);
					ImportStmt::Entry e; e.packageName = pkg; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
					if (match({TokenType::As})) {
						e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
					}
					imp->entries.push_back(e);
				}
			(void)match({TokenType::Comma});
		}
		consume(TokenType::RightParen, "Expect ')' after import list");
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	}
	// Support: import "file" [as alias];  OR keep existing package import forms
	if (match({TokenType::String})) {
		Token t = previous();
		ImportStmt::Entry e; e.isFile = true; e.filePath = t.lexeme; e.line = t.line; e.column = t.column; e.length = t.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	}
	auto pathParts = parseQualifiedIdentifiers("Expect package name");
	if (check(TokenType::Dot)) {
		consume(TokenType::Dot, "Expect '.' after package name");
		auto pkgName = joinIdentifiers(pathParts, 0, pathParts.size());
		if (match({TokenType::Star})) {
			Token starTok = previous();
			ImportStmt::Entry e; e.packageName = pkgName; e.symbol = std::string("*"); e.isFile = false; e.line = starTok.line; e.column = starTok.column; e.length = std::max(1, starTok.length); imp->entries.push_back(e);
			consume(TokenType::Semicolon, "Expect ';' after import statement");
			return imp;
		}
		if (match({TokenType::LeftParen})) {
			while (!check(TokenType::RightParen) && !isAtEnd()) {
				auto symTok = consume(TokenType::Identifier, "Expect symbol name");
				ImportStmt::Entry e; e.packageName = pkgName; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
				}
				imp->entries.push_back(e);
				(void)match({TokenType::Comma});
			}
			consume(TokenType::RightParen, "Expect ')' after symbol list");
			consume(TokenType::Semicolon, "Expect ';' after import statement");
			return imp;
		}
		std::ostringstream oss;
		oss << "Expect '*' or '(' after package '.' at line " << peek().line << ", column " << peek().column;
		throw std::runtime_error(oss.str());
	} else if (pathParts.size() >= 2) {
		auto symTok = pathParts.back();
		auto pkgName = joinIdentifiers(pathParts, 0, pathParts.size() - 1);
		ImportStmt::Entry e; e.packageName = pkgName; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	} else {
		// Allow shorthand imports like `import json;` and map them to top-level package `json`.
		// Do NOT implicitly place packages under `std.`; keep top-level packages independent.
		auto tok = pathParts.back();
		std::string shorthandPkg = tok.lexeme;
		// Use special symbol marker to indicate binding the package object itself
		ImportStmt::Entry e; e.packageName = shorthandPkg; e.symbol = std::string("__module__"); e.isFile = false; e.line = tok.line; e.column = tok.column; e.length = tok.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	}
}

StmtPtr Parser::interfaceDeclaration(bool isExported) {
	// 语法：interface Name ; | interface Name { function sig(...); ... }
	auto nameTok = consume(TokenType::Identifier, "Expect interface name");
	auto st = std::make_shared<InterfaceStmt>(); st->name = nameTok.lexeme; st->isExported = isExported;
	if (match({TokenType::Semicolon})) return st;
	consume(TokenType::LeftBrace, "Expect '{' before interface body");
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		(void)match({TokenType::Async}); // 忽略 async 关键字
		(void)match({TokenType::Function});
		auto mname = consume(TokenType::Identifier, "Expect method name").lexeme;
		consume(TokenType::LeftParen, "Expect '('");
		// 跳过参数列表
		if (!check(TokenType::RightParen)) {
			do {
				(void)consume(TokenType::Identifier, "Expect parameter name");
				// optional type annotation after parameter name
				if (match({TokenType::Colon})) { (void)consume(TokenType::Identifier, "Expect type name after ':'"); }
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "Expect ')'");
		// 检查是否有函数体（不允许）
		if (check(TokenType::LeftBrace)) {
			const Token& tok = peek();
			std::ostringstream oss;
			oss << "Interface methods cannot have function bodies. Use ';' instead of '{...}' at line " << tok.line << ", column " << tok.column << "\n";
			oss << "Method '" << mname << "' in interface '" << st->name << "' should be declared as: function " << mname << "(...);";
			throw std::runtime_error(oss.str());
		}
		consume(TokenType::Semicolon, "Expect ';' after interface method signature");
		st->methodNames.push_back(mname);
	}
	consume(TokenType::RightBrace, "Expect '}' after interface body");
	// 允许可选分号：`interface Name { ... };`
	(void)match({TokenType::Semicolon});
	return st;
}

StmtPtr Parser::classDeclaration(bool isExported) {
	auto nameTok = consume(TokenType::Identifier, "Expect class name");
	auto cls = std::make_shared<ClassStmt>();
	cls->name = nameTok.lexeme;
	cls->isExported = isExported;
	// 支持三种：class Name ; | class Name <- Supers | class Name [<- Supers] { ... }
	if (match({TokenType::Semicolon})) return cls; // 空类声明
	if (match({TokenType::LeftArrow}) || match({TokenType::Extends})) {
		// 解析父类：单个或 (A,B,...)
		if (match({TokenType::LeftParen})) {
			do { cls->superNames.push_back(consume(TokenType::Identifier, "Expect base class name").lexeme); } while (match({TokenType::Comma}));
			consume(TokenType::RightParen, "Expect ')' after base list");
		} else {
			cls->superNames.push_back(consume(TokenType::Identifier, "Expect base class name").lexeme);
		}
	}
	if (match({TokenType::LeftBrace})) {
		while (!check(TokenType::RightBrace) && !isAtEnd()) {
			bool isStatic = match({TokenType::Static});
			bool isAsync = match({TokenType::Async});
			(void)match({TokenType::Function});
			auto mname = consume(TokenType::Identifier, "Expect method name").lexeme;
			consume(TokenType::LeftParen, "Expect '('");
			std::vector<Param> params;
			if (!check(TokenType::RightParen)) {
				do {
					auto pname = consume(TokenType::Identifier, "Expect parameter name").lexeme;
					std::optional<std::string> ptype = std::nullopt;
					if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "Expect type name after ':'").lexeme;
					params.emplace_back(pname, ptype);
				} while (match({TokenType::Comma}));
			}
			consume(TokenType::RightParen, "Expect ')'");
			// optional return type
			std::optional<std::string> retType = std::nullopt;
			if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "Expect return type name after ':'").lexeme;
			auto body = statement();
			cls->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, retType, isStatic));
		}
		consume(TokenType::RightBrace, "Expect '}' after class body");
		// 可选分号：class Name { ... };
		(void)match({TokenType::Semicolon});
	}
	return cls;
}

StmtPtr Parser::extendsDeclaration() {
	// 语法：extends Name { methods }
	auto nameTok = consume(TokenType::Identifier, "Expect class name after 'extends'");
	consume(TokenType::LeftBrace, "Expect '{' before extension body");
	auto ext = std::make_shared<ExtendStmt>();
	ext->name = nameTok.lexeme;
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		bool isAsync = match({TokenType::Async});
		(void)match({TokenType::Function});
		auto mname = consume(TokenType::Identifier, "Expect method name").lexeme;
		consume(TokenType::LeftParen, "Expect '('");
		std::vector<Param> params;
		if (!check(TokenType::RightParen)) {
			do {
				auto pname = consume(TokenType::Identifier, "Expect parameter name").lexeme;
				std::optional<std::string> ptype = std::nullopt;
				if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "Expect type name after ':'").lexeme;
				params.emplace_back(pname, ptype);
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "Expect ')'");
		// optional return type
		std::optional<std::string> retType = std::nullopt;
		if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "Expect return type name after ':'").lexeme;
		auto body = statement();
		ext->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, retType));
	}
	consume(TokenType::RightBrace, "Expect '}' after extension body");
	// 可选分号：extends Name { ... };
	(void)match({TokenType::Semicolon});
	return ext;
}

StmtPtr Parser::functionDecl(bool isAsync, bool isExported) {
	auto name = consume(TokenType::Identifier, "Expect function name").lexeme;
	consume(TokenType::LeftParen, "Expect '('");
	std::vector<Param> params;
	bool hasRest = false;
	bool hasDefault = false;  // 跟踪是否有默认参数
	if (!check(TokenType::RightParen)) {
		do {
			// Check for rest parameter: ...paramName
			bool isRest = false;
			if (match({TokenType::Ellipsis})) {
				if (hasRest) {
					throw std::runtime_error("Only one rest parameter allowed");
				}
				isRest = true;
				hasRest = true;
			}
			
			auto pname = consume(TokenType::Identifier, "Expect parameter name").lexeme;
			std::optional<std::string> ptype = std::nullopt;
			if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "Expect type name after ':'").lexeme;
			
			// Check for default value: param = defaultExpr
			ExprPtr defaultValue = nullptr;
			if (match({TokenType::Equal})) {
				if (isRest) {
					throw std::runtime_error("Rest parameter cannot have a default value");
				}
				if (hasRest) {
					throw std::runtime_error("Default parameter cannot come after rest parameter");
				}
				defaultValue = assignment();  // 解析默认值表达式
				hasDefault = true;
			} else if (hasDefault && !isRest) {
				throw std::runtime_error("Required parameter cannot follow default parameter");
			}
			
			params.emplace_back(pname, ptype, isRest, defaultValue);
			
			// Rest parameter must be last
			if (isRest && !check(TokenType::RightParen)) {
				throw std::runtime_error("Rest parameter must be last");
			}
		} while (match({TokenType::Comma}));
	}
	consume(TokenType::RightParen, "Expect ')'");
	// optional return type (accept ':' or '->')
	std::optional<std::string> retType = std::nullopt;
	if (match({TokenType::Colon, TokenType::Arrow})) retType = consume(TokenType::Identifier, "Expect return type name after ':' or '->'").lexeme;
	auto body = statement();
	return std::make_shared<FunctionStmt>(name, params, body, isAsync, retType, false, isExported);
}

StmtPtr Parser::varDeclaration(bool isExported) {
	auto name = consume(TokenType::Identifier, "Expect variable name").lexeme;
	std::optional<std::string> type = std::nullopt;
	ExprPtr typeExpr = nullptr;
	if (match({TokenType::Colon})) {
		// allow a non-assignment expression (e.g. typeof(...)) as the type annotation
		typeExpr = logicalOr();
	}
	ExprPtr init;
	if (match({TokenType::Equal})) init = expression();
	consume(TokenType::Semicolon, "Expect ';' after variable declaration");
	return std::make_shared<VarDecl>(name, type, typeExpr, init, isExported);
}

StmtPtr Parser::statement() {
	if (match({TokenType::If})) return ifStatement();
	if (match({TokenType::While})) return whileStatement();
	if (match({TokenType::Do})) return doWhileStatement();
	if (match({TokenType::For})) return forStatement();
	if (match({TokenType::ForEach})) return forEachStatement();
	if (match({TokenType::Switch})) return switchStatement();
	if (match({TokenType::Return})) return returnStatement();
	if (match({TokenType::Throw})) { auto v = expression(); consume(TokenType::Semicolon, "Expect ';' after throw"); return std::make_shared<ThrowStmt>(v); }
	// 空语句：允许单独的 ';'，不执行任何操作（支持多连分号）
	if (match({TokenType::Semicolon})) { return std::make_shared<EmptyStmt>(); }
	if (match({TokenType::Try})) {
		// try 后接任意语句（通常为块）
		auto tryB = statement();
		consume(TokenType::Catch, "Expect 'catch' after try block");
		consume(TokenType::LeftParen, "Expect '(' after catch");
		auto name = consume(TokenType::Identifier, "Expect identifier in catch").lexeme;
		consume(TokenType::RightParen, "Expect ')' after catch param");
		auto catchB = statement();
		// Optional finally block
		StmtPtr finallyB = nullptr;
		if (match({TokenType::Finally})) {
			finallyB = statement();
		}
		return std::make_shared<TryCatchStmt>(tryB, name, catchB, finallyB);
	}
	if (match({TokenType::Go})) { auto expr = expression(); consume(TokenType::Semicolon, "Expect ';' after go call"); return std::make_shared<GoStmt>(expr); }
	if (match({TokenType::Break})) { consume(TokenType::Semicolon, "Expect ';' after break"); return std::make_shared<BreakStmt>(); }
	if (match({TokenType::Continue})) { consume(TokenType::Semicolon, "Expect ';' after continue"); return std::make_shared<ContinueStmt>(); }
	if (match({TokenType::LeftBrace})) return std::make_shared<BlockStmt>(block());
	return expressionStatement();
}

StmtPtr Parser::forStatement() {
	consume(TokenType::LeftParen, "Expect '('");
	StmtPtr init;
	if (match({TokenType::Semicolon})) {
		init = nullptr;
	} else if (match({TokenType::Let, TokenType::Var, TokenType::Const})) {
		init = varDeclaration();
	} else {
		init = expressionStatement();
	}
	ExprPtr cond = nullptr;
	if (!check(TokenType::Semicolon)) cond = expression();
	consume(TokenType::Semicolon, "Expect ';' after loop condition");
	ExprPtr post = nullptr;
	if (!check(TokenType::RightParen)) post = expression();
	consume(TokenType::RightParen, "Expect ')' after for clauses");
	auto body = statement();
	return std::make_shared<ForStmt>(init, cond, post, body);
}

StmtPtr Parser::forEachStatement() {
	// foreach (varName in iterable) body
	consume(TokenType::LeftParen, "Expect '(' after 'foreach'");
	
	if (!check(TokenType::Identifier)) {
		const Token& tok = peek();
		std::ostringstream oss;
		oss << "Expect variable name in foreach at line " << tok.line << "\n";
		oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
		throw std::runtime_error(oss.str());
	}
	std::string varName = advance().lexeme;
	
	consume(TokenType::In, "Expect 'in' after variable name in foreach");
	
	ExprPtr iterable = expression();
	consume(TokenType::RightParen, "Expect ')' after foreach clauses");
	auto body = statement();
	
	return std::make_shared<ForEachStmt>(varName, iterable, body);
}

StmtPtr Parser::switchStatement() {
	// switch (expr) { case val: ... case val2: ... default: ... }
	consume(TokenType::LeftParen, "Expect '(' after 'switch'");
	ExprPtr expr = expression();
	consume(TokenType::RightParen, "Expect ')' after switch expression");
	consume(TokenType::LeftBrace, "Expect '{' after switch header");
	
	std::vector<SwitchStmt::CaseClause> cases;
	
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		if (match({TokenType::Case})) {
			// case value:
			ExprPtr caseValue = expression();
			consume(TokenType::Colon, "Expect ':' after case value");
			
			// Collect statements until next case/default or closing brace
			std::vector<StmtPtr> caseBody;
			while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
				caseBody.push_back(statement());
			}
			
			cases.push_back({caseValue, caseBody});
		} else if (match({TokenType::Default})) {
			// default:
			consume(TokenType::Colon, "Expect ':' after 'default'");
			
			// Collect statements until next case or closing brace
			std::vector<StmtPtr> defaultBody;
			while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
				defaultBody.push_back(statement());
			}
			
			cases.push_back({nullptr, defaultBody}); // nullptr indicates default case
		} else {
			const Token& tok = peek();
			std::ostringstream oss;
			oss << "Expect 'case' or 'default' in switch body at line " << tok.line << "\n";
			oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
			throw std::runtime_error(oss.str());
		}
	}
	
	consume(TokenType::RightBrace, "Expect '}' after switch body");
	return std::make_shared<SwitchStmt>(expr, cases);
}

StmtPtr Parser::returnStatement() {
	Token kw = previous();
	ExprPtr val;
	if (!check(TokenType::Semicolon)) val = expression();
	consume(TokenType::Semicolon, "Expect ';' after return value");
	return std::make_shared<ReturnStmt>(kw, val);
}

StmtPtr Parser::ifStatement() {
	consume(TokenType::LeftParen, "Expect '('");
	auto cond = expression();
	consume(TokenType::RightParen, "Expect ')'");
	auto thenB = statement();
	StmtPtr elseB;
	if (match({TokenType::Else})) elseB = statement();
	return std::make_shared<IfStmt>(cond, thenB, elseB);
}

StmtPtr Parser::whileStatement() {
	consume(TokenType::LeftParen, "Expect '('");
	auto cond = expression();
	consume(TokenType::RightParen, "Expect ')'");
	auto body = statement();
	return std::make_shared<WhileStmt>(cond, body);
}

StmtPtr Parser::doWhileStatement() {
	auto body = statement();
	consume(TokenType::While, "Expect 'while' after do-loop body");
	consume(TokenType::LeftParen, "Expect '(' after 'while'");
	auto cond = expression();
	consume(TokenType::RightParen, "Expect ')' after condition");
	consume(TokenType::Semicolon, "Expect ';' after do-while condition");
	return std::make_shared<DoWhileStmt>(cond, body);
}

std::vector<StmtPtr> Parser::block() {
	std::vector<StmtPtr> stmts;
	while (!check(TokenType::RightBrace) && !isAtEnd()) stmts.push_back(declaration());
	consume(TokenType::RightBrace, "Expect '}' after block");
	return stmts;
}

StmtPtr Parser::expressionStatement() {
	auto expr = expression();
	consume(TokenType::Semicolon, "Expect ';' after expression");
	return std::make_shared<ExprStmt>(expr);
}

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
	auto expr = conditional();
	
	// Logical assignment operators: ??=, &&=, ||= (short-circuiting)
	if (match({TokenType::QuestionQuestionEqual, TokenType::AndAndEqual, TokenType::OrOrEqual})) {
		Token op = previous();
		auto value = assignment();
		
		// These operators only assign if the condition is met:
		// x ??= y  =>  x ?? (x = y)   (assigns only if x is null/undefined)
		// x &&= y  =>  x && (x = y)   (assigns only if x is truthy)
		// x ||= y  =>  x || (x = y)   (assigns only if x is falsy)
		
		// Convert to logical expression with assignment in right operand
		TokenType logicalOp;
		switch (op.type) {
			case TokenType::QuestionQuestionEqual: logicalOp = TokenType::QuestionQuestion; break;
			case TokenType::AndAndEqual: logicalOp = TokenType::AndAnd; break;
			case TokenType::OrOrEqual: logicalOp = TokenType::OrOr; break;
			default: logicalOp = TokenType::QuestionQuestion; break;
		}
		
		// Create assignment expression for right side
		ExprPtr assignExpr;
		if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
			assignExpr = std::make_shared<AssignExpr>(var->name, value, var->line);
		} else if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			assignExpr = std::make_shared<SetPropExpr>(getp->object, getp->name, value, getp->line, getp->column, getp->length);
		} else if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			assignExpr = std::make_shared<SetIndexExpr>(idx->object, idx->index, value, idx->line, idx->column, idx->length);
		} else {
			const Token& tok = op;
			std::ostringstream oss;
			oss << "Invalid assignment target for logical assignment at line " << tok.line << ", column " << tok.column << "\n";
			oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
			throw std::runtime_error(oss.str());
		}
		
		Token logicalToken{logicalOp, op.lexeme, op.line};
		return std::make_shared<LogicalExpr>(expr, logicalToken, assignExpr);
	}
	
	// 复合赋值运算符：+=, -=, *=, /=, %=
	if (match({TokenType::PlusEqual, TokenType::MinusEqual, TokenType::StarEqual, TokenType::SlashEqual, TokenType::PercentEqual})) {
		Token op = previous();
		auto value = assignment();
		
		// 转换为 x = x op value
		TokenType binaryOp;
		switch (op.type) {
			case TokenType::PlusEqual: binaryOp = TokenType::Plus; break;
			case TokenType::MinusEqual: binaryOp = TokenType::Minus; break;
			case TokenType::StarEqual: binaryOp = TokenType::Star; break;
			case TokenType::SlashEqual: binaryOp = TokenType::Slash; break;
			case TokenType::PercentEqual: binaryOp = TokenType::Percent; break;
			default: binaryOp = TokenType::Plus; break;
		}
		Token binaryToken{binaryOp, op.lexeme, op.line};
		auto binaryExpr = std::make_shared<BinaryExpr>(expr, binaryToken, value);
		
		if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
			return std::make_shared<AssignExpr>(var->name, binaryExpr, var->line);
		}
		if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			return std::make_shared<SetPropExpr>(getp->object, getp->name, binaryExpr, getp->line, getp->column, getp->length);
		}
		if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			return std::make_shared<SetIndexExpr>(idx->object, idx->index, binaryExpr, idx->line, idx->column, idx->length);
		}
		{
			const Token& tok = op;
			std::ostringstream oss;
			oss << "Invalid assignment target at line " << tok.line << ", column " << tok.column << "\n";
			oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
			throw std::runtime_error(oss.str());
		}
	}
	
	if (match({TokenType::Equal})) {
		auto value = assignment();
		if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
			return std::make_shared<AssignExpr>(var->name, value, var->line);
		}
		if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			return std::make_shared<SetPropExpr>(getp->object, getp->name, value, getp->line, getp->column, getp->length);
		}
		if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			return std::make_shared<SetIndexExpr>(idx->object, idx->index, value, idx->line, idx->column, idx->length);
		}
		{
			const Token& tok = previous();
			std::ostringstream oss;
			oss << "Invalid assignment target at line " << tok.line << ", column " << tok.column << "\n";
			oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
			throw std::runtime_error(oss.str());
		}
	}
	return expr;
}

ExprPtr Parser::conditional() {
	// 三元运算符：condition ? thenExpr : elseExpr
	auto expr = nullishCoalescing();
	
	if (match({TokenType::Question})) {
		Token questionToken = previous();
		auto thenBranch = expression();  // 递归调用 expression 以支持嵌套
		
		if (!match({TokenType::Colon})) {
			const Token& tok = peek();
			std::ostringstream oss;
			oss << "Expect ':' after then branch in ternary operator at line " << tok.line << "\n";
			oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
			throw std::runtime_error(oss.str());
		}
		
		auto elseBranch = conditional();  // 右结合，支持 a ? b : c ? d : e
		return std::make_shared<ConditionalExpr>(expr, thenBranch, elseBranch, questionToken.line, questionToken.column, std::max(1, questionToken.length));
	}
	
	return expr;
}

ExprPtr Parser::nullishCoalescing() {
	// Nullish coalescing: expr ?? defaultExpr
	auto expr = logicalOr();
	while (match({TokenType::QuestionQuestion})) {
		Token op = previous();
		auto right = logicalOr();
		expr = std::make_shared<LogicalExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::logicalOr() {
	auto expr = logicalAnd();
	while (match({TokenType::OrOr})) {
		Token op = previous();
		auto right = logicalAnd();
		expr = std::make_shared<LogicalExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::logicalAnd() {
	auto expr = bitwiseOr();
	while (match({TokenType::AndAnd})) {
		Token op = previous();
		auto right = bitwiseOr();
		expr = std::make_shared<LogicalExpr>(expr, op, right);
	}
	return expr;
}

// bitwise OR level
ExprPtr Parser::bitwiseOr() {
	auto expr = bitwiseXor();
	while (match({TokenType::Pipe})) {
		Token op = previous();
		auto right = bitwiseXor();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::bitwiseXor() {
	auto expr = bitwiseAnd();
	while (match({TokenType::Caret})) {
		Token op = previous();
		auto right = bitwiseAnd();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::bitwiseAnd() {
	auto expr = equality();
	while (match({TokenType::Ampersand})) {
		Token op = previous();
		auto right = equality();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::equality() {
	auto expr = comparison();
	while (match({TokenType::BangEqual, TokenType::EqualEqual, TokenType::StrictEqual, TokenType::StrictNotEqual})) {
		Token op = previous();
		auto right = comparison();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::comparison() {
	auto expr = shift();
	while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual, TokenType::MatchInterface})) {
		Token op = previous();
		auto right = shift();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::shift() {
	auto expr = term();
	while (match({TokenType::ShiftLeft, TokenType::ShiftRight})) {
		Token op = previous();
		auto right = term();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::term() {
	auto expr = factor();
	while (match({TokenType::Plus, TokenType::Minus})) {
		Token op = previous();
		auto right = factor();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::factor() {
	auto expr = unary();
	while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
		Token op = previous();
		auto right = unary();
		expr = std::make_shared<BinaryExpr>(expr, op, right);
	}
	return expr;
}

ExprPtr Parser::unary() {
	// 前置递增/递减：++x, --x
	if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
		Token op = previous();
		auto operand = unary();
		return std::make_shared<UpdateExpr>(op, operand, true, op.line, op.column, std::max(1, op.length));
	}
	if (match({TokenType::Bang, TokenType::Minus, TokenType::Tilde})) {
		Token op = previous();
		auto right = unary();
		return std::make_shared<UnaryExpr>(op, right);
	}
	if (match({TokenType::Await})) {
		Token awTok = previous();
		auto inner = unary();
		return std::make_shared<AwaitExpr>(inner, awTok.line, awTok.column, std::max(1, awTok.length));
	}
	return postfix();
}

ExprPtr Parser::postfix() {
	auto expr = call();
	// 后置递增/递减：x++, x--
	if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
		Token op = previous();
		return std::make_shared<UpdateExpr>(op, expr, false, op.line, op.column, std::max(1, op.length));
	}
	return expr;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
	std::vector<ExprPtr> args;
	if (!check(TokenType::RightParen)) {
		do { args.push_back(expression()); } while (match({TokenType::Comma}));
	}
	Token rp = consume(TokenType::RightParen, "Expect ')' after arguments");
	return std::make_shared<CallExpr>(callee, args, rp.line, rp.column, std::max(1, rp.length));
}

ExprPtr Parser::call() {
	auto expr = primary();
	for (;;) {
		if (match({TokenType::LeftParen})) expr = finishCall(expr);
		else if (match({TokenType::Dot})) {
			std::string name; Token nameTok;
			if (check(TokenType::Identifier)) { nameTok = advance(); name = nameTok.lexeme; }
			else if (check(TokenType::Catch)) { nameTok = advance(); name = nameTok.lexeme; /* allow .catch */ }
			else {
				const Token& tok = peek();
				std::ostringstream oss;
				oss << "[Parse] Expect property name after '.' at line " << tok.line << ", column " << tok.column << "\n";
				oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
				throw std::runtime_error(oss.str());
			}
			expr = std::make_shared<GetPropExpr>(expr, name, nameTok.line, nameTok.column, std::max(1, nameTok.length));
		}
		else if (match({TokenType::LeftBracket})) {
			Token lb = previous();
			auto idx = expression();
			consume(TokenType::RightBracket, "Expect ']' after index");
			expr = std::make_shared<IndexExpr>(expr, idx, lb.line, lb.column, 1);
		}
		else break;
	}
	return expr;
}

ExprPtr Parser::primary() {
	// 支持匿名函数：[](x, y){ ... }
	if (check(TokenType::LeftBracket)) {
		// 仅当模式为 [] ( 开始时，识别为 lambda；否则按数组字面量
		if (current + 2 < tokens.size() && tokens[current].type == TokenType::LeftBracket && tokens[current+1].type == TokenType::RightBracket && tokens[current+2].type == TokenType::LeftParen) {
			advance(); // [
			advance(); // ]
			advance(); // (
			std::vector<Param> params;
			bool hasRest = false;
			bool hasDefault = false;
			if (!check(TokenType::RightParen)) {
				do {
					// Check for rest parameter: ...paramName
					bool isRest = false;
					if (match({TokenType::Ellipsis})) {
						if (hasRest) {
							throw std::runtime_error("Only one rest parameter allowed");
						}
						isRest = true;
						hasRest = true;
					}
					
					auto pname = consume(TokenType::Identifier, "Expect parameter name").lexeme;
					std::optional<std::string> ptype = std::nullopt;
					if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "Expect type name after ':'").lexeme;
					
					// Check for default value
					ExprPtr defaultValue = nullptr;
					if (match({TokenType::Equal})) {
						if (isRest) {
							throw std::runtime_error("Rest parameter cannot have a default value");
						}
						if (hasRest) {
							throw std::runtime_error("Default parameter cannot come after rest parameter");
						}
						defaultValue = assignment();
						hasDefault = true;
					} else if (hasDefault && !isRest) {
						throw std::runtime_error("Required parameter cannot follow default parameter");
					}
					
					params.emplace_back(pname, ptype, isRest, defaultValue);
					
					// Rest parameter must be last
					if (isRest && !check(TokenType::RightParen)) {
						throw std::runtime_error("Rest parameter must be last");
					}
				} while (match({TokenType::Comma}));
			}
			consume(TokenType::RightParen, "Expect ')' after lambda parameters");
			auto body = statement();
			return std::make_shared<FunctionExpr>(params, body);
		}
	}
	if (match({TokenType::New})) {
		Token newTok = previous();
		Token nameTok = consume(TokenType::Identifier, "Expect class name after 'new'");
		ExprPtr callee = std::make_shared<VariableExpr>(nameTok.lexeme, nameTok.line, nameTok.column, nameTok.length);
		while (match({TokenType::Dot})) {
			Token propTok = consume(TokenType::Identifier, "Expect property name after '.'");
			callee = std::make_shared<GetPropExpr>(callee, propTok.lexeme, propTok.line, propTok.column, propTok.length);
		}
		consume(TokenType::LeftParen, "Expect '('");
		std::vector<ExprPtr> args;
		if (!check(TokenType::RightParen)) { do { args.push_back(expression()); } while (match({TokenType::Comma})); }
		consume(TokenType::RightParen, "Expect ')'");
		return std::make_shared<NewExpr>(callee, args, newTok.line, newTok.column, std::max(1, newTok.length));
	}
	if (match({TokenType::False})) return std::make_shared<LiteralExpr>(Value{false});
	if (match({TokenType::True})) return std::make_shared<LiteralExpr>(Value{true});
	if (match({TokenType::Null})) return std::make_shared<LiteralExpr>(Value{std::monostate{}});
	if (match({TokenType::Number})) return std::make_shared<LiteralExpr>(Value{std::stod(previous().lexeme)});
	if (match({TokenType::String})) {
		auto tok = previous();
		const std::string& s = tok.lexeme;
		if (s.find("${") == std::string::npos) {
			return std::make_shared<LiteralExpr>(Value{s});
		}
		return parseInterpolatedString(s, tok.line, tok.column, std::max(1, tok.length));
	}
	if (match({TokenType::Identifier})) { auto tok = previous(); return std::make_shared<VariableExpr>(tok.lexeme, tok.line, tok.column, tok.length); }
	if (match({TokenType::LeftBracket})) {
		std::vector<ExprPtr> elems;
		if (!check(TokenType::RightBracket)) {
			do {
				if (match({TokenType::Ellipsis})) {
					// spread element
					auto spreadTok = previous();
					auto inner = expression();
					elems.push_back(std::make_shared<SpreadExpr>(inner, spreadTok.line, spreadTok.column, spreadTok.length));
				} else {
					elems.push_back(expression());
				}
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightBracket, "Expect ']' after array literal");
		return std::make_shared<ArrayLiteralExpr>(elems);
	}
	if (match({TokenType::LeftBrace})) {
		std::vector<ObjectLiteralExpr::Prop> props;
		if (!check(TokenType::RightBrace)) {
			do {
				ObjectLiteralExpr::Prop p{};

				if (match({TokenType::Ellipsis})) {
					auto spreadTok = previous();
					p.isSpread = true;
					p.value = expression();
					p.line = spreadTok.line; p.column = spreadTok.column; p.length = spreadTok.length;
				} else {
					if (match({TokenType::Identifier})) { p.computed = false; p.name = previous().lexeme; }
					else if (match({TokenType::String})) { p.computed = false; p.name = previous().lexeme; }
					else if (match({TokenType::LeftBracket})) {
						p.computed = true; p.keyExpr = expression();
						consume(TokenType::RightBracket, "Expect ']' after computed key");
					}
					else throw std::runtime_error("Expect property name in object literal");
					consume(TokenType::Colon, "Expect ':' after property name");
					p.value = expression();
				}
				props.push_back(std::move(p));
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightBrace, "Expect '}' after object literal");
		return std::make_shared<ObjectLiteralExpr>(props);
	}
	if (match({TokenType::LeftParen})) { auto e = expression(); consume(TokenType::RightParen, "Expect ')'"); return e; }
	{
		const Token& tok = peek();
		std::ostringstream oss;
		oss << "Expect expression at line " << tok.line << ", column " << tok.column << "\n";
		oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
		throw std::runtime_error(oss.str());
	}
}

// --- 插值字符串支持："hello ${expr} world" -> 通过'+'串联 ---
ExprPtr Parser::parseInterpolatedString(const std::string& s, int line, int column, int length) {
	std::vector<ExprPtr> parts;
	std::string raw;
	auto flushRaw = [&](){ if (!raw.empty()) { parts.push_back(std::make_shared<LiteralExpr>(Value{raw})); raw.clear(); } };
	for (size_t i=0;i<s.size();) {
		if (s[i] == '$' && i+1 < s.size() && s[i+1] == '{') {
			flushRaw();
			size_t startPos = i; // 记录 ${ 的起始位置
			i += 2; // skip ${
			int depth = 1; bool inStr = false; bool esc = false;
			std::string exprText;
			for (; i < s.size(); ++i) {
				char c = s[i];
				if (inStr) {
					if (esc) { esc = false; exprText.push_back(c); continue; }
					if (c == '\\') { esc = true; exprText.push_back(c); continue; }
					if (c == '"') { inStr = false; exprText.push_back(c); continue; }
					exprText.push_back(c); continue;
				}
				if (c == '"') { inStr = true; exprText.push_back(c); continue; }
				if (c == '{') { depth++; exprText.push_back(c); continue; }
				if (c == '}') { depth--; if (depth == 0) { ++i; break; } exprText.push_back(c); continue; }
				exprText.push_back(c);
			}
			// 计算插值表达式在源代码中的准确列位置
			// column 是字符串开始的列，加上开头的引号(1)，再加上字符串内的偏移
			int interpolationColumn = column + 1 + startPos;
			int interpolationLength = i - startPos; // 包含 ${ ... } 的完整长度
			// 解析 exprText 为表达式
			parts.push_back(parseExprSnippet(exprText, line, interpolationColumn, interpolationLength));
			continue;
		}
		raw.push_back(s[i]);
		++i;
	}
	flushRaw();
	if (parts.empty()) return std::make_shared<LiteralExpr>(Value{std::string("")});
	// 折叠为加号连接
	ExprPtr acc = parts[0];
	for (size_t i=1;i<parts.size();++i) {
		Token plusTok{TokenType::Plus, "+", line};
		acc = std::make_shared<BinaryExpr>(acc, plusTok, parts[i]);
	}
	return acc;
}

ExprPtr Parser::parseExprSnippet(const std::string& code, int line, int column, int length) {
	// 将子表达式封装为一个独立的解析： (expr);
	// 使用括号避免以 '{' 开头被误判为块语句。
	std::string snippet = "(";
	snippet += code;
	snippet += ")";
	snippet.push_back(';');
	Lexer lx(snippet);
	auto toks = lx.scanTokens();
	Parser sub(toks, snippet);
	std::vector<StmtPtr> stmts;
	try {
		stmts = sub.parse();
	} catch (const std::exception& e) {
		// 捕获子解析器的异常，重新抛出为包含正确位置信息的异常
		// 不包含行文本和箭头，让 printErrorWithContext 来格式化
		std::ostringstream oss;
		oss << "Expect expression at line " << line << ", column " << column << ", length " << length;
		throw std::runtime_error(oss.str());
	}
	if (stmts.empty()) {
		std::ostringstream oss;
		oss << "Empty interpolation expression at line " << line << ", column " << column << ", length " << length;
		throw std::runtime_error(oss.str());
	}
	if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmts[0])) return es->expr;
	std::ostringstream oss;
	oss << "Invalid interpolation expression at line " << line << ", column " << column << ", length " << length;
	throw std::runtime_error(oss.str());
}

} // namespace asul
