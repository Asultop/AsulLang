#include "StdBuiltin.h"
#include "../../../AsulInterpreter.h"
#include <chrono>

namespace asul {

void registerStdBuiltinPackage(Interpreter& interp) {
	auto globals = interp.globalsEnv();
	
	// len(x): string/array/object length
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

	// push(arr, ...values): append elements, return new length
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

	// typeof(x): return a type-name string for x
	auto typeFn = std::make_shared<Function>();
	typeFn->isBuiltin = true;
	typeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos) -> Value {
		if (args.size() != 1) throw std::runtime_error("typeof expects 1 argument");
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
	globals->define("typeof", typeFn);

	// performance.now()
	auto perfObj = std::make_shared<Object>();
	auto nowFn = std::make_shared<Function>(); 
	nowFn->isBuiltin = true;
	nowFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
		using namespace std::chrono;
		static auto start = high_resolution_clock::now();
		auto now = high_resolution_clock::now();
		duration<double, std::milli> ms = now - start;
		return Value{ ms.count() };
	};
	(*perfObj)["now"] = Value{nowFn};
	globals->define("performance", Value{perfObj});

	// TODO: Extract remaining builtins (quote, eval, sleep, Promise) which use [this] captures
	// These need interpreter pointer handling and will be added in next iteration
}

} // namespace asul
