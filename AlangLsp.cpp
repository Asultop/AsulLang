#include "src/AsulLexer.h"
#include "src/AsulParser.h"
#include "src/AsulAst.h"
#include "src/AsulPackages/AsulPackages.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <unordered_map>
#include <fstream>

namespace alang_lsp {

struct Json;
using JsonObject = std::map<std::string, Json>;
using JsonArray = std::vector<Json>;

struct Json {
	using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;
	Value value;

	Json() : value(nullptr) {}
	Json(std::nullptr_t) : value(nullptr) {}
	Json(bool v) : value(v) {}
	Json(double v) : value(v) {}
	Json(std::string v) : value(std::move(v)) {}
	Json(const char* v) : value(std::string(v)) {}
	Json(JsonArray v) : value(std::move(v)) {}
	Json(JsonObject v) : value(std::move(v)) {}

	bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
	bool isBool() const { return std::holds_alternative<bool>(value); }
	bool isNumber() const { return std::holds_alternative<double>(value); }
	bool isString() const { return std::holds_alternative<std::string>(value); }
	bool isArray() const { return std::holds_alternative<JsonArray>(value); }
	bool isObject() const { return std::holds_alternative<JsonObject>(value); }

	const JsonObject* asObject() const { return std::get_if<JsonObject>(&value); }
	JsonObject* asObject() { return std::get_if<JsonObject>(&value); }
	const JsonArray* asArray() const { return std::get_if<JsonArray>(&value); }
	const std::string* asString() const { return std::get_if<std::string>(&value); }
	const double* asNumber() const { return std::get_if<double>(&value); }
};

static void jsonSkipWs(const std::string& s, size_t& i) {
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
}

static std::string jsonParseString(const std::string& s, size_t& i) {
	if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON: 期望 '""'");
	i++;
	std::string out;
	while (i < s.size()) {
		char c = s[i++];
		if (c == '"') return out;
		if (c == '\\') {
			if (i >= s.size()) throw std::runtime_error("JSON: 错误的转义字符");
			char e = s[i++];
			switch (e) {
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\r'); break;
			case 't': out.push_back('\t'); break;
			case 'u': {
				if (i + 4 > s.size()) throw std::runtime_error("JSON: 无效的 unicode 转义");
				unsigned int code = 0;
				for (int k = 0; k < 4; k++) {
					char h = s[i++];
					code <<= 4;
					if (h >= '0' && h <= '9') code += h - '0';
					else if (h >= 'a' && h <= 'f') code += 10 + (h - 'a');
					else if (h >= 'A' && h <= 'F') code += 10 + (h - 'A');
					else throw std::runtime_error("JSON: 无效的 unicode 十六进制");
				}
				// Minimal UTF-8 encode for BMP
				if (code <= 0x7F) out.push_back(static_cast<char>(code));
				else if (code <= 0x7FF) {
					out.push_back(static_cast<char>(0xC0 | ((code >> 6) & 0x1F)));
					out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				} else {
					out.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
					out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
				break;
			}
			default: out.push_back(e); break;
			}
		} else {
			out.push_back(c);
		}
	}
	throw std::runtime_error("JSON: 未终止的字符串");
}

static Json jsonParseValue(const std::string& s, size_t& i);

static Json jsonParseNumber(const std::string& s, size_t& i) {
	size_t start = i;
	if (i < s.size() && s[i] == '-') i++;
	while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
	if (i < s.size() && s[i] == '.') {
		i++;
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
	}
	if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
	}
	double v = 0.0;
	try {
		v = std::stod(s.substr(start, i - start));
	} catch (...) {
		throw std::runtime_error("JSON: 无效的数字");
	}
	return Json(v);
}

static Json jsonParseArray(const std::string& s, size_t& i) {
	JsonArray arr;
	i++; // [
	jsonSkipWs(s, i);
	if (i < s.size() && s[i] == ']') {
		i++;
		return Json(std::move(arr));
	}
	for (;;) {
		jsonSkipWs(s, i);
		arr.push_back(jsonParseValue(s, i));
		jsonSkipWs(s, i);
		if (i < s.size() && s[i] == ',') {
			i++;
			continue;
		}
		if (i < s.size() && s[i] == ']') {
			i++;
			break;
		}
		throw std::runtime_error("JSON: 期望 ',' 或 ']'");
	}
	return Json(std::move(arr));
}

static Json jsonParseObject(const std::string& s, size_t& i) {
	JsonObject obj;
	i++; // {
	jsonSkipWs(s, i);
	if (i < s.size() && s[i] == '}') {
		i++;
		return Json(std::move(obj));
	}
	for (;;) {
		jsonSkipWs(s, i);
		if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON: 期望字符串键");
		std::string key = jsonParseString(s, i);
		jsonSkipWs(s, i);
		if (i >= s.size() || s[i] != ':') throw std::runtime_error("JSON: 期望 ':'");
		i++;
		jsonSkipWs(s, i);
		obj[key] = jsonParseValue(s, i);
		jsonSkipWs(s, i);
		if (i < s.size() && s[i] == ',') {
			i++;
			continue;
		}
		if (i < s.size() && s[i] == '}') {
			i++;
			break;
		}
		throw std::runtime_error("JSON: 期望 ',' 或 '}'");
	}
	return Json(std::move(obj));
}

static Json jsonParseValue(const std::string& s, size_t& i) {
	jsonSkipWs(s, i);
	if (i >= s.size()) throw std::runtime_error("JSON: 意外的结尾");
	char c = s[i];
	if (c == '{') return jsonParseObject(s, i);
	if (c == '[') return jsonParseArray(s, i);
	if (c == '"') return Json(jsonParseString(s, i));
	if (c == 't' && s.substr(i, 4) == "true") {
		i += 4;
		return Json(true);
	}
	if (c == 'f' && s.substr(i, 5) == "false") {
		i += 5;
		return Json(false);
	}
	if (c == 'n' && s.substr(i, 4) == "null") {
		i += 4;
		return Json(nullptr);
	}
	if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return jsonParseNumber(s, i);
	throw std::runtime_error("JSON: 意外的 token");
}

static Json jsonParse(const std::string& s) {
	size_t i = 0;
	Json v = jsonParseValue(s, i);
	jsonSkipWs(s, i);
	if (i != s.size()) throw std::runtime_error("JSON: 尾部有多余字符");
	return v;
}

static std::string jsonEscape(const std::string& in) {
	std::ostringstream o;
	o << '"';
	for (char c : in) {
		switch (c) {
		case '"': o << "\\\""; break;
		case '\\': o << "\\\\"; break;
		case '\b': o << "\\b"; break;
		case '\f': o << "\\f"; break;
		case '\n': o << "\\n"; break;
		case '\r': o << "\\r"; break;
		case '\t': o << "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
				  << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
			} else {
				o << c;
			}
			break;
		}
	}
	o << '"';
	return o.str();
}

