#include "AsulParser.h"

#include <sstream>
#include <stdexcept>

namespace asul {

static std::string unescapeString(const std::string& in) {
	std::string out; out.reserve(in.size());
	for (size_t i=0; i<in.size(); ++i) {
		char c = in[i];
		if (c == '\\' && i + 1 < in.size()) {
			char n = in[++i];
			switch (n) {
			case 'n': out.push_back('\n'); break;
			case 't': out.push_back('\t'); break;
			case 'r': out.push_back('\r'); break;
			case '\\': out.push_back('\\'); break;
			case '"': out.push_back('"'); break;
			case '\'': out.push_back('\''); break;
			case '0': out.push_back('\0'); break;
			default: out.push_back(n); break;
			}
		} else {
			out.push_back(c);
		}
	}
	return out;
}

class ParserException : public std::exception {};

// Helper function to check if a token type can be used as a property name
// Many keywords can be used as property names in member access expressions
static bool isPropertyNameToken(TokenType type) {
	switch (type) {
		case TokenType::Identifier:
		// Allow keywords as property names (like JavaScript)
		case TokenType::Catch:
		case TokenType::Match:
		case TokenType::Yield:
		case TokenType::Let:
		case TokenType::Var:
		case TokenType::Const:
		case TokenType::Function:
		case TokenType::Return:
		case TokenType::If:
		case TokenType::Else:
		case TokenType::While:
		case TokenType::Do:
		case TokenType::For:
		case TokenType::ForEach:
		case TokenType::In:
		case TokenType::Break:
		case TokenType::Continue:
		case TokenType::Switch:
		case TokenType::Case:
		case TokenType::Default:
		case TokenType::Class:
		case TokenType::Extends:
		case TokenType::New:
		case TokenType::True:
		case TokenType::False:
		case TokenType::Null:
		case TokenType::Await:
		case TokenType::Async:
		case TokenType::Go:
		case TokenType::Try:
		case TokenType::Finally:
		case TokenType::Throw:
		case TokenType::Interface:
		case TokenType::Import:
		case TokenType::From:
		case TokenType::As:
		case TokenType::Export:
		case TokenType::Static:
			return true;
		default:
			return false;
	}
}

Parser::Parser(const std::vector<Token>& t, const std::string& src)
	: tokens(t), source(src) {}

std::vector<StmtPtr> Parser::parse() {
	std::vector<StmtPtr> stmts;
	while (!isAtEnd()) {
		try {
			stmts.push_back(declaration());
		} catch (ParserException&) {
			synchronize();
		}
	}
	// If there were parse errors, throw an exception
	if (!errors.empty()) {
		std::ostringstream oss;
		oss << "[Parse] " << errors[0].message << " at line " << errors[0].line << ", column " << errors[0].column;
		throw std::runtime_error(oss.str());
	}
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
	error(peek(), message);
}

void Parser::error(const char* message) {
	error(peek(), message);
}

void Parser::error(const Token& tok, const char* message) {
	errors.push_back({tok.line, tok.column, tok.length, message});
	throw ParserException();
}

void Parser::synchronize() {
	advance();
	while (!isAtEnd()) {
		if (previous().type == TokenType::Semicolon) return;
		switch (peek().type) {
			case TokenType::Class:
			case TokenType::Function:
			case TokenType::Var:
			case TokenType::For:
			case TokenType::If:
			case TokenType::While:
			case TokenType::Return:
			case TokenType::Import:
			case TokenType::Export:
				return;
			default:
				advance();
		}
	}
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
	// Parse decorators first (if any)
	std::vector<ExprPtr> decorators;
	while (match({TokenType::At})) {
		// @decorator or @decorator(args)
		ExprPtr decoratorExpr = call(); // Parse decorator as a call expression
		decorators.push_back(decoratorExpr);
	}
	
	bool isExported = false;
	if (match({TokenType::Export})) {
		isExported = true;
	}
	
	StmtPtr target = nullptr;
	if (match({TokenType::Async})) { 
		consume(TokenType::Function, "在 'async' 后缺少 'function'"); 
		target = functionDecl(true, isExported); 
	} else if (match({TokenType::Function})) {
		target = functionDecl(false, isExported);
	} else if (match({TokenType::Class})) {
		target = classDeclaration(isExported);
	} else if (match({TokenType::Extends})) {
		if (!decorators.empty()) error(previous(), "装饰器不能应用于 'extends' 声明");
		return extendsDeclaration(); // extends cannot be exported
	} else if (match({TokenType::Interface})) {
		if (!decorators.empty()) error(previous(), "装饰器不能应用于 'interface' 声明");
		return interfaceDeclaration(isExported);
	} else if (match({TokenType::Import})) {
		if (!decorators.empty()) error(previous(), "装饰器不能应用于 'import' 语句");
		return importDeclaration(false);
	} else if (match({TokenType::From})) {
		if (!decorators.empty()) error(previous(), "装饰器不能应用于 'from' 语句");
		return importDeclaration(true);
	} else if (match({TokenType::Let, TokenType::Var, TokenType::Const})) {
		if (!decorators.empty()) error(previous(), "装饰器不能应用于变量声明");
		return varDeclaration(isExported);
	} else {
		if (!decorators.empty()) error("装饰器只能应用于函数或类");
		if (isExported) error("语句前出现意外的 'export'");
		return statement();
	}
	
	// If we have decorators, wrap the target in a DecoratorStmt
	if (!decorators.empty()) {
		return std::make_shared<DecoratorStmt>(decorators, target);
	}
	
	return target;
}

StmtPtr Parser::importDeclaration(bool isFrom) {
	auto imp = std::make_shared<ImportStmt>();
	if (isFrom) {
		// Support both: from Package import name | from "file" import name
		if (match({TokenType::String})) {
			// from "file" import symbol | from "file" import (a b ...)
			Token t = previous();
			auto filePath = unescapeString(t.lexeme);
			consume(TokenType::Import, "文件路径后缺少 'import'");
			if (match({TokenType::LeftParen})) {
				while (!check(TokenType::RightParen) && !isAtEnd()) {
					auto nameTok = consume(TokenType::Identifier, "缺少符号名称");
					ImportStmt::Entry e; e.isFile = true; e.filePath = filePath; e.symbol = nameTok.lexeme; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
					if (match({TokenType::As})) {
						e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
					}
					imp->entries.push_back(e);
					(void)match({TokenType::Comma});
				}
				consume(TokenType::RightParen, "导入列表后缺少 ')'");
				consume(TokenType::Semicolon, "导入语句后缺少 ';'");
				return imp;
			} else {
				auto nameTok = consume(TokenType::Identifier, "缺少符号名称");
				ImportStmt::Entry e; e.isFile = true; e.filePath = filePath; e.symbol = nameTok.lexeme; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
				}
				imp->entries.push_back(e);
				consume(TokenType::Semicolon, "导入语句后缺少 ';'");
				return imp;
			}
		}
		// from Package import name | from Package import (name1 name2 ...)
		// fallthrough to existing package handling
		auto pkgParts = parseQualifiedIdentifiers("'from' 后缺少包名");
		auto pkg = joinIdentifiers(pkgParts, 0, pkgParts.size());
		consume(TokenType::Import, "包名后缺少 'import'");
		if (match({TokenType::LeftParen})) {
			while (!check(TokenType::RightParen) && !isAtEnd()) {
				auto nameTok = consume(TokenType::Identifier, "缺少符号名称");
				ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
				}
				imp->entries.push_back(e);
				(void)match({TokenType::Comma});
			}
			consume(TokenType::RightParen, "导入列表后缺少 ')'");
		} else {
			auto nameTok = consume(TokenType::Identifier, "缺少符号名称");
			ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length;
			if (match({TokenType::As})) {
				e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
			}
			imp->entries.push_back(e);
		}
		consume(TokenType::Semicolon, "导入语句后缺少 ';'");
		return imp;
	}

	// import Package.* | import Package.(a b ...) | import (Pkg.a Pkg.b ...) | import "file" | import ("f1" "f2" ...)
	if (match({TokenType::LeftParen})) {
		// import (Pkg.a Pkg.b ...) or ("file1" "file2" ...)
		while (!check(TokenType::RightParen) && !isAtEnd()) {
			if (match({TokenType::String})) {
				// file import entry from string literal
				Token t = previous();
				ImportStmt::Entry e; e.isFile = true; e.filePath = unescapeString(t.lexeme); e.line = t.line; e.column = t.column; e.length = t.length; imp->entries.push_back(e);
				} else {
					auto parts = parseQualifiedIdentifiers("缺少包符号");
					if (parts.size() < 2) error(parts[0], "导入列表项必须引用 package.symbol");
					auto symTok = parts.back();
					auto pkg = joinIdentifiers(parts, 0, parts.size()-1);
					ImportStmt::Entry e; e.packageName = pkg; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
					if (match({TokenType::As})) {
						e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
					}
					imp->entries.push_back(e);
				}
			(void)match({TokenType::Comma});
		}
		consume(TokenType::RightParen, "导入列表后缺少 ')'");
		consume(TokenType::Semicolon, "导入语句后缺少 ';'");
		return imp;
	}
	// Support: import "file" [as alias];  OR keep existing package import forms
	if (match({TokenType::String})) {
		Token t = previous();
		ImportStmt::Entry e; e.isFile = true; e.filePath = unescapeString(t.lexeme); e.line = t.line; e.column = t.column; e.length = t.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "导入语句后缺少 ';'");
		return imp;
	}
	auto pathParts = parseQualifiedIdentifiers("缺少包名");
	if (check(TokenType::Dot)) {
		consume(TokenType::Dot, "包名后缺少 '.'");
		auto pkgName = joinIdentifiers(pathParts, 0, pathParts.size());
		if (match({TokenType::Star})) {
			Token starTok = previous();
			ImportStmt::Entry e; e.packageName = pkgName; e.symbol = std::string("*"); e.isFile = false; e.line = starTok.line; e.column = starTok.column; e.length = std::max(1, starTok.length); imp->entries.push_back(e);
			consume(TokenType::Semicolon, "导入语句后缺少 ';'");
			return imp;
		}
		if (match({TokenType::LeftParen})) {
			while (!check(TokenType::RightParen) && !isAtEnd()) {
				auto symTok = consume(TokenType::Identifier, "缺少符号名称");
				ImportStmt::Entry e; e.packageName = pkgName; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
				if (match({TokenType::As})) {
					e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
				}
				imp->entries.push_back(e);
				(void)match({TokenType::Comma});
			}
			consume(TokenType::RightParen, "符号列表后缺少 ')'");
			consume(TokenType::Semicolon, "导入语句后缺少 ';'");
			return imp;
		}
		error("包名 '.' 后缺少 '*' 或 '('");
	} else if (pathParts.size() >= 2) {
		auto symTok = pathParts.back();
		auto pkgName = joinIdentifiers(pathParts, 0, pathParts.size() - 1);
		ImportStmt::Entry e; e.packageName = pkgName; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "导入语句后缺少 ';'");
		return imp;
	} else {
		// Allow shorthand imports like `import json;` and map them to top-level package `json`.
		// Do NOT implicitly place packages under `std.`; keep top-level packages independent.
		auto tok = pathParts.back();
		std::string shorthandPkg = tok.lexeme;
		// Use special symbol marker to indicate binding the package object itself
		ImportStmt::Entry e; e.packageName = shorthandPkg; e.symbol = std::string("__module__"); e.isFile = false; e.line = tok.line; e.column = tok.column; e.length = tok.length;
		if (match({TokenType::As})) {
			e.alias = consume(TokenType::Identifier, "缺少别名").lexeme;
		}
		imp->entries.push_back(e);
		consume(TokenType::Semicolon, "导入语句后缺少 ';'");
		return imp;
	}
}

