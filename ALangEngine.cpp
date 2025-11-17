#include "ALangEngine.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <queue>

namespace {

// ----------- Lexer -----------

enum class TokenType {
	// Single-char
	LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
	Comma, Semicolon, Colon, Dot,
	Plus, Minus, Star, Slash, Percent,
	Bang, Equal, Less, Greater,
	// One or two char
	BangEqual, EqualEqual, LessEqual, GreaterEqual, LeftArrow,
	AndAnd, OrOr,
	// Literals
	Identifier, String, Number,
	// Keywords
	Let, Var, Const, Function, Return, If, Else, While, For, Break, Continue, Class, Extends, New, True, False, Null, Await, Async, Go,
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
			{"for", TokenType::For}, {"break", TokenType::Break}, {"continue", TokenType::Continue},
			{"class", TokenType::Class}, {"extends", TokenType::Extends}, {"new", TokenType::New},
			{"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
			{"await", TokenType::Await},
			{"async", TokenType::Async},
			{"go", TokenType::Go},
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
		case '[': add(TokenType::LeftBracket); break;
		case ']': add(TokenType::RightBracket); break;
		case ',': add(TokenType::Comma); break;
		case ';': add(TokenType::Semicolon); break;
		case ':': add(TokenType::Colon); break;
		case '.': add(TokenType::Dot); break;
		case '+': add(TokenType::Plus); break;
		case '-': add(TokenType::Minus); break;
		case '*': add(TokenType::Star); break;
		case '%': add(TokenType::Percent); break;
		case '!': add(match('=') ? TokenType::BangEqual : TokenType::Bang); break;
		case '=': add(match('=') ? TokenType::EqualEqual : TokenType::Equal); break;
		case '<': {
			if (match('-')) { add(TokenType::LeftArrow); }
			else if (match('=')) { add(TokenType::LessEqual); }
			else { add(TokenType::Less); }
			break;
		}
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
struct PromiseState;

using Array = std::vector<struct ValueTag>;
using Object = std::unordered_map<std::string, struct ValueTag>;

struct ClassInfo;
struct Instance;

// forward for variant recursive types
struct ValueTag : public std::variant<std::monostate,double,std::string,bool,std::shared_ptr<Function>,std::shared_ptr<Array>,std::shared_ptr<Object>,std::shared_ptr<ClassInfo>,std::shared_ptr<Instance>,std::shared_ptr<PromiseState>> {
	using variant::variant;
};

using Value = ValueTag;

inline std::string typeOf(const Value& v) {
	switch (v.index()) {
	case 0: return "null";
	case 1: return "number";
	case 2: return "string";
	case 3: return "boolean";
	case 4: return "function";
	case 5: return "array";
	case 6: return "object";
	case 7: return "class";
	case 8: return "instance";
	case 9: return "promise";
	default: return "unknown";
	}
}

inline bool isTruthy(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return false;
	if (auto b = std::get_if<bool>(&v)) return *b;
	if (auto n = std::get_if<double>(&v)) return *n != 0.0;
	if (auto s = std::get_if<std::string>(&v)) return !s->empty();
	if (std::holds_alternative<std::shared_ptr<Array>>(v)) return true;
	if (std::holds_alternative<std::shared_ptr<Object>>(v)) return true;
	return true;
}

inline std::string toString(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return "null";
	if (auto n = std::get_if<double>(&v)) {
		std::ostringstream oss; oss << *n; return oss.str();
	}
	if (auto s = std::get_if<std::string>(&v)) return *s;
	if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
	if (std::holds_alternative<std::shared_ptr<Function>>(v)) return "[Function]";
	if (auto arr = std::get_if<std::shared_ptr<Array>>(&v)) {
		std::ostringstream oss; oss << "[";
		if (*arr) {
			for (size_t i=0;i<(*arr)->size();++i) {
				if (i) oss << ", ";
				oss << toString((**arr)[i]);
			}
		}
		oss << "]"; return oss.str();
	}
	if (auto obj = std::get_if<std::shared_ptr<Object>>(&v)) {
		std::ostringstream oss; oss << "{"; bool first=true;
		if (*obj) {
			for (auto& kv : **obj) {
				if (!first) oss << ", "; first=false;
				oss << kv.first << ": " << toString(kv.second);
			}
		}
		oss << "}"; return oss.str();
	}
	if (std::holds_alternative<std::shared_ptr<ClassInfo>>(v)) return "[Class]";
	if (std::holds_alternative<std::shared_ptr<Instance>>(v)) return "[Object]";
	if (std::holds_alternative<std::shared_ptr<PromiseState>>(v)) return "[Promise]";
	return "unknown";
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
	bool isAsync{false};
	std::function<Value(const std::vector<Value>&, std::shared_ptr<Environment>)> builtin;
};

struct ClassInfo {
	std::string name;
	std::vector<std::shared_ptr<ClassInfo>> supers; // 多继承支持，按声明顺序线性查找
	std::unordered_map<std::string, std::shared_ptr<Function>> methods;
};

struct Instance {
	std::shared_ptr<ClassInfo> klass;
	std::unordered_map<std::string, Value> fields;
};

// Promise 状态：用于 await 等待
struct PromiseState {
	std::mutex mtx;
	std::condition_variable cv;
	bool settled{false};
	bool rejected{false};
	Value result{std::monostate{}};
	// 简单事件循环指针，用于 then/catch 回调分发
	void* loopPtr{nullptr};
	// then/catch 回调以及链式的下一 Promise
	std::vector<std::pair<std::shared_ptr<Function>, std::shared_ptr<PromiseState>>> thenCallbacks;
	std::vector<std::pair<std::shared_ptr<Function>, std::shared_ptr<PromiseState>>> catchCallbacks;
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
struct NewExpr : Expr { ExprPtr callee; std::vector<ExprPtr> args; NewExpr(ExprPtr c, std::vector<ExprPtr> a): callee(std::move(c)), args(std::move(a)){} };
struct GetPropExpr : Expr { ExprPtr object; std::string name; GetPropExpr(ExprPtr o, std::string n): object(std::move(o)), name(std::move(n)){} };
struct IndexExpr : Expr { ExprPtr object; ExprPtr index; IndexExpr(ExprPtr o, ExprPtr i): object(std::move(o)), index(std::move(i)){} };
struct SetPropExpr : Expr { ExprPtr object; std::string name; ExprPtr value; SetPropExpr(ExprPtr o, std::string n, ExprPtr v): object(std::move(o)), name(std::move(n)), value(std::move(v)){} };
struct SetIndexExpr : Expr { ExprPtr object; ExprPtr index; ExprPtr value; SetIndexExpr(ExprPtr o, ExprPtr i, ExprPtr v): object(std::move(o)), index(std::move(i)), value(std::move(v)){} };
struct ArrayLiteralExpr : Expr { std::vector<ExprPtr> elements; explicit ArrayLiteralExpr(std::vector<ExprPtr> e): elements(std::move(e)){} };
struct ObjectLiteralExpr : Expr { std::vector<std::pair<std::string, ExprPtr>> props; explicit ObjectLiteralExpr(std::vector<std::pair<std::string, ExprPtr>> p): props(std::move(p)){} };
struct AwaitExpr : Expr { ExprPtr expr; explicit AwaitExpr(ExprPtr e): expr(std::move(e)){} };
struct FunctionExpr : Expr { std::vector<std::string> params; StmtPtr body; explicit FunctionExpr(std::vector<std::string> p, StmtPtr b): params(std::move(p)), body(std::move(b)){} };

struct Stmt { virtual ~Stmt() = default; };
struct ExprStmt : Stmt { ExprPtr expr; explicit ExprStmt(ExprPtr e): expr(std::move(e)){} };
struct VarDecl : Stmt { std::string name; ExprPtr init; VarDecl(std::string n, ExprPtr i): name(std::move(n)), init(std::move(i)){} };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; explicit BlockStmt(std::vector<StmtPtr> s): statements(std::move(s)){} };
struct IfStmt : Stmt { ExprPtr cond; StmtPtr thenB; StmtPtr elseB; IfStmt(ExprPtr c, StmtPtr t, StmtPtr e): cond(std::move(c)), thenB(std::move(t)), elseB(std::move(e)){} };
struct WhileStmt : Stmt { ExprPtr cond; StmtPtr body; WhileStmt(ExprPtr c, StmtPtr b): cond(std::move(c)), body(std::move(b)){} };
struct ReturnStmt : Stmt { Token keyword; ExprPtr value; ReturnStmt(Token k, ExprPtr v): keyword(std::move(k)), value(std::move(v)){} };
struct FunctionStmt : Stmt { std::string name; std::vector<std::string> params; StmtPtr body; bool isAsync{false}; FunctionStmt(std::string n, std::vector<std::string> p, StmtPtr b, bool a=false): name(std::move(n)), params(std::move(p)), body(std::move(b)), isAsync(a){} };
struct ClassStmt : Stmt { std::string name; std::vector<std::string> superNames; std::vector<std::shared_ptr<FunctionStmt>> methods; };
struct ExtendStmt : Stmt { std::string name; std::vector<std::shared_ptr<FunctionStmt>> methods; };
struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};
struct ForStmt : Stmt { StmtPtr init; ExprPtr cond; ExprPtr post; StmtPtr body; ForStmt(StmtPtr i, ExprPtr c, ExprPtr p, StmtPtr b): init(std::move(i)), cond(std::move(c)), post(std::move(p)), body(std::move(b)){} };
struct GoStmt : Stmt { ExprPtr call; explicit GoStmt(ExprPtr c): call(std::move(c)){} };

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
		if (match({TokenType::Async})) { consume(TokenType::Function, "Expect 'function' after 'async'"); return functionDecl(true); }
		if (match({TokenType::Function})) return functionDecl(false);
		if (match({TokenType::Class})) return classDeclaration();
		if (match({TokenType::Extends})) return extendsDeclaration();
		if (match({TokenType::Let, TokenType::Var, TokenType::Const})) return varDeclaration();
		return statement();
	}