static std::string jsonDump(const Json& v);

static std::string jsonDumpObject(const JsonObject& obj) {
	std::ostringstream o;
	o << '{';
	bool first = true;
	for (const auto& kv : obj) {
		if (!first) o << ',';
		first = false;
		o << jsonEscape(kv.first) << ':' << jsonDump(kv.second);
	}
	o << '}';
	return o.str();
}

static std::string jsonDumpArray(const JsonArray& arr) {
	std::ostringstream o;
	o << '[';
	for (size_t i = 0; i < arr.size(); i++) {
		if (i) o << ',';
		o << jsonDump(arr[i]);
	}
	o << ']';
	return o.str();
}

static std::string jsonDump(const Json& v) {
	if (std::holds_alternative<std::nullptr_t>(v.value)) return "null";
	if (auto b = std::get_if<bool>(&v.value)) return *b ? "true" : "false";
	if (auto n = std::get_if<double>(&v.value)) {
		std::ostringstream o;
		o << *n;
		return o.str();
	}
	if (auto s = std::get_if<std::string>(&v.value)) return jsonEscape(*s);
	if (auto a = std::get_if<JsonArray>(&v.value)) return jsonDumpArray(*a);
	if (auto o = std::get_if<JsonObject>(&v.value)) return jsonDumpObject(*o);
	return "null";
}

static const Json* jsonGet(const JsonObject& obj, const std::string& key) {
	auto it = obj.find(key);
	return it == obj.end() ? nullptr : &it->second;
}

static std::optional<int> jsonAsInt(const Json& v) {
	if (auto n = v.asNumber()) return static_cast<int>(*n);
	return std::nullopt;
}

// ---------------- LSP transport ----------------

static bool readLspMessage(std::istream& in, std::string& out) {
	std::string line;
	int contentLength = -1;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) break;
		constexpr const char* kPrefix = "Content-Length:";
		if (line.rfind(kPrefix, 0) == 0) {
			std::string v = line.substr(std::string(kPrefix).size());
			v.erase(v.begin(), std::find_if(v.begin(), v.end(), [](unsigned char c) { return !std::isspace(c); }));
			contentLength = std::atoi(v.c_str());
		}
	}
	if (contentLength <= 0) return false;
	out.assign(static_cast<size_t>(contentLength), '\0');
	in.read(&out[0], contentLength);
	return in.gcount() == contentLength;
}

static void writeLspMessage(std::ostream& out, const std::string& jsonBody) {
	out << "Content-Length: " << jsonBody.size() << "\r\n\r\n";
	out << jsonBody;
	out.flush();
}

