#ifndef ASUL_RUNTIME_H
#define ASUL_RUNTIME_H

#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace asul {

// Forward declarations
struct Function;
struct PromiseState;
struct ClassInfo;
struct Instance;
struct Stmt;
struct Expr;

using StmtPtr = std::shared_ptr<Stmt>;
using ExprPtr = std::shared_ptr<Expr>;

// ----------- Value Types -----------
using Array = std::vector<struct ValueTag>;
using Object = std::unordered_map<std::string, struct ValueTag>;

// Recursive variant wrapper to allow shared_ptr recursive types.
struct ValueTag : public std::variant<std::monostate,double,std::string,bool,std::shared_ptr<Function>,std::shared_ptr<Array>,std::shared_ptr<Object>,std::shared_ptr<ClassInfo>,std::shared_ptr<Instance>,std::shared_ptr<PromiseState>> {
	using variant::variant;
};

using Value = ValueTag;

// ----------- Value Helper Functions -----------
std::string typeOf(const Value& v);
bool isTruthy(const Value& v);
std::string toString(const Value& v);
bool valueEqual(const Value& a, const Value& b);
size_t valueHash(const Value& v);

// Utility: get numeric value from Value or throw
inline double getNumber(const Value& v, const char* where) {
	if (auto n = std::get_if<double>(&v)) return *n;
	if (auto s = std::get_if<std::string>(&v)) {
		char* end = nullptr; double d = std::strtod(s->c_str(), &end); if (end && *end=='\0') return d;
	}
	throw std::runtime_error(std::string("Expected number at ") + where);
}

// Functor wrappers to use Value as key in unordered_map/set
struct ValueHash { size_t operator()(const Value& v) const noexcept { return valueHash(v); } };
struct ValueEq { bool operator()(const Value& a, const Value& b) const noexcept { return valueEqual(a, b); } };

// ----------- Native Container Types -----------
struct NativeMap { std::unordered_map<Value, Value, ValueHash, ValueEq> m; std::vector<Value> order; std::unordered_map<Value, size_t, ValueHash, ValueEq> index; };
struct NativeSet { std::unordered_set<Value, ValueHash, ValueEq> s; std::vector<Value> order; std::unordered_map<Value, size_t, ValueHash, ValueEq> index; };
struct NativeDeque { std::deque<Value> d; };
struct NativeStack { std::vector<Value> v; };

// ----------- Environment -----------
struct Environment : std::enable_shared_from_this<Environment> {
	std::shared_ptr<Environment> parent;
	std::unordered_map<std::string, Value> values;
	// declared types for variables (optional): maps variable name -> declared type name
	std::unordered_map<std::string, std::string> declaredTypes;
	// Explicitly exported symbols in this environment (for module scopes)
	std::unordered_set<std::string> explicitExports;

	explicit Environment(std::shared_ptr<Environment> p = nullptr) : parent(std::move(p)) {}

	void define(const std::string& name, const Value& val);
	// define with optional declared type
	void defineWithType(const std::string& name, const Value& val, const std::optional<std::string>& typeName);
	std::optional<std::string> getDeclaredType(const std::string& name);
	bool assign(const std::string& name, const Value& val);
	Value get(const std::string& name);
};

// ----------- Function -----------
struct Function {
	std::vector<std::string> params;
	int restParamIndex{-1}; // -1 表示没有 rest 参数，否则表示 rest 参数的索引
	std::vector<ExprPtr> defaultValues;  // 默认参数值（与 params 对应）
	std::vector<StmtPtr> body;
	std::shared_ptr<Environment> closure;
	bool isBuiltin{false};
	bool isAsync{false};
	bool isGenerator{false};
	std::function<Value(const std::vector<Value>&, std::shared_ptr<Environment>)> builtin;
};

// ----------- ClassInfo -----------
struct ClassInfo {
	std::string name;
	std::vector<std::shared_ptr<ClassInfo>> supers; // 多继承支持，按声明顺序线性查找
	std::unordered_map<std::string, std::shared_ptr<Function>> methods;
	std::unordered_map<std::string, std::shared_ptr<Function>> staticMethods;
	bool isNative{false}; // If true, new creates InstanceExt
};

// ----------- Instance -----------
struct Instance {
	std::shared_ptr<ClassInfo> klass;
	std::unordered_map<std::string, Value> fields;
	virtual ~Instance() = default;
};

// Allow Instance to own a native handle for host-wrapped classes
struct InstanceExt : Instance {
	void* nativeHandle{nullptr};
	std::function<void(void*)> nativeDestructor{nullptr};
	~InstanceExt() {
		if (nativeDestructor && nativeHandle) nativeDestructor(nativeHandle);
	}
};

// ----------- Stream Wrappers -----------
struct StreamWrapper {
	virtual size_t read(char* buf, size_t n) = 0;
	virtual void write(const char* buf, size_t n) = 0;
	virtual void close() = 0;
	virtual bool eof() { return false; }
	virtual ~StreamWrapper() = default;
};

struct FStreamWrapper : StreamWrapper {
	std::fstream fs;
	FStreamWrapper(const std::string& path, std::ios_base::openmode mode) : fs(path, mode) {}
	size_t read(char* buf, size_t n) override;
	void write(const char* buf, size_t n) override;
	void close() override;
	bool eof() override;
};

struct StdinWrapper : StreamWrapper {
	size_t read(char* buf, size_t n) override;
	void write(const char* buf, size_t n) override;
	void close() override;
	bool eof() override;
};

struct StdoutWrapper : StreamWrapper {
	size_t read(char* buf, size_t n) override;
	void write(const char* buf, size_t n) override;
	void close() override;
};

struct StderrWrapper : StreamWrapper {
	size_t read(char* buf, size_t n) override;
	void write(const char* buf, size_t n) override;
	void close() override;
};

struct FilePtrWrapper : StreamWrapper {
	FILE* fp;
	std::function<void(FILE*)> closer;
	FilePtrWrapper(FILE* f, std::function<void(FILE*)> c) : fp(f), closer(c) {}
	size_t read(char* buf, size_t n) override;
	void write(const char* buf, size_t n) override;
	void close() override;
	bool eof() override;
};

// ----------- Promise State -----------
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

} // namespace asul

#endif // ASUL_RUNTIME_H