	StmtPtr classDeclaration() {
		auto nameTok = consume(TokenType::Identifier, "Expect class name");
		auto cls = std::make_shared<ClassStmt>();
		cls->name = nameTok.lexeme;
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
				bool isAsync = match({TokenType::Async});
				(void)match({TokenType::Function});
				auto mname = consume(TokenType::Identifier, "Expect method name").lexeme;
				consume(TokenType::LeftParen, "Expect '('");
				std::vector<std::string> params;
				if (!check(TokenType::RightParen)) {
					do { params.push_back(consume(TokenType::Identifier, "Expect parameter name").lexeme); } while (match({TokenType::Comma}));
				}
				consume(TokenType::RightParen, "Expect ')'");
				auto body = statement();
				cls->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync));
			}
			consume(TokenType::RightBrace, "Expect '}' after class body");
		}
		return cls;
	}

	StmtPtr extendsDeclaration() {
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
			std::vector<std::string> params;
			if (!check(TokenType::RightParen)) {
				do { params.push_back(consume(TokenType::Identifier, "Expect parameter name").lexeme); } while (match({TokenType::Comma}));
			}
			consume(TokenType::RightParen, "Expect ')'");
			auto body = statement();
			ext->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync));
		}
		consume(TokenType::RightBrace, "Expect '}' after extension body");
		return ext;
	}

	StmtPtr functionDecl(bool isAsync) {
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
		return std::make_shared<FunctionStmt>(name, params, body, isAsync);
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
		if (match({TokenType::For})) return forStatement();
		if (match({TokenType::Return})) return returnStatement();
		if (match({TokenType::Go})) { auto expr = expression(); consume(TokenType::Semicolon, "Expect ';' after go call"); return std::make_shared<GoStmt>(expr); }
		if (match({TokenType::Break})) { consume(TokenType::Semicolon, "Expect ';' after break"); return std::make_shared<BreakStmt>(); }
		if (match({TokenType::Continue})) { consume(TokenType::Semicolon, "Expect ';' after continue"); return std::make_shared<ContinueStmt>(); }
		if (match({TokenType::LeftBrace})) return std::make_shared<BlockStmt>(block());
		return expressionStatement();
	}

	StmtPtr forStatement() {
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
			if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
				return std::make_shared<SetPropExpr>(getp->object, getp->name, value);
			}
			if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
				return std::make_shared<SetIndexExpr>(idx->object, idx->index, value);
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
		if (match({TokenType::Await})) {
			auto inner = unary();
			return std::make_shared<AwaitExpr>(inner);
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
			else if (match({TokenType::Dot})) {
				auto name = consume(TokenType::Identifier, "Expect property name after '.'").lexeme;
				expr = std::make_shared<GetPropExpr>(expr, name);
			}
			else if (match({TokenType::LeftBracket})) {
				auto idx = expression();
				consume(TokenType::RightBracket, "Expect ']' after index");
				expr = std::make_shared<IndexExpr>(expr, idx);
			}
			else break;
		}
		return expr;
	}

	ExprPtr primary() {
		// 支持匿名函数：[](x, y){ ... }
		if (check(TokenType::LeftBracket)) {
			// 仅当模式为 [] ( 开始时，识别为 lambda；否则按数组字面量
			if (current + 2 < tokens.size() && tokens[current].type == TokenType::LeftBracket && tokens[current+1].type == TokenType::RightBracket && tokens[current+2].type == TokenType::LeftParen) {
				advance(); // [
				advance(); // ]
				advance(); // (
				std::vector<std::string> params;
				if (!check(TokenType::RightParen)) {
					do { params.push_back(consume(TokenType::Identifier, "Expect parameter name").lexeme); } while (match({TokenType::Comma}));
				}
				consume(TokenType::RightParen, "Expect ')' after lambda parameters");
				auto body = statement();
				return std::make_shared<FunctionExpr>(params, body);
			}
		}
		if (match({TokenType::New})) {
			auto nameTok = consume(TokenType::Identifier, "Expect class name after 'new'");
			consume(TokenType::LeftParen, "Expect '('");
			std::vector<ExprPtr> args;
			if (!check(TokenType::RightParen)) { do { args.push_back(expression()); } while (match({TokenType::Comma})); }
			consume(TokenType::RightParen, "Expect ')'");
			return std::make_shared<NewExpr>(std::make_shared<VariableExpr>(nameTok.lexeme), args);
		}
		if (match({TokenType::False})) return std::make_shared<LiteralExpr>(Value{false});
		if (match({TokenType::True})) return std::make_shared<LiteralExpr>(Value{true});
		if (match({TokenType::Null})) return std::make_shared<LiteralExpr>(Value{std::monostate{}});
		if (match({TokenType::Number})) return std::make_shared<LiteralExpr>(Value{std::stod(previous().lexeme)});
		if (match({TokenType::String})) return std::make_shared<LiteralExpr>(Value{previous().lexeme});
		if (match({TokenType::Identifier})) return std::make_shared<VariableExpr>(previous().lexeme);
		if (match({TokenType::LeftBracket})) {
			std::vector<ExprPtr> elems;
			if (!check(TokenType::RightBracket)) {
				do { elems.push_back(expression()); } while (match({TokenType::Comma}));
			}
			consume(TokenType::RightBracket, "Expect ']' after array literal");
			return std::make_shared<ArrayLiteralExpr>(elems);
		}
		if (match({TokenType::LeftBrace})) {
			std::vector<std::pair<std::string, ExprPtr>> props;
			if (!check(TokenType::RightBrace)) {
				do {
					std::string key;
					if (match({TokenType::Identifier})) key = previous().lexeme;
					else if (match({TokenType::String})) key = previous().lexeme;
					else throw std::runtime_error("Expect property name in object literal");
					consume(TokenType::Colon, "Expect ':' after property name");
					auto val = expression();
					props.emplace_back(std::move(key), val);
				} while (match({TokenType::Comma}));
			}
			consume(TokenType::RightBrace, "Expect '}' after object literal");
			return std::make_shared<ObjectLiteralExpr>(props);
		}
		if (match({TokenType::LeftParen})) { auto e = expression(); consume(TokenType::RightParen, "Expect ')'"); return e; }
		throw std::runtime_error("Expect expression at line " + std::to_string(peek().line));
	}
};

