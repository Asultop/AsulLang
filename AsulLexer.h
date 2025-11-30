#ifndef ASUL_LEXER_H
#define ASUL_LEXER_H

#include <string>
#include <vector>
#include <unordered_map>

// Lightweight placeholder API — full lexer implementation currently lives
// inside `ALangEngine.cpp`. This header provides a stable include for
// downstream code during the refactor.

enum class TokenType {
    LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
    Comma, Semicolon, Colon, Dot,
    Plus, Minus, Star, Slash, Percent,
    Ampersand, Pipe, Caret,
    Tilde,
    Bang, Equal, Less, Greater, Question,
    BangEqual, StrictNotEqual, EqualEqual, StrictEqual, LessEqual, GreaterEqual, LeftArrow,
    MatchInterface,
    ShiftLeft, ShiftRight,
    Arrow,
    Ellipsis,
    AndAnd, OrOr,
    PlusPlus, MinusMinus,
    PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
    Identifier, String, Number,
    Let, Var, Const, Function, Return, If, Else, While, Do, For, ForEach, In, Break, Continue, Switch, Case, Default, Class, Extends, New, True, False, Null, Await, Async, Go, Try, Catch, Throw, Interface, Import, From, As, Export, Static,
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

#endif // ASUL_LEXER_H
