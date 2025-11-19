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
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <utility>
#include <variant>
#include <vector>
#include <queue>
#include <filesystem>
#include <fstream>
#include <optional>
#include <cstring>
#include "AsulFormatString/AsulFormatString.h"

namespace {

// ----------- Lexer -----------

enum class TokenType {
	// Single-char
	LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
	Comma, Semicolon, Colon, Dot,
	Plus, Minus, Star, Slash, Percent,
	Tilde,
	Bang, Equal, Less, Greater, Question,
	// One, two or three char
	BangEqual, StrictNotEqual, EqualEqual, StrictEqual, LessEqual, GreaterEqual, LeftArrow,
	Arrow,
	Ellipsis,
	AndAnd, OrOr,
	// Increment/Decrement and Compound Assignment
	PlusPlus, MinusMinus,
	PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
	// Literals
	Identifier, String, Number,
	// Keywords
	Let, Var, Const, Function, Return, If, Else, While, For, ForEach, In, Break, Continue, Switch, Case, Default, Class, Extends, New, True, False, Null, Await, Async, Go, Try, Catch, Throw, Interface, Import, From,
	EndOfFile
};

struct Token {
	TokenType type;
	std::string lexeme;
	int line;
	int column{1};
	int length{1};
};

class Lexer {
public:
	explicit Lexer(const std::string& src) : source(src) {}
	std::vector<Token> scanTokens() {
		while (!isAtEnd()) {
			start = current;
			scanToken();
		}
		int col = static_cast<int>((current >= lineStart) ? (current - lineStart + 1) : 1);
		tokens.push_back(Token{TokenType::EndOfFile, "", line, col, 0});
		return tokens;
	}

private:
	const std::string& source;
	std::vector<Token> tokens;
	size_t start{0};
	size_t current{0};
	int line{1};
	size_t lineStart{0};

	bool isAtEnd() const { return current >= source.size(); }
	char advance() { return source[current++]; }
	char peek() const { return isAtEnd() ? '\0' : source[current]; }
	char peekNext() const { return (current + 1 >= source.size()) ? '\0' : source[current + 1]; }
	bool match(char expected) {
		if (isAtEnd() || source[current] != expected) return false;
		current++;
		return true;
	}
	void add(TokenType type) {
		int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
		int len = static_cast<int>(current - start);
		tokens.push_back(Token{type, source.substr(start, current - start), line, col, len});
	}

	void string() {
int startLine = line;  // Save the starting line
size_t startLineStart = lineStart;  // Save starting line position
		while (!isAtEnd() && peek() != '"') {
			if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
			advance();
		}
		if (isAtEnd()) {
			int col = static_cast<int>((start >= startLineStart) ? (start - startLineStart + 1) : 1);
			int len = static_cast<int>(current - start);
			// construct line text and caret
			size_t ls = startLineStart;
			size_t le = ls;
			while (le < source.size() && source[le] != '\n' && source[le] != '\r') le++;
			std::string lineStr = source.substr(ls, le - ls);
			std::ostringstream oss;
			oss << "Unterminated string at line " << startLine << ", column " << col << ", length " << len << "\n";
			oss << lineStr << "\n" << std::string(col > 1 ? col - 1 : 0, ' ') << std::string(std::max(1, len), '^');
			throw std::runtime_error(oss.str());
		}
		advance(); // closing quote
		std::string raw = source.substr(start + 1, current - start - 2);
		// Unescape common sequences: \n, \t, \r, \\, \", \' and \0
		auto unescape = [](const std::string& in)->std::string{
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
					default: out.push_back(n); break; // unknown escapes: keep the char
					}
				} else {
					out.push_back(c);
				}
			}
			return out;
		};
		std::string value = unescape(raw);
		int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
		int len = static_cast<int>(current - start); // include quotes
		tokens.push_back(Token{TokenType::String, value, line, col, len});
	}

	void number() {
		while (std::isdigit(peek())) advance();
		if (peek() == '.' && std::isdigit(peekNext())) {
			advance();
			while (std::isdigit(peek())) advance();
		}
		int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
		int len = static_cast<int>(current - start);
		tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line, col, len});
	}

	void identifier() {
		while (std::isalnum(peek()) || peek() == '_') advance();
		std::string text = source.substr(start, current - start);
		static const std::unordered_map<std::string, TokenType> keywords{
			{"let", TokenType::Let}, {"var", TokenType::Var}, {"const", TokenType::Const},
			{"function", TokenType::Function}, {"return", TokenType::Return},
			{"if", TokenType::If}, {"else", TokenType::Else}, {"while", TokenType::While},
			{"for", TokenType::For}, {"foreach", TokenType::ForEach}, {"in", TokenType::In}, {"break", TokenType::Break}, {"continue", TokenType::Continue},
			{"switch", TokenType::Switch}, {"case", TokenType::Case}, {"default", TokenType::Default},
			{"class", TokenType::Class}, {"extends", TokenType::Extends}, {"new", TokenType::New},
			{"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
			{"await", TokenType::Await},
			{"async", TokenType::Async},
			{"go", TokenType::Go},
			{"try", TokenType::Try}, {"catch", TokenType::Catch}, {"throw", TokenType::Throw},
			{"interface", TokenType::Interface},
			{"import", TokenType::Import}, {"from", TokenType::From},
		};
		auto it = keywords.find(text);
		int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
		int len = static_cast<int>(current - start);
		if (it != keywords.end()) tokens.push_back(Token{it->second, text, line, col, len});
		else tokens.push_back(Token{TokenType::Identifier, text, line, col, len});
	}

	void skipWhitespaceAndComments() {
		for (;;) {
			char c = peek();
			switch (c) {
			case ' ': case '\r': case '\t': advance(); break;
			case '\n': line++; advance(); lineStart = current; break;
			case '"':
				// Support pure triple-double-quote block comments: """ ... """
				if (current + 2 < source.size() && peekNext() == '"' && source[current+2] == '"') {
					// consume three quotes
					advance(); advance(); advance();
					while (!isAtEnd() && !(peek() == '"' && peekNext() == '"' && (current + 2 < source.size() && source[current+2] == '"'))) {
						if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); advance(); }
				} else return;
				break;
			case '\'':
				// Support pure triple-single-quote block comments: ''' ... '''
				if (current + 2 < source.size() && peekNext() == '\'' && source[current+2] == '\'') {
					// consume three single quotes
					advance(); advance(); advance();
					while (!isAtEnd() && !(peek() == '\'' && peekNext() == '\'' && (current + 2 < source.size() && source[current+2] == '\''))) {
						if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); advance(); }
				} else return;
				break;
			case '/':
				if (peekNext() == '/') {
					while (!isAtEnd() && peek() != '\n') advance();
				} else if (peekNext() == '*') {
					advance(); advance();
					while (!isAtEnd() && !(peek() == '*' && peekNext() == '/')) {
						if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); }
				} else return;
				break;
			case '#':
				// Support Python-style single-line comments starting with '#'
				// and block comments that start with #"""...""" or #'''...'''
				if (current + 3 < source.size() && source[current+1] == '"' && source[current+2] == '"' && source[current+3] == '"') {
					// consume '#' and opening triple quotes
					advance(); advance(); advance(); advance();
					// scan until closing triple double-quotes
					while (!isAtEnd() && !(peek() == '"' && peekNext() == '"' && (current + 2 < source.size() && source[current+2] == '"'))) {
						if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); advance(); }
				} else if (current + 3 < source.size() && source[current+1] == '\'' && source[current+2] == '\'' && source[current+3] == '\'') {
					// consume '#' and opening triple single-quotes
					advance(); advance(); advance(); advance();
					// scan until closing triple single-quotes
					while (!isAtEnd() && !(peek() == '\'' && peekNext() == '\'' && (current + 2 < source.size() && source[current+2] == '\''))) {
						if (peek() == '\n') { line++; advance(); lineStart = current; continue; }
						advance();
					}
					if (!isAtEnd()) { advance(); advance(); advance(); }
				} else {
					// single-line '#'-style comment
					advance();
					while (!isAtEnd() && peek() != '\n') advance();
				}
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
		case '~': add(TokenType::Tilde); break;
		case '(': add(TokenType::LeftParen); break;
		case ')': add(TokenType::RightParen); break;
		case '{': add(TokenType::LeftBrace); break;
		case '}': add(TokenType::RightBrace); break;
		case '[': add(TokenType::LeftBracket); break;
		case ']': add(TokenType::RightBracket); break;
		case ',': add(TokenType::Comma); break;
		case ';': add(TokenType::Semicolon); break;
		case ':': add(TokenType::Colon); break;
		case '?': add(TokenType::Question); break;
		case '.':
			// support spread '...'
			if (match('.') && match('.')) add(TokenType::Ellipsis);
			else add(TokenType::Dot);
			break;
		case '+':
			if (match('+')) add(TokenType::PlusPlus);
			else if (match('=')) add(TokenType::PlusEqual);
			else add(TokenType::Plus);
			break;
		case '-':
			if (match('>')) add(TokenType::Arrow);
			else if (match('-')) add(TokenType::MinusMinus);
			else if (match('=')) add(TokenType::MinusEqual);
			else add(TokenType::Minus);
			break;
		case '*':
			if (match('=')) add(TokenType::StarEqual);
			else add(TokenType::Star);
			break;
		case '%':
			if (match('=')) add(TokenType::PercentEqual);
			else add(TokenType::Percent);
			break;
		case '!': {
			if (match('=')) {
				if (match('=')) add(TokenType::StrictNotEqual);
				else add(TokenType::BangEqual);
			} else add(TokenType::Bang);
			break;
		}
		case '=': {
			if (match('=')) {
				if (match('=')) add(TokenType::StrictEqual);
				else add(TokenType::EqualEqual);
			} else add(TokenType::Equal);
			break;
		}
		case '<': {
			if (match('-')) { add(TokenType::LeftArrow); }
			else if (match('=')) { add(TokenType::LessEqual); }
			else { add(TokenType::Less); }
			break;
		}
		case '>': add(match('=') ? TokenType::GreaterEqual : TokenType::Greater); break;
		case '&': if (match('&')) add(TokenType::AndAnd); else {
			// 详细错误：包含字符、行列与上下文
			size_t pos = current - 1;
			size_t ls = lineStart;
			size_t le = pos;
			while (le < source.size() && source[le] != '\n' && source[le] != '\r') le++;
			std::string lineStr = source.substr(ls, le - ls);
			size_t col = (pos >= ls ? (pos - ls + 1) : 1);
			std::ostringstream caret; caret << std::string(col > 1 ? col - 1 : 0, ' ') << '^';
			std::ostringstream ch; ch << '\'' << c << "' (U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
				<< static_cast<int>(static_cast<unsigned char>(c)) << ")";
			std::ostringstream oss;
			oss << "Unexpected character " << ch.str() << " at line " << line << ", column " << col << "\n"
				<< lineStr << "\n" << caret.str();
			throw std::runtime_error(oss.str());
		} break;
		case '|': if (match('|')) add(TokenType::OrOr); else {
			size_t pos = current - 1;
			size_t ls = lineStart;
			size_t le = pos;
			while (le < source.size() && source[le] != '\n' && source[le] != '\r') le++;
			std::string lineStr = source.substr(ls, le - ls);
			size_t col = (pos >= ls ? (pos - ls + 1) : 1);
			std::ostringstream caret; caret << std::string(col > 1 ? col - 1 : 0, ' ') << '^';
			std::ostringstream ch; ch << '\'' << c << "' (U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
				<< static_cast<int>(static_cast<unsigned char>(c)) << ")";
			std::ostringstream oss;
			oss << "Unexpected character " << ch.str() << " at line " << line << ", column " << col << "\n"
				<< lineStr << "\n" << caret.str();
			throw std::runtime_error(oss.str());
		} break;
		case '/':
			if (match('=')) add(TokenType::SlashEqual);
			else add(TokenType::Slash);
			break;
		case '"': string(); break;
		default:
			if (std::isdigit(c)) { while (std::isdigit(peek()) || (peek()=='.' && std::isdigit(peekNext()))) advance(); int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1); int len = static_cast<int>(current - start); tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line, col, len}); }
			else if (std::isalpha(c) || c == '_') identifier();
			else {
				size_t pos = current - 1;
				size_t ls = lineStart;
				size_t le = pos;
				while (le < source.size() && source[le] != '\n' && source[le] != '\r') le++;
				std::string lineStr = source.substr(ls, le - ls);
				size_t col = (pos >= ls ? (pos - ls + 1) : 1);
				std::ostringstream caret; caret << std::string(col > 1 ? col - 1 : 0, ' ') << '^';
				std::ostringstream ch; ch << '\'' << c << "' (U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
					<< static_cast<int>(static_cast<unsigned char>(c)) << ")";
				std::ostringstream oss;
				oss << "Unexpected character " << ch.str() << " at line " << line << ", column " << col << "\n"
					<< lineStr << "\n" << caret.str();
				throw std::runtime_error(oss.str());
			}
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
	// declared types for variables (optional): maps variable name -> declared type name
	std::unordered_map<std::string, std::string> declaredTypes;

	explicit Environment(std::shared_ptr<Environment> p = nullptr) : parent(std::move(p)) {}

	void define(const std::string& name, const Value& val) { values[name] = val; }
	// define with optional declared type
	void defineWithType(const std::string& name, const Value& val, const std::optional<std::string>& typeName) {
		values[name] = val;
		if (typeName && !typeName->empty()) declaredTypes[name] = *typeName;
	}
	std::optional<std::string> getDeclaredType(const std::string& name) {
		auto it = declaredTypes.find(name);
		if (it != declaredTypes.end()) return it->second;
		if (parent) return parent->getDeclaredType(name);
		return std::nullopt;
	}
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
	int restParamIndex{-1}; // -1 表示没有 rest 参数，否则表示 rest 参数的索引
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
struct VariableExpr : Expr { std::string name; int line{0}; int column{1}; int length{1}; VariableExpr(std::string n, int l, int c, int len): name(std::move(n)), line(l), column(c), length(len){} };
struct AssignExpr : Expr { std::string name; ExprPtr value; int line{0}; AssignExpr(std::string n, ExprPtr v, int l): name(std::move(n)), value(std::move(v)), line(l){} };
struct UnaryExpr : Expr { Token op; ExprPtr right; UnaryExpr(Token o, ExprPtr r): op(std::move(o)), right(std::move(r)){} };
struct UpdateExpr : Expr { Token op; ExprPtr operand; bool isPrefix; int line{0}, column{1}, length{1}; UpdateExpr(Token o, ExprPtr e, bool pre, int l, int c, int len): op(std::move(o)), operand(std::move(e)), isPrefix(pre), line(l), column(c), length(len){} };
struct BinaryExpr : Expr { ExprPtr left; Token op; ExprPtr right; BinaryExpr(ExprPtr l, Token o, ExprPtr r): left(std::move(l)), op(std::move(o)), right(std::move(r)){} };
struct LogicalExpr : Expr { ExprPtr left; Token op; ExprPtr right; LogicalExpr(ExprPtr l, Token o, ExprPtr r): left(std::move(l)), op(std::move(o)), right(std::move(r)){} };
struct ConditionalExpr : Expr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; int line{0}, column{1}, length{1}; ConditionalExpr(ExprPtr c, ExprPtr t, ExprPtr e, int l, int col, int len): condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)), line(l), column(col), length(len){} };
struct CallExpr : Expr { ExprPtr callee; std::vector<ExprPtr> args; int line{0}, column{1}, length{1}; CallExpr(ExprPtr c, std::vector<ExprPtr> a, int l, int c0, int len): callee(std::move(c)), args(std::move(a)), line(l), column(c0), length(len){} };
struct NewExpr : Expr { ExprPtr callee; std::vector<ExprPtr> args; int line{0}, column{1}, length{1}; NewExpr(ExprPtr c, std::vector<ExprPtr> a, int l, int c0, int len): callee(std::move(c)), args(std::move(a)), line(l), column(c0), length(len){} };
struct GetPropExpr : Expr { ExprPtr object; std::string name; int line{0}, column{1}, length{1}; GetPropExpr(ExprPtr o, std::string n, int l, int c0, int len): object(std::move(o)), name(std::move(n)), line(l), column(c0), length(len){} };
struct IndexExpr : Expr { ExprPtr object; ExprPtr index; int line{0}, column{1}, length{1}; IndexExpr(ExprPtr o, ExprPtr i, int l, int c0, int len): object(std::move(o)), index(std::move(i)), line(l), column(c0), length(len){} };
struct SetPropExpr : Expr { ExprPtr object; std::string name; ExprPtr value; int line{0}, column{1}, length{1}; SetPropExpr(ExprPtr o, std::string n, ExprPtr v, int l, int c0, int len): object(std::move(o)), name(std::move(n)), value(std::move(v)), line(l), column(c0), length(len){} };
struct SetIndexExpr : Expr { ExprPtr object; ExprPtr index; ExprPtr value; int line{0}, column{1}, length{1}; SetIndexExpr(ExprPtr o, ExprPtr i, ExprPtr v, int l, int c0, int len): object(std::move(o)), index(std::move(i)), value(std::move(v)), line(l), column(c0), length(len){} };
struct ArrayLiteralExpr : Expr { std::vector<ExprPtr> elements; explicit ArrayLiteralExpr(std::vector<ExprPtr> e): elements(std::move(e)){} };
struct ObjectLiteralExpr : Expr {
	struct Prop { bool computed; bool isSpread{false}; std::string name; ExprPtr keyExpr; ExprPtr value; };
	std::vector<Prop> props;
	explicit ObjectLiteralExpr(std::vector<Prop> p): props(std::move(p)){}
};
struct SpreadExpr : Expr { ExprPtr expr; explicit SpreadExpr(ExprPtr e): expr(std::move(e)){} };
struct AwaitExpr : Expr { ExprPtr expr; int line{0}, column{1}, length{1}; explicit AwaitExpr(ExprPtr e, int l=0, int c0=1, int len=1): expr(std::move(e)), line(l), column(c0), length(len){} };
struct Param { 
	std::string name; 
	std::optional<std::string> type; 
	bool isRest{false}; 
	Param(std::string n, std::optional<std::string> t = std::nullopt, bool rest = false): name(std::move(n)), type(std::move(t)), isRest(rest) {} 
};