// ----------- Interpreter -----------

struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

class Interpreter {
public:
	Interpreter() { globals = std::make_shared<Environment>(); env = globals; installBuiltins(); }

	void execute(const std::vector<StmtPtr>& stmts) {
		for (auto& s : stmts) execute(s);
	}

	// 事件循环：用于分发 then/catch 与 go 任务
	void postTask(std::function<void()> fn) {
		{
			std::lock_guard<std::mutex> lk(loopMutex);
			taskQueue.push(std::move(fn));
		}
		loopCv.notify_one();
	}
	void runEventLoopUntilIdle() {
		for (;;) {
			std::function<void()> fn;
			{
				std::unique_lock<std::mutex> lk(loopMutex);
				if (taskQueue.empty()) break;
				fn = std::move(taskQueue.front()); taskQueue.pop();
			}
			if (fn) fn();
		}
	}

	Value evaluate(const ExprPtr& expr) {
		if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) return lit->value;
		if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) return env->get(var->name);
		if (auto asg = std::dynamic_pointer_cast<AssignExpr>(expr)) {
			Value v = evaluate(asg->value);
			if (!env->assign(asg->name, v)) throw std::runtime_error("Undefined variable '" + asg->name + "'");
			return v;
		}
		if (auto arr = std::dynamic_pointer_cast<ArrayLiteralExpr>(expr)) {
			auto av = std::make_shared<Array>();
			av->reserve(arr->elements.size());
			for (auto& e : arr->elements) av->push_back(evaluate(e));
			return Value{std::shared_ptr<Array>(av)};
		}
		if (auto obj = std::dynamic_pointer_cast<ObjectLiteralExpr>(expr)) {
			auto ov = std::make_shared<Object>();
			for (auto& p : obj->props) (*ov)[p.first] = evaluate(p.second);
			return Value{std::shared_ptr<Object>(ov)};
		}
		if (auto gp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			Value o = evaluate(gp->object);
			return getProperty(o, gp->name);
		}
		if (auto ix = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			Value o = evaluate(ix->object);
			Value k = evaluate(ix->index);
			return getIndex(o, k);
		}
		if (auto sp = std::dynamic_pointer_cast<SetPropExpr>(expr)) {
			Value& ov = ensureObjectRef(sp->object);
			Value v = evaluate(sp->value);
			if (auto pobj = std::get_if<std::shared_ptr<Object>>(&ov)) {
				(**pobj)[sp->name] = v;
				return v;
			}
			if (auto pins = std::get_if<std::shared_ptr<Instance>>(&ov)) {
				(**pins).fields[sp->name] = v;
				return v;
			}
			return v;
		}
		if (auto si = std::dynamic_pointer_cast<SetIndexExpr>(expr)) {
			Value& ov = evaluateRef(si->object);
			Value idxv = evaluate(si->index);
			Value v = evaluate(si->value);
			if (auto parr = std::get_if<std::shared_ptr<Array>>(&ov)) {
				size_t idx = indexFromValue(idxv);
				auto& vec = **parr;
				if (idx >= vec.size()) throw std::runtime_error("Array index out of range");
				vec[idx] = v; return v;
			}
			if (auto pobj = std::get_if<std::shared_ptr<Object>>(&ov)) {
				std::string key = keyFromValue(idxv);
				(**pobj)[key] = v; return v;
			}
			if (auto pins = std::get_if<std::shared_ptr<Instance>>(&ov)) {
				std::string key = keyFromValue(idxv);
				(*pins)->fields[key] = v; return v;
			}
			throw std::runtime_error("Index assignment on non-array/object");
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
		if (auto aw = std::dynamic_pointer_cast<AwaitExpr>(expr)) {
			Value v = evaluate(aw->expr);
			if (!std::holds_alternative<std::shared_ptr<PromiseState>>(v)) {
				throw std::runtime_error("await expects a Promise");
			}
			auto p = std::get<std::shared_ptr<PromiseState>>(v);
			if (!p) return Value{std::monostate{}};
			std::unique_lock<std::mutex> lk(p->mtx);
			p->cv.wait(lk, [&]{ return p->settled; });
			if (p->rejected) throw std::runtime_error("Promise rejected");
			return p->result;
		}
		if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
			Value cal = evaluate(call->callee);
			if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) throw std::runtime_error("Can only call functions");
			auto fn = std::get<std::shared_ptr<Function>>(cal);
			std::vector<Value> args; args.reserve(call->args.size());
			for (auto& a : call->args) args.push_back(evaluate(a));
			if (fn->isBuiltin) return fn->builtin(args, fn->closure);
			// 支持调用由 FunctionExpr 生成的普通函数
			if (fn->isAsync) {
				// 返回一个 Promise，并将函数体作为任务投递
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				postTask([this, fn, args, p]{
					// 在闭包环境基础上创建局部环境并执行
					auto local = std::make_shared<Environment>(fn->closure);
					if (args.size() != fn->params.size()) {
						for (size_t i=0;i<fn->params.size() && i<args.size();++i) local->define(fn->params[i], args[i]);
					} else {
						for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
					}
					Value ret{std::monostate{}};
					try {
						executeBlock(fn->body, local);
					} catch (const ReturnSignal& rs) {
						ret = rs.value;
					}
					settlePromise(p, false, ret);
				});
				return Value{p};
			}
			if (args.size() != fn->params.size()) throw std::runtime_error("Arity mismatch");
			auto local = std::make_shared<Environment>(fn->closure);
			for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
			try {
				executeBlock(fn->body, local);
			} catch (const ReturnSignal& rs) { return rs.value; }
			return Value{std::monostate{}};
		}
		if (auto fexpr = std::dynamic_pointer_cast<FunctionExpr>(expr)) {
			auto fn = std::make_shared<Function>();
			fn->params = fexpr->params;
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(fexpr->body)) fn->body = innerBlock->statements; else fn->body = { fexpr->body };
			fn->closure = env; // 关闭环境捕获
			return Value{fn};
		}
		if (auto nw = std::dynamic_pointer_cast<NewExpr>(expr)) {
			Value cal = evaluate(nw->callee);
			if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(cal)) throw std::runtime_error("new: target is not a class");
			auto klass = std::get<std::shared_ptr<ClassInfo>>(cal);
			auto inst = std::make_shared<Instance>();
			inst->klass = klass;
			// constructor (lookup super chain)
			auto ctor = findMethod(klass, "constructor");
			if (ctor) {
				std::vector<Value> args; args.reserve(nw->args.size());
				for (auto& a : nw->args) args.push_back(evaluate(a));
				// bind this
				auto bound = std::make_shared<Function>(*ctor);
				auto thisEnv = std::make_shared<Environment>(bound->closure);
				thisEnv->define("this", inst);
				bound->closure = thisEnv;
				if (bound->isBuiltin) (void)bound->builtin(args, bound->closure); else {
					if (args.size() != bound->params.size()) throw std::runtime_error("Arity mismatch");
					auto local = std::make_shared<Environment>(bound->closure);
					for (size_t i=0;i<args.size();++i) local->define(bound->params[i], args[i]);
					try { executeBlock(bound->body, local); } catch (const ReturnSignal&) {}
				}
			}
			return inst;
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
			while (isTruthy(evaluate(w->cond))) {
				try { execute(w->body); }
				catch (const ContinueSignal&) { /* continue loop */ }
				catch (const BreakSignal&) { break; }
			}
			return;
		}
		if (auto f = std::dynamic_pointer_cast<ForStmt>(stmt)) {
			if (f->init) execute(f->init);
			for (;;) {
				if (f->cond) { if (!isTruthy(evaluate(f->cond))) break; }
				try { execute(f->body); }
				catch (const ContinueSignal&) { /* go to post */ }
				catch (const BreakSignal&) { break; }
				if (f->post) (void)evaluate(f->post);
			}
			return;
		}
		if (auto r = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
			Value val = r->value ? evaluate(r->value) : Value{std::monostate{}};
			throw ReturnSignal{val};
		}
		if (std::dynamic_pointer_cast<BreakStmt>(stmt)) { throw BreakSignal{}; }
		if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) { throw ContinueSignal{}; }
		if (auto f = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
			auto fn = std::make_shared<Function>();
			fn->params = f->params;
			// 将语句体包装成块：函数体如果是单个语句，处理成block便于复用
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(f->body)) fn->body = innerBlock->statements;
			else fn->body = { f->body };
			fn->closure = env;
			fn->isAsync = f->isAsync;
			env->define(f->name, fn);
			return;
		}
		if (auto c = std::dynamic_pointer_cast<ClassStmt>(stmt)) {
			auto klass = std::make_shared<ClassInfo>();
			klass->name = c->name;
			// 解析多父类
			for (auto& sname : c->superNames) {
				Value sv = env->get(sname);
				if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(sv)) throw std::runtime_error("Base must be a class: " + sname);
				klass->supers.push_back(std::get<std::shared_ptr<ClassInfo>>(sv));
			}
			// methods
			for (auto& m : c->methods) {
				auto fn = std::make_shared<Function>();
				fn->params = m->params;
				if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(m->body)) fn->body = innerBlock->statements; else fn->body = { m->body };
				fn->closure = env;
				klass->methods[m->name] = fn;
			}
			env->define(c->name, klass);
			return;
		}
		if (auto ext = std::dynamic_pointer_cast<ExtendStmt>(stmt)) {
			Value cv = env->get(ext->name);
			if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(cv)) throw std::runtime_error("extends: target is not a class: " + ext->name);
			auto klass = std::get<std::shared_ptr<ClassInfo>>(cv);
			for (auto& m : ext->methods) {
				auto fn = std::make_shared<Function>();
				fn->params = m->params;
				if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(m->body)) fn->body = innerBlock->statements; else fn->body = { m->body };
				fn->closure = env;
				fn->isAsync = m->isAsync;
				klass->methods[m->name] = fn; // 覆盖或新增
			}
			return;
		}
		if (auto go = std::dynamic_pointer_cast<GoStmt>(stmt)) {
			// 调度一个任务在事件循环中执行表达式（通常为调用表达式）
			auto exprCopy = go->call;
			auto envSnap = env;
			postTask([this, exprCopy, envSnap]{
				auto prev = env;
				env = envSnap;
				try { (void) evaluate(exprCopy); } catch (...) { /* 丢弃 go 任务中的异常 */ }
				env = prev;
			});
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

	// 供宿主侧调用全局函数的便捷方法
	Value callFunction(const std::string& name, const std::vector<Value>& args) {
		Value cal = globals->get(name);
		if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) throw std::runtime_error("callFunction: target is not a function: " + name);
		auto fn = std::get<std::shared_ptr<Function>>(cal);
		if (fn->isBuiltin) {
			return fn->builtin(args, fn->closure);
		}
		if (args.size() != fn->params.size()) throw std::runtime_error("callFunction: arity mismatch");
		auto local = std::make_shared<Environment>(fn->closure);
		for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
		try {
			executeBlock(fn->body, local);
		} catch (const ReturnSignal& rs) {
			return rs.value;
		}
		return Value{std::monostate{}};
	}