struct Position {
	int line = 0;
	int character = 0;
};

struct Range {
	Position start;
	Position end;
};

static std::optional<std::tuple<int, int, int>> extractLineColLen(const std::string& msg) {
	// Patterns produced by AsulLexer/AsulParser:
	// "... at line X, column Y, length L"
	auto findIntAfter = [&](const std::string& needle) -> std::optional<int> {
		auto pos = msg.find(needle);
		if (pos == std::string::npos) return std::nullopt;
		pos += needle.size();
		while (pos < msg.size() && std::isspace(static_cast<unsigned char>(msg[pos]))) pos++;
		int v = 0;
		bool any = false;
		while (pos < msg.size() && std::isdigit(static_cast<unsigned char>(msg[pos]))) {
			any = true;
			v = v * 10 + (msg[pos] - '0');
			pos++;
		}
		if (!any) return std::nullopt;
		return v;
	};
	auto line = findIntAfter("line ");
	auto col = findIntAfter("column ");
	if (!line || !col) return std::nullopt;
	auto len = findIntAfter("length ");
	return std::make_tuple(*line, *col, len.value_or(1));
}

static Range toRange(int oneBasedLine, int oneBasedCol, int length) {
	int l = std::max(1, oneBasedLine) - 1;
	int c = std::max(1, oneBasedCol) - 1;
	int len = std::max(1, length);
	Range r;
	r.start = {l, c};
	r.end = {l, c + len};
	return r;
}

static Json makePosition(const Position& p) {
	return JsonObject{{"line", Json(static_cast<double>(p.line))}, {"character", Json(static_cast<double>(p.character))}};
}

static Json makeRange(const Range& r) {
	return JsonObject{{"start", makePosition(r.start)}, {"end", makePosition(r.end)}};
}

static Json makeDiagnostic(const Range& r, int severity, std::string message) {
	return JsonObject{
		{"range", makeRange(r)},
		{"severity", Json(static_cast<double>(severity))},
		{"source", Json("alang-lsp")},
		{"message", Json(std::move(message))},
	};
}

struct Symbol {
    std::string name;
    Range defRange;
    std::string kind; // "var", "func", "param", "class"
    int minParams = 0;
    int maxParams = 0;
    std::string uri;
    std::string typeName;
};

struct Reference {
    Range range;
    std::string targetUri;
    Range targetRange;
};

struct SemanticData {
    std::vector<Json> diagnostics;
    std::vector<Reference> references;
};

std::map<std::string, SemanticData> g_documentSemantics;

class SemanticAnalyzer {
    std::string uri;
    struct Scope {
        std::map<std::string, Symbol> symbols;
        std::shared_ptr<Scope> parent;
    };
    std::shared_ptr<Scope> currentScope;
    SemanticData data;

    struct TypeDef {
        std::string name;
        std::map<std::string, int> methods; // name -> minParams (-1 for property/var)
    };
    std::map<std::string, TypeDef> builtInTypes;
    std::map<std::string, std::vector<std::string>> packageExports;

public:
    SemanticAnalyzer(std::string u) : uri(std::move(u)) {
        currentScope = std::make_shared<Scope>();
        initBuiltins();
        // Add common builtins
        addBuiltin("print", -1);
        addBuiltin("println", -1);
        addBuiltin("len", 1);
        addBuiltin("range", -1);
        addBuiltin("push", -1);
        addBuiltin("pop", -1);
        addBuiltin("shift", -1);
        addBuiltin("unshift", -1);
        addBuiltin("slice", -1);
        addBuiltin("typeof", 1);
        addBuiltin("eval", 1);
        addBuiltin("quote", 1);
        addBuiltin("isArray", 1);
        addBuiltin("isObject", 1);
        addBuiltin("isFunction", 1);
        addBuiltin("isString", 1);
        addBuiltin("isNumber", 1);
        addBuiltin("isBoolean", 1);
        addBuiltin("isNull", 1);
        addBuiltin("sleep", 1);
        addBuiltin("setTimeout", -1);
        addBuiltin("setInterval", -1);
        addBuiltin("clearTimeout", 1);
        addBuiltin("clearInterval", 1);
        addBuiltin("parseInt", -1);
        addBuiltin("parseFloat", 1);
        addBuiltin("isNaN", 1);
        addBuiltin("isFinite", 1);
        addBuiltin("encodeURI", 1);
        addBuiltin("decodeURI", 1);
        addBuiltin("encodeURIComponent", 1);
        addBuiltin("decodeURIComponent", 1);
        addBuiltin("assert", -1);
        addBuiltin("type", 1);
        addBuiltin("str", 1);
        addBuiltin("chr", 1);
        addBuiltin("ord", 1);
        addBuiltin("min", -1);
        addBuiltin("max", -1);
        addBuiltin("abs", 1);
        addBuiltin("floor", 1);
        addBuiltin("ceil", 1);
        addBuiltin("round", 1);
        addBuiltin("sqrt", 1);
        addBuiltin("pow", 2);
        
        // Add 'this' as a special symbol in global scope (will be available everywhere)
        defineSymbol("this", {{0,0},{0,0}}, "keyword");
    }