StmtPtr Parser::interfaceDeclaration(bool isExported) {
	// 语法：interface Name ; | interface Name { function sig(...); ... }
	auto nameTok = consume(TokenType::Identifier, "缺少接口名称");
	auto st = std::make_shared<InterfaceStmt>(); st->name = nameTok.lexeme; st->isExported = isExported;
	if (match({TokenType::Semicolon})) return st;
	consume(TokenType::LeftBrace, "接口主体前缺少 '{'");
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		(void)match({TokenType::Async}); // 忽略 async 关键字
		(void)match({TokenType::Function});
		auto mname = consume(TokenType::Identifier, "缺少方法名称").lexeme;
		consume(TokenType::LeftParen, "缺少 '('");
		// 跳过参数列表
		if (!check(TokenType::RightParen)) {
			do {
				(void)consume(TokenType::Identifier, "缺少参数名称");
				// optional type annotation after parameter name
				if (match({TokenType::Colon})) { (void)consume(TokenType::Identifier, "':' 后缺少类型名称"); }
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "缺少 ')'");
		// 检查是否有函数体（不允许）
		if (check(TokenType::LeftBrace)) {
			std::ostringstream oss;
			oss << "接口方法不能有函数体。请使用 ';' 代替 '{...}'\n";
			oss << "接口 '" << st->name << "' 中的方法 '" << mname << "' 应声明为: function " << mname << "(...);";
			error(oss.str().c_str());
		}
		consume(TokenType::Semicolon, "接口方法签名后缺少 ';'");
		st->methodNames.push_back(mname);
	}
	consume(TokenType::RightBrace, "接口主体后缺少 '}'");
	// 允许可选分号：`interface Name { ... };`
	(void)match({TokenType::Semicolon});
	return st;
}

