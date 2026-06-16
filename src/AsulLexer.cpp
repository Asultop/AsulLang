#include "AsulLexer.h"

#include <cctype>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace asul {

Lexer::Lexer(const std::string& src) : source(src) {}

std::vector<Token> Lexer::scanTokens() {
	while (!isAtEnd()) {
		start = current;
		scanToken();
	}
	int col = static_cast<int>((current >= lineStart) ? (current - lineStart + 1) : 1);
	tokens.push_back(Token{TokenType::EndOfFile, "", line, col, 0});
	return tokens;
}

bool Lexer::isAtEnd() const { return current >= source.size(); }
char Lexer::advance() { return source[current++]; }
char Lexer::peek() const { return isAtEnd() ? '\0' : source[current]; }
char Lexer::peekNext() const { return (current + 1 >= source.size()) ? '\0' : source[current + 1]; }

bool Lexer::match(char expected) {
	if (isAtEnd() || source[current] != expected) return false;
	current++;
	return true;
}

void Lexer::add(TokenType type) {
	int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
	int len = static_cast<int>(current - start);
	tokens.push_back(Token{type, source.substr(start, current - start), line, col, len});
}

void Lexer::string() {
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
		oss << "未终止的字符串，位于行 " << startLine << ", 列 " << col << ", 长度 " << len << "\n";
		oss << lineStr << "\n" << std::string(col > 1 ? col - 1 : 0, ' ') << std::string(std::max(1, len), '^');
		throw std::runtime_error(oss.str());
	}
	advance(); // closing quote
	std::string raw = source.substr(start + 1, current - start - 2);
	int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
	int len = static_cast<int>(current - start); // include quotes
	tokens.push_back(Token{TokenType::String, raw, line, col, len});
}

void Lexer::number() {
	while (std::isdigit(peek())) advance();
	if (peek() == '.' && std::isdigit(peekNext())) {
		advance();
		while (std::isdigit(peek())) advance();
	}
	int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
	int len = static_cast<int>(current - start);
	tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line, col, len});
}

void Lexer::identifier() {
	while (std::isalnum(peek()) || peek() == '_' || (static_cast<unsigned char>(peek()) >= 0x80)) advance();
	std::string text = source.substr(start, current - start);
	static const std::unordered_map<std::string, TokenType> keywords{
		{"let", TokenType::Let}, {"var", TokenType::Var}, {"const", TokenType::Const},
		{"function", TokenType::Function}, {"fn", TokenType::Function}, {"return", TokenType::Return},
		{"if", TokenType::If}, {"else", TokenType::Else}, {"while", TokenType::While}, {"do", TokenType::Do},
		{"for", TokenType::For}, {"foreach", TokenType::ForEach}, {"in", TokenType::In}, {"break", TokenType::Break}, {"continue", TokenType::Continue},
		{"switch", TokenType::Switch}, {"case", TokenType::Case}, {"default", TokenType::Default},
		{"class", TokenType::Class}, {"extends", TokenType::Extends}, {"new", TokenType::New},
		{"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
		{"await", TokenType::Await},
		{"async", TokenType::Async},
		{"go", TokenType::Go},
		{"try", TokenType::Try}, {"catch", TokenType::Catch}, {"finally", TokenType::Finally}, {"throw", TokenType::Throw},
		{"interface", TokenType::Interface},
		{"import", TokenType::Import}, {"from", TokenType::From}, {"as", TokenType::As}, {"export", TokenType::Export},
		{"static", TokenType::Static},
		{"match", TokenType::Match}, {"yield", TokenType::Yield},
	};
	auto it = keywords.find(text);
	int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1);
	int len = static_cast<int>(current - start);
	if (it != keywords.end()) tokens.push_back(Token{it->second, text, line, col, len});
	else tokens.push_back(Token{TokenType::Identifier, text, line, col, len});
}

void Lexer::skipWhitespaceAndComments() {
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

void Lexer::scanToken() {
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
	case '?':
		// Support ?? and ??= and ?.
		if (match('?')) {
			if (match('=')) add(TokenType::QuestionQuestionEqual);
			else add(TokenType::QuestionQuestion);
		} else if (match('.')) {
			add(TokenType::QuestionDot);
		} else add(TokenType::Question);
		break;
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
			// Support '=~=' composite operator for interface matching before checking equality operators
			if (peek() == '~' && peekNext() == '=') {
				advance(); // consume '~'
				advance(); // consume '='
				add(TokenType::MatchInterface);
			} else if (match('=')) {
				if (match('=')) add(TokenType::StrictEqual);
				else add(TokenType::EqualEqual);
			} else add(TokenType::Equal);
		break;
	}
	case '<': {
		if (match('-')) { add(TokenType::LeftArrow); }
		else if (match('<')) { add(TokenType::ShiftLeft); }
		else if (match('=')) { add(TokenType::LessEqual); }
		else { add(TokenType::Less); }
		break;
	}
	case '>': {
		if (match('>')) add(TokenType::ShiftRight);
		else if (match('=')) add(TokenType::GreaterEqual);
		else add(TokenType::Greater);
		break;
	}
	case '&':
		if (match('&')) {
			if (match('=')) add(TokenType::AndAndEqual);
			else add(TokenType::AndAnd);
		} else {
			add(TokenType::Ampersand);
		}
		break;
	case '|':
		if (match('|')) {
			if (match('=')) add(TokenType::OrOrEqual);
			else add(TokenType::OrOr);
		} else {
			add(TokenType::Pipe);
		}
		break;
	case '^': add(TokenType::Caret); break;
	case '@': add(TokenType::At); break;
	case '/':
		if (match('=')) add(TokenType::SlashEqual);
		else add(TokenType::Slash);
		break;
	case '"': string(); break;
	default:
		if (std::isdigit(c)) { while (std::isdigit(peek()) || (peek()=='.' && std::isdigit(peekNext()))) advance(); int col = static_cast<int>((start >= lineStart) ? (start - lineStart + 1) : 1); int len = static_cast<int>(current - start); tokens.push_back(Token{TokenType::Number, source.substr(start, current - start), line, col, len}); }
		else if (std::isalpha(c) || c == '_' || (static_cast<unsigned char>(c) >= 0x80)) identifier();
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

} // namespace asul