    void initBuiltins() {
        // Array
        TypeDef arrayType;
        arrayType.name = "Array";
        arrayType.methods = {
            {"push", 1}, {"pop", 0}, {"shift", 0}, {"unshift", 1},
            {"map", 1}, {"filter", 1}, {"reduce", 1}, {"forEach", 1},
            {"find", 1}, {"findIndex", 1}, {"join", 0}, {"slice", 0},
            {"splice", 2}, {"includes", 1}, {"indexOf", 1},
            {"reverse", 0}, {"sort", 0}, {"length", -1}, {"len", 0},
            {"flat", -1}, {"flatMap", 1}, {"some", 1}, {"every", 1}
        };
        builtInTypes["Array"] = arrayType;

        // String
        TypeDef stringType;
        stringType.name = "String";
        stringType.methods = {
            {"length", -1}, {"split", 1}, {"trim", 0}, {"substring", 1},
            {"substr", 1}, {"replace", 2}, {"replaceAll", 2},
            {"indexOf", 1}, {"lastIndexOf", 1}, {"startsWith", 1},
            {"endsWith", 1}, {"toLowerCase", 0}, {"toUpperCase", 0},
            {"charCodeAt", 1}, {"len", 0}
        };
        builtInTypes["String"] = stringType;

        // Number
        TypeDef numberType;
        numberType.name = "Number";
        numberType.methods = {
            {"toFixed", 0}, {"toPrecision", 0}, {"toExponential", 0},
            {"toString", 0}, {"valueOf", 0}
        };
        builtInTypes["Number"] = numberType;

        // Object  
        TypeDef objectType;
        objectType.name = "Object";
        objectType.methods = {
            {"keys", 0}, {"values", 0}, {"entries", 0},
            {"hasOwnProperty", 1}, {"toString", 0}
        };
        builtInTypes["Object"] = objectType;

        // Initialize package exports
        packageExports["std.array"] = {"flat", "flatMap", "unique", "chunk", "groupBy", "zip", "diff"};
        packageExports["std.collections"] = {
            "Map", "map", "Set", "set", "Deque", "deque", 
            "Stack", "stack", "PriorityQueue", "priorityQueue", "binarySearch"
        };

        // Map
        TypeDef mapType;
        mapType.name = "Map";
        mapType.methods = {
            {"set", 2}, {"get", 1}, {"has", 1}, {"delete", 1},
            {"size", 0}, {"clear", 0}, {"keys", 0}, {"values", 0}, {"entries", 0}
        };
        builtInTypes["Map"] = mapType;

        // Set
        TypeDef setType;
        setType.name = "Set";
        setType.methods = {
            {"add", 1}, {"has", 1}, {"delete", 1}, {"size", 0},
            {"values", 0}, {"union", 1}, {"intersection", 1}, {"difference", 1}
        };
        builtInTypes["Set"] = setType;

        // Deque
        TypeDef dequeType;
        dequeType.name = "Deque";
        dequeType.methods = {
            {"push", 1}, {"pop", 0}, {"unshift", 1}, {"shift", 0},
            {"peek", 0}, {"size", 0}, {"clear", 0}
        };
        builtInTypes["Deque"] = dequeType;

        // Stack
        TypeDef stackType;
        stackType.name = "Stack";
        stackType.methods = {
            {"push", 1}, {"pop", 0}, {"peek", 0}, {"size", 0}
        };
        builtInTypes["Stack"] = stackType;

        // PriorityQueue
        TypeDef pqType;
        pqType.name = "PriorityQueue";
        pqType.methods = {
            {"push", 2}, {"pop", 0}, {"peek", 0}, {"size", 0}
        };
        builtInTypes["PriorityQueue"] = pqType;

        // Load metadata from AsulPackages
        const auto& packages = asul::getPackageMetadata();
        for (const auto& pkg : packages) {
            packageExports[pkg.name] = pkg.exports;
            for (const auto& cls : pkg.classes) {
                TypeDef td;
                td.name = cls.name;
				for (const auto& m : cls.methods) td.methods[m.name] = m.minParams;
                builtInTypes[cls.name] = td;
            }
        }
    }

    void addBuiltin(const std::string& name, int params) {
        Symbol s;
        s.name = name;
        s.kind = "func";
        s.minParams = params == -1 ? 0 : params;
        s.maxParams = params;
        s.uri = "";
        currentScope->symbols[name] = s;
    }