StmtPtr Parser::classDeclaration(bool isExported) {
	auto nameTok = consume(TokenType::Identifier, "缺少类名");
	auto cls = std::make_shared<ClassStmt>();
	cls->name = nameTok.lexeme;
	cls->isExported = isExported;
	// 支持三种：class Name ; | class Name <- Supers | class Name [<- Supers] { ... }
	if (match({TokenType::Semicolon})) return cls; // 空类声明
	if (match({TokenType::LeftArrow}) || match({TokenType::Extends})) {
		// 解析父类：单个或 (A,B,...)
		if (match({TokenType::LeftParen})) {
			do { cls->superNames.push_back(consume(TokenType::Identifier, "缺少基类名称").lexeme); } while (match({TokenType::Comma}));
			consume(TokenType::RightParen, "基类列表后缺少 ')'");
		} else {
			cls->superNames.push_back(consume(TokenType::Identifier, "缺少基类名称").lexeme);
		}
	}
	if (match({TokenType::LeftBrace})) {
		while (!check(TokenType::RightBrace) && !isAtEnd()) {
			std::vector<ExprPtr> decorators;
			while (match({TokenType::At})) {
				decorators.push_back(call());
			}

			bool isStatic = match({TokenType::Static});
			bool isAsync = match({TokenType::Async});
			(void)match({TokenType::Function});
			bool isGenerator = match({TokenType::Star});
			auto mname = consume(TokenType::Identifier, "缺少方法名称").lexeme;
			consume(TokenType::LeftParen, "缺少 '('");
			std::vector<Param> params;
			if (!check(TokenType::RightParen)) {
				do {
					auto pname = consume(TokenType::Identifier, "缺少参数名称").lexeme;
					std::optional<std::string> ptype = std::nullopt;
					if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "':' 后缺少类型名称").lexeme;
					params.emplace_back(pname, ptype);
				} while (match({TokenType::Comma}));
			}
			consume(TokenType::RightParen, "缺少 ')'");
			// optional return type
			std::optional<std::string> retType = std::nullopt;
			if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "':' 后缺少返回类型名称").lexeme;
			auto body = statement();
			cls->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, isGenerator, retType, isStatic, false, 0, 1, 1, decorators));
		}
		consume(TokenType::RightBrace, "类主体后缺少 '}'");
		// 可选分号：class Name { ... };
		(void)match({TokenType::Semicolon});
	}
	return cls;
}