struct FunctionExpr : Expr { std::vector<Param> params; StmtPtr body; explicit FunctionExpr(std::vector<Param> p, StmtPtr b): params(std::move(p)), body(std::move(b)){} };

struct Stmt { virtual ~Stmt() = default; };
struct ExprStmt : Stmt { ExprPtr expr; explicit ExprStmt(ExprPtr e): expr(std::move(e)){} };
struct VarDecl : Stmt { std::string name; std::optional<std::string> type; ExprPtr typeExpr; ExprPtr init; VarDecl(std::string n, std::optional<std::string> t, ExprPtr te, ExprPtr i): name(std::move(n)), type(std::move(t)), typeExpr(std::move(te)), init(std::move(i)){} };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; explicit BlockStmt(std::vector<StmtPtr> s): statements(std::move(s)){} };
struct IfStmt : Stmt { ExprPtr cond; StmtPtr thenB; StmtPtr elseB; IfStmt(ExprPtr c, StmtPtr t, StmtPtr e): cond(std::move(c)), thenB(std::move(t)), elseB(std::move(e)){} };
struct WhileStmt : Stmt { ExprPtr cond; StmtPtr body; WhileStmt(ExprPtr c, StmtPtr b): cond(std::move(c)), body(std::move(b)){} };
struct ReturnStmt : Stmt { Token keyword; ExprPtr value; ReturnStmt(Token k, ExprPtr v): keyword(std::move(k)), value(std::move(v)){} };
struct FunctionStmt : Stmt { std::string name; std::vector<Param> params; StmtPtr body; bool isAsync{false}; std::optional<std::string> returnType; FunctionStmt(std::string n, std::vector<Param> p, StmtPtr b, bool a=false, std::optional<std::string> r = std::nullopt): name(std::move(n)), params(std::move(p)), body(std::move(b)), isAsync(a), returnType(std::move(r)){} };
struct ClassStmt : Stmt { std::string name; std::vector<std::string> superNames; std::vector<std::shared_ptr<FunctionStmt>> methods; };
struct ExtendStmt : Stmt { std::string name; std::vector<std::shared_ptr<FunctionStmt>> methods; };
struct InterfaceStmt : Stmt { std::string name; std::vector<std::string> methodNames; };
struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};
struct ForStmt : Stmt { StmtPtr init; ExprPtr cond; ExprPtr post; StmtPtr body; ForStmt(StmtPtr i, ExprPtr c, ExprPtr p, StmtPtr b): init(std::move(i)), cond(std::move(c)), post(std::move(p)), body(std::move(b)){} };
struct ForEachStmt : Stmt { std::string varName; ExprPtr iterable; StmtPtr body; ForEachStmt(std::string v, ExprPtr i, StmtPtr b): varName(std::move(v)), iterable(std::move(i)), body(std::move(b)){} };
struct SwitchStmt : Stmt {
	struct CaseClause {
		ExprPtr value; // null for default case
		std::vector<StmtPtr> body;
	};
	ExprPtr expr;
	std::vector<CaseClause> cases;
	SwitchStmt(ExprPtr e, std::vector<CaseClause> c): expr(std::move(e)), cases(std::move(c)){}
};
struct GoStmt : Stmt { ExprPtr call; explicit GoStmt(ExprPtr c): call(std::move(c)){} };
struct ThrowStmt : Stmt { ExprPtr value; explicit ThrowStmt(ExprPtr v): value(std::move(v)){} };
struct TryCatchStmt : Stmt { StmtPtr tryBlock; std::string catchName; StmtPtr catchBlock; TryCatchStmt(StmtPtr t, std::string n, StmtPtr c): tryBlock(std::move(t)), catchName(std::move(n)), catchBlock(std::move(c)){} };
struct EmptyStmt : Stmt {};
struct ImportStmt : Stmt {
	struct Entry {
		// Package import (existing behavior): symbol == "*" means wildcard
		std::string packageName;
		std::string symbol;
		// File import: when isFile == true, use filePath and ignore packageName/symbol
		bool isFile{false};
		std::string filePath; // may be relative or absolute; .alang suffix may be omitted
		int line{0};
		int column{1};
		int length{1};
	};
	std::vector<Entry> entries;
};

