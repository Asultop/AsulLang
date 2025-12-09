#ifndef ASUL_LEXER_H
#define ASUL_LEXER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace asul {

// ----------- Token Types -----------
enum class TokenType {
	// Single-char
	LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
	Comma, Semicolon, Colon, Dot,
	Plus, Minus, Star, Slash, Percent,
	Ampersand, Pipe, Caret,
	Tilde,
	Bang, Equal, Less, Greater, Question,
	// One, two or three char
	BangEqual, StrictNotEqual, EqualEqual, StrictEqual, LessEqual, GreaterEqual, LeftArrow,
	MatchInterface, // '=~=' binary operator for interface/class descriptor matching
	ShiftLeft, ShiftRight,
	Arrow,
	Ellipsis,
	AndAnd, OrOr,
	QuestionDot, // '?.' for optional chaining
	At, // '@' for decorators
	// Increment/Decrement and Compound Assignment
	PlusPlus, MinusMinus,
	PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
	// Logical Assignment and Nullish Coalescing
	QuestionQuestion, QuestionQuestionEqual, AndAndEqual, OrOrEqual,
	// Literals
	Identifier, String, Number,
	// Keywords
	Let, Var, Const, Function, Return, If, Else, While, Do, For, ForEach, In, Break, Continue, Switch, Case, Default, Class, Extends, New, True, False, Null, Await, Async, Go, Try, Catch, Finally, Throw, Interface, Import, From, As, Export, Static, Match, Yield,
	EndOfFile
};

// ----------- Token -----------
struct Token {
	TokenType type;
	std::string lexeme;
	int line;
	int column{1};
	int length{1};
};

// ----------- Lexer -----------
class Lexer {
public:
	explicit Lexer(const std::string& src);
	std::vector<Token> scanTokens();

private:
	const std::string& source;
	std::vector<Token> tokens;
	size_t start{0};
	size_t current{0};
	int line{1};
	size_t lineStart{0};

	bool isAtEnd() const;
	char advance();
	char peek() const;
	char peekNext() const;
	bool match(char expected);
	void add(TokenType type);

	void string();
	void number();
	void identifier();
	void skipWhitespaceAndComments();
	void scanToken();
};

} // namespace asul

#endif // ASUL_LEXER_H