StmtPtr Parser::extendsDeclaration() {
	// 语法：extends Name { methods }
	auto nameTok = consume(TokenType::Identifier, "'extends' 后缺少类名");
	consume(TokenType::LeftBrace, "扩展主体前缺少 '{'");
	auto ext = std::make_shared<ExtendStmt>();
	ext->name = nameTok.lexeme;
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		bool isAsync = match({TokenType::Async});
		(void)match({TokenType::Function});
		bool isGenerator = match({TokenType::Star});
		auto mname = consume(TokenType::Identifier, "缺少方法名称").lexeme;
		consume(TokenType::LeftParen, "缺少 '('");
		std::vector<Param> params;
		if (!check(TokenType::RightParen)) {
			do {
				auto pname = consume(TokenType::Identifier, "缺少参数名称").lexeme;
				std::optional<std::string> ptype = std::nullopt;
				if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "':' 后缺少类型名称").lexeme;
				params.emplace_back(pname, ptype);
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "缺少 ')'");
		// optional return type
		std::optional<std::string> retType = std::nullopt;
		if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "':' 后缺少返回类型名称").lexeme;
		auto body = statement();
		ext->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, isGenerator, retType));
	}
	consume(TokenType::RightBrace, "扩展主体后缺少 '}'");
	// 可选分号：extends Name { ... };
	(void)match({TokenType::Semicolon});
	return ext;
}

StmtPtr Parser::functionDecl(bool isAsync, bool isExported) {
	// Check for generator function: function* name()
	bool isGenerator = match({TokenType::Star});
	auto nameTok = consume(TokenType::Identifier, "缺少函数名");
	auto name = nameTok.lexeme;
	consume(TokenType::LeftParen, "缺少 '('");
	std::vector<Param> params;
	bool hasRest = false;
	bool hasDefault = false;  // 跟踪是否有默认参数
	if (!check(TokenType::RightParen)) {
		do {
			// Check for rest parameter: ...paramName
			bool isRest = false;
			if (match({TokenType::Ellipsis})) {
				if (hasRest) {
					error(previous(), "只允许一个剩余参数");
				}
				isRest = true;
				hasRest = true;
			}
			
			auto pname = consume(TokenType::Identifier, "缺少参数名称").lexeme;
			std::optional<std::string> ptype = std::nullopt;
			if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "':' 后缺少类型名称").lexeme;
			
			// Check for default value: param = defaultExpr
			ExprPtr defaultValue = nullptr;
			if (match({TokenType::Equal})) {
				if (isRest) {
					error(previous(), "剩余参数不能有默认值");
				}
				if (hasRest) {
					error(previous(), "默认参数不能在剩余参数之后");
				}
				defaultValue = assignment();  // 解析默认值表达式
				hasDefault = true;
			} else if (hasDefault && !isRest) {
				error(previous(), "必选参数不能在默认参数之后");
			}
			
			params.emplace_back(pname, ptype, isRest, defaultValue);
			
			// Rest parameter must be last
			if (isRest && !check(TokenType::RightParen)) {
				error("剩余参数必须在最后");
			}
		} while (match({TokenType::Comma}));
	}
	consume(TokenType::RightParen, "缺少 ')'");
	// optional return type (accept ':' or '->')
	std::optional<std::string> retType = std::nullopt;
	if (match({TokenType::Colon, TokenType::Arrow})) retType = consume(TokenType::Identifier, "':' 或 '->' 后缺少返回类型名称").lexeme;
	auto body = statement();
	return std::make_shared<FunctionStmt>(name, params, body, isAsync, isGenerator, retType, false, isExported, nameTok.line, nameTok.column, nameTok.length);
}

StmtPtr Parser::varDeclaration(bool isExported) {
	// Check if this is a destructuring pattern
	if (check(TokenType::LeftBracket) || check(TokenType::LeftBrace)) {
		auto pattern = parsePattern();
		consume(TokenType::Equal, "解构声明中缺少 '='");
		auto init = expression();
		consume(TokenType::Semicolon, "变量声明后缺少 ';'");
		return std::make_shared<VarDeclDestructuring>(pattern, init, isExported);
	}
	
	// Regular variable declaration
	auto nameTok = consume(TokenType::Identifier, "缺少变量名");
	auto name = nameTok.lexeme;
	std::optional<std::string> type = std::nullopt;
	ExprPtr typeExpr = nullptr;
	if (match({TokenType::Colon})) {
		// allow a non-assignment expression (e.g. typeof(...)) as the type annotation
		typeExpr = logicalOr();
	}
	ExprPtr init;
	if (match({TokenType::Equal})) init = expression();
	consume(TokenType::Semicolon, "变量声明后缺少 ';'");
	return std::make_shared<VarDecl>(name, type, typeExpr, init, isExported, nameTok.line, nameTok.column, nameTok.length);
}