// ----------- Parser -----------

class Parser {
public:
	explicit Parser(const std::vector<Token>& t, const std::string& src): tokens(t), source(src) {}
	std::vector<StmtPtr> parse() {
		std::vector<StmtPtr> stmts;
		while (!isAtEnd()) stmts.push_back(declaration());
		return stmts;
	}

private:
	const std::vector<Token>& tokens;
	size_t current{0};
	const std::string& source;

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
		const Token& tok = peek();
		std::ostringstream oss;
		oss << "[Parse] " << message << " at line " << tok.line << ", column " << tok.column << "\n";
		oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
		throw std::runtime_error(oss.str());
	}

	std::string getLineText(int line) const {
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

	StmtPtr declaration() {
		if (match({TokenType::Async})) { consume(TokenType::Function, "Expect 'function' after 'async'"); return functionDecl(true); }
		if (match({TokenType::Function})) return functionDecl(false);
		if (match({TokenType::Class})) return classDeclaration();
		if (match({TokenType::Extends})) return extendsDeclaration();
		if (match({TokenType::Interface})) return interfaceDeclaration();
		if (match({TokenType::Import})) return importDeclaration(false);
		if (match({TokenType::From})) return importDeclaration(true);
		if (match({TokenType::Let, TokenType::Var, TokenType::Const})) return varDeclaration();
		return statement();
	}

	StmtPtr importDeclaration(bool isFrom) {
		auto imp = std::make_shared<ImportStmt>();
		if (isFrom) {
			// from Package import name | from Package import (name1 name2 ...)
			auto pkgTok = consume(TokenType::Identifier, "Expect package name after 'from'");
			auto pkg = pkgTok.lexeme;
			consume(TokenType::Import, "Expect 'import' after package name");
			if (match({TokenType::LeftParen})) {
				while (!check(TokenType::RightParen) && !isAtEnd()) {
					auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
					ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length; imp->entries.push_back(e);
					(void)match({TokenType::Comma});
				}
				consume(TokenType::RightParen, "Expect ')' after import list");
			} else {
				auto nameTok = consume(TokenType::Identifier, "Expect symbol name");
				ImportStmt::Entry e; e.packageName = pkg; e.symbol = nameTok.lexeme; e.isFile = false; e.line = nameTok.line; e.column = nameTok.column; e.length = nameTok.length; imp->entries.push_back(e);
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
					auto pkgTok = consume(TokenType::Identifier, "Expect package name");
					auto pkg = pkgTok.lexeme;
					consume(TokenType::Dot, "Expect '.' after package name");
					auto symTok = consume(TokenType::Identifier, "Expect symbol name");
					ImportStmt::Entry e; e.packageName = pkg; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length; imp->entries.push_back(e);
				}
				(void)match({TokenType::Comma});
			}
			consume(TokenType::RightParen, "Expect ')' after import list");
			consume(TokenType::Semicolon, "Expect ';' after import statement");
			return imp;
		}
		// Support: import "file";  OR keep existing package import forms
		if (match({TokenType::String})) {
			Token t = previous();
			ImportStmt::Entry e; e.isFile = true; e.filePath = t.lexeme; e.line = t.line; e.column = t.column; e.length = t.length; imp->entries.push_back(e);
			consume(TokenType::Semicolon, "Expect ';' after import statement");
			return imp;
		}
		auto pkgTok = consume(TokenType::Identifier, "Expect package name");
		auto pkg = pkgTok.lexeme;
		consume(TokenType::Dot, "Expect '.' after package name");
		if (match({TokenType::Star})) {
			Token starTok = previous();
			ImportStmt::Entry e; e.packageName = pkg; e.symbol = std::string("*"); e.isFile = false; e.line = starTok.line; e.column = starTok.column; e.length = std::max(1, starTok.length); imp->entries.push_back(e);
			consume(TokenType::Semicolon, "Expect ';' after import statement");
			return imp;
		}
		consume(TokenType::LeftParen, "Expect '(' after package '.' for symbol list");
		while (!check(TokenType::RightParen) && !isAtEnd()) {
			auto symTok = consume(TokenType::Identifier, "Expect symbol name");
			ImportStmt::Entry e; e.packageName = pkg; e.symbol = symTok.lexeme; e.isFile = false; e.line = symTok.line; e.column = symTok.column; e.length = symTok.length; imp->entries.push_back(e);
			(void)match({TokenType::Comma});
		}
		consume(TokenType::RightParen, "Expect ')' after symbol list");
		consume(TokenType::Semicolon, "Expect ';' after import statement");
		return imp;
	}
	StmtPtr interfaceDeclaration() {
		// 语法：interface Name ; | interface Name { function sig(...); ... }
		auto nameTok = consume(TokenType::Identifier, "Expect interface name");
		auto st = std::make_shared<InterfaceStmt>(); st->name = nameTok.lexeme;
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
			consume(TokenType::Semicolon, "Expect ';' after interface method signature");
			st->methodNames.push_back(mname);
		}
		consume(TokenType::RightBrace, "Expect '}' after interface body");
		// 允许可选分号：`interface Name { ... };`
		(void)match({TokenType::Semicolon});
		return st;
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
				cls->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, retType));
			}
			consume(TokenType::RightBrace, "Expect '}' after class body");
			// 可选分号：class Name { ... };
			(void)match({TokenType::Semicolon});
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

	StmtPtr functionDecl(bool isAsync) {
		auto name = consume(TokenType::Identifier, "Expect function name").lexeme;
		consume(TokenType::LeftParen, "Expect '('");
		std::vector<Param> params;
		bool hasRest = false;
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
				params.emplace_back(pname, ptype, isRest);
				
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
		return std::make_shared<FunctionStmt>(name, params, body, isAsync, retType);
	}

	StmtPtr varDeclaration() {
		auto name = consume(TokenType::Identifier, "Expect variable name").lexeme;
		std::optional<std::string> type = std::nullopt;
		ExprPtr typeExpr = nullptr;
		if (match({TokenType::Colon})) {
			// allow a non-assignment expression (e.g. TYPE(...)) as the type annotation
			typeExpr = logicalOr();
		}
		ExprPtr init;
		if (match({TokenType::Equal})) init = expression();
		consume(TokenType::Semicolon, "Expect ';' after variable declaration");
		return std::make_shared<VarDecl>(name, type, typeExpr, init);
	}

	StmtPtr statement() {
		if (match({TokenType::If})) return ifStatement();
		if (match({TokenType::While})) return whileStatement();
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
			return std::make_shared<TryCatchStmt>(tryB, name, catchB);
		}
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

	StmtPtr forEachStatement() {
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

	StmtPtr switchStatement() {
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
		auto expr = conditional();
		
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

	ExprPtr conditional() {
		// 三元运算符：condition ? thenExpr : elseExpr
		auto expr = logicalOr();
		
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
		while (match({TokenType::BangEqual, TokenType::EqualEqual, TokenType::StrictEqual, TokenType::StrictNotEqual})) {
			Token op = previous();
			auto right = comparison();
			expr = std::make_shared<BinaryExpr>(expr, op, right);
		}
		return expr;
	}

	ExprPtr comparison() {
		auto expr = term();
		while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual, TokenType::Tilde})) {
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
		// 前置递增/递减：++x, --x
		if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
			Token op = previous();
			auto operand = unary();
			return std::make_shared<UpdateExpr>(op, operand, true, op.line, op.column, std::max(1, op.length));
		}
		if (match({TokenType::Bang, TokenType::Minus})) {
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

	ExprPtr postfix() {
		auto expr = call();
		// 后置递增/递减：x++, x--
		if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
			Token op = previous();
			return std::make_shared<UpdateExpr>(op, expr, false, op.line, op.column, std::max(1, op.length));
		}
		return expr;
	}

	ExprPtr finishCall(ExprPtr callee) {
		std::vector<ExprPtr> args;
		if (!check(TokenType::RightParen)) {
			do { args.push_back(expression()); } while (match({TokenType::Comma}));
		}
		Token rp = consume(TokenType::RightParen, "Expect ')' after arguments");
		return std::make_shared<CallExpr>(callee, args, rp.line, rp.column, std::max(1, rp.length));
	}

	ExprPtr call() {
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

	ExprPtr primary() {
		// 支持匿名函数：[](x, y){ ... }
		if (check(TokenType::LeftBracket)) {
			// 仅当模式为 [] ( 开始时，识别为 lambda；否则按数组字面量
			if (current + 2 < tokens.size() && tokens[current].type == TokenType::LeftBracket && tokens[current+1].type == TokenType::RightBracket && tokens[current+2].type == TokenType::LeftParen) {
				advance(); // [
				advance(); // ]
				advance(); // (
				std::vector<Param> params;
				bool hasRest = false;
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
						params.emplace_back(pname, ptype, isRest);
						
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
			auto nameTok = consume(TokenType::Identifier, "Expect class name after 'new'");
			consume(TokenType::LeftParen, "Expect '('");
			std::vector<ExprPtr> args;
			if (!check(TokenType::RightParen)) { do { args.push_back(expression()); } while (match({TokenType::Comma})); }
			consume(TokenType::RightParen, "Expect ')'");
			return std::make_shared<NewExpr>(std::make_shared<VariableExpr>(nameTok.lexeme, nameTok.line, nameTok.column, nameTok.length), args, newTok.line, newTok.column, std::max(1, newTok.length));
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
						auto inner = expression();
						elems.push_back(std::make_shared<SpreadExpr>(inner));
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
						p.isSpread = true;
						p.value = expression();
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
	ExprPtr parseInterpolatedString(const std::string& s, int line, int column, int length) {
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

	ExprPtr parseExprSnippet(const std::string& code, int line, int column, int length) {
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
};

// ----------- Interpreter -----------

struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};
struct ExceptionSignal { Value value; };

class Interpreter {
public:
	Interpreter() { globals = std::make_shared<Environment>(); env = globals; installBuiltins(); }

	void setImportBaseDir(const std::string& base) {
		try { importBaseDir = std::filesystem::path(base); }
		catch (...) { importBaseDir.clear(); }
	}

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

	// Import external file: resolve path, read, parse and execute in isolated env, then merge symbols
	void importFilePath(const std::string& rawPath) {
		// capture context for error pretty-printing + import chain
		std::string ctxCode; std::string ctxFile;
		try {
			namespace fs = std::filesystem;
			fs::path p(rawPath);
			// Try both as-is and with .alang suffix if no extension
			auto resolveCandidate = [&](const fs::path& cand, fs::path& out)->bool{
				std::error_code ec{};
				fs::path base = importBaseDir.empty() ? fs::current_path(ec) : importBaseDir;
				fs::path abs = cand.is_absolute() ? cand : (base / cand);
				if (!ec && fs::exists(abs)) { out = fs::weakly_canonical(abs, ec); return true; }
				return false;
			};
			fs::path finalPath;
			bool found = false;
			if (p.has_extension()) {
				found = resolveCandidate(p, finalPath);
			} else {
				// try without extension first, then add .alang
				found = resolveCandidate(p, finalPath);
				if (!found) {
					fs::path withExt = p.string() + ".alang";
					found = resolveCandidate(withExt, finalPath);
				}
			}
			if (!found) {
				throw std::runtime_error(std::string("Import file not found: ") + rawPath);
			}
			std::string key = finalPath.string();
			ctxFile = key;
			if (importedFiles.find(key) != importedFiles.end()) return; // already imported

			// Read file content
			std::ifstream in(key, std::ios::in | std::ios::binary);
			if (!in) throw std::runtime_error(std::string("Cannot open import file: ") + key);
			std::ostringstream ss; ss << in.rdbuf();
			std::string code = ss.str();
			ctxCode = code;

			// push import chain
			importStack.push_back(key);
			struct ImportGuard { std::vector<std::string>& st; ~ImportGuard(){ st.pop_back(); } } guard{importStack};

			// Lex/parse
			Lexer lx(code);
			auto tokens = lx.scanTokens();
			Parser ps(tokens, code);
			auto stmts = ps.parse();

			// Execute in an isolated environment that can see globals (builtins/classes)
			auto fileEnv = std::make_shared<Environment>(globals);
			// Run
			executeBlock(stmts, fileEnv);

			// Merge symbols into current env (similar to wildcard import)
			for (const auto& kv : fileEnv->values) {
				// Bring into the current environment
				env->define(kv.first, kv.second);
			}

			// Mark imported
			importedFiles.insert(key);
		} catch (const ExceptionSignal& ex) {
			// Record error source for upper-level pretty printing
			lastErrorSource = ctxCode; lastErrorFilename = ctxFile;
			// attach import chain
			std::ostringstream oss; oss << toString(ex.value);
			if (!importStack.empty()) {
				oss << " | import chain: ";
				for (size_t i=0;i<importStack.size();++i) { if (i) oss << " -> "; oss << importStack[i]; }
			}
			throw std::runtime_error(oss.str());
		} catch (const std::exception& ex) {
			lastErrorSource = ctxCode; lastErrorFilename = ctxFile;
			std::ostringstream oss; oss << ex.what();
			if (!importStack.empty()) {
				oss << " | import chain: ";
				for (size_t i=0;i<importStack.size();++i) { if (i) oss << " -> "; oss << importStack[i]; }
			}
			throw std::runtime_error(oss.str());
		}
	}

	Value evaluate(const ExprPtr& expr) {
		if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) return lit->value;
			if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
				try {
					return env->get(var->name);
				} catch (const std::exception& ex) {
					std::ostringstream oss;
					oss << ex.what() << " at line " << var->line << ", column " << var->column << ", length " << var->length;
					throw std::runtime_error(oss.str());
				}
			}
			if (auto asg = std::dynamic_pointer_cast<AssignExpr>(expr)) {
				Value v = evaluate(asg->value);
				if (!env->assign(asg->name, v)) throw std::runtime_error("Undefined variable '" + asg->name + "' at line " + std::to_string(asg->line));
				return v;
			}
		if (auto arr = std::dynamic_pointer_cast<ArrayLiteralExpr>(expr)) {
			auto av = std::make_shared<Array>();
			av->reserve(arr->elements.size());
			for (auto& e : arr->elements) {
				if (auto sp = std::dynamic_pointer_cast<SpreadExpr>(e)) {
					Value v = evaluate(sp->expr);
					if (auto a = std::get_if<std::shared_ptr<Array>>(&v)) {
						if (!*a) continue;
						for (auto &it : **a) av->push_back(it);
					} else {
						std::ostringstream oss; oss << "Spread element is not an array"; throw std::runtime_error(oss.str());
					}
				} else {
					av->push_back(evaluate(e));
				}
			}
			return Value{std::shared_ptr<Array>(av)};
		}
		if (auto obj = std::dynamic_pointer_cast<ObjectLiteralExpr>(expr)) {
			auto ov = std::make_shared<Object>();
			for (auto& pr : obj->props) {
				if (pr.isSpread) {
					Value v = evaluate(pr.value);
					if (auto o = std::get_if<std::shared_ptr<Object>>(&v)) {
						if (!*o) continue;
						for (auto &kv : **o) {
							(*ov)[kv.first] = kv.second;
						}
					} else {
						std::ostringstream oss; oss << "Spread value is not an object"; throw std::runtime_error(oss.str());
					}
				} else {
					std::string key;
					if (pr.computed) {
						Value kv = evaluate(pr.keyExpr);
						key = keyFromValue(kv);
					} else {
						key = pr.name;
					}
					(*ov)[key] = evaluate(pr.value);
				}
			}
			return Value{std::shared_ptr<Object>(ov)};
		}
		if (auto gp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			// Special-case property access on a VariableExpr to provide reflection helpers
			if (auto ve = std::dynamic_pointer_cast<VariableExpr>(gp->object)) {
				if (gp->name == "type") {
					auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
					std::string vname = ve->name;
					fn->builtin = [vname](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						try {
							auto dt = clos->getDeclaredType(vname);
							if (dt) return Value{ *dt };
							// try runtime value
							try { Value rv = clos->get(vname); return Value{ typeOf(rv) }; } catch(...) { return Value{ std::string("undefined") }; }
						} catch(...) { return Value{ std::string("undefined") }; }
					};
					return fn;
				}
				if (gp->name == "literalName") {
					auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
					std::string vname = ve->name;
					fn->builtin = [vname](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ vname }; };
					return fn;
				}
			}
			Value o = evaluate(gp->object);
			return getProperty(o, gp->name);
		}
		if (auto ix = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			Value o = evaluate(ix->object);
			Value k = evaluate(ix->index);
			try { return getIndex(o, k); }
			catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << ix->line << ", column " << ix->column << ", length " << ix->length; throw std::runtime_error(oss.str());
			}
		}
		if (auto sp = std::dynamic_pointer_cast<SetPropExpr>(expr)) {
			Value& ov = *[&]()->Value*{
				try { return &ensureObjectRef(sp->object); }
				catch (const std::exception& ex) {
					std::ostringstream oss; oss << ex.what() << " at line " << sp->line << ", column " << sp->column << ", length " << sp->length; throw std::runtime_error(oss.str());
				}
			}();
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
			try {
				Value& ov = evaluateRef(si->object);
				Value idxv = evaluate(si->index);
				Value v = evaluate(si->value);
				if (auto parr = std::get_if<std::shared_ptr<Array>>(&ov)) {
					size_t idx = indexFromValue(idxv);
					auto& vec = **parr;
					if (idx >= vec.size()) {
						std::ostringstream oss; oss << "Array index out of range at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
					}
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
				{
					std::ostringstream oss; oss << "Index assignment on non-array/object at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
				}
			} catch (const std::exception& ex) {
				std::string s = ex.what();
				if (s.find("line ") == std::string::npos) {
					std::ostringstream oss; oss << s << " at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
				}
				throw;
			}
		}
		if (auto un = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
			Value r = evaluate(un->right);
			try {
				switch (un->op.type) {
				case TokenType::Bang: return Value{!isTruthy(r)};
				case TokenType::Minus: {
					double rv = getNumber(r, "unary '-'");
					return Value{-rv};
				}
				default: break;
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << un->op.line << ", column " << un->op.column << ", length " << std::max(1, un->op.length); throw std::runtime_error(oss.str());
			}
		}
		if (auto update = std::dynamic_pointer_cast<UpdateExpr>(expr)) {
			// 处理递增/递减运算符：++x, --x, x++, x--
			try {
				// 获取当前值
				Value oldValue;
				std::string varName;
				std::shared_ptr<Object> objPtr;
				std::string propName;
				std::shared_ptr<Array> arrPtr;
				Value indexValue;
				bool isVar = false, isProp = false, isIndex = false;
				
				if (auto var = std::dynamic_pointer_cast<VariableExpr>(update->operand)) {
					oldValue = env->get(var->name);
					varName = var->name;
					isVar = true;
				} else if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(update->operand)) {
					Value obj = evaluate(getp->object);
					if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
						objPtr = *po;
						propName = getp->name;
						if (objPtr && objPtr->find(propName) != objPtr->end()) {
							oldValue = (*objPtr)[propName];
						} else {
							oldValue = Value{std::monostate{}};
						}
						isProp = true;
					} else if (auto inst = std::get_if<std::shared_ptr<Instance>>(&obj)) {
						if (*inst) {
							auto it = (*inst)->fields.find(getp->name);
							if (it != (*inst)->fields.end()) {
								oldValue = it->second;
							} else {
								oldValue = Value{std::monostate{}};
							}
							// 处理实例字段更新需要特殊处理
							throw std::runtime_error("Update operators on instance properties not yet fully supported");
						}
					} else {
						throw std::runtime_error("Cannot apply update operator to non-object property");
					}
				} else if (auto idx = std::dynamic_pointer_cast<IndexExpr>(update->operand)) {
					Value obj = evaluate(idx->object);
					indexValue = evaluate(idx->index);
					if (auto pa = std::get_if<std::shared_ptr<Array>>(&obj)) {
						arrPtr = *pa;
						if (!arrPtr) throw std::runtime_error("Cannot index null array");
						int i = static_cast<int>(getNumber(indexValue, "array index"));
						if (i < 0 || i >= static_cast<int>(arrPtr->size())) {
							throw std::runtime_error("Array index out of range");
						}
						oldValue = (*arrPtr)[i];
						isIndex = true;
					} else if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
						objPtr = *po;
						propName = keyFromValue(indexValue);
						if (objPtr && objPtr->find(propName) != objPtr->end()) {
							oldValue = (*objPtr)[propName];
						} else {
							oldValue = Value{std::monostate{}};
						}
						isProp = true;
					} else {
						throw std::runtime_error("Cannot apply update operator to non-indexable value");
					}
				} else {
					throw std::runtime_error("Invalid operand for update operator");
				}
				
				// 计算新值
				double numValue = getNumber(oldValue, "update operator");
				double newNumValue = (update->op.type == TokenType::PlusPlus) ? numValue + 1 : numValue - 1;
				Value newValue{newNumValue};
				
				// 更新值
				if (isVar) {
					env->assign(varName, newValue);
				} else if (isProp && objPtr) {
					(*objPtr)[propName] = newValue;
				} else if (isIndex && arrPtr) {
					int i = static_cast<int>(getNumber(indexValue, "array index"));
					(*arrPtr)[i] = newValue;
				}
				
				// 返回值：前置返回新值，后置返回旧值
				return update->isPrefix ? newValue : Value{numValue};
			} catch (const std::exception& ex) {
				std::ostringstream oss;
				oss << ex.what() << " at line " << update->line << ", column " << update->column << ", length " << update->length;
				throw std::runtime_error(oss.str());
			}
		}
		if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
			Value l = evaluate(bin->left); Value r = evaluate(bin->right);
			try {
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
				case TokenType::EqualEqual: return Value{isJSEqual(l, r)};
				case TokenType::BangEqual: return Value{!isJSEqual(l, r)};
				case TokenType::StrictEqual: return Value{isStrictEqual(l, r)};
				case TokenType::StrictNotEqual: return Value{!isStrictEqual(l, r)};
				case TokenType::Tilde: {
					// Adapter/interface matching: right operand must be a ClassInfo (interface or class descriptor)
					if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(r)) {
						throw std::runtime_error("'~' right-hand side must be an interface/class descriptor");
					}
					auto target = std::get<std::shared_ptr<ClassInfo>>(r);
					// If left is an instance, check its class (and supers) for presence of all required methods
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (!*pins || !(*pins)->klass) return Value{false};
						for (auto &kv : target->methods) {
							const std::string& mname = kv.first;
							auto f = findMethod((*pins)->klass, mname);
							if (!f) return Value{false};
						}
						return Value{true};
					}
					// If left is a plain object, check it has the named properties (functions or values)
					if (auto po = std::get_if<std::shared_ptr<Object>>(&l)) {
						if (!*po) return Value{false};
						for (auto &kv : target->methods) {
							const std::string& mname = kv.first;
							if ((**po).find(mname) == (**po).end()) return Value{false};
						}
						return Value{true};
					}
					// Otherwise, no match
					return Value{false};
				}
				default: break;
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << bin->op.line << ", column " << bin->op.column << ", length " << std::max(1, bin->op.length); throw std::runtime_error(oss.str());
			}
		}
		if (auto lg = std::dynamic_pointer_cast<LogicalExpr>(expr)) {
			Value l = evaluate(lg->left);
			if (lg->op.type == TokenType::OrOr) return isTruthy(l) ? l : evaluate(lg->right);
			else return !isTruthy(l) ? l : evaluate(lg->right);
		}
		if (auto cond = std::dynamic_pointer_cast<ConditionalExpr>(expr)) {
			// 三元运算符：condition ? thenBranch : elseBranch
			try {
				Value condValue = evaluate(cond->condition);
				if (isTruthy(condValue)) {
					return evaluate(cond->thenBranch);
				} else {
					return evaluate(cond->elseBranch);
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss;
				oss << ex.what() << " in ternary operator at line " << cond->line << ", column " << cond->column;
				throw std::runtime_error(oss.str());
			}
		}
		if (auto aw = std::dynamic_pointer_cast<AwaitExpr>(expr)) {
			Value v = evaluate(aw->expr);
			if (!std::holds_alternative<std::shared_ptr<PromiseState>>(v)) {
				std::ostringstream oss; oss << "await expects a Promise at line " << aw->line << ", column " << aw->column << ", length " << aw->length; throw std::runtime_error(oss.str());
			}
			auto p = std::get<std::shared_ptr<PromiseState>>(v);
			if (!p) return Value{std::monostate{}};
			std::unique_lock<std::mutex> lk(p->mtx);
			p->cv.wait(lk, [&]{ return p->settled; });
			if (p->rejected) throw ExceptionSignal{ p->result };
			return p->result;
		}
		if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
			// derive callee name for stack trace
			auto deriveName = [&](const ExprPtr& e)->std::string{
				if (auto v = std::dynamic_pointer_cast<VariableExpr>(e)) return v->name;
				if (auto gp = std::dynamic_pointer_cast<GetPropExpr>(e)) return gp->name;
				return std::string("call");
			};
			std::string calleeDesc = deriveName(call->callee);
			// push call frame
			callStack.push_back(calleeDesc + std::string(" at line ") + std::to_string(call->line));
			struct FrameGuard { std::vector<std::string>& st; ~FrameGuard(){ st.pop_back(); } } _fg{callStack};
			try {
				Value cal = evaluate(call->callee);
			if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) {
				std::ostringstream oss; oss << "Can only call functions at line " << call->line << ", column " << call->column << ", length " << call->length; throw std::runtime_error(oss.str());
			}
			auto fn = std::get<std::shared_ptr<Function>>(cal);
			std::vector<Value> args; args.reserve(call->args.size());
			for (auto& a : call->args) args.push_back(evaluate(a));
			if (fn->isBuiltin) {
				try { return fn->builtin(args, fn->closure); }
				catch (const std::exception& ex) {
					std::string s = ex.what();
					if (s.find("line ") == std::string::npos) {
						std::ostringstream oss; oss << s << " at line " << call->line << ", column " << call->column << ", length " << call->length; throw std::runtime_error(oss.str());
					}
					throw;
				}
			}
			// 支持调用由 FunctionExpr 生成的普通函数
			if (fn->isAsync) {
				// 返回一个 Promise，并将函数体作为任务投递
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				postTask([this, fn, args, p]{
					// 在闭包环境基础上创建局部环境并执行
					auto local = std::make_shared<Environment>(fn->closure);
					
					// Handle rest parameters
					if (fn->restParamIndex >= 0) {
						// Bind normal parameters
						for (int i = 0; i < fn->restParamIndex && i < static_cast<int>(args.size()); ++i) {
							local->define(fn->params[i], args[i]);
						}
						// Collect rest parameters into array
						auto restArray = std::make_shared<std::vector<Value>>();
						for (size_t i = fn->restParamIndex; i < args.size(); ++i) {
							restArray->push_back(args[i]);
						}
						local->define(fn->params[fn->restParamIndex], Value{restArray});
					} else {
						// No rest parameter, use old logic
						if (args.size() != fn->params.size()) {
							for (size_t i=0;i<fn->params.size() && i<args.size();++i) local->define(fn->params[i], args[i]);
						} else {
							for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
						}
					}
					
					Value ret{std::monostate{}};
					try {
						executeBlock(fn->body, local);
					} catch (const ReturnSignal& rs) {
						ret = rs.value;
					} catch (const ExceptionSignal& ex) {
						settlePromise(p, true, ex.value);
						return;
					}
					settlePromise(p, false, ret);
				});
				return Value{p};
			}
			
			// Synchronous function call
			// Handle rest parameters
			if (fn->restParamIndex >= 0) {
				// Check minimum parameter count
				int minParams = fn->restParamIndex;
				if (static_cast<int>(args.size()) < minParams) {
					std::ostringstream oss; 
					oss << "Expected at least " << minParams << " arguments but got " << args.size() 
					    << " at line " << call->line << ", column " << call->column << ", length " << call->length; 
					throw std::runtime_error(oss.str());
				}
				
				auto local = std::make_shared<Environment>(fn->closure);
				// Bind normal parameters
				for (int i = 0; i < fn->restParamIndex; ++i) {
					local->define(fn->params[i], args[i]);
				}
				// Collect rest parameters into array
				auto restArray = std::make_shared<std::vector<Value>>();
				for (size_t i = fn->restParamIndex; i < args.size(); ++i) {
					restArray->push_back(args[i]);
				}
				local->define(fn->params[fn->restParamIndex], Value{restArray});
				
				try {
					executeBlock(fn->body, local);
				} catch (const ReturnSignal& rs) { return rs.value; }
				return Value{std::monostate{}};
			}
			
			// No rest parameter
			if (args.size() != fn->params.size()) {
				std::ostringstream oss; oss << "Arity mismatch at line " << call->line << ", column " << call->column << ", length " << call->length; throw std::runtime_error(oss.str());
			}
			auto local = std::make_shared<Environment>(fn->closure);
			for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
			try {
				executeBlock(fn->body, local);
			} catch (const ReturnSignal& rs) { return rs.value; }
			return Value{std::monostate{}};
			} catch (const ExceptionSignal&) { throw; }
			catch (const std::exception& ex) {
				// Attach call stack if not present
				std::string msg = ex.what();
				if (msg.find("Stack:") == std::string::npos && !callStack.empty()) {
					std::ostringstream oss; oss << msg << "\n" << "Stack:";
					for (int i = static_cast<int>(callStack.size()) - 1; i >= 0; --i) {
						oss << "\n  -> " << callStack[static_cast<size_t>(i)];
					}
					throw std::runtime_error(oss.str());
				}
				throw;
			}
		}
		if (auto fexpr = std::dynamic_pointer_cast<FunctionExpr>(expr)) {
			auto fn = std::make_shared<Function>();
			// extract parameter names (ignore optional types at runtime)
			fn->params.clear();
			fn->restParamIndex = -1;
			for (size_t i = 0; i < fexpr->params.size(); ++i) {
				fn->params.push_back(fexpr->params[i].name);
				if (fexpr->params[i].isRest) {
					fn->restParamIndex = static_cast<int>(i);
				}
			}
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(fexpr->body)) fn->body = innerBlock->statements; else fn->body = { fexpr->body };
			fn->closure = env; // 关闭环境捕获
			return Value{fn};
		}
		if (auto nw = std::dynamic_pointer_cast<NewExpr>(expr)) {
			Value cal = evaluate(nw->callee);
			if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(cal)) {
				std::ostringstream oss; oss << "new: target is not a class at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
			}
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
				if (bound->isBuiltin) {
					try { (void)bound->builtin(args, bound->closure); }
					catch (const std::exception& ex) {
						std::string s = ex.what();
						if (s.find("line ") == std::string::npos) {
							std::ostringstream oss; oss << s << " at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
						}
						throw;
					}
				} else {
					if (args.size() != bound->params.size()) {
						std::ostringstream oss; oss << "Arity mismatch at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
					}
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
		if (std::dynamic_pointer_cast<EmptyStmt>(stmt)) { return; }
		if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) {
			for (auto& ent : imp->entries) {
				if (ent.isFile) {
					try { importFilePath(ent.filePath); }
					catch (const std::exception& ex) {
						std::ostringstream oss; oss << ex.what();
						// 附加 import 语句位置
						oss << " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
						throw std::runtime_error(oss.str());
					}
					continue;
				}
				auto it = packages.find(ent.packageName);
				if (it == packages.end()) {
					std::ostringstream oss; oss << "Unknown package: " << ent.packageName
						<< " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
					throw std::runtime_error(oss.str());
				}
				auto pobj = it->second;
				if (!pobj) continue;
				if (ent.symbol == "*") {
					for (auto& kv : *pobj) env->define(kv.first, kv.second);
				} else {
					auto fit = pobj->find(ent.symbol);
					if (fit == pobj->end()) {
						std::ostringstream oss; oss << "Package '" << ent.packageName << "' has no symbol '" << ent.symbol << "'"
							<< " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
						throw std::runtime_error(oss.str());
					}
					env->define(ent.symbol, fit->second);
				}
			}
			return;
		}
		if (auto v = std::dynamic_pointer_cast<VarDecl>(stmt)) {
			Value init = v->init ? evaluate(v->init) : Value{std::monostate{}};
			// resolve declared type if a type expression was provided
			std::optional<std::string> declaredName = std::nullopt;
			if (v->typeExpr) {
				try {
					Value tv = evaluate(v->typeExpr);
					if (auto ps = std::get_if<std::string>(&tv)) {
						declaredName = *ps;
					} else if (auto po = std::get_if<std::shared_ptr<Object>>(&tv)) {
						if (*po) {
							auto it = (**po).find("declaredType");
							if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) declaredName = std::get<std::string>(it->second);
							else {
								auto it2 = (**po).find("runtimeType");
								if (it2 != (**po).end() && std::holds_alternative<std::string>(it2->second)) declaredName = std::get<std::string>(it2->second);
							}
						}
					} else {
						declaredName = typeOf(tv);
					}
				} catch(...) { /* ignore and leave declaredName nullopt */ }
			}
			if (declaredName) env->defineWithType(v->name, init, declaredName); else env->define(v->name, init);
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
		if (auto fe = std::dynamic_pointer_cast<ForEachStmt>(stmt)) {
			// foreach (varName in iterable) body
			Value iterableValue = evaluate(fe->iterable);
			
			// 创建新的作用域用于循环变量
			auto loopEnv = std::make_shared<Environment>(env);
			loopEnv->define(fe->varName, Value{std::monostate{}});
			
			// 根据 iterable 类型进行迭代
			if (auto arr = std::get_if<std::shared_ptr<std::vector<Value>>>(&iterableValue)) {
				// 数组：遍历每个元素
				for (const auto& elem : **arr) {
					loopEnv->assign(fe->varName, elem);
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else if (auto obj = std::get_if<std::shared_ptr<std::unordered_map<std::string,Value>>>(&iterableValue)) {
				// 对象：遍历每个键
				for (const auto& [key, value] : **obj) {
					loopEnv->assign(fe->varName, Value{key});
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else if (auto str = std::get_if<std::string>(&iterableValue)) {
				// 字符串：遍历每个字符
				for (char ch : *str) {
					loopEnv->assign(fe->varName, Value{std::string(1, ch)});
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else {
				throw std::runtime_error("foreach requires an iterable (array, object, or string)");
			}
			return;
		}
		if (auto sw = std::dynamic_pointer_cast<SwitchStmt>(stmt)) {
			// switch (expr) { case val: ... default: ... }
			Value switchValue = evaluate(sw->expr);
			
			bool matched = false;
			bool fallThrough = false;
			
			try {
				for (const auto& caseClause : sw->cases) {
					// Check if this is a matching case or if we're in fall-through mode
					if (!matched && !fallThrough) {
						if (caseClause.value == nullptr) {
							// This is the default case - matches if no previous case matched
							matched = true;
						} else {
							// Evaluate case value and compare
							Value caseValue = evaluate(caseClause.value);
							if (isStrictEqual(switchValue, caseValue)) {
								matched = true;
							}
						}
					}
					
					// Execute case body if matched or in fall-through
					if (matched || fallThrough) {
						for (const auto& s : caseClause.body) {
							execute(s);
						}
						fallThrough = true; // Enable fall-through for subsequent cases
					}
				}
			} catch (const BreakSignal&) {
				// Break out of switch
			}
			return;
		}
		if (auto r = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
			Value val = r->value ? evaluate(r->value) : Value{std::monostate{}};
			throw ReturnSignal{val};
		}
		if (auto t = std::dynamic_pointer_cast<ThrowStmt>(stmt)) {
			Value val = t->value ? evaluate(t->value) : Value{std::monostate{}};
			throw ExceptionSignal{ val };
		}
		if (auto tc = std::dynamic_pointer_cast<TryCatchStmt>(stmt)) {
			try {
				execute(tc->tryBlock);
			} catch (const ExceptionSignal& ex) {
				auto local = std::make_shared<Environment>(env);
				local->define(tc->catchName, ex.value);
				// 在新的局部环境中执行 catch 块
				if (auto block = std::dynamic_pointer_cast<BlockStmt>(tc->catchBlock)) {
					executeBlock(block->statements, local);
				} else {
					executeBlock(std::vector<StmtPtr>{ tc->catchBlock }, local);
				}
			}
			return;
		}
		if (std::dynamic_pointer_cast<BreakStmt>(stmt)) { throw BreakSignal{}; }
		if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) { throw ContinueSignal{}; }
		if (auto f = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
			auto fn = std::make_shared<Function>();
			// extract parameter names (ignore optional types at runtime)
			fn->params.clear();
			fn->restParamIndex = -1;
			for (size_t i = 0; i < f->params.size(); ++i) {
				fn->params.push_back(f->params[i].name);
				if (f->params[i].isRest) {
					fn->restParamIndex = static_cast<int>(i);
				}
			}
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
				fn->params.clear(); for (auto &p : m->params) fn->params.push_back(p.name);
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
				fn->params.clear(); for (auto &p : m->params) fn->params.push_back(p.name);
				if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(m->body)) fn->body = innerBlock->statements; else fn->body = { m->body };
				fn->closure = env;
				fn->isAsync = m->isAsync;
				klass->methods[m->name] = fn; // 覆盖或新增
			}
			return;
		}
		if (auto itf = std::dynamic_pointer_cast<InterfaceStmt>(stmt)) {
			// 将 interface 作为空方法集合的 ClassInfo 注入环境，可作为多继承的父类使用
			auto klass = std::make_shared<ClassInfo>();
			klass->name = itf->name;
			// 可选：记录方法名（不作校验）
			for (auto& mn : itf->methodNames) {
				if (klass->methods.find(mn) == klass->methods.end()) {
					klass->methods[mn] = nullptr; // 占位
				}
			}
			env->define(itf->name, klass);
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
	std::unordered_map<std::string, std::shared_ptr<Object>> packages;
	std::unordered_set<std::string> importedFiles;
    std::filesystem::path importBaseDir;
	// Error context for pretty printing from imported files
	std::string lastErrorSource;
	std::string lastErrorFilename;
	std::vector<std::string> importStack;
	std::vector<std::string> callStack;

public:
	bool takeErrorContext(std::string& outSrc, std::string& outFile) {
		if (lastErrorSource.empty()) return false;
		outSrc = lastErrorSource; outFile = lastErrorFilename;
		lastErrorSource.clear(); lastErrorFilename.clear();
		return true;
	}

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
					} catch (const ExceptionSignal& ex) {
						settlePromise(nextP, true, ex.value);
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					} catch (...) {
						settlePromise(nextP, true, Value{ std::string("error") });
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
					} catch (const ExceptionSignal& ex) {
						settlePromise(nextP, true, ex.value);
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					} catch (...) {
						settlePromise(nextP, true, Value{ std::string("error") });
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

	// Strict equality: types must match; primitives compare by value; non-primitives compare by pointer
	static bool isStrictEqual(const Value& a, const Value& b) {
		if (a.index() != b.index()) {
			return false;
		}
		if (std::holds_alternative<std::monostate>(a)) return true;
		if (auto na = std::get_if<double>(&a)) return *na == std::get<double>(b);
		if (auto sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
		if (auto ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
		if (std::holds_alternative<std::shared_ptr<Function>>(a)) return std::get<std::shared_ptr<Function>>(a).get() == std::get<std::shared_ptr<Function>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Array>>(a)) return std::get<std::shared_ptr<Array>>(a).get() == std::get<std::shared_ptr<Array>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Object>>(a)) return std::get<std::shared_ptr<Object>>(a).get() == std::get<std::shared_ptr<Object>>(b).get();
		if (std::holds_alternative<std::shared_ptr<ClassInfo>>(a)) return std::get<std::shared_ptr<ClassInfo>>(a).get() == std::get<std::shared_ptr<ClassInfo>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Instance>>(a)) return std::get<std::shared_ptr<Instance>>(a).get() == std::get<std::shared_ptr<Instance>>(b).get();
		if (std::holds_alternative<std::shared_ptr<PromiseState>>(a)) return std::get<std::shared_ptr<PromiseState>>(a).get() == std::get<std::shared_ptr<PromiseState>>(b).get();
		return false;
	}

	// Convert various types to a numeric value similar to JS ToNumber
	static double toNumberPrimitive(const Value& v, bool& ok) {
		ok = true;
		if (std::holds_alternative<std::monostate>(v)) return 0.0; // null -> +0
		if (auto n = std::get_if<double>(&v)) return *n;
		if (auto s = std::get_if<std::string>(&v)) {
			char* end = nullptr; double d = std::strtod(s->c_str(), &end);
			if (end && *end == '\0') return d;
			ok = false; return std::numeric_limits<double>::quiet_NaN();
		}
		if (auto b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
		// Objects/functions/arrays -> ToPrimitive then ToNumber: use string coercion via toString()
		std::string s = toString(v);
		char* end = nullptr; double d = std::strtod(s.c_str(), &end);
		if (end && *end == '\0') return d;
		ok = false; return std::numeric_limits<double>::quiet_NaN();
	}

	// ToPrimitive fallback: use toString for non-primitive values
	static Value toPrimitive(const Value& v) {
		if (std::holds_alternative<std::monostate>(v) || std::get_if<double>(&v) || std::get_if<std::string>(&v) || std::get_if<bool>(&v)) return v;
		return Value{toString(v)};
	}

	// Abstract equality algorithm (JS-like == with coercion)
	static bool isJSEqual(const Value& x, const Value& y) {
		// If same type, use strict equality
		if (x.index() == y.index()) return isStrictEqual(x, y);

		// Number == String
		if (std::get_if<double>(&x) && std::get_if<std::string>(&y)) {
			bool ok; double yn = toNumberPrimitive(y, ok); if (!ok) return false; return std::get<double>(x) == yn;
		}
		if (std::get_if<std::string>(&x) && std::get_if<double>(&y)) {
			bool ok; double xn = toNumberPrimitive(x, ok); if (!ok) return false; return xn == std::get<double>(y);
		}

		// Boolean -> convert to number
		if (std::get_if<bool>(&x)) {
			bool ok; double xn = toNumberPrimitive(x, ok); return isJSEqual(Value{xn}, y);
		}
		if (std::get_if<bool>(&y)) {
			bool ok; double yn = toNumberPrimitive(y, ok); return isJSEqual(x, Value{yn});
		}

		// Object to primitive then compare
		auto isObjectType = [](const Value& v)->bool{
			return std::holds_alternative<std::shared_ptr<Object>>(v) || std::holds_alternative<std::shared_ptr<Array>>(v) || std::holds_alternative<std::shared_ptr<Instance>>(v) || std::holds_alternative<std::shared_ptr<ClassInfo>>(v) || std::holds_alternative<std::shared_ptr<Function>>(v) || std::holds_alternative<std::shared_ptr<PromiseState>>(v);
		};
		if (isObjectType(x) && (std::get_if<std::string>(&y) || std::get_if<double>(&y))) {
			Value px = toPrimitive(x);
			return isJSEqual(px, y);
		}
		if (isObjectType(y) && (std::get_if<std::string>(&x) || std::get_if<double>(&x))) {
			Value py = toPrimitive(y);
			return isJSEqual(x, py);
		}

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
			return Value{std::string("undefined")};
		}
		// For numbers and other primitives, return "undefined" instead of null
		if (std::get_if<double>(&obj) || std::get_if<bool>(&obj)) {
			return Value{std::string("undefined")};
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
									} catch (const ExceptionSignal& ex) {
										loop->settlePromise(nextP, true, ex.value);
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									} catch (...) {
										loop->settlePromise(nextP, true, Value{ std::string("error") });
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
									} catch (const ExceptionSignal& ex) {
										loop->settlePromise(nextP, true, ex.value);
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									} catch (...) {
										loop->settlePromise(nextP, true, Value{ std::string("error") });
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
		printFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			for (auto& v : args) std::cout << toString(v);
			// no newline, no separators (flat output)
			return Value{std::monostate{}};
		};
		globals->define("print", printFn);

		auto printlnFn = std::make_shared<Function>();
		printlnFn->isBuiltin = true;
		printlnFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			for (auto& v : args) std::cout << toString(v);
			std::cout << std::endl;
			return Value{std::monostate{}};
		};
		globals->define("println", printlnFn);

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

		// quote(str): 返回一个对象 { tokens: [...], source: string, apply: function() }
		auto quoteFn = std::make_shared<Function>();
		quoteFn->isBuiltin = true;
		quoteFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("quote expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("quote expects a string");
			std::string src = std::get<std::string>(args[0]);
			Lexer lx(src);
			auto toks = lx.scanTokens();
			auto arr = std::make_shared<Array>();
			for (auto &t : toks) {
				if (t.type == TokenType::EndOfFile) continue;
				auto obj = std::make_shared<Object>();
				std::string tname;
				switch (t.type) {
					case TokenType::LeftParen: tname = "LeftParen"; break;
					case TokenType::RightParen: tname = "RightParen"; break;
					case TokenType::LeftBrace: tname = "LeftBrace"; break;
					case TokenType::RightBrace: tname = "RightBrace"; break;
					case TokenType::LeftBracket: tname = "LeftBracket"; break;
					case TokenType::RightBracket: tname = "RightBracket"; break;
					case TokenType::Comma: tname = "Comma"; break;
					case TokenType::Semicolon: tname = "Semicolon"; break;
					case TokenType::Colon: tname = "Colon"; break;
					case TokenType::Dot: tname = "Dot"; break;
					case TokenType::Ellipsis: tname = "Ellipsis"; break;
					case TokenType::Plus: tname = "Plus"; break;
					case TokenType::Minus: tname = "Minus"; break;
					case TokenType::Star: tname = "Star"; break;
					case TokenType::Slash: tname = "Slash"; break;
					case TokenType::Percent: tname = "Percent"; break;
					case TokenType::Tilde: tname = "Tilde"; break;
					case TokenType::Bang: tname = "Bang"; break;
					case TokenType::Equal: tname = "Equal"; break;
					case TokenType::Less: tname = "Less"; break;
					case TokenType::Greater: tname = "Greater"; break;
					case TokenType::BangEqual: tname = "BangEqual"; break;
					case TokenType::EqualEqual: tname = "EqualEqual"; break;
					case TokenType::StrictEqual: tname = "StrictEqual"; break;
					case TokenType::StrictNotEqual: tname = "StrictNotEqual"; break;
					case TokenType::LessEqual: tname = "LessEqual"; break;
					case TokenType::GreaterEqual: tname = "GreaterEqual"; break;
					case TokenType::LeftArrow: tname = "LeftArrow"; break;
					case TokenType::Arrow: tname = "Arrow"; break;
					case TokenType::AndAnd: tname = "AndAnd"; break;
					case TokenType::OrOr: tname = "OrOr"; break;
					case TokenType::Identifier: tname = "Identifier"; break;
					case TokenType::String: tname = "String"; break;
					case TokenType::Number: tname = "Number"; break;
					case TokenType::Let: tname = "Let"; break;
					case TokenType::Var: tname = "Var"; break;
					case TokenType::Const: tname = "Const"; break;
					case TokenType::Function: tname = "Function"; break;
					case TokenType::Return: tname = "Return"; break;
					case TokenType::If: tname = "If"; break;
					case TokenType::Else: tname = "Else"; break;
					case TokenType::While: tname = "While"; break;
					case TokenType::For: tname = "For"; break;
					case TokenType::Break: tname = "Break"; break;
					case TokenType::Continue: tname = "Continue"; break;
					case TokenType::Class: tname = "Class"; break;
					case TokenType::Extends: tname = "Extends"; break;
					case TokenType::New: tname = "New"; break;
					case TokenType::True: tname = "True"; break;
					case TokenType::False: tname = "False"; break;
					case TokenType::Null: tname = "Null"; break;
					case TokenType::Await: tname = "Await"; break;
					case TokenType::Async: tname = "Async"; break;
					case TokenType::Go: tname = "Go"; break;
					case TokenType::Try: tname = "Try"; break;
					case TokenType::Catch: tname = "Catch"; break;
					case TokenType::Throw: tname = "Throw"; break;
					case TokenType::Interface: tname = "Interface"; break;
					case TokenType::Import: tname = "Import"; break;
					case TokenType::From: tname = "From"; break;
					default: tname = "Unknown"; break;
				}
				(*obj)["token"] = Value{ tname };
				(*obj)["lexeme"] = Value{ t.lexeme };
				(*obj)["line"] = Value{ static_cast<double>(t.line) };
				(*obj)["column"] = Value{ static_cast<double>(t.column) };
				(*obj)["length"] = Value{ static_cast<double>(t.length) };
				arr->push_back(Value{ obj });
			}
			// 构建带有 apply() 的容器对象
			auto qobj = std::make_shared<Object>();
			(*qobj)["tokens"] = Value{ arr };
			(*qobj)["source"] = Value{ src };
			// apply(): 基于当前 tokens 重新拼接代码并执行（eval），返回其值
			{
				auto self = qobj; // 捕获对象以读取被用户修改后的 tokens
				auto applyFn = std::make_shared<Function>();
				applyFn->isBuiltin = true;
				applyFn->builtin = [this, self](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
					// 读取 tokens 数组
					auto it = self->find("tokens");
					if (it == self->end() || !std::holds_alternative<std::shared_ptr<Array>>(it->second))
						throw std::runtime_error("quote.apply: missing tokens array");
					auto arrPtr = std::get<std::shared_ptr<Array>>(it->second);
					if (!arrPtr) return Value{std::monostate{}};
					// 将 tokens 重建为源码
					auto escapeString = [](const std::string& s){
						std::string out; out.reserve(s.size()+2);
						for (char c: s) {
							switch (c) {
								case '\\': out += "\\\\"; break;
								case '"': out += "\\\""; break;
								case '\n': out += "\\n"; break;
								case '\t': out += "\\t"; break;
								case '\r': out += "\\r"; break;
								case '\0': out += "\\0"; break;
								default: out.push_back(c); break;
							}
						}
						return std::string("\"") + out + std::string("\"");
					};
					std::string code;
					bool first = true;
					for (const auto& v : *arrPtr) {
						if (!std::holds_alternative<std::shared_ptr<Object>>(v)) continue;
						auto tobj = std::get<std::shared_ptr<Object>>(v);
						if (!tobj) continue;
						std::string tname, lex;
						auto itName = tobj->find("token");
						if (itName != tobj->end() && std::holds_alternative<std::string>(itName->second)) tname = std::get<std::string>(itName->second);
						auto itLex = tobj->find("lexeme");
						if (itLex != tobj->end() && std::holds_alternative<std::string>(itLex->second)) lex = std::get<std::string>(itLex->second);
						std::string piece;
						if (tname == "String") piece = escapeString(lex);
						else piece = lex;
						if (!first) code.push_back(' ');
						first = false;
						code += piece;
					}
					// 通过内置 eval 处理，复用其单/多语句与返回值规则
					return callFunction("eval", std::vector<Value>{ Value{code} });
				};
				(*qobj)["apply"] = Value{ applyFn };
			}
			return Value{ qobj };
		};
		globals->define("quote", quoteFn);

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

		// TYPE(x): return a type-name string for x; useful in type expressions e.g. : TYPE(b.type())
		auto typeFn = std::make_shared<Function>();
		typeFn->isBuiltin = true;
		typeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos) -> Value {
			if (args.size() != 1) throw std::runtime_error("TYPE expects 1 argument");
			const Value& v = args[0];
			if (auto ps = std::get_if<std::string>(&v)) return Value{ *ps };
			if (auto po = std::get_if<std::shared_ptr<Object>>(&v)) {
				if (*po) {
					auto it = (**po).find("declaredType");
					if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) return Value{ std::get<std::string>(it->second) };
					it = (**po).find("runtimeType");
					if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) return Value{ std::get<std::string>(it->second) };
				}
			}
			return Value{ typeOf(v) };
		};
		globals->define("TYPE", typeFn);

		// eval(str): evaluate ALang source in a child environment and return last-expression value or null
		auto evalFn = std::make_shared<Function>();
		evalFn->isBuiltin = true;
		evalFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("eval expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("eval expects a string");
			std::string code = std::get<std::string>(args[0]);
			// Try full parse first; on failure (e.g., missing semicolons), fallback to single-expression snippet
			std::vector<StmtPtr> stmts;
			{
				Lexer lx(code);
				auto toks = lx.scanTokens();
				Parser ps(toks, code);
				try {
					stmts = ps.parse();
				} catch (const std::exception&) {
					// Try adding a trailing semicolon to support code ending with a bare expression
					std::string withSemi = code;
					withSemi.push_back(';');
					try {
						Lexer lxSemi(withSemi);
						auto toksSemi = lxSemi.scanTokens();
						Parser psSemi(toksSemi, withSemi);
						stmts = psSemi.parse();
					} catch (const std::exception&) {
						// Fallback to single-expression snippet `(expr);`
						std::string snippet = "(" + code + ")";
						snippet.push_back(';');
						Lexer lx2(snippet);
						auto toks2 = lx2.scanTokens();
						Parser ps2(toks2, snippet);
						auto s2 = ps2.parse();
						if (s2.size() == 1) {
							if (auto es = std::dynamic_pointer_cast<ExprStmt>(s2[0])) {
								auto evalEnv = std::make_shared<Environment>(this->env);
								auto prev = this->env; this->env = evalEnv;
								try { Value v = evaluate(es->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
							}
						}
						throw; // not an expression either
					}
				}
			}
			// execute in a child environment so we don't pollute caller
			auto evalEnv = std::make_shared<Environment>(this->env);
			if (stmts.empty()) return Value{std::monostate{}};
			if (stmts.size() == 1) {
				if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmts[0])) {
					auto prev = this->env; this->env = evalEnv;
					try { Value v = evaluate(es->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
				} else {
					executeBlock(stmts, evalEnv);
					return Value{std::monostate{}};
				}
			}
			// multiple statements: execute all but last, then evaluate last expression if expression-stmt
			if (stmts.size() > 1) {
				std::vector<StmtPtr> prefix(stmts.begin(), stmts.end() - 1);
				executeBlock(prefix, evalEnv);
				if (auto lastEs = std::dynamic_pointer_cast<ExprStmt>(stmts.back())) {
					auto prev = this->env; this->env = evalEnv;
					try { Value v = evaluate(lastEs->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
				} else {
					executeBlock(std::vector<StmtPtr>{ stmts.back() }, evalEnv);
					return Value{std::monostate{}};
				}
			}
			return Value{std::monostate{}};
		};
		globals->define("eval", evalFn);

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

		// --- Packages ---
		// Math: pi, abs
		auto math = std::make_shared<Object>();
		(*math)["pi"] = Value{ 3.14159265358979323846 };
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				if (args.empty()) return Value{0.0};
				double x = getNumber(args[0], "abs x");
				return Value{ x < 0 ? -x : x };
			};
			(*math)["abs"] = fn;
		}
		packages["Math"] = math;
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
	// 安装 AsulFormatString 色彩与日志标签（用于错误美化输出）
	try {
		auto& afs = asul_formatter();
		afs.installColorFormatAdapter();
		afs.installLogLabelAdapter();
		afs.installResetLabelAdapter();
	} catch (...) {
		// 忽略格式器初始化异常，保持回退到纯文本错误输出
	}
}

// 全局可配置的错误配色映射：{"header","code","caret"}
static std::unordered_map<std::string, std::string> g_errorColorMap;

void ALangEngine::setSource(const std::string& code) { impl->source = code; }

void ALangEngine::setErrorColorMap(const std::unordered_map<std::string, std::string>& colorMap) {
	g_errorColorMap = colorMap;
}

void ALangEngine::execute() {
	if (impl->source.empty()) return;
	execute(impl->source);
}

static std::string colorize(const std::string& key, const std::string& text, const char* defColor) {
    std::string color = defColor;
    auto it = g_errorColorMap.find(key);
    if (it != g_errorColorMap.end() && !it->second.empty()) color = it->second;
    return f("{" + color + "}", text);
}

static std::string sanitizeHeaderMsg(const std::string& msg) {
	// Remove ", column N" and ", length M" from header text, keep "line N" if present
	std::string s = msg;
	// operate only on the first line for caret-context messages; but safe to run on whole
	auto removeSegment = [&](const char* key){
		size_t p = 0;
		while ((p = s.find(key, p)) != std::string::npos) {
			size_t end = p + std::strlen(key);
			while (end < s.size() && isspace(static_cast<unsigned char>(s[end]))) end++;
			while (end < s.size() && isdigit(static_cast<unsigned char>(s[end]))) end++;
			// Remove preceding comma and space if present
			size_t start = p;
			if (start >= 2 && s[start-2] == ',' && s[start-1] == ' ') start -= 2;
			s.erase(start, end - start);
		}
	};
	removeSegment("column");
	removeSegment("length");
	return s;
}

static void printErrorWithContext(const std::string& src, const std::string& msg, const std::string& filename = std::string()) {
    // Trim message body if it already embeds a caret block; keep only first line
    std::string cleanMsg = msg;
    if (cleanMsg.find('\n') != std::string::npos && cleanMsg.find('^') != std::string::npos) {
        size_t nl = cleanMsg.find('\n');
        if (nl != std::string::npos) cleanMsg = cleanMsg.substr(0, nl);
    }
    // Try to extract line and column numbers: patterns like "line N, column M" or "at line N"
	int line = -1, col = 1, width = 1;
	size_t p = cleanMsg.find("line ");
	if (p != std::string::npos) {
		p += 5;
		size_t q = p;
		while (q < cleanMsg.size() && isdigit(static_cast<unsigned char>(cleanMsg[q]))) q++;
		if (q > p) {
			line = std::stoi(cleanMsg.substr(p, q - p));
			size_t cpos = cleanMsg.find("column ", q);
			if (cpos != std::string::npos) {
				cpos += 7;
				size_t r = cpos;
				while (r < cleanMsg.size() && isdigit(static_cast<unsigned char>(cleanMsg[r]))) r++;
				if (r > cpos) col = std::stoi(cleanMsg.substr(cpos, r - cpos));
			}
			// try parse length
			size_t lpos = cleanMsg.find("length ", q);
			if (lpos != std::string::npos) {
				lpos += 7;
				size_t r2 = lpos;
				while (r2 < cleanMsg.size() && isdigit(static_cast<unsigned char>(cleanMsg[r2]))) r2++;
				if (r2 > lpos) width = std::max(1, std::stoi(cleanMsg.substr(lpos, r2 - lpos)));
			}
		}
	}
	if (line >= 1) {
		// Extract line text
		int curLine = 1;
		size_t i = 0, startIdx = 0;
		for (; i < src.size(); ++i) { if (curLine == line) { startIdx = i; break; } if (src[i] == '\n') curLine++; }
		size_t j = startIdx; while (j < src.size() && src[j] != '\n' && src[j] != '\r') j++;
		std::string lineStr = (curLine == line) ? src.substr(startIdx, j - startIdx) : std::string();
		std::string head = colorize("header", std::string("[ALang Error]"), "RED");
		// Render token inside the code line
		int c0 = std::max(1, col) - 1; int w = std::max(1, width);
		if (c0 > static_cast<int>(lineStr.size())) c0 = static_cast<int>(lineStr.size());
		int endPos = std::min(static_cast<int>(lineStr.size()), c0 + w);
		std::string before = lineStr.substr(0, c0);
		std::string mid = lineStr.substr(c0, endPos - c0);
		std::string after = lineStr.substr(endPos);
		std::string codeLine = colorize("code", before, "LIGHT_GRAY")
							 + colorize("token", mid, "RED")
							 + colorize("code", after, "LIGHT_GRAY");
		std::string caretStr = colorize("caret", std::string(width, '^'), "RED");
		// line prefix (colored): optional filename + "line N: "
		std::string filePrefix;
		if (!filename.empty()) {
			filePrefix = colorize("fileLabel", std::string("file "), "YELLOW")
					   + colorize("fileValue", filename, "CYAN")
					   + colorize("lineLabel", std::string(", "), "YELLOW");
		}
		std::string linePrefix = colorize("lineLabel", std::string("line "), "YELLOW")
							   + colorize("lineValue", std::to_string(line), "CYAN")
							   + colorize("lineLabel", std::string(": "), "YELLOW");
		int prefixLen = 5 + static_cast<int>(std::to_string(line).size()) + 2; // "line " + digits + ": "
		std::cerr << head << " " << sanitizeHeaderMsg(cleanMsg) << "\n"
				  << (filePrefix.empty() ? std::string() : filePrefix) << linePrefix << codeLine << "\n"
				  << std::string((int)(filePrefix.empty() ? 0 : 0) + prefixLen + (col > 1 ? col - 1 : 0), ' ') << caretStr
				  << std::endl;
		return;
	}
	// Fallback
	std::string head = colorize("header", std::string("[ALang Error]"), "RED");
	std::cerr << head << " " << sanitizeHeaderMsg(cleanMsg) << std::endl;
}

void ALangEngine::execute(const std::string& code) {
	try {
		Lexer lx(code);
		auto tokens = lx.scanTokens();
		Parser ps(tokens, code);
		auto stmts = ps.parse();
		impl->interpreter.execute(stmts);
	} catch (const ExceptionSignal& ex) {
		// Prefer imported-file context if any
		std::string altSrc, altFile;
		if (impl->interpreter.takeErrorContext(altSrc, altFile)) {
			printErrorWithContext(altSrc, toString(ex.value), altFile);
		} else {
			printErrorWithContext(code, toString(ex.value));
		}
		throw;
	} catch (const std::exception& ex) {
		std::string altSrc, altFile;
		if (impl->interpreter.takeErrorContext(altSrc, altFile)) {
			printErrorWithContext(altSrc, ex.what(), altFile);
		} else {
			printErrorWithContext(code, ex.what());
		}
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
		printErrorWithContext(impl->source, std::string("callFunction: ") + ex.what());
		throw;
	}
}

void ALangEngine::runEventLoopUntilIdle() {
	impl->interpreter.runEventLoopUntilIdle();
}

void ALangEngine::setImportBaseDir(const std::string& dir) {
	impl->interpreter.setImportBaseDir(dir);
}

// --- Host registration APIs ---
void ALangEngine::setGlobal(const std::string& name, const NativeValue& value) {
	try {
		Value v = nativeToValue(value);
		impl->interpreter.globalsEnv()->define(name, v);
	} catch (const std::exception& ex) {
		printErrorWithContext(impl->source, std::string("setGlobal: ") + ex.what());
		throw;
	}
}

void ALangEngine::registerFunction(const std::string& name, NativeFunc func) {
	if (!func) return;
	auto fn = std::make_shared<Function>();
	fn->isBuiltin = true;
	fn->builtin = [func](const std::vector<Value>& args, std::shared_ptr<Environment> /*clos*/) -> Value {
		std::vector<NativeValue> na; na.reserve(args.size());
		for (auto& a : args) na.push_back(valueToNative(a));
		auto ret = func(na, nullptr);
		return nativeToValue(ret);
	};
	impl->interpreter.globalsEnv()->define(name, fn);
}

void ALangEngine::registerInterface(const std::string& name, const std::vector<std::string>& methodNames) {
	// Interface is represented as a ClassInfo with method placeholders
	auto klass = std::make_shared<ClassInfo>();
	klass->name = name;
	for (const auto& mn : methodNames) {
		if (klass->methods.find(mn) == klass->methods.end()) {
			klass->methods[mn] = nullptr; // placeholder
		}
	}
	impl->interpreter.globalsEnv()->define(name, klass);
}

