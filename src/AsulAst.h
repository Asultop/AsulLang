#ifndef ASUL_AST_H
#define ASUL_AST_H

#include "AsulLexer.h"
#include "AsulRuntime.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace asul {

// ----------- AST Nodes -----------

// Forward declarations (already in AsulRuntime.h but redeclared for clarity)
struct Stmt;
struct Expr;
using StmtPtr = std::shared_ptr<Stmt>;
using ExprPtr = std::shared_ptr<Expr>;

// ----------- Expressions -----------
struct Expr { virtual ~Expr() = default; };
struct LiteralExpr : Expr { Value value; explicit LiteralExpr(Value v): value(std::move(v)){} };
struct VariableExpr : Expr { std::string name; int line{0}; int column{1}; int length{1}; VariableExpr(std::string n, int l, int c, int len): name(std::move(n)), line(l), column(c), length(len){} };
struct AssignExpr : Expr { std::string name; ExprPtr value; int line{0}; AssignExpr(std::string n, ExprPtr v, int l): name(std::move(n)), value(std::move(v)), line(l){} };
struct DestructuringPattern; using PatternPtr = std::shared_ptr<DestructuringPattern>;
struct DestructuringAssignExpr : Expr { PatternPtr pattern; ExprPtr value; int line{0}; DestructuringAssignExpr(PatternPtr p, ExprPtr v, int l): pattern(std::move(p)), value(std::move(v)), line(l){} };
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
	struct Prop { bool computed; bool isSpread{false}; std::string name; ExprPtr keyExpr; ExprPtr value; int line{0}, column{1}, length{1}; };
	std::vector<Prop> props;
	explicit ObjectLiteralExpr(std::vector<Prop> p): props(std::move(p)){}
};
struct SpreadExpr : Expr { ExprPtr expr; int line{0}, column{1}, length{1}; explicit SpreadExpr(ExprPtr e, int l=0, int c=1, int len=1): expr(std::move(e)), line(l), column(c), length(len){} };
struct AwaitExpr : Expr { ExprPtr expr; int line{0}, column{1}, length{1}; explicit AwaitExpr(ExprPtr e, int l=0, int c0=1, int len=1): expr(std::move(e)), line(l), column(c0), length(len){} };
struct OptionalChainingExpr : Expr { ExprPtr object; std::string name; int line{0}, column{1}, length{1}; OptionalChainingExpr(ExprPtr o, std::string n, int l, int c0, int len): object(std::move(o)), name(std::move(n)), line(l), column(c0), length(len){} };
struct YieldExpr : Expr { ExprPtr value; bool isDelegate{false}; int line{0}, column{1}, length{1}; YieldExpr(ExprPtr v, bool delegate, int l, int c0, int len): value(std::move(v)), isDelegate(delegate), line(l), column(c0), length(len){} };

// Destructuring patterns
struct DestructuringPattern {
	virtual ~DestructuringPattern() = default;
};

struct IdentifierPattern : DestructuringPattern {
	std::string name;
	ExprPtr defaultValue{nullptr};
	explicit IdentifierPattern(std::string n, ExprPtr def = nullptr): name(std::move(n)), defaultValue(std::move(def)) {}
};

struct ArrayPattern : DestructuringPattern {
	std::vector<PatternPtr> elements;
	bool hasRest{false};
	std::string restName;
	explicit ArrayPattern(std::vector<PatternPtr> e, bool rest = false, std::string rn = ""): elements(std::move(e)), hasRest(rest), restName(std::move(rn)) {}
};

struct ObjectPattern : DestructuringPattern {
	struct Property {
		std::string key;
		PatternPtr pattern;
		ExprPtr defaultValue{nullptr};
	};
	std::vector<Property> properties;
	bool hasRest{false};
	std::string restName;
	explicit ObjectPattern(std::vector<Property> p, bool rest = false, std::string rn = ""): properties(std::move(p)), hasRest(rest), restName(std::move(rn)) {}
};

// Parameter for function definitions
struct Param { 
	std::string name; 
	std::optional<std::string> type; 
	bool isRest{false};
	ExprPtr defaultValue{nullptr};  // 默认参数值
	Param(std::string n, std::optional<std::string> t = std::nullopt, bool rest = false, ExprPtr defVal = nullptr): 
		name(std::move(n)), type(std::move(t)), isRest(rest), defaultValue(std::move(defVal)) {} 
};

struct FunctionExpr : Expr { std::vector<Param> params; StmtPtr body; bool isGenerator{false}; FunctionExpr(std::vector<Param> p, StmtPtr b, bool g=false): params(std::move(p)), body(std::move(b)), isGenerator(g){} };