StmtPtr Parser::statement() {
	if (match({TokenType::If})) return ifStatement();
	if (match({TokenType::While})) return whileStatement();
	if (match({TokenType::Do})) return doWhileStatement();
	if (match({TokenType::For})) return forStatement();
	if (match({TokenType::ForEach})) return forEachStatement();
	if (match({TokenType::Switch})) return switchStatement();
	if (match({TokenType::Match})) return matchStatement();
	if (match({TokenType::Return})) return returnStatement();
	if (match({TokenType::Throw})) { auto v = expression(); consume(TokenType::Semicolon, "throw 后缺少 ';'"); return std::make_shared<ThrowStmt>(v); }
	// 空语句：允许单独的 ';'，不执行任何操作（支持多连分号）
	if (match({TokenType::Semicolon})) { return std::make_shared<EmptyStmt>(); }
	if (match({TokenType::Try})) {
		// try 后接任意语句（通常为块）
		auto tryB = statement();
		consume(TokenType::Catch, "try 块后缺少 'catch'");
		consume(TokenType::LeftParen, "catch 后缺少 '('");
		auto name = consume(TokenType::Identifier, "catch 中缺少标识符").lexeme;
		consume(TokenType::RightParen, "catch 参数后缺少 ')'");
		auto catchB = statement();
		// Optional finally block
		StmtPtr finallyB = nullptr;
		if (match({TokenType::Finally})) {
			finallyB = statement();
		}
		return std::make_shared<TryCatchStmt>(tryB, name, catchB, finallyB);
	}
	if (match({TokenType::Go})) { auto expr = expression(); consume(TokenType::Semicolon, "go 调用后缺少 ';'"); return std::make_shared<GoStmt>(expr); }
	if (match({TokenType::Break})) { consume(TokenType::Semicolon, "break 后缺少 ';'"); return std::make_shared<BreakStmt>(); }
	if (match({TokenType::Continue})) { consume(TokenType::Semicolon, "continue 后缺少 ';'"); return std::make_shared<ContinueStmt>(); }
	if (match({TokenType::LeftBrace})) return std::make_shared<BlockStmt>(block());
	return expressionStatement();
}

StmtPtr Parser::forStatement() {
	consume(TokenType::LeftParen, "缺少 '('");
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
	consume(TokenType::Semicolon, "循环条件后缺少 ';'");
	ExprPtr post = nullptr;
	if (!check(TokenType::RightParen)) post = expression();
	consume(TokenType::RightParen, "for 子句后缺少 ')'");
	auto body = statement();
	return std::make_shared<ForStmt>(init, cond, post, body);
}

StmtPtr Parser::forEachStatement() {
	// foreach (varName in iterable) body
	consume(TokenType::LeftParen, "'foreach' 后缺少 '('");
	
	if (!check(TokenType::Identifier)) {
		error("foreach 中缺少变量名");
	}
	std::string varName = advance().lexeme;
	
	consume(TokenType::In, "foreach 变量名后缺少 'in'");
	
	ExprPtr iterable = expression();
	consume(TokenType::RightParen, "foreach 子句后缺少 ')'");
	auto body = statement();
	
	return std::make_shared<ForEachStmt>(varName, iterable, body);
}

StmtPtr Parser::switchStatement() {
	// switch (expr) { case val: ... case val2: ... default: ... }
	consume(TokenType::LeftParen, "'switch' 后缺少 '('");
	ExprPtr expr = expression();
	consume(TokenType::RightParen, "switch 表达式后缺少 ')'");
	consume(TokenType::LeftBrace, "switch 头部后缺少 '{'");
	
	std::vector<SwitchStmt::CaseClause> cases;
	
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		if (match({TokenType::Case})) {
			// case value:
			ExprPtr caseValue = expression();
			consume(TokenType::Colon, "case 值后缺少 ':'");
			
			// Collect statements until next case/default or closing brace
			std::vector<StmtPtr> caseBody;
			while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
				caseBody.push_back(statement());
			}
			
			cases.push_back({caseValue, caseBody});
		} else if (match({TokenType::Default})) {
			// default:
			consume(TokenType::Colon, "'default' 后缺少 ':'");
			
			// Collect statements until next case or closing brace
			std::vector<StmtPtr> defaultBody;
			while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
				defaultBody.push_back(statement());
			}
			
			cases.push_back({nullptr, defaultBody}); // nullptr indicates default case
		} else {
			error("switch 主体中缺少 'case' 或 'default'");
		}
	}
	
	consume(TokenType::RightBrace, "switch 主体后缺少 '}'");
	return std::make_shared<SwitchStmt>(expr, cases);
}

