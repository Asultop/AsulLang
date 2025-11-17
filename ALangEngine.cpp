#include "ALangEngine.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

// ----------- Lexer -----------

enum class TokenType {
	// Single-char
	LeftParen, RightParen, LeftBrace, RightBrace, Comma, Semicolon,
	Plus, Minus, Star, Slash, Percent,
	Bang, Equal, Less, Greater,
	// One or two char
	BangEqual, EqualEqual, LessEqual, GreaterEqual,
	AndAnd, OrOr,
	// Literals
	Identifier, String, Number,
	// Keywords
	Let, Var, Const, Function, Return, If, Else, While, True, False, Null,
	EndOfFile
};

struct Token {
	TokenType type;
	std::string lexeme;
	int line;
};

class Lexer {
public:
	explicit Lexer(const std::string& src) : source(src) {}
	std::vector<Token> scanTokens() {
		while (!isAtEnd()) {
			start = current;
			scanToken();
		}
		tokens.push_back(Token{TokenType::EndOfFile, "", line});
		return tokens;
	}

private:
	const std::string& source;
	std::vector<Token> tokens;
	size_t start{0};
	size_t current{0};
	int line{1};

	bool isAtEnd() const { return current >= source.size(); }
	char advance() { return source[current++]; }
	char peek() const { return isAtEnd() ? '\0' : source[current]; }
	char peekNext() const { return (current + 1 >= source.size()) ? '\0' : source[current + 1]; }
	bool match(char expected) {
		if (isAtEnd() || source[current] != expected) return false;
		current++;
		return true;
	}
	void add(TokenType type) { tokens.push_back(Token{type, source.substr(start, current - start), line}); }

	void string() {
		while (!isAtEnd() && peek() != '"') {
			if (peek() == '\n') line++;
			advance();
		}
		if (isAtEnd()) throw std::runtime_error("Unterminated string at line " + std::to_string(line));
		advance(); // closing quote
		std::string value = source.substr(start + 1, current - start - 2);
		tokens.push_back(Token{TokenType::String, value, line});
	}

	void number() {
		while (std::isdigit(peek())) advance();
		if (peek() == '.' && std::isdigit(peekNext())) {
			advance();
			while (std::isdigit(peek())) advance();
		}
		tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line});
	}

	void identifier() {
		while (std::isalnum(peek()) || peek() == '_') advance();
		std::string text = source.substr(start, current - start);
		static const std::unordered_map<std::string, TokenType> keywords{
			{"let", TokenType::Let}, {"var", TokenType::Var}, {"const", TokenType::Const},
			{"function", TokenType::Function}, {"return", TokenType::Return},
			{"if", TokenType::If}, {"else", TokenType::Else}, {"while", TokenType::While},
			{"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
		};
		auto it = keywords.find(text);
		if (it != keywords.end()) tokens.push_back(Token{it->second, text, line});
		else tokens.push_back(Token{TokenType::Identifier, text, line});
	}

	void skipWhitespaceAndComments() {
		for (;;) {
			char c = peek();
			switch (c) {
			case ' ': case '\r': case '\t': advance(); break;
			case '\n': line++; advance(); break;
			case '/':
				if (peekNext() == '/') {
					while (!isAtEnd() && peek() != '\n') advance();
				} else if (peekNext() == '*') {
					advance(); advance();
					while (!isAtEnd() && !(peek() == '*' && peekNext() == '/')) {
						if (peek() == '\n') line++;
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); }
				} else return;
				break;
			default:
				return;
			}
		}
	}

	void scanToken() {
		skipWhitespaceAndComments();
		if (isAtEnd()) return;
		start = current;
		char c = advance();
		switch (c) {
		case '(': add(TokenType::LeftParen); break;
		case ')': add(TokenType::RightParen); break;
		case '{': add(TokenType::LeftBrace); break;
		case '}': add(TokenType::RightBrace); break;
		case ',': add(TokenType::Comma); break;
		case ';': add(TokenType::Semicolon); break;
		case '+': add(TokenType::Plus); break;
		case '-': add(TokenType::Minus); break;
		case '*': add(TokenType::Star); break;
		case '%': add(TokenType::Percent); break;
		case '!': add(match('=') ? TokenType::BangEqual : TokenType::Bang); break;
		case '=': add(match('=') ? TokenType::EqualEqual : TokenType::Equal); break;
		case '<': add(match('=') ? TokenType::LessEqual : TokenType::Less); break;
		case '>': add(match('=') ? TokenType::GreaterEqual : TokenType::Greater); break;
		case '&': if (match('&')) add(TokenType::AndAnd); else throw std::runtime_error("Unexpected '&' at line " + std::to_string(line)); break;
		case '|': if (match('|')) add(TokenType::OrOr); else throw std::runtime_error("Unexpected '|' at line " + std::to_string(line)); break;
		case '/': add(TokenType::Slash); break;
		case '"': string(); break;
		default:
			if (std::isdigit(c)) { while (std::isdigit(peek()) || (peek()=='.' && std::isdigit(peekNext()))) advance(); tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line}); }
			else if (std::isalpha(c) || c == '_') identifier();
			else throw std::runtime_error("Unexpected character at line " + std::to_string(line));
		}
	}
};