    SemanticData analyze(const std::vector<asul::StmtPtr>& stmts) {
        for (const auto& stmt : stmts) {
            visitStmt(stmt);
        }
        return data;
    }

private:
    void enterScope() {
        auto newScope = std::make_shared<Scope>();
        newScope->parent = currentScope;
        currentScope = newScope;
    }

    void exitScope() {
        if (currentScope->parent) {
            currentScope = currentScope->parent;
        }
    }

    void defineSymbol(const std::string& name, Range r, const std::string& kind, int minP = 0, int maxP = 0, std::string typeName = "Any") {
        Symbol s;
        s.name = name;
        s.defRange = r;
        s.kind = kind;
        s.minParams = minP;
        s.maxParams = maxP;
        s.uri = uri;
        s.typeName = typeName;
        currentScope->symbols[name] = s;
    }

    std::optional<Symbol> resolve(const std::string& name) {
        auto scope = currentScope;
        while (scope) {
            auto it = scope->symbols.find(name);
            if (it != scope->symbols.end()) return it->second;
            scope = scope->parent;
        }
        return std::nullopt;
    }

    std::string inferType(const asul::ExprPtr& expr) {
        if (!expr) return "Any";
        if (auto e = std::dynamic_pointer_cast<asul::LiteralExpr>(expr)) {
            if (std::holds_alternative<std::string>(e->value)) return "String";
            if (std::holds_alternative<double>(e->value)) return "Number";
            if (std::holds_alternative<bool>(e->value)) return "Boolean";
        }
        if (std::dynamic_pointer_cast<asul::ArrayLiteralExpr>(expr)) return "Array";
        if (std::dynamic_pointer_cast<asul::ObjectLiteralExpr>(expr)) return "Object";
        if (auto e = std::dynamic_pointer_cast<asul::NewExpr>(expr)) {
             if (auto v = std::dynamic_pointer_cast<asul::VariableExpr>(e->callee)) return v->name;
        }
        if (auto e = std::dynamic_pointer_cast<asul::VariableExpr>(expr)) {
            auto sym = resolve(e->name);
            if (sym) return sym->typeName;
        }
        // Simple chain inference for array methods that return arrays
        if (auto e = std::dynamic_pointer_cast<asul::CallExpr>(expr)) {
            if (auto prop = std::dynamic_pointer_cast<asul::GetPropExpr>(e->callee)) {
                std::string objType = inferType(prop->object);
                if (objType == "Array") {
                    if (prop->name == "map" || prop->name == "filter" || prop->name == "slice" || 
                        prop->name == "concat" || prop->name == "reverse" || prop->name == "sort" ||
                        prop->name == "flat" || prop->name == "flatMap") {
                        return "Array";
                    }
                }
            }
        }
        return "Any";
    }

    void visitStmt(const asul::StmtPtr& stmt) {
        if (!stmt) return;
        if (auto s = std::dynamic_pointer_cast<asul::VarDecl>(stmt)) {
            std::string type = "Any";
            if (s->init) {
                visitExpr(s->init);
                type = inferType(s->init);
            }
            defineSymbol(s->name, toRange(s->line, s->column, s->length), "var", 0, 0, type);
        } else if (auto s = std::dynamic_pointer_cast<asul::FunctionStmt>(stmt)) {
            defineSymbol(s->name, toRange(s->line, s->column, s->length), "func", s->params.size(), s->params.size());
            enterScope();
            for (const auto& p : s->params) {
                defineSymbol(p.name, {{0,0},{0,0}}, "param");
                if (p.defaultValue) visitExpr(p.defaultValue);
            }
            visitStmt(s->body);
            exitScope();
        } else if (auto s = std::dynamic_pointer_cast<asul::BlockStmt>(stmt)) {
            enterScope();
            for (const auto& sub : s->statements) visitStmt(sub);
            exitScope();
        } else if (auto s = std::dynamic_pointer_cast<asul::ExprStmt>(stmt)) {
            visitExpr(s->expr);
        } else if (auto s = std::dynamic_pointer_cast<asul::ReturnStmt>(stmt)) {
            if (s->value) visitExpr(s->value);
        } else if (auto s = std::dynamic_pointer_cast<asul::IfStmt>(stmt)) {
            visitExpr(s->cond);
            visitStmt(s->thenB);
            if (s->elseB) visitStmt(s->elseB);
        } else if (auto s = std::dynamic_pointer_cast<asul::WhileStmt>(stmt)) {
            visitExpr(s->cond);
            visitStmt(s->body);
        } else if (auto s = std::dynamic_pointer_cast<asul::DoWhileStmt>(stmt)) {
            visitExpr(s->cond);
            visitStmt(s->body);
        } else if (auto s = std::dynamic_pointer_cast<asul::ForStmt>(stmt)) {
            enterScope();
            if (s->init) visitStmt(s->init);
            if (s->cond) visitExpr(s->cond);
            if (s->post) visitExpr(s->post);
            visitStmt(s->body);
            exitScope();
        } else if (auto s = std::dynamic_pointer_cast<asul::ForEachStmt>(stmt)) {
            enterScope();
            visitExpr(s->iterable);
            defineSymbol(s->varName, {{0,0},{0,0}}, "var");
            visitStmt(s->body);
            exitScope();
        } else if (auto s = std::dynamic_pointer_cast<asul::ImportStmt>(stmt)) {
            for (const auto& e : s->entries) {
                std::string name = e.alias.has_value() ? *e.alias : e.symbol;
                if (name == "*") {
                    // Handle wildcard import
                    if (packageExports.count(e.packageName)) {
                        for (const auto& exportedSym : packageExports[e.packageName]) {
                            defineSymbol(exportedSym, toRange(e.line, e.column, e.length), "import");
                        }
                    }
                } else {
                    defineSymbol(name, toRange(e.line, e.column, e.length), "import");
                }
            }
        } else if (auto s = std::dynamic_pointer_cast<asul::ClassStmt>(stmt)) {
            defineSymbol(s->name, toRange(s->line, s->column, s->length), "class");
            enterScope();
            for (const auto& m : s->methods) {
                visitStmt(m);
            }
            exitScope();
        } else if (auto s = std::dynamic_pointer_cast<asul::TryCatchStmt>(stmt)) {
            visitStmt(s->tryBlock);
            if (s->catchBlock) {
                enterScope();
                if (!s->catchName.empty()) {
                    defineSymbol(s->catchName, {{0,0},{0,0}}, "var");
                }
                visitStmt(s->catchBlock);
                exitScope();
            }
            if (s->finallyBlock) visitStmt(s->finallyBlock);
        }
    }