StmtPtr Parser::matchStatement() {
	// match (expr) { case pattern => stmt, ... }
	consume(TokenType::LeftParen, "'match' 后缺少 '('");
	ExprPtr expr = expression();
	consume(TokenType::RightParen, "match 表达式后缺少 ')'");
	consume(TokenType::LeftBrace, "match 头部后缺少 '{'");
	
	std::vector<MatchStmt::MatchArm> arms;
	
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		if (match({TokenType::Case})) {
			// case pattern [if guard] => body
			// Use conditional() to stop before commas and arrows
			ExprPtr pattern = conditional();
			
			// Optional guard clause
			ExprPtr guard = nullptr;
			if (match({TokenType::If})) {
				guard = conditional();
			}
			
			// Expect => (using arrow token)
			consume(TokenType::Arrow, "match 模式后缺少 '=>'");
			
			// Body can be a single statement or a block
			StmtPtr body = statement();
			
			arms.push_back({pattern, guard, body});
			
			// Optional comma after arm
			match({TokenType::Comma});
		} else if (match({TokenType::Default})) {
			// default => body (catchall pattern)
			consume(TokenType::Arrow, "'default' 后缺少 '=>'");
			StmtPtr body = statement();
			
			// Use null pattern to indicate default
			arms.push_back({nullptr, nullptr, body});
			
			// Optional comma after arm
			match({TokenType::Comma});
		} else {
			error("match 主体中缺少 'case' 或 'default'");
		}
	}
	
	consume(TokenType::RightBrace, "match 主体后缺少 '}'");
	return std::make_shared<MatchStmt>(expr, arms);
}

StmtPtr Parser::returnStatement() {
	Token kw = previous();
	ExprPtr val;
	if (!check(TokenType::Semicolon)) val = expression();
	consume(TokenType::Semicolon, "return 值后缺少 ';'");
	return std::make_shared<ReturnStmt>(kw, val);
}

StmtPtr Parser::ifStatement() {
	consume(TokenType::LeftParen, "缺少 '('");
	auto cond = expression();
	consume(TokenType::RightParen, "缺少 ')'");
	auto thenB = statement();
	StmtPtr elseB;
	if (match({TokenType::Else})) elseB = statement();
	return std::make_shared<IfStmt>(cond, thenB, elseB);
}

StmtPtr Parser::whileStatement() {
	consume(TokenType::LeftParen, "缺少 '('");
	auto cond = expression();
	consume(TokenType::RightParen, "缺少 ')'");
	auto body = statement();
	return std::make_shared<WhileStmt>(cond, body);
}

StmtPtr Parser::doWhileStatement() {
	auto body = statement();
	consume(TokenType::While, "do-loop 主体后缺少 'while'");
	consume(TokenType::LeftParen, "'while' 后缺少 '('");
	auto cond = expression();
	consume(TokenType::RightParen, "条件后缺少 ')'");
	consume(TokenType::Semicolon, "do-while 条件后缺少 ';'");
	return std::make_shared<DoWhileStmt>(cond, body);
}

std::vector<StmtPtr> Parser::block() {
	std::vector<StmtPtr> stmts;
	while (!check(TokenType::RightBrace) && !isAtEnd()) stmts.push_back(declaration());
	consume(TokenType::RightBrace, "块后缺少 '}'");
	return stmts;
}

StmtPtr Parser::expressionStatement() {
	auto expr = expression();
	consume(TokenType::Semicolon, "表达式后缺少 ';'");
	return std::make_shared<ExprStmt>(expr);
}

ExprPtr Parser::expression() { return assignment(); }

static PatternPtr exprToPattern(ExprPtr e) {
	if (auto v = std::dynamic_pointer_cast<VariableExpr>(e)) {
		return std::make_shared<IdentifierPattern>(v->name);
	}
	if (auto arr = std::dynamic_pointer_cast<ArrayLiteralExpr>(e)) {
		std::vector<PatternPtr> elements;
		bool hasRest = false;
		std::string restName;
		for (auto& el : arr->elements) {
			if (auto spread = std::dynamic_pointer_cast<SpreadExpr>(el)) {
				if (hasRest) throw std::runtime_error("Rest element must be last");
				hasRest = true;
				if (auto v = std::dynamic_pointer_cast<VariableExpr>(spread->expr)) {
					restName = v->name;
				} else {
					throw std::runtime_error("Rest element must be identifier");
				}
			} else {
				if (hasRest) throw std::runtime_error("Rest element must be last");
				elements.push_back(exprToPattern(el));
			}
		}
		return std::make_shared<ArrayPattern>(elements, hasRest, restName);
	}
	if (auto obj = std::dynamic_pointer_cast<ObjectLiteralExpr>(e)) {
		std::vector<ObjectPattern::Property> props;
		bool hasRest = false;
		std::string restName;
		for (auto& p : obj->props) {
			if (p.isSpread) {
				if (hasRest) throw std::runtime_error("Rest element must be last");
				hasRest = true;
				if (auto v = std::dynamic_pointer_cast<VariableExpr>(p.value)) {
					restName = v->name;
				} else {
					throw std::runtime_error("Rest element must be identifier");
				}
			} else {
				if (hasRest) throw std::runtime_error("Rest element must be last");
				ObjectPattern::Property prop;
				prop.key = p.name;
				prop.pattern = exprToPattern(p.value);
				props.push_back(prop);
			}
		}
		return std::make_shared<ObjectPattern>(props, hasRest, restName);
	}
	if (auto assign = std::dynamic_pointer_cast<AssignExpr>(e)) {
		return std::make_shared<IdentifierPattern>(assign->name, assign->value);
	}
	throw std::runtime_error("Invalid destructuring assignment target");
}

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
			error(op, "逻辑赋值的目标无效");
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
		error(op, "赋值目标无效");
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

		// Destructuring assignment
		if (std::dynamic_pointer_cast<ArrayLiteralExpr>(expr) || std::dynamic_pointer_cast<ObjectLiteralExpr>(expr)) {
			try {
				auto pattern = exprToPattern(expr);
				return std::make_shared<DestructuringAssignExpr>(pattern, value, previous().line);
			} catch (const std::exception& e) {
				error(previous(), e.what());
			}
		}

		error(previous(), "赋值目标无效");
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
			error("三元运算符 then 分支后缺少 ':'");
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
	if (match({TokenType::Yield})) {
		Token yieldTok = previous();
		bool isDelegate = false;
		ExprPtr value = nullptr;
		// yield* for delegating to another generator
		if (match({TokenType::Star})) {
			isDelegate = true;
		}
		// yield can have an optional value
		if (!check(TokenType::Semicolon) && !check(TokenType::RightParen) && !check(TokenType::RightBrace)) {
			value = unary();
		}
		return std::make_shared<YieldExpr>(value, isDelegate, yieldTok.line, yieldTok.column, std::max(1, yieldTok.length));
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
	Token rp = consume(TokenType::RightParen, "参数后缺少 ')'");
	return std::make_shared<CallExpr>(callee, args, rp.line, rp.column, std::max(1, rp.length));
}