// ----------- Runtime Values and Environment -----------

struct Function;

using Value = std::variant<std::monostate,double,std::string,bool,std::shared_ptr<Function>>;

inline std::string typeOf(const Value& v) {
	switch (v.index()) {
	case 0: return "null";
	case 1: return "number";
	case 2: return "string";
	case 3: return "boolean";
	case 4: return "function";
	default: return "unknown";
	}
}

inline bool isTruthy(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return false;
	if (auto b = std::get_if<bool>(&v)) return *b;
	if (auto n = std::get_if<double>(&v)) return *n != 0.0;
	if (auto s = std::get_if<std::string>(&v)) return !s->empty();
	return true;
}

inline std::string toString(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return "null";
	if (auto n = std::get_if<double>(&v)) {
		std::ostringstream oss; oss << *n; return oss.str();
	}
	if (auto s = std::get_if<std::string>(&v)) return *s;
	if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
	return "[Function]";
}

struct Environment : std::enable_shared_from_this<Environment> {
	std::shared_ptr<Environment> parent;
	std::unordered_map<std::string, Value> values;

	explicit Environment(std::shared_ptr<Environment> p = nullptr) : parent(std::move(p)) {}

	void define(const std::string& name, const Value& val) { values[name] = val; }
	bool assign(const std::string& name, const Value& val) {
		if (values.find(name) != values.end()) { values[name] = val; return true; }
		if (parent) return parent->assign(name, val);
		return false;
	}
	Value get(const std::string& name) {
		auto it = values.find(name);
		if (it != values.end()) return it->second;
		if (parent) return parent->get(name);
		throw std::runtime_error("Undefined variable '" + name + "'");
	}
};

// Forward declarations for AST
struct Stmt; struct Expr; using StmtPtr = std::shared_ptr<Stmt>; using ExprPtr = std::shared_ptr<Expr>;

struct Function {
	std::vector<std::string> params;
	std::vector<StmtPtr> body;
	std::shared_ptr<Environment> closure;
	bool isBuiltin{false};
	std::function<Value(const std::vector<Value>&)> builtin;
};

// ----------- AST Nodes -----------

struct Expr { virtual ~Expr() = default; };
struct LiteralExpr : Expr { Value value; explicit LiteralExpr(Value v): value(std::move(v)){} };
struct VariableExpr : Expr { std::string name; explicit VariableExpr(std::string n): name(std::move(n)){} };
struct AssignExpr : Expr { std::string name; ExprPtr value; AssignExpr(std::string n, ExprPtr v): name(std::move(n)), value(std::move(v)){} };
struct UnaryExpr : Expr { Token op; ExprPtr right; UnaryExpr(Token o, ExprPtr r): op(std::move(o)), right(std::move(r)){} };
struct BinaryExpr : Expr { ExprPtr left; Token op; ExprPtr right; BinaryExpr(ExprPtr l, Token o, ExprPtr r): left(std::move(l)), op(std::move(o)), right(std::move(r)){} };
struct LogicalExpr : Expr { ExprPtr left; Token op; ExprPtr right; LogicalExpr(ExprPtr l, Token o, ExprPtr r): left(std::move(l)), op(std::move(o)), right(std::move(r)){} };
struct CallExpr : Expr { ExprPtr callee; std::vector<ExprPtr> args; CallExpr(ExprPtr c, std::vector<ExprPtr> a): callee(std::move(c)), args(std::move(a)){} };

