#include "AsulParser.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>

using namespace asul;
using asul::Value;
using asul::Stmt;
using asul::StmtPtr;
using asul::Expr;
using asul::ExprPtr;
using asul::Param;

static std::runtime_error makeParseError(const Token& t, const std::string& msg) {
    std::ostringstream oss;
    oss << "Parse error at line " << t.line << ", column " << t.column << ", length " << t.length << ": " << msg;
    return std::runtime_error(oss.str());
}

AsulParser::AsulParser(const std::vector<Token>& t, const std::string& src)
    : tokens(t), source(src) {}

std::vector<asul::StmtPtr> AsulParser::parse() {
    std::vector<StmtPtr> stmts;
    while (!isAtEnd()) stmts.push_back(declaration());
    return stmts;
}

const Token& AsulParser::peek() const {
    if (current >= tokens.size()) return tokens.back();
    return tokens[current];
}

const Token& AsulParser::previous() const {
    if (current == 0) return tokens[0];
    return tokens[current - 1];
}

const Token& AsulParser::advance() {
    if (!isAtEnd()) { current++; }
    return previous();
}

bool AsulParser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile || current >= tokens.size();
}

bool AsulParser::check(TokenType type) const {
    return !isAtEnd() && peek().type == type;
}

bool AsulParser::match(std::initializer_list<TokenType> types) {
    for (auto t : types) {
        if (check(t)) { current++; return true; }
    }
    return false;
}

const Token& AsulParser::consume(TokenType type, const char* message) {
    if (check(type)) { current++; return previous(); }
    throw makeParseError(peek(), message ? message : "parse error");
}

std::vector<Token> AsulParser::parseQualifiedIdentifiers(const char* message) {
    auto first = consume(TokenType::Identifier, message);
    std::vector<Token> parts{ first };
    while (peek().type == TokenType::Dot) {
        size_t saved = current;
        current++;
        if (check(TokenType::Identifier)) {
            parts.push_back(consume(TokenType::Identifier, "Expect identifier after '.'"));
        } else {
            current = saved;
            break;
        }
    }
    return parts;
}

std::string AsulParser::joinIdentifiers(const std::vector<Token>& parts, size_t begin, size_t end) const {
    std::string res;
    for (size_t i = begin; i < end && i < parts.size(); ++i) {
        if (i > begin) res.push_back('.');
        res += parts[i].lexeme;
    }
    return res;
}

std::string AsulParser::getLineText(int line) const {
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

// Declaration parsing
StmtPtr AsulParser::declaration() {
    bool isExported = false;
    if (match({TokenType::Export})) {
        isExported = true;
    }
    if (match({TokenType::Async})) { consume(TokenType::Function, "Expect 'function' after 'async'"); return functionDecl(true, isExported); }
    if (match({TokenType::Function})) return functionDecl(false, isExported);
    if (match({TokenType::Class})) return classDeclaration(isExported);
    if (match({TokenType::Extends})) return extendsDeclaration();
    if (match({TokenType::Interface})) return interfaceDeclaration(isExported);
    if (match({TokenType::Import})) return importDeclaration(false);
    if (match({TokenType::From})) return importDeclaration(true);
    if (match({TokenType::Let, TokenType::Var, TokenType::Const})) return varDeclaration(isExported);
    if (isExported) throw std::runtime_error("Unexpected 'export' before statement");
    return statement();
}

StmtPtr AsulParser::importDeclaration(bool isFrom) {
    auto imp = std::make_shared<ImportStmt>();
    if (isFrom) {
        if (match({TokenType::String})) {
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

    if (match({TokenType::LeftParen})) {
        while (!check(TokenType::RightParen) && !isAtEnd()) {
            if (match({TokenType::String})) {
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
        auto tok = pathParts.back();
        std::string shorthandPkg = tok.lexeme;
        ImportStmt::Entry e; e.packageName = shorthandPkg; e.symbol = std::string("__module__"); e.isFile = false; e.line = tok.line; e.column = tok.column; e.length = tok.length;
        if (match({TokenType::As})) {
            e.alias = consume(TokenType::Identifier, "Expect alias name").lexeme;
        }
        imp->entries.push_back(e);
        consume(TokenType::Semicolon, "Expect ';' after import statement");
        return imp;
    }
}

StmtPtr AsulParser::interfaceDeclaration(bool isExported) {
    auto nameTok = consume(TokenType::Identifier, "Expect interface name");
    auto st = std::make_shared<InterfaceStmt>(); st->name = nameTok.lexeme; st->isExported = isExported;
    if (match({TokenType::Semicolon})) return st;
    consume(TokenType::LeftBrace, "Expect '{' before interface body");
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        (void)match({TokenType::Async});
        (void)match({TokenType::Function});
        auto mname = consume(TokenType::Identifier, "Expect method name").lexeme;
        consume(TokenType::LeftParen, "Expect '('");
        if (!check(TokenType::RightParen)) {
            do {
                (void)consume(TokenType::Identifier, "Expect parameter name");
                if (match({TokenType::Colon})) { (void)consume(TokenType::Identifier, "Expect type name after ':'"); }
            } while (match({TokenType::Comma}));
        }
        consume(TokenType::RightParen, "Expect ')'");
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
    (void)match({TokenType::Semicolon});
    return st;
}

StmtPtr AsulParser::classDeclaration(bool isExported) {
    auto nameTok = consume(TokenType::Identifier, "Expect class name");
    auto cls = std::make_shared<ClassStmt>();
    cls->name = nameTok.lexeme;
    cls->isExported = isExported;
    if (match({TokenType::Semicolon})) return cls;
    if (match({TokenType::LeftArrow}) || match({TokenType::Extends})) {
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
            std::optional<std::string> retType = std::nullopt;
            if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "Expect return type name after ':'").lexeme;
            auto body = statement();
            cls->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, retType, isStatic));
        }
        consume(TokenType::RightBrace, "Expect '}' after class body");
        (void)match({TokenType::Semicolon});
    }
    return cls;
}

StmtPtr AsulParser::extendsDeclaration() {
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
        std::optional<std::string> retType = std::nullopt;
        if (match({TokenType::Colon})) retType = consume(TokenType::Identifier, "Expect return type name after ':'").lexeme;
        auto body = statement();
        ext->methods.push_back(std::make_shared<FunctionStmt>(mname, params, body, isAsync, retType));
    }
    consume(TokenType::RightBrace, "Expect '}' after extension body");
    (void)match({TokenType::Semicolon});
    return ext;
}