ExprPtr Parser::call() {
	auto expr = primary();
	for (;;) {
		if (match({TokenType::LeftParen})) expr = finishCall(expr);
		else if (match({TokenType::QuestionDot})) {
			// Optional chaining: obj?.prop
			std::string name; Token nameTok;
			if (isPropertyNameToken(peek().type)) { nameTok = advance(); name = nameTok.lexeme; }
			else {
				error("'?.' 后缺少属性名");
			}
			expr = std::make_shared<OptionalChainingExpr>(expr, name, nameTok.line, nameTok.column, std::max(1, nameTok.length));
		}
		else if (match({TokenType::Dot})) {
			std::string name; Token nameTok;
			if (isPropertyNameToken(peek().type)) { nameTok = advance(); name = nameTok.lexeme; }
			else {
				error("'.' 后缺少属性名");
			}
			expr = std::make_shared<GetPropExpr>(expr, name, nameTok.line, nameTok.column, std::max(1, nameTok.length));
		}
		else if (match({TokenType::LeftBracket})) {
			Token lb = previous();
			auto idx = expression();
			consume(TokenType::RightBracket, "索引后缺少 ']'");
			expr = std::make_shared<IndexExpr>(expr, idx, lb.line, lb.column, 1);
		}
		else break;
	}
	return expr;
}

ExprPtr Parser::primary() {
	// 支持匿名函数：[](x, y){ ... } 或生成器: []*() { ... }
	if (check(TokenType::LeftBracket)) {
		// 仅当模式为 [] ( 或 []*( 开始时，识别为 lambda；否则按数组字面量
		bool isLambda = false;
		if (current + 2 < tokens.size() && 
		    tokens[current].type == TokenType::LeftBracket && 
		    tokens[current+1].type == TokenType::RightBracket && 
		    tokens[current+2].type == TokenType::LeftParen) {
			isLambda = true;
		} else if (current + 3 < tokens.size() && 
		           tokens[current].type == TokenType::LeftBracket && 
		           tokens[current+1].type == TokenType::RightBracket && 
		           tokens[current+2].type == TokenType::Star &&
		           tokens[current+3].type == TokenType::LeftParen) {
			isLambda = true;
		}
		
		if (isLambda) {
			advance(); // [
			advance(); // ]
			// Check for generator lambda: []*()
			bool isGenerator = match({TokenType::Star});
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
							error(previous(), "只允许一个剩余参数");
						}
						isRest = true;
						hasRest = true;
					}
					
					auto pname = consume(TokenType::Identifier, "缺少参数名称").lexeme;
					std::optional<std::string> ptype = std::nullopt;
					if (match({TokenType::Colon})) ptype = consume(TokenType::Identifier, "':' 后缺少类型名称").lexeme;
					
					// Check for default value
					ExprPtr defaultValue = nullptr;
					if (match({TokenType::Equal})) {
						if (isRest) {
							error(previous(), "剩余参数不能有默认值");
						}
						if (hasRest) {
							error(previous(), "默认参数不能在剩余参数之后");
						}
						defaultValue = assignment();
						hasDefault = true;
					} else if (hasDefault && !isRest) {
						error(previous(), "必选参数不能在默认参数之后");
					}
					
					params.emplace_back(pname, ptype, isRest, defaultValue);
					
					// Rest parameter must be last
					if (isRest && !check(TokenType::RightParen)) {
						error("剩余参数必须在最后");
					}
				} while (match({TokenType::Comma}));
			}
			consume(TokenType::RightParen, "lambda 参数后缺少 ')'");
			auto body = statement();
			return std::make_shared<FunctionExpr>(params, body, isGenerator);
		}
	}
	if (match({TokenType::New})) {
		Token newTok = previous();
		Token nameTok = consume(TokenType::Identifier, "'new' 后缺少类名");
		ExprPtr callee = std::make_shared<VariableExpr>(nameTok.lexeme, nameTok.line, nameTok.column, nameTok.length);
		while (match({TokenType::Dot})) {
			Token propTok = consume(TokenType::Identifier, "'.' 后缺少属性名");
			callee = std::make_shared<GetPropExpr>(callee, propTok.lexeme, propTok.line, propTok.column, propTok.length);
		}
		consume(TokenType::LeftParen, "缺少 '('");
		std::vector<ExprPtr> args;
		if (!check(TokenType::RightParen)) { do { args.push_back(expression()); } while (match({TokenType::Comma})); }
		consume(TokenType::RightParen, "缺少 ')'");
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
			return std::make_shared<LiteralExpr>(Value{unescapeString(s)});
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
		consume(TokenType::RightBracket, "数组字面量后缺少 ']'");
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
					else if (match({TokenType::String})) { p.computed = false; p.name = unescapeString(previous().lexeme); }
					else if (match({TokenType::LeftBracket})) {
						p.computed = true; p.keyExpr = expression();
						consume(TokenType::RightBracket, "computed key 后缺少 ']'");
					}
					else error("对象字面量中缺少属性名");
					consume(TokenType::Colon, "属性名后缺少 ':'");
					p.value = expression();
				}
				props.push_back(std::move(p));
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightBrace, "对象字面量后缺少 '}'");
		return std::make_shared<ObjectLiteralExpr>(props);
	}
	if (match({TokenType::LeftParen})) { auto e = expression(); consume(TokenType::RightParen, "缺少 ')'"); return e; }
	error("缺少表达式");
}