    void visitExpr(const asul::ExprPtr& expr) {
        if (!expr) return;
        if (auto e = std::dynamic_pointer_cast<asul::VariableExpr>(expr)) {
            // Skip 'this' keyword - it's a special built-in identifier
            if (e->name == "this") {
                return;
            }
            
            auto sym = resolve(e->name);
            Range r = toRange(e->line, e->column, e->length);
            if (!sym) {
                data.diagnostics.push_back(makeDiagnostic(r, 1, "未定义的变量: " + e->name));
            } else {
                Reference ref;
                ref.range = r;
                ref.targetUri = sym->uri;
                ref.targetRange = sym->defRange;
                data.references.push_back(ref);
            }
        } else if (auto e = std::dynamic_pointer_cast<asul::CallExpr>(expr)) {
            // Check if calling a method on an object
            if (auto prop = std::dynamic_pointer_cast<asul::GetPropExpr>(e->callee)) {
                visitExpr(prop->object);
                std::string type = inferType(prop->object);
                
                if (builtInTypes.count(type)) {
                    const auto& methods = builtInTypes[type].methods;
                    auto it = methods.find(prop->name);
                    if (it == methods.end()) {
                        // Use proper position - if prop has valid position, use it; otherwise use call position
                        Range r;
                        if (prop->line > 0 && prop->column > 0) {
                            r = toRange(prop->line, prop->column, prop->name.length());
                        } else {
                            r = toRange(e->line, e->column, e->length);
                        }
                        data.diagnostics.push_back(makeDiagnostic(r, 1, "类型 '" + type + "' 没有方法 '" + prop->name + "'"));
                    } else if (it->second >= 0) {
                        // Check parameter count for methods with known param counts
                        int expectedParams = it->second;
                        if (expectedParams > 0 && (int)e->args.size() < expectedParams) {
                            Range r = toRange(e->line, e->column, e->length);
                            data.diagnostics.push_back(makeDiagnostic(r, 2, "方法 '" + prop->name + "' 参数不足。期望至少 " + std::to_string(expectedParams) + " 个"));
                        }
                    }
                }
            } else {
                visitExpr(e->callee);
            }
            
            for (const auto& arg : e->args) visitExpr(arg);
            
            if (auto v = std::dynamic_pointer_cast<asul::VariableExpr>(e->callee)) {
                auto sym = resolve(v->name);
                if (sym && sym->kind == "func") {
                    if (sym->maxParams != -1 && (int)e->args.size() != sym->minParams) {
                         Range r = toRange(e->line, e->column, e->length);
                         data.diagnostics.push_back(makeDiagnostic(r, 2, "参数个数不正确。期望 " + std::to_string(sym->minParams) + ", 实际 " + std::to_string(e->args.size())));
                    }
                }
            }
        } else if (auto e = std::dynamic_pointer_cast<asul::GetPropExpr>(expr)) {
            visitExpr(e->object);
            // For GetPropExpr accessed as property (not method call), we're lenient
            // We don't report warnings for unknown members since they could be dynamic properties
            // Method calls are checked separately in CallExpr handler above
        } else if (auto e = std::dynamic_pointer_cast<asul::BinaryExpr>(expr)) {
            visitExpr(e->left);
            visitExpr(e->right);
        } else if (auto e = std::dynamic_pointer_cast<asul::AssignExpr>(expr)) {
            visitExpr(e->value);
            auto sym = resolve(e->name);
            if (!sym) {
                 Range r = toRange(e->line, 1, e->name.length());
                 data.diagnostics.push_back(makeDiagnostic(r, 1, "未定义的变量: " + e->name));
            }
        }
    }
};