private:
	std::shared_ptr<Environment> globals;
	std::shared_ptr<Environment> env;
	std::mutex loopMutex;
	std::condition_variable loopCv;
	std::queue<std::function<void()>> taskQueue;

	void settlePromise(std::shared_ptr<PromiseState> p, bool rejected, const Value& result) {
		{
			std::lock_guard<std::mutex> lk(p->mtx);
			p->settled = true; p->rejected = rejected; p->result = result;
		}
		p->cv.notify_all();
		dispatchPromiseCallbacks(p);
	}

	void dispatchPromiseCallbacks(std::shared_ptr<PromiseState> p) {
		if (!p->loopPtr) return;
		auto loop = static_cast<Interpreter*>(p->loopPtr);
		if (!p->rejected) {
			for (auto& pair : p->thenCallbacks) {
				auto cb = pair.first; auto nextP = pair.second;
				loop->postTask([this, cb, nextP, p]() {
					try {
						std::vector<Value> a{ p->result };
						Value ret{std::monostate{}};
						if (cb->isBuiltin) ret = cb->builtin(a, cb->closure);
						else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (a.size() != cb->params.size()) {
								if (!cb->params.empty()) local->define(cb->params[0], p->result);
							} else {
								for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]);
							}
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
						}
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
							auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
							// 链接：inner 完成后再 settle nextP
							{
								std::lock_guard<std::mutex> lk(inner->mtx);
								inner->loopPtr = this;
								inner->thenCallbacks.push_back({ makeResolver(), nextP });
								inner->catchCallbacks.push_back({ makeRejecter(), nextP });
							}
							if (inner->settled) dispatchPromiseCallbacks(inner);
						} else {
							settlePromise(nextP, false, ret);
						}
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					}
				});
			}
		} else {
			for (auto& pair : p->catchCallbacks) {
				auto cb = pair.first; auto nextP = pair.second;
				loop->postTask([this, cb, nextP, p]() {
					try {
						std::vector<Value> a{ p->result };
						Value ret{std::monostate{}};
						if (cb->isBuiltin) ret = cb->builtin(a, cb->closure);
						else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (a.size() != cb->params.size()) {
								if (!cb->params.empty()) local->define(cb->params[0], p->result);
							} else {
								for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]);
							}
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
						}
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
							auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
							{
								std::lock_guard<std::mutex> lk(inner->mtx);
								inner->loopPtr = this;
								inner->thenCallbacks.push_back({ makeResolver(), nextP });
								inner->catchCallbacks.push_back({ makeRejecter(), nextP });
							}
							if (inner->settled) dispatchPromiseCallbacks(inner);
						} else {
							settlePromise(nextP, false, ret);
						}
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					}
				});
			}
		}
	}

	// 生成一个 resolver/rejecter 回调函数（形如 x => x 或 e => throw e）用于链接
	std::shared_ptr<Function> makeResolver() {
		auto f = std::make_shared<Function>();
		f->isBuiltin = true;
		f->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty()) return Value{std::monostate{}};
			return args[0];
		};
		return f;
	}
	std::shared_ptr<Function> makeRejecter() {
		auto f = std::make_shared<Function>();
		f->isBuiltin = true;
		f->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			// 通过抛异常传播拒绝
			if (args.empty()) throw std::runtime_error("Promise rejected");
			std::string msg = toString(args[0]);
			throw std::runtime_error(msg);
		};
		return f;
	}

	static bool isEqual(const Value& a, const Value& b) {
		if (a.index() != b.index()) {
			//keep strict by type. NOT ALLOWED JAVA-SCRIPT STYLE TYPE COERCION
			return false;
		}
		if (std::holds_alternative<std::monostate>(a)) return true;
		if (auto na = std::get_if<double>(&a)) return *na == std::get<double>(b);
		if (auto sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
		if (auto ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
		// functions compare by pointer
		if (std::holds_alternative<std::shared_ptr<Function>>(a)) return std::get<std::shared_ptr<Function>>(a).get() == std::get<std::shared_ptr<Function>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Array>>(a)) return std::get<std::shared_ptr<Array>>(a).get() == std::get<std::shared_ptr<Array>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Object>>(a)) return std::get<std::shared_ptr<Object>>(a).get() == std::get<std::shared_ptr<Object>>(b).get();
		if (std::holds_alternative<std::shared_ptr<ClassInfo>>(a)) return std::get<std::shared_ptr<ClassInfo>>(a).get() == std::get<std::shared_ptr<ClassInfo>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Instance>>(a)) return std::get<std::shared_ptr<Instance>>(a).get() == std::get<std::shared_ptr<Instance>>(b).get();
		if (std::holds_alternative<std::shared_ptr<PromiseState>>(a)) return std::get<std::shared_ptr<PromiseState>>(a).get() == std::get<std::shared_ptr<PromiseState>>(b).get();
		return false;
	}

	static double getNumber(const Value& v, const char* where) {
		if (auto n = std::get_if<double>(&v)) return *n;
		if (auto s = std::get_if<std::string>(&v)) {
			char* end = nullptr; double d = std::strtod(s->c_str(), &end); if (end && *end=='\0') return d;
		}
		throw std::runtime_error(std::string("Expected number at ") + where);
	}

	// Helpers for object/array/class/instance access
	static std::shared_ptr<Function> findMethod(std::shared_ptr<ClassInfo> k, const std::string& name) {
		if (!k) return nullptr;
		auto it = k->methods.find(name);
		if (it != k->methods.end()) return it->second;
		// 多继承：按声明顺序递归线性查找
		for (auto& s : k->supers) {
			auto f = findMethod(s, name);
			if (f) return f;
		}
		return nullptr;
	}
	static Value getProperty(const Value& obj, const std::string& name) {
		// Instance: fields then methods
		if (auto pins = std::get_if<std::shared_ptr<Instance>>(&obj)) {
			if (*pins) {
				auto fit = (*pins)->fields.find(name);
				if (fit != (*pins)->fields.end()) return fit->second;
				if ((*pins)->klass) {
					auto m = findMethod((*pins)->klass, name);
					if (m) {
						auto bound = std::make_shared<Function>(*m);
						auto thisEnv = std::make_shared<Environment>(bound->closure);
						thisEnv->define("this", obj);
						bound->closure = thisEnv;
						return bound;
					}
				}
				return Value{std::monostate{}};
			}
		}
		// Object
		if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
			auto it = (**po).find(name);
			if (it != (**po).end()) return it->second;
			// synthetic len()
			if (name == "len") {
				auto lenFn = std::make_shared<Function>(); lenFn->isBuiltin = true;
				auto o = *po; lenFn->builtin = [o](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(o ? o->size() : 0) }; };
				return lenFn;
			}
			return Value{std::monostate{}};
		}
		// Array synthetic methods
		if (auto parr = std::get_if<std::shared_ptr<Array>>(&obj)) {
			if (name == "len") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [a](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(a ? a->size() : 0) }; };
				return fn;
			}
			if (name == "push") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{0.0}; for (auto& v: args) a->push_back(v); return Value{ static_cast<double>(a->size()) }; };
				return fn;
			}
			return Value{std::monostate{}};
		}
		// String synthetic methods
		if (auto ps = std::get_if<std::string>(&obj)) {
			if (name == "len") { auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(s.size()) }; }; return fn; }
			return Value{std::monostate{}};
		}
		// Promise synthetic methods: then/catch
		if (auto p = std::get_if<std::shared_ptr<PromiseState>>(&obj)) {
			if (name == "then") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				auto ps = *p;
				fn->builtin = [ps](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("then expects a function");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto nextP = std::make_shared<PromiseState>();
					nextP->loopPtr = ps->loopPtr;
					{
						std::lock_guard<std::mutex> lk(ps->mtx);
						if (ps->settled && !ps->rejected) {
							if (ps->loopPtr) {
								auto loop = static_cast<Interpreter*>(ps->loopPtr);
								loop->postTask([ps, cb, nextP, loop]{
									try {
										std::vector<Value> a{ ps->result };
										Value ret{std::monostate{}};
										if (cb->isBuiltin) ret = cb->builtin(a, cb->closure); else {
											auto local = std::make_shared<Environment>(cb->closure);
											if (a.size() != cb->params.size()) { if (!cb->params.empty()) local->define(cb->params[0], ps->result); }
											else { for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]); }
											try { loop->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
										}
										if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
											auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
											{
												std::lock_guard<std::mutex> lk2(inner->mtx);
												inner->loopPtr = loop;
												inner->thenCallbacks.push_back({ loop->makeResolver(), nextP });
												inner->catchCallbacks.push_back({ loop->makeRejecter(), nextP });
											}
											if (inner->settled) loop->dispatchPromiseCallbacks(inner);
										} else {
											loop->settlePromise(nextP, false, ret);
										}
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									}
								});
							}
						} else {
							ps->thenCallbacks.push_back({ cb, nextP });
						}
					}
					return Value{nextP};
				};
				return fn;
			}
			if (name == "catch") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				auto ps = *p;
				fn->builtin = [ps](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("catch expects a function");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto nextP = std::make_shared<PromiseState>();
					nextP->loopPtr = ps->loopPtr;
					{
						std::lock_guard<std::mutex> lk(ps->mtx);
						if (ps->settled && ps->rejected) {
							if (ps->loopPtr) {
								auto loop = static_cast<Interpreter*>(ps->loopPtr);
								loop->postTask([ps, cb, nextP, loop]{
									try {
										std::vector<Value> a{ ps->result };
										Value ret{std::monostate{}};
										if (cb->isBuiltin) ret = cb->builtin(a, cb->closure); else {
											auto local = std::make_shared<Environment>(cb->closure);
											if (a.size() != cb->params.size()) { if (!cb->params.empty()) local->define(cb->params[0], ps->result); }
											else { for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]); }
											try { loop->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
										}
										if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
											auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
											{
												std::lock_guard<std::mutex> lk2(inner->mtx);
												inner->loopPtr = loop;
												inner->thenCallbacks.push_back({ loop->makeResolver(), nextP });
												inner->catchCallbacks.push_back({ loop->makeRejecter(), nextP });
											}
											if (inner->settled) loop->dispatchPromiseCallbacks(inner);
										} else {
											loop->settlePromise(nextP, false, ret);
										}
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									}
								});
							}
						} else {
							ps->catchCallbacks.push_back({ cb, nextP });
						}
					}
					return Value{nextP};
				};
				return fn;
			}
			return Value{std::monostate{}};
		}
		// Class / others: no properties
		return Value{std::monostate{}};
	}
	static Value getIndex(const Value& obj, const Value& key) {
		if (auto parr = std::get_if<std::shared_ptr<Array>>(&obj)) {
			size_t idx = indexFromValue(key);
			auto& vec = **parr;
			if (idx >= vec.size()) throw std::runtime_error("Array index out of range");
			return vec[idx];
		}
		if (auto pins = std::get_if<std::shared_ptr<Instance>>(&obj)) {
			std::string k = keyFromValue(key);
			auto it = (*pins)->fields.find(k);
			if (it != (*pins)->fields.end()) return it->second;
			return Value{std::monostate{}};
		}
		if (auto pobj = std::get_if<std::shared_ptr<Object>>(&obj)) {
			std::string k = keyFromValue(key);
			auto it = (**pobj).find(k);
			if (it == (**pobj).end()) return Value{std::monostate{}};
			return it->second;
		}
		throw std::runtime_error("Index access on non-array/object");
	}
	static size_t indexFromValue(const Value& v) {
		double d = getNumber(v, "array index");
		if (d < 0) throw std::runtime_error("Negative index");
		size_t idx = static_cast<size_t>(d);
		if (static_cast<double>(idx) != d) throw std::runtime_error("Index must be integer");
		return idx;
	}
	static std::string keyFromValue(const Value& v) {
		if (auto s = std::get_if<std::string>(&v)) return *s;
		if (auto n = std::get_if<double>(&v)) { std::ostringstream oss; oss << *n; return oss.str(); }
		if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
		if (std::holds_alternative<std::monostate>(v)) return "null";
		throw std::runtime_error("Unsupported key type");
	}

	// Evaluate to a reference-like concept: here we just ensure object is object and return Value& by storing object evaluated value back? For simplicity, we evaluate then require it's object/array and return a reference to the held shared_ptr so we can mutate its contents.
	Value& ensureObjectRef(const ExprPtr& objExpr) {
		tempStorage = evaluate(objExpr);
		if (!std::holds_alternative<std::shared_ptr<Object>>(tempStorage) && !std::holds_alternative<std::shared_ptr<Instance>>(tempStorage)) throw std::runtime_error("Target is not an object");
		return tempStorage;
	}
	Value& evaluateRef(const ExprPtr& objExpr) {
		tempStorage = evaluate(objExpr);
		if (std::holds_alternative<std::shared_ptr<Array>>(tempStorage) || std::holds_alternative<std::shared_ptr<Object>>(tempStorage) || std::holds_alternative<std::shared_ptr<Instance>>(tempStorage)) return tempStorage;
		throw std::runtime_error("Target is not indexable");
	}

	Value tempStorage; // used to hold temporary during Set* operations

	void installBuiltins() {
		auto printFn = std::make_shared<Function>();
		printFn->isBuiltin = true;
		printFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			for (size_t i=0;i<args.size();++i) {
				std::cout << toString(args[i]);
				if (i+1<args.size()) std::cout << " ";
			}
			std::cout << std::endl;
			return Value{std::monostate{}};
		};
		globals->define("print", printFn);

		// len(x): string/array/object长度
		auto lenFn = std::make_shared<Function>();
		lenFn->isBuiltin = true;
		lenFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("len expects 1 argument");
			const Value& v = args[0];
			if (auto s = std::get_if<std::string>(&v)) return Value{ static_cast<double>(s->size()) };
			if (auto a = std::get_if<std::shared_ptr<Array>>(&v)) return Value{ static_cast<double>((*a) ? (*a)->size() : 0) };
			if (auto o = std::get_if<std::shared_ptr<Object>>(&v)) return Value{ static_cast<double>((*o) ? (*o)->size() : 0) };
			if (std::holds_alternative<std::monostate>(v)) return Value{ 0.0 };
			throw std::runtime_error("len: unsupported type: " + typeOf(v));
		};
		globals->define("len", lenFn);

		// push(arr, ...values): 追加元素，返回新长度
		auto pushFn = std::make_shared<Function>();
		pushFn->isBuiltin = true;
		pushFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.empty()) throw std::runtime_error("push expects at least 1 argument");
			const Value& target = args[0];
			auto parr = std::get_if<std::shared_ptr<Array>>(&target);
			if (!parr || !(*parr)) throw std::runtime_error("push: first argument must be array");
			auto& vec = **parr;
			for (size_t i=1;i<args.size();++i) vec.push_back(args[i]);
			return Value{ static_cast<double>(vec.size()) };
		};
		globals->define("push", pushFn);

		// sleep(ms): 返回一个 Promise，在 ms 毫秒后 resolve(null)
		auto sleepFn = std::make_shared<Function>();
		sleepFn->isBuiltin = true;
		sleepFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("sleep expects 1 argument (ms)");
			double ms = getNumber(args[0], "sleep ms");
			auto p = std::make_shared<PromiseState>();
			p->loopPtr = this; // 指向当前解释器以便派发回调
			std::thread([p, this, ms]{
				std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(ms)));
				settlePromise(p, false, Value{std::monostate{}});
			}).detach();
			return Value{p};
		};
		globals->define("sleep", sleepFn);

		// Promise 对象：resolve / reject
		auto promiseObj = std::make_shared<Object>();
		// Promise.resolve(value)
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				Value v = args.empty() ? Value{std::monostate{}} : args[0];
				settlePromise(p, false, v);
				return Value{p};
			};
			(*promiseObj)["resolve"] = fn;
		}
		// Promise.reject(reason)
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				Value v = args.empty() ? Value{std::string("Promise rejected")} : args[0];
				settlePromise(p, true, v);
				return Value{p};
			};
			(*promiseObj)["reject"] = fn;
		}
		globals->define("Promise", Value{promiseObj});
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