// ----------- Statements -----------
struct Stmt { virtual ~Stmt() = default; int line{0}; int column{1}; int length{1}; };
struct ExprStmt : Stmt { ExprPtr expr; explicit ExprStmt(ExprPtr e): expr(std::move(e)){} };
struct VarDecl : Stmt { std::string name; std::optional<std::string> type; ExprPtr typeExpr; ExprPtr init; bool isExported{false}; VarDecl(std::string n, std::optional<std::string> t, ExprPtr te, ExprPtr i, bool exported=false, int l=0, int c=1, int len=1): name(std::move(n)), type(std::move(t)), typeExpr(std::move(te)), init(std::move(i)), isExported(exported){ line=l; column=c; length=len; } };
struct VarDeclDestructuring : Stmt { PatternPtr pattern; ExprPtr init; bool isExported{false}; VarDeclDestructuring(PatternPtr p, ExprPtr i, bool exported=false, int l=0, int c=1, int len=1): pattern(std::move(p)), init(std::move(i)), isExported(exported){ line=l; column=c; length=len; } };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; explicit BlockStmt(std::vector<StmtPtr> s, int l=0, int c=1, int len=1): statements(std::move(s)){ line=l; column=c; length=len; } };
struct IfStmt : Stmt { ExprPtr cond; StmtPtr thenB; StmtPtr elseB; IfStmt(ExprPtr c, StmtPtr t, StmtPtr e, int l=0, int col=1, int len=1): cond(std::move(c)), thenB(std::move(t)), elseB(std::move(e)){ line=l; column=col; length=len; } };
struct WhileStmt : Stmt { ExprPtr cond; StmtPtr body; WhileStmt(ExprPtr c, StmtPtr b, int l=0, int col=1, int len=1): cond(std::move(c)), body(std::move(b)){ line=l; column=col; length=len; } };
struct DoWhileStmt : Stmt { ExprPtr cond; StmtPtr body; DoWhileStmt(ExprPtr c, StmtPtr b, int l=0, int col=1, int len=1): cond(std::move(c)), body(std::move(b)){ line=l; column=col; length=len; } };
struct ReturnStmt : Stmt { Token keyword; ExprPtr value; ReturnStmt(Token k, ExprPtr v, int l=0, int c=1, int len=1): keyword(std::move(k)), value(std::move(v)){ line=l; column=c; length=len; } };
struct FunctionStmt : Stmt { std::string name; std::vector<Param> params; StmtPtr body; bool isAsync{false}; bool isGenerator{false}; std::optional<std::string> returnType; bool isStatic{false}; bool isExported{false}; std::vector<ExprPtr> decorators; FunctionStmt(std::string n, std::vector<Param> p, StmtPtr b, bool a=false, bool g=false, std::optional<std::string> r = std::nullopt, bool s=false, bool exported=false, int l=0, int c=1, int len=1, std::vector<ExprPtr> decs = {}): name(std::move(n)), params(std::move(p)), body(std::move(b)), isAsync(a), isGenerator(g), returnType(std::move(r)), isStatic(s), isExported(exported), decorators(std::move(decs)){ line=l; column=c; length=len; } };
struct ClassStmt : Stmt { std::string name; std::vector<std::string> superNames; std::vector<std::shared_ptr<FunctionStmt>> methods; bool isExported{false}; };
struct ExtendStmt : Stmt { std::string name; std::vector<std::shared_ptr<FunctionStmt>> methods; };
struct InterfaceStmt : Stmt { std::string name; std::vector<std::string> methodNames; bool isExported{false}; };
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
struct TryCatchStmt : Stmt {
	StmtPtr tryBlock;
	std::string catchName;
	StmtPtr catchBlock;
	StmtPtr finallyBlock; // optional finally block
	TryCatchStmt(StmtPtr t, std::string n, StmtPtr c, StmtPtr f = nullptr)
		: tryBlock(std::move(t)), catchName(std::move(n)), catchBlock(std::move(c)), finallyBlock(std::move(f)) {}
};
struct EmptyStmt : Stmt {};
struct ImportStmt : Stmt {
	struct Entry {
		// Package import (existing behavior): symbol == "*" means wildcard
		std::string packageName;
		std::string symbol;
		// File import: when isFile == true, use filePath and ignore packageName/symbol
		bool isFile{false};
		std::string filePath; // may be relative or absolute; .alang suffix may be omitted
		std::optional<std::string> alias;
		int line{0};
		int column{1};
		int length{1};
	};
	std::vector<Entry> entries;
};

// Match expression and pattern matching
struct MatchStmt : Stmt {
	struct MatchArm {
		ExprPtr pattern; // Can be literal, variable, or special patterns
		ExprPtr guard; // Optional when condition
		StmtPtr body;
	};
	ExprPtr expr;
	std::vector<MatchArm> arms;
	MatchStmt(ExprPtr e, std::vector<MatchArm> a): expr(std::move(e)), arms(std::move(a)) {}
};

// Decorator support for functions and classes
struct DecoratorStmt : Stmt {
	std::vector<ExprPtr> decorators; // List of decorator expressions
	StmtPtr target; // The function or class being decorated
	DecoratorStmt(std::vector<ExprPtr> d, StmtPtr t): decorators(std::move(d)), target(std::move(t)) {}
};

} // namespace asul

#endif // ASUL_AST_H