static std::vector<Json> computeDiagnostics(const std::string& text, const std::string& uri) {
	std::vector<Json> diags;
	try {
		asul::Lexer lex(text);
		auto tokens = lex.scanTokens();
		asul::Parser parser(tokens, text);
		auto stmts = parser.parse();
		
		for (const auto& err : parser.getErrors()) {
			Range r = toRange(err.line, err.column, err.length);
			diags.push_back(makeDiagnostic(r, 1 /* Error */, err.message));
		}

        SemanticAnalyzer analyzer(uri);
        auto semanticData = analyzer.analyze(stmts);
        diags.insert(diags.end(), semanticData.diagnostics.begin(), semanticData.diagnostics.end());
        g_documentSemantics[uri] = semanticData;

	} catch (const std::exception& ex) {
		std::string msg = ex.what();
		auto loc = extractLineColLen(msg);
		Range r = loc ? toRange(std::get<0>(*loc), std::get<1>(*loc), std::get<2>(*loc)) : Range{{0, 0}, {0, 1}};
		auto nl = msg.find('\n');
		if (nl != std::string::npos) msg = msg.substr(0, nl);
		diags.push_back(makeDiagnostic(r, 1 /* Error */, msg));
	}
	return diags;
}

static void publishDiagnostics(std::ostream& out, const std::string& uri, const std::string& text) {
	JsonArray diags;
	for (auto& d : computeDiagnostics(text, uri)) diags.push_back(std::move(d));
	Json params = JsonObject{{"uri", Json(uri)}, {"diagnostics", Json(std::move(diags))}};
	Json notif = JsonObject{{"jsonrpc", Json("2.0")}, {"method", Json("textDocument/publishDiagnostics")}, {"params", params}};
	writeLspMessage(out, jsonDump(notif));
}

} // namespace alang_lsp

#include <fstream>
#include <chrono>
#include <ctime>

namespace alang_lsp {
    std::ofstream logFile;
    void log(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::tm tm = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << "] " << msg;
        
        std::string formattedMsg = oss.str();

        if (logFile.is_open()) {
            logFile << formattedMsg << std::endl;
        }
        std::cerr << formattedMsg << std::endl;
    }
// ... existing code ...
}