StmtPtr AsulParser::functionDecl(bool isAsync, bool isExported) {
    auto name = consume(TokenType::Identifier, "Expect function name").lexeme;
    consume(TokenType::LeftParen, "Expect '('");
    std::vector<Param> params;
    bool hasRest = false;
    bool hasDefault = false;
    if (!check(TokenType::RightParen)) {
        do {
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
            
            if (isRest && !check(TokenType::RightParen)) {
                throw std::runtime_error("Rest parameter must be last");
            }
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightParen, "Expect ')'");
    std::optional<std::string> retType = std::nullopt;
    if (match({TokenType::Colon, TokenType::Arrow})) retType = consume(TokenType::Identifier, "Expect return type name after ':' or '->'").lexeme;
    auto body = statement();
    return std::make_shared<FunctionStmt>(name, params, body, isAsync, retType, false, isExported);
}

StmtPtr AsulParser::varDeclaration(bool isExported) {
    auto name = consume(TokenType::Identifier, "Expect variable name").lexeme;
    std::optional<std::string> type = std::nullopt;
    ExprPtr typeExpr = nullptr;
    if (match({TokenType::Colon})) {
        typeExpr = logicalOr();
    }
    ExprPtr init;
    if (match({TokenType::Equal})) init = expression();
    consume(TokenType::Semicolon, "Expect ';' after variable declaration");
    return std::make_shared<VarDecl>(name, type, typeExpr, init, isExported);
}

// Statement parsing
StmtPtr AsulParser::statement() {
    if (match({TokenType::If})) return ifStatement();
    if (match({TokenType::While})) return whileStatement();
    if (match({TokenType::Do})) return doWhileStatement();
    if (match({TokenType::For})) return forStatement();
    if (match({TokenType::ForEach})) return forEachStatement();
    if (match({TokenType::Switch})) return switchStatement();
    if (match({TokenType::Return})) return returnStatement();
    if (match({TokenType::Throw})) { auto v = expression(); consume(TokenType::Semicolon, "Expect ';' after throw"); return std::make_shared<ThrowStmt>(v); }
    if (match({TokenType::Semicolon})) { return std::make_shared<EmptyStmt>(); }
    if (match({TokenType::Try})) {
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

StmtPtr AsulParser::forStatement() {
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

StmtPtr AsulParser::forEachStatement() {
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

StmtPtr AsulParser::switchStatement() {
    consume(TokenType::LeftParen, "Expect '(' after 'switch'");
    ExprPtr expr = expression();
    consume(TokenType::RightParen, "Expect ')' after switch expression");
    consume(TokenType::LeftBrace, "Expect '{' after switch header");
    
    std::vector<SwitchStmt::CaseClause> cases;
    
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        if (match({TokenType::Case})) {
            ExprPtr caseValue = expression();
            consume(TokenType::Colon, "Expect ':' after case value");
            
            std::vector<StmtPtr> caseBody;
            while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
                caseBody.push_back(statement());
            }
            
            cases.push_back({caseValue, caseBody});
        } else if (match({TokenType::Default})) {
            consume(TokenType::Colon, "Expect ':' after 'default'");
            
            std::vector<StmtPtr> defaultBody;
            while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RightBrace) && !isAtEnd()) {
                defaultBody.push_back(statement());
            }
            
            cases.push_back({nullptr, defaultBody});
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

StmtPtr AsulParser::returnStatement() {
    Token kw = previous();
    ExprPtr val;
    if (!check(TokenType::Semicolon)) val = expression();
    consume(TokenType::Semicolon, "Expect ';' after return value");
    return std::make_shared<ReturnStmt>(kw, val);
}

StmtPtr AsulParser::ifStatement() {
    consume(TokenType::LeftParen, "Expect '('");
    auto cond = expression();
    consume(TokenType::RightParen, "Expect ')'");
    auto thenB = statement();
    StmtPtr elseB;
    if (match({TokenType::Else})) elseB = statement();
    return std::make_shared<IfStmt>(cond, thenB, elseB);
}

StmtPtr AsulParser::whileStatement() {
    consume(TokenType::LeftParen, "Expect '('");
    auto cond = expression();
    consume(TokenType::RightParen, "Expect ')'");
    auto body = statement();
    return std::make_shared<WhileStmt>(cond, body);
}

StmtPtr AsulParser::doWhileStatement() {
    auto body = statement();
    consume(TokenType::While, "Expect 'while' after do-loop body");
    consume(TokenType::LeftParen, "Expect '(' after 'while'");
    auto cond = expression();
    consume(TokenType::RightParen, "Expect ')' after condition");
    consume(TokenType::Semicolon, "Expect ';' after do-while condition");
    return std::make_shared<DoWhileStmt>(cond, body);
}

std::vector<StmtPtr> AsulParser::block() {
    std::vector<StmtPtr> stmts;
    while (!check(TokenType::RightBrace) && !isAtEnd()) stmts.push_back(declaration());
    consume(TokenType::RightBrace, "Expect '}' after block");
    return stmts;
}

StmtPtr AsulParser::expressionStatement() {
    auto expr = expression();
    consume(TokenType::Semicolon, "Expect ';' after expression");
    return std::make_shared<ExprStmt>(expr);
}

// Expression parsing
ExprPtr AsulParser::expression() { return assignment(); }

ExprPtr AsulParser::assignment() {
    auto expr = conditional();
    
    if (match({TokenType::PlusEqual, TokenType::MinusEqual, TokenType::StarEqual, TokenType::SlashEqual, TokenType::PercentEqual})) {
        Token op = previous();
        auto value = assignment();
        
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

ExprPtr AsulParser::conditional() {
    auto expr = logicalOr();
    
    if (match({TokenType::Question})) {
        Token questionToken = previous();
        auto thenBranch = expression();
        
        if (!match({TokenType::Colon})) {
            const Token& tok = peek();
            std::ostringstream oss;
            oss << "Expect ':' after then branch in ternary operator at line " << tok.line << "\n";
            oss << getLineText(tok.line) << "\n" << std::string(tok.column > 1 ? tok.column - 1 : 0, ' ') << std::string(std::max(1, tok.length), '^');
            throw std::runtime_error(oss.str());
        }
        
        auto elseBranch = conditional();
        return std::make_shared<ConditionalExpr>(expr, thenBranch, elseBranch, questionToken.line, questionToken.column, std::max(1, questionToken.length));
    }
    
    return expr;
}

ExprPtr AsulParser::logicalOr() {
    auto expr = logicalAnd();
    while (match({TokenType::OrOr})) {
        Token op = previous();
        auto right = logicalAnd();
        expr = std::make_shared<LogicalExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::logicalAnd() {
    auto expr = bitwiseOr();
    while (match({TokenType::AndAnd})) {
        Token op = previous();
        auto right = bitwiseOr();
        expr = std::make_shared<LogicalExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::bitwiseOr() {
    auto expr = bitwiseXor();
    while (match({TokenType::Pipe})) {
        Token op = previous();
        auto right = bitwiseXor();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::bitwiseXor() {
    auto expr = bitwiseAnd();
    while (match({TokenType::Caret})) {
        Token op = previous();
        auto right = bitwiseAnd();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::bitwiseAnd() {
    auto expr = equality();
    while (match({TokenType::Ampersand})) {
        Token op = previous();
        auto right = equality();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::equality() {
    auto expr = comparison();
    while (match({TokenType::BangEqual, TokenType::EqualEqual, TokenType::StrictEqual, TokenType::StrictNotEqual})) {
        Token op = previous();
        auto right = comparison();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::comparison() {
    auto expr = shift();
    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual, TokenType::MatchInterface})) {
        Token op = previous();
        auto right = shift();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::shift() {
    auto expr = term();
    while (match({TokenType::ShiftLeft, TokenType::ShiftRight})) {
        Token op = previous();
        auto right = term();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::term() {
    auto expr = factor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        Token op = previous();
        auto right = factor();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::factor() {
    auto expr = unary();
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        Token op = previous();
        auto right = unary();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }
    return expr;
}

ExprPtr AsulParser::unary() {
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

ExprPtr AsulParser::postfix() {
    auto expr = call();
    if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
        Token op = previous();
        return std::make_shared<UpdateExpr>(op, expr, false, op.line, op.column, std::max(1, op.length));
    }
    return expr;
}

ExprPtr AsulParser::finishCall(ExprPtr callee) {
    std::vector<ExprPtr> args;
    if (!check(TokenType::RightParen)) {
        do { args.push_back(expression()); } while (match({TokenType::Comma}));
    }
    Token rp = consume(TokenType::RightParen, "Expect ')' after arguments");
    return std::make_shared<CallExpr>(callee, args, rp.line, rp.column, std::max(1, rp.length));
}

ExprPtr AsulParser::call() {
    auto expr = primary();
    for (;;) {
        if (match({TokenType::LeftParen})) expr = finishCall(expr);
        else if (match({TokenType::Dot})) {
            std::string name; Token nameTok;
            if (check(TokenType::Identifier)) { nameTok = advance(); name = nameTok.lexeme; }
            else if (check(TokenType::Catch)) { nameTok = advance(); name = nameTok.lexeme; }
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

ExprPtr AsulParser::primary() {
    // Lambda: [](x, y){ ... }
    if (check(TokenType::LeftBracket)) {
        if (current + 2 < tokens.size() && tokens[current].type == TokenType::LeftBracket && tokens[current+1].type == TokenType::RightBracket && tokens[current+2].type == TokenType::LeftParen) {
            advance(); // [
            advance(); // ]
            advance(); // (
            std::vector<Param> params;
            bool hasRest = false;
            bool hasDefault = false;
            if (!check(TokenType::RightParen)) {
                do {
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

ExprPtr AsulParser::parseInterpolatedString(const std::string& s, int line, int column, int length) {
    std::vector<ExprPtr> parts;
    std::string raw;
    auto flushRaw = [&](){ if (!raw.empty()) { parts.push_back(std::make_shared<LiteralExpr>(Value{raw})); raw.clear(); } };
    for (size_t i=0;i<s.size();) {
        if (s[i] == '$' && i+1 < s.size() && s[i+1] == '{') {
            flushRaw();
            size_t startPos = i;
            i += 2;
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
            int interpolationColumn = column + 1 + startPos;
            int interpolationLength = i - startPos;
            parts.push_back(parseExprSnippet(exprText, line, interpolationColumn, interpolationLength));
            continue;
        }
        raw.push_back(s[i]);
        ++i;
    }
    flushRaw();
    if (parts.empty()) return std::make_shared<LiteralExpr>(Value{std::string("")});
    ExprPtr acc = parts[0];
    for (size_t i=1;i<parts.size();++i) {
        Token plusTok{TokenType::Plus, "+", line};
        acc = std::make_shared<BinaryExpr>(acc, plusTok, parts[i]);
    }
    return acc;
}

ExprPtr AsulParser::parseExprSnippet(const std::string& code, int line, int column, int length) {
    std::string snippet = "(";
    snippet += code;
    snippet += ")";
    snippet.push_back(';');
    Lexer lx(snippet);
    auto toks = lx.scanTokens();
    AsulParser sub(toks, snippet);
    std::vector<StmtPtr> stmts;
    try {
        stmts = sub.parse();
    } catch (const std::exception& e) {
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