struct Stmt { virtual ~Stmt() = default; };
struct ExprStmt : Stmt { ExprPtr expr; explicit ExprStmt(ExprPtr e): expr(std::move(e)){} };
struct VarDecl : Stmt { std::string name; ExprPtr init; VarDecl(std::string n, ExprPtr i): name(std::move(n)), init(std::move(i)){} };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; explicit BlockStmt(std::vector<StmtPtr> s): statements(std::move(s)){} };
struct IfStmt : Stmt { ExprPtr cond; StmtPtr thenB; StmtPtr elseB; IfStmt(ExprPtr c, StmtPtr t, StmtPtr e): cond(std::move(c)), thenB(std::move(t)), elseB(std::move(e)){} };
struct WhileStmt : Stmt { ExprPtr cond; StmtPtr body; WhileStmt(ExprPtr c, StmtPtr b): cond(std::move(c)), body(std::move(b)){} };
struct ReturnStmt : Stmt { Token keyword; ExprPtr value; ReturnStmt(Token k, ExprPtr v): keyword(std::move(k)), value(std::move(v)){} };
struct FunctionStmt : Stmt { std::string name; std::vector<std::string> params; StmtPtr body; FunctionStmt(std::string n, std::vector<std::string> p, StmtPtr b): name(std::move(n)), params(std::move(p)), body(std::move(b)){} };

// ----------- Parser -----------

class Parser {
public:
	explicit Parser(const std::vector<Token>& t): tokens(t) {}
	std::vector<StmtPtr> parse() {
		std::vector<StmtPtr> stmts;
		while (!isAtEnd()) stmts.push_back(declaration());
		return stmts;
	}

private:
	const std::vector<Token>& tokens;
	size_t current{0};

	bool isAtEnd() const { return peek().type == TokenType::EndOfFile; }
	const Token& peek() const { return tokens[current]; }
	const Token& previous() const { return tokens[current-1]; }
	const Token& advance() { if (!isAtEnd()) current++; return previous(); }
	bool check(TokenType type) const { return !isAtEnd() && peek().type == type; }
	bool match(std::initializer_list<TokenType> types) {
		for (auto t : types) if (check(t)) { advance(); return true; }
		return false;
	}
	const Token& consume(TokenType type, const char* message) {
		if (check(type)) return advance();
		throw std::runtime_error(std::string("[Parse] ") + message + " at line " + std::to_string(peek().line));
	}

	StmtPtr declaration() {
		if (match({TokenType::Function})) return functionDecl("function");
		if (match({TokenType::Let, TokenType::Var, TokenType::Const})) return varDeclaration();
		return statement();
	}

	StmtPtr functionDecl(const std::string&) {
		auto name = consume(TokenType::Identifier, "Expect function name").lexeme;
		consume(TokenType::LeftParen, "Expect '('");
		std::vector<std::string> params;
		if (!check(TokenType::RightParen)) {
			do {
				params.push_back(consume(TokenType::Identifier, "Expect parameter name").lexeme);
			} while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "Expect ')'");
		auto body = statement();
		return std::make_shared<FunctionStmt>(name, params, body);
	}

	StmtPtr varDeclaration() {
		auto name = consume(TokenType::Identifier, "Expect variable name").lexeme;
		ExprPtr init;
		if (match({TokenType::Equal})) init = expression();
		consume(TokenType::Semicolon, "Expect ';' after variable declaration");
		return std::make_shared<VarDecl>(name, init);
	}

	StmtPtr statement() {
		if (match({TokenType::If})) return ifStatement();
		if (match({TokenType::While})) return whileStatement();
		if (match({TokenType::Return})) return returnStatement();
		if (match({TokenType::LeftBrace})) return std::make_shared<BlockStmt>(block());
		return expressionStatement();
	}

	StmtPtr returnStatement() {
		Token kw = previous();
		ExprPtr val;
		if (!check(TokenType::Semicolon)) val = expression();
		consume(TokenType::Semicolon, "Expect ';' after return value");
		return std::make_shared<ReturnStmt>(kw, val);
	}