int main() {
    alang_lsp::logFile.open("/tmp/alang-lsp.log", std::ios::out | std::ios::app);
    alang_lsp::log("alang-lsp starting...");
	using namespace alang_lsp;
	std::map<std::string, std::string> docs;
	bool shutdownRequested = false;

	std::string body;
	while (readLspMessage(std::cin, body)) {
        alang_lsp::log("Received message: " + body);
		Json msg;
		try {
			msg = jsonParse(body);
		} catch (const std::exception& e) {
            alang_lsp::log(std::string("JSON parse error: ") + e.what());
			continue;
		}
		auto* obj = msg.asObject();
		if (!obj) continue;

		const Json* methodV = jsonGet(*obj, "method");
		std::string method = (methodV && methodV->isString()) ? *methodV->asString() : "";
        alang_lsp::log("Method: " + method);
		const Json* idV = jsonGet(*obj, "id");

		auto sendResult = [&](Json result) {
			if (!idV) return;
			JsonObject resp;
			resp["jsonrpc"] = Json("2.0");
			resp["id"] = *idV; // preserve id type
			resp["result"] = std::move(result);
            std::string dump = jsonDump(Json(std::move(resp)));
            alang_lsp::log("Sending result: " + dump);
			writeLspMessage(std::cout, dump);
		};

		if (method == "initialize") {
            alang_lsp::log("Handling initialize");
			JsonObject caps;
			caps["textDocumentSync"] = JsonObject{{"openClose", Json(true)}, {"change", Json(1.0)}}; // Full
            caps["definitionProvider"] = Json(true);
			JsonObject serverInfo{{"name", Json("alang-lsp")}, {"version", Json("0.1")}};
			sendResult(JsonObject{{"capabilities", Json(std::move(caps))}, {"serverInfo", Json(std::move(serverInfo))}});
			continue;
		}
// ... existing code ...

		if (method == "shutdown") {
			shutdownRequested = true;
			sendResult(Json(nullptr));
			continue;
		}

		if (method == "exit") {
			return shutdownRequested ? 0 : 1;
		}

        if (method == "textDocument/definition") {
            const Json* paramsV = jsonGet(*obj, "params");
            auto* params = paramsV ? paramsV->asObject() : nullptr;
            if (!params) continue;
            const Json* tdV = jsonGet(*params, "textDocument");
            auto* td = tdV ? tdV->asObject() : nullptr;
            std::string uri = (td && jsonGet(*td, "uri") && jsonGet(*td, "uri")->isString()) ? *jsonGet(*td, "uri")->asString() : "";
            
            const Json* posV = jsonGet(*params, "position");
            auto* posObj = posV ? posV->asObject() : nullptr;
            int line = (posObj && jsonGet(*posObj, "line")) ? *jsonAsInt(*jsonGet(*posObj, "line")) : -1;
            int character = (posObj && jsonGet(*posObj, "character")) ? *jsonAsInt(*jsonGet(*posObj, "character")) : -1;

            if (uri.empty() || line == -1 || character == -1) {
                sendResult(Json(nullptr));
                continue;
            }

            auto it = g_documentSemantics.find(uri);
            if (it != g_documentSemantics.end()) {
                bool found = false;
                for (const auto& ref : it->second.references) {
                    if (line == ref.range.start.line && character >= ref.range.start.character && character <= ref.range.end.character) {
                        JsonObject loc;
                        loc["uri"] = Json(ref.targetUri.empty() ? uri : ref.targetUri);
                        loc["range"] = makeRange(ref.targetRange);
                        sendResult(Json(std::move(loc)));
                        found = true;
                        break;
                    }
                }
                if (found) continue;
            }
            sendResult(Json(nullptr));
            continue;
        }

		if (method == "textDocument/didOpen") {
			const Json* paramsV = jsonGet(*obj, "params");
			auto* params = paramsV ? paramsV->asObject() : nullptr;
			if (!params) continue;
			const Json* tdV = jsonGet(*params, "textDocument");
			auto* td = tdV ? tdV->asObject() : nullptr;
			if (!td) continue;
			std::string uri = (jsonGet(*td, "uri") && jsonGet(*td, "uri")->isString()) ? *jsonGet(*td, "uri")->asString() : "";
			std::string text = (jsonGet(*td, "text") && jsonGet(*td, "text")->isString()) ? *jsonGet(*td, "text")->asString() : "";
			if (!uri.empty()) {
				docs[uri] = text;
				publishDiagnostics(std::cout, uri, text);
			}
			continue;
		}

		if (method == "textDocument/didChange") {
			const Json* paramsV = jsonGet(*obj, "params");
			auto* params = paramsV ? paramsV->asObject() : nullptr;
			if (!params) continue;
			const Json* tdV = jsonGet(*params, "textDocument");
			auto* td = tdV ? tdV->asObject() : nullptr;
			if (!td) continue;
			std::string uri = (jsonGet(*td, "uri") && jsonGet(*td, "uri")->isString()) ? *jsonGet(*td, "uri")->asString() : "";
			const Json* changesV = jsonGet(*params, "contentChanges");
			const JsonArray* changes = changesV ? changesV->asArray() : nullptr;
			if (uri.empty() || !changes || changes->empty()) continue;
			const JsonObject* firstChange = (*changes)[0].asObject();
			if (!firstChange) continue;
			const Json* textV = jsonGet(*firstChange, "text");
			if (!textV || !textV->isString()) continue;
			docs[uri] = *textV->asString();
			publishDiagnostics(std::cout, uri, docs[uri]);
			continue;
		}

		if (method == "textDocument/didClose") {
			const Json* paramsV = jsonGet(*obj, "params");
			auto* params = paramsV ? paramsV->asObject() : nullptr;
			if (!params) continue;
			const Json* tdV = jsonGet(*params, "textDocument");
			auto* td = tdV ? tdV->asObject() : nullptr;
			if (!td) continue;
			std::string uri = (jsonGet(*td, "uri") && jsonGet(*td, "uri")->isString()) ? *jsonGet(*td, "uri")->asString() : "";
			docs.erase(uri);
			// publish empty diagnostics
			Json paramsOut = JsonObject{{"uri", Json(uri)}, {"diagnostics", Json(JsonArray{})}};
			Json notif = JsonObject{{"jsonrpc", Json("2.0")}, {"method", Json("textDocument/publishDiagnostics")}, {"params", paramsOut}};
			writeLspMessage(std::cout, jsonDump(notif));
			continue;
		}
	}

	return shutdownRequested ? 0 : 1;
}