// --- 插值字符串支持："hello ${expr} world" -> 通过'+'串联 ---
ExprPtr Parser::parseInterpolatedString(const std::string& s, int line, int column, int length) {
	std::vector<ExprPtr> parts;
	std::string raw;
	auto flushRaw = [&](){ if (!raw.empty()) { parts.push_back(std::make_shared<LiteralExpr>(Value{unescapeString(raw)})); raw.clear(); } };
	for (size_t i=0;i<s.size();) {
		if (s[i] == '\\') {
			raw.push_back(s[i]);
			i++;
			if (i < s.size()) {
				raw.push_back(s[i]);
				i++;
			}
			continue;
		}
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
		oss << "在行 " << line << ", 列 " << column << ", 长度 " << length << " 处缺少表达式";
		throw std::runtime_error(oss.str());
	}
	if (stmts.empty()) {
		std::ostringstream oss;
		oss << "[Parse] 在行 " << line << ", 列 " << column << ", 长度 " << length << " 处插值表达式为空";
		throw std::runtime_error(oss.str());
	}
	if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmts[0])) return es->expr;
	std::ostringstream oss;
	oss << "[Parse] 在行 " << line << ", 列 " << column << ", 长度 " << length << " 处插值表达式无效";
	throw std::runtime_error(oss.str());
}

// Parse destructuring patterns
PatternPtr Parser::parsePattern() {
	if (check(TokenType::LeftBracket)) {
		return parseArrayPattern();
	} else if (check(TokenType::LeftBrace)) {
		return parseObjectPattern();
	} else if (check(TokenType::Identifier)) {
		auto name = advance().lexeme;
		ExprPtr defaultValue = nullptr;
		if (match({TokenType::Equal})) {
			defaultValue = assignment();
		}
		return std::make_shared<IdentifierPattern>(name, defaultValue);
	}
	error("缺少标识符、数组模式或对象模式");
}

PatternPtr Parser::parseArrayPattern() {
	consume(TokenType::LeftBracket, "缺少 '['");
	std::vector<PatternPtr> elements;
	bool hasRest = false;
	std::string restName;
	
	while (!check(TokenType::RightBracket) && !isAtEnd()) {
		if (match({TokenType::Ellipsis})) {
			hasRest = true;
			restName = consume(TokenType::Identifier, "'...' 后缺少标识符").lexeme;
			break;
		}
		elements.push_back(parsePattern());
		if (!check(TokenType::RightBracket)) {
			consume(TokenType::Comma, "数组模式中缺少 ',' 或 ']'");
		}
	}
	
	consume(TokenType::RightBracket, "缺少 ']'");
	return std::make_shared<ArrayPattern>(elements, hasRest, restName);
}

PatternPtr Parser::parseObjectPattern() {
	consume(TokenType::LeftBrace, "缺少 '{'");
	std::vector<ObjectPattern::Property> properties;
	bool hasRest = false;
	std::string restName;
	
	while (!check(TokenType::RightBrace) && !isAtEnd()) {
		if (match({TokenType::Ellipsis})) {
			hasRest = true;
			restName = consume(TokenType::Identifier, "'...' 后缺少标识符").lexeme;
			break;
		}
		
		auto key = consume(TokenType::Identifier, "缺少属性名").lexeme;
		PatternPtr pattern;
		ExprPtr defaultValue = nullptr;
		
		if (match({TokenType::Colon})) {
			pattern = parsePattern();
		} else {
			// Shorthand: { x } is equivalent to { x: x }
			pattern = std::make_shared<IdentifierPattern>(key, nullptr);
		}
		
		if (match({TokenType::Equal})) {
			defaultValue = assignment();
		}
		
		properties.push_back({key, pattern, defaultValue});
		
		if (!check(TokenType::RightBrace)) {
			consume(TokenType::Comma, "对象模式中缺少 ',' 或 '}'");
		}
	}
	
	consume(TokenType::RightBrace, "缺少 '}'");
	return std::make_shared<ObjectPattern>(properties, hasRest, restName);
}

} // namespace asul
