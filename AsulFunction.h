#ifndef ASUL_FUNCTION_H
#define ASUL_FUNCTION_H

#include "AsulAST.h"
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <condition_variable>
#include <mutex>

namespace asul {

// Value-related helper functions
std::string typeOf(const Value& v);
bool isTruthy(const Value& v);
std::string toString(const Value& v);
bool valueEqual(const Value& a, const Value& b);
size_t valueHash(const Value& v);

// Functor wrappers to use Value as key in unordered_map/set
struct ValueHash { size_t operator()(const Value& v) const noexcept { return valueHash(v); } };
struct ValueEq { bool operator()(const Value& a, const Value& b) const noexcept { return valueEqual(a, b); } };

// Environment for variable storage
struct Environment : std::enable_shared_from_this<Environment> {
	std::shared_ptr<Environment> parent;
	std::unordered_map<std::string, Value> values;
	// declared types for variables (optional): maps variable name -> declared type name
	std::unordered_map<std::string, std::string> declaredTypes;
	// Explicitly exported symbols in this environment (for module scopes)
	std::unordered_set<std::string> explicitExports;

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

// Function representation
struct Function {
	std::vector<std::string> params;
	int restParamIndex{-1}; // -1 means no rest parameter, otherwise index of rest param
	std::vector<ExprPtr> defaultValues;  // default parameter values (corresponding to params)
	std::vector<StmtPtr> body;
	std::shared_ptr<Environment> closure;
	bool isBuiltin{false};
	bool isAsync{false};
	std::function<Value(const std::vector<Value>&, std::shared_ptr<Environment>)> builtin;
};

// Class information
struct ClassInfo {
	std::string name;
	std::vector<std::shared_ptr<ClassInfo>> supers; // multi-inheritance support
	std::unordered_map<std::string, std::shared_ptr<Function>> methods;
	std::unordered_map<std::string, std::shared_ptr<Function>> staticMethods;
	bool isNative{false}; // If true, new creates InstanceExt
};

// Instance of a class
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

// Promise state for await
struct PromiseState {
	std::mutex mtx;
	std::condition_variable cv;
	bool settled{false};
	bool rejected{false};
	Value result{std::monostate{}};
	// event loop pointer for then/catch callback dispatch
	void* loopPtr{nullptr};
	// then/catch callbacks and chained next Promise
	std::vector<std::pair<std::shared_ptr<Function>, std::shared_ptr<PromiseState>>> thenCallbacks;
	std::vector<std::pair<std::shared_ptr<Function>, std::shared_ptr<PromiseState>>> catchCallbacks;
};

// Native container holder types (used by host-backed classes)
struct NativeMap { std::unordered_map<Value, Value, ValueHash, ValueEq> m; std::vector<Value> order; std::unordered_map<Value, size_t, ValueHash, ValueEq> index; };
struct NativeSet { std::unordered_set<Value, ValueHash, ValueEq> s; std::vector<Value> order; std::unordered_map<Value, size_t, ValueHash, ValueEq> index; };
struct NativeDeque { std::deque<Value> d; };
struct NativeStack { std::vector<Value> v; };

// Stream wrapper interfaces
struct StreamWrapper {
	virtual size_t read(char* buf, size_t n) = 0;
	virtual void write(const char* buf, size_t n) = 0;
	virtual void close() = 0;
	virtual bool eof() { return false; }
	virtual ~StreamWrapper() = default;
};

} // namespace asul

#endif // ASUL_FUNCTION_H