// 宿主类注册：使用内置Function包装，将NativeValue与内部Value互转
static Value nativeToValue(const ALangEngine::NativeValue& nv) {
	switch (nv.index()) {
		case 0: return Value{std::monostate{}};
		case 1: return Value{std::get<double>(nv)};
		case 2: return Value{std::get<std::string>(nv)};
		case 3: return Value{std::get<bool>(nv)};
	}
	return Value{std::monostate{}};
}
static ALangEngine::NativeValue valueToNative(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return ALangEngine::NativeValue{std::monostate{}};
	if (auto d = std::get_if<double>(&v)) return ALangEngine::NativeValue{*d};
	if (auto s = std::get_if<std::string>(&v)) return ALangEngine::NativeValue{*s};
	if (auto b = std::get_if<bool>(&v)) return ALangEngine::NativeValue{*b};
	// 非基元类型一律视作 null
	return ALangEngine::NativeValue{std::monostate{}};
}

void ALangEngine::registerClass(
	const std::string& className,
	NativeFunc constructor,
	const std::unordered_map<std::string, NativeFunc>& methods,
	const std::vector<std::string>& baseClasses
) {
	// 构造ClassInfo
	auto& interp = impl->interpreter;
	// 访问私有类型：此处位于同一翻译单元，直接构建ClassInfo
	auto klass = std::make_shared<ClassInfo>();
	klass->name = className;
	for (auto& bn : baseClasses) {
		try {
			Value bv = interp.globalsEnv()->get(bn);
			if (std::holds_alternative<std::shared_ptr<ClassInfo>>(bv)) {
				klass->supers.push_back(std::get<std::shared_ptr<ClassInfo>>(bv));
			}
		} catch (...) { /* 忽略缺失的基类 */ }
	}

	// 构造器
	if (constructor) {
		auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
		fn->builtin = [constructor](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			std::vector<NativeValue> na; na.reserve(args.size());
			for (auto& a : args) na.push_back(valueToNative(a));
			void* thisHandle = nullptr;
			if (clos) {
				try {
					Value tv = clos->get("this");
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) thisHandle = pins->get();
				} catch (...) {}
			}
			auto ret = constructor(na, thisHandle);
			return nativeToValue(ret);
		};
		klass->methods["constructor"] = fn;
	}
	// 方法
	for (auto& kv : methods) {
		auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
		auto native = kv.second;
		fn->builtin = [native](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			std::vector<NativeValue> na; na.reserve(args.size());
			for (auto& a : args) na.push_back(valueToNative(a));
			void* thisHandle = nullptr;
			if (clos) {
				try {
					Value tv = clos->get("this");
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) thisHandle = pins->get();
				} catch (...) {}
			}
			auto ret = native(na, thisHandle);
			return nativeToValue(ret);
		};
		klass->methods[kv.first] = fn;
	}

	// 注入全局
	impl->interpreter.globalsEnv()->define(className, klass);
}

ALangEngine::NativeValue ALangEngine::callFunction(
	const std::string& functionName,
	const std::vector<NativeValue>& args
) {
	try {
		std::vector<Value> va; va.reserve(args.size());
		for (auto& a : args) va.push_back(nativeToValue(a));
		Value ret = impl->interpreter.callFunction(functionName, va);
		return valueToNative(ret);
	} catch (const std::exception& ex) {
		std::cerr << "[ALang Error] callFunction: " << ex.what() << std::endl;
		throw;
	}
}

void ALangEngine::runEventLoopUntilIdle() {
	impl->interpreter.runEventLoopUntilIdle();
}