	StmtPtr ifStatement() {
		consume(TokenType::LeftParen, "Expect '('");
		auto cond = expression();
		consume(TokenType::RightParen, "Expect ')'");
		auto thenB = statement();
		StmtPtr elseB;
		if (match({TokenType::Else})) elseB = statement();
		return std::make_shared<IfStmt>(cond, thenB, elseB);
	}

	StmtPtr whileStatement() {
		consume(TokenType::LeftParen, "Expect '('");
		auto cond = expression();
		consume(TokenType::RightParen, "Expect ')'");
		auto body = statement();
		return std::make_shared<WhileStmt>(cond, body);
	}

	std::vector<StmtPtr> block() {
		std::vector<StmtPtr> stmts;
		while (!check(TokenType::RightBrace) && !isAtEnd()) stmts.push_back(declaration());
		consume(TokenType::RightBrace, "Expect '}' after block");
		return stmts;
	}

	StmtPtr expressionStatement() {
		auto expr = expression();
		consume(TokenType::Semicolon, "Expect ';' after expression");
		return std::make_shared<ExprStmt>(expr);
	}

	ExprPtr expression() { return assignment(); }

	ExprPtr assignment() {
		auto expr = logicalOr();
		if (match({TokenType::Equal})) {
			auto value = assignment();
			if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
				return std::make_shared<AssignExpr>(var->name, value);
			}
			throw std::runtime_error("Invalid assignment target at line " + std::to_string(previous().line));
		}
		return expr;
	}

	ExprPtr logicalOr() {
		auto expr = logicalAnd();
		while (match({TokenType::OrOr})) {
			Token op = previous();
			auto right = logicalAnd();
			expr = std::make_shared<LogicalExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr logicalAnd() {
		auto expr = equality();
		while (match({TokenType::AndAnd})) {
			Token op = previous();
			auto right = equality();
			expr = std::make_shared<LogicalExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr equality() {
		auto expr = comparison();
		while (match({TokenType::BangEqual, TokenType::EqualEqual})) {
			Token op = previous();
			auto right = comparison();
			expr = std::make_shared<BinaryExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr comparison() {
		auto expr = term();
		while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
			Token op = previous();
			auto right = term();
			expr = std::make_shared<BinaryExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr term() {
		auto expr = factor();
		while (match({TokenType::Plus, TokenType::Minus})) {
			Token op = previous();
			auto right = factor();
			expr = std::make_shared<BinaryExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr factor() {
		auto expr = unary();
		while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
			Token op = previous();
			auto right = unary();
			expr = std::make_shared<BinaryExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr unary() {
		if (match({TokenType::Bang, TokenType::Minus})) {
			Token op = previous();
			auto right = unary();
			return std::make_shared<UnaryExpr>(op, right);
		}
		return call();
	}

	ExprPtr finishCall(ExprPtr callee) {
		std::vector<ExprPtr> args;
		if (!check(TokenType::RightParen)) {
			do { args.push_back(expression()); } while (match({TokenType::Comma}));
		}
		consume(TokenType::RightParen, "Expect ')' after arguments");
		return std::make_shared<CallExpr>(callee, args);
	}

	ExprPtr call() {
		auto expr = primary();
		for (;;) {
			if (match({TokenType::LeftParen})) expr = finishCall(expr);
			else break;
		}
		return expr;
	}

	ExprPtr primary() {
		if (match({TokenType::False})) return std::make_shared<LiteralExpr>(Value{false});
		if (match({TokenType::True})) return std::make_shared<LiteralExpr>(Value{true});
		if (match({TokenType::Null})) return std::make_shared<LiteralExpr>(Value{std::monostate{}});
		if (match({TokenType::Number})) return std::make_shared<LiteralExpr>(Value{std::stod(previous().lexeme)});
		if (match({TokenType::String})) return std::make_shared<LiteralExpr>(Value{previous().lexeme});
		if (match({TokenType::Identifier})) return std::make_shared<VariableExpr>(previous().lexeme);
		if (match({TokenType::LeftParen})) { auto e = expression(); consume(TokenType::RightParen, "Expect ')'"); return e; }
		throw std::runtime_error("Expect expression at line " + std::to_string(peek().line));
	}
};

// ----------- Interpreter -----------

struct ReturnSignal { Value value; };

class Interpreter {
public:
	Interpreter() { globals = std::make_shared<Environment>(); env = globals; installBuiltins(); }

	void execute(const std::vector<StmtPtr>& stmts) {
		for (auto& s : stmts) execute(s);
	}

	Value evaluate(const ExprPtr& expr) {
		if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) return lit->value;
		if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) return env->get(var->name);
		if (auto asg = std::dynamic_pointer_cast<AssignExpr>(expr)) {
			Value v = evaluate(asg->value);
			if (!env->assign(asg->name, v)) throw std::runtime_error("Undefined variable '" + asg->name + "'");
			return v;
		}
		if (auto un = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
			Value r = evaluate(un->right);
			switch (un->op.type) {
			case TokenType::Bang: return Value{!isTruthy(r)};
			case TokenType::Minus: {
				if (auto n = std::get_if<double>(&r)) return Value{-*n};
				throw std::runtime_error("Unary '-' requires number");
			}
			default: break;
			}
		}
		if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
			Value l = evaluate(bin->left); Value r = evaluate(bin->right);
			switch (bin->op.type) {
			case TokenType::Plus:
				if (auto ln = std::get_if<double>(&l)) {
					if (auto rn = std::get_if<double>(&r)) return Value{*ln + *rn};
					if (auto rs = std::get_if<std::string>(&r)) return Value{toString(l) + *rs};
				}
				if (auto ls = std::get_if<std::string>(&l)) return Value{*ls + toString(r)};
				throw std::runtime_error("'+' requires numbers or strings");
			case TokenType::Minus:
				return Value{getNumber(l, "left of '-' ") - getNumber(r, "right of '-' ")};
			case TokenType::Star:
				return Value{getNumber(l, "left of '*' ") * getNumber(r, "right of '*' ")};
			case TokenType::Slash: {
				double denom = getNumber(r, "right of '/' ");
				return Value{getNumber(l, "left of '/' ") / denom};
			}
			case TokenType::Percent: {
				double rv = getNumber(r, "right of '%' ");
				return Value{std::fmod(getNumber(l, "left of '%' "), rv)};
			}
			case TokenType::Greater: return Value{getNumber(l, ">") > getNumber(r, ">")};
			case TokenType::GreaterEqual: return Value{getNumber(l, ">=") >= getNumber(r, ">=")};
			case TokenType::Less: return Value{getNumber(l, "<") < getNumber(r, "<")};
			case TokenType::LessEqual: return Value{getNumber(l, "<=") <= getNumber(r, "<=")};
			case TokenType::EqualEqual: return Value{isEqual(l, r)};
			case TokenType::BangEqual: return Value{!isEqual(l, r)};
			default: break;
			}
		}
		if (auto lg = std::dynamic_pointer_cast<LogicalExpr>(expr)) {
			Value l = evaluate(lg->left);
			if (lg->op.type == TokenType::OrOr) return isTruthy(l) ? l : evaluate(lg->right);
			else return !isTruthy(l) ? l : evaluate(lg->right);
		}
		if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
			Value cal = evaluate(call->callee);
			if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) throw std::runtime_error("Can only call functions");
			auto fn = std::get<std::shared_ptr<Function>>(cal);
			std::vector<Value> args; args.reserve(call->args.size());
			for (auto& a : call->args) args.push_back(evaluate(a));
			if (fn->isBuiltin) return fn->builtin(args);
			if (args.size() != fn->params.size()) throw std::runtime_error("Arity mismatch");
			auto local = std::make_shared<Environment>(fn->closure);
			for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
			try {
				executeBlock(fn->body, local);
			} catch (const ReturnSignal& rs) { return rs.value; }
			return Value{std::monostate{}};
		}
		throw std::runtime_error("Unknown expression type");
	}

	void execute(const StmtPtr& stmt) {
		if (auto e = std::dynamic_pointer_cast<ExprStmt>(stmt)) { (void)evaluate(e->expr); return; }
		if (auto v = std::dynamic_pointer_cast<VarDecl>(stmt)) {
			Value init = v->init ? evaluate(v->init) : Value{std::monostate{}};
			env->define(v->name, init);
			return;
		}
		if (auto b = std::dynamic_pointer_cast<BlockStmt>(stmt)) { executeBlock(b->statements, std::make_shared<Environment>(env)); return; }
		if (auto i = std::dynamic_pointer_cast<IfStmt>(stmt)) {
			if (isTruthy(evaluate(i->cond))) execute(i->thenB); else if (i->elseB) execute(i->elseB);
			return;
		}
		if (auto w = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
			while (isTruthy(evaluate(w->cond))) execute(w->body);
			return;
		}
		if (auto r = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
			Value val = r->value ? evaluate(r->value) : Value{std::monostate{}};
			throw ReturnSignal{val};
		}
		if (auto f = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
			auto fn = std::make_shared<Function>();
			fn->params = f->params;
			// 将语句体包装成块：函数体如果是单个语句，处理成block便于复用
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(f->body)) fn->body = innerBlock->statements;
			else fn->body = { f->body };
			fn->closure = env;
			env->define(f->name, fn);
			return;
		}
		throw std::runtime_error("Unknown statement type");
	}

	void executeBlock(const std::vector<StmtPtr>& stmts, std::shared_ptr<Environment> newEnv) {
		auto previous = env;
		env = newEnv;
		try {
			for (auto& s : stmts) execute(s);
		} catch (...) {
			env = previous; throw;
		}
		env = previous;
	}

	std::shared_ptr<Environment> globalsEnv() const { return globals; }

private:
	std::shared_ptr<Environment> globals;
	std::shared_ptr<Environment> env;

	static bool isEqual(const Value& a, const Value& b) {
		if (a.index() != b.index()) {
			// number vs string etc.: try JS-like weak equality? Here keep strict by type.
			return false;
		}
		if (std::holds_alternative<std::monostate>(a)) return true;
		if (auto na = std::get_if<double>(&a)) return *na == std::get<double>(b);
		if (auto sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
		if (auto ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
		// functions compare by pointer
		return std::get<std::shared_ptr<Function>>(a).get() == std::get<std::shared_ptr<Function>>(b).get();
	}

	static double getNumber(const Value& v, const char* where) {
		if (auto n = std::get_if<double>(&v)) return *n;
		if (auto s = std::get_if<std::string>(&v)) {
			char* end = nullptr; double d = std::strtod(s->c_str(), &end); if (end && *end=='\0') return d;
		}
		throw std::runtime_error(std::string("Expected number at ") + where);
	}

	void installBuiltins() {
		auto printFn = std::make_shared<Function>();
		printFn->isBuiltin = true;
		printFn->builtin = [](const std::vector<Value>& args)->Value{
			for (size_t i=0;i<args.size();++i) {
				std::cout << toString(args[i]);
				if (i+1<args.size()) std::cout << " ";
			}
			std::cout << std::endl;
			return Value{std::monostate{}};
		};
		globals->define("print", printFn);
	}
};

} // namespace

// ----------- ALangEngine facade -----------

struct ALangEngine::Impl {
	std::string source;
	Interpreter interpreter;
};

ALangEngine::ALangEngine() : impl(new Impl()) {}
ALangEngine::~ALangEngine() { delete impl; }

void ALangEngine::initialize() {
	// 目前Interpreter构造已安装内置，无需额外初始化
}

void ALangEngine::setSource(const std::string& code) { impl->source = code; }

void ALangEngine::execute() {
	if (impl->source.empty()) return;
	execute(impl->source);
}

void ALangEngine::execute(const std::string& code) {
	try {
		Lexer lx(code);
		auto tokens = lx.scanTokens();
		Parser ps(tokens);
		auto stmts = ps.parse();
		impl->interpreter.execute(stmts);
	} catch (const std::exception& ex) {
		std::cerr << "[ALang Error] " << ex.what() << std::endl;
		throw; // 也可选择吞掉错误，根据需要
	}
}

void ALangEngine::registerModule(const char* /*moduleName*/, std::function<void()> initFunc) {
	if (initFunc) initFunc();
}

