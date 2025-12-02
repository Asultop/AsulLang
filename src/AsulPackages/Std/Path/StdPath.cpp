#include "StdPath.h"
#include "../../../AsulInterpreter.h"
#include <filesystem>

namespace asul {

void registerStdPathPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.path", [](std::shared_ptr<Object> pathPkg) {
		namespace fs = std::filesystem;
		
		// join(...paths)
		auto joinFn = std::make_shared<Function>(); joinFn->isBuiltin = true;
		joinFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			fs::path p;
			for (const auto& arg : args) {
				p /= toString(arg);
			}
			return Value{ p.string() };
		};
		(*pathPkg)["join"] = Value{ joinFn };

		// resolve(...paths)
		auto resolveFn = std::make_shared<Function>(); resolveFn->isBuiltin = true;
		resolveFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			fs::path p = fs::current_path();
			for (const auto& arg : args) {
				p /= toString(arg);
			}
			return Value{ fs::weakly_canonical(p).string() };
		};
		(*pathPkg)["resolve"] = Value{ resolveFn };

		// dirname(path)
		auto dirnameFn = std::make_shared<Function>(); dirnameFn->isBuiltin = true;
		dirnameFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) return Value{ std::string(".") };
			fs::path p(toString(args[0]));
			return Value{ p.parent_path().string() };
		};
		(*pathPkg)["dirname"] = Value{ dirnameFn };

		// basename(path, [ext])
		auto basenameFn = std::make_shared<Function>(); basenameFn->isBuiltin = true;
		basenameFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) return Value{ std::string("") };
			fs::path p(toString(args[0]));
			std::string name = p.filename().string();
			if (args.size() > 1) {
				std::string ext = toString(args[1]);
				if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
					return Value{ name.substr(0, name.size() - ext.size()) };
				}
			}
			return Value{ name };
		};
		(*pathPkg)["basename"] = Value{ basenameFn };

		// extname(path)
		auto extnameFn = std::make_shared<Function>(); extnameFn->isBuiltin = true;
		extnameFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) return Value{ std::string("") };
			fs::path p(toString(args[0]));
			return Value{ p.extension().string() };
		};
		(*pathPkg)["extname"] = Value{ extnameFn };

		// isAbsolute(path)
		auto isAbsFn = std::make_shared<Function>(); isAbsFn->isBuiltin = true;
		isAbsFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) return Value{ false };
			fs::path p(toString(args[0]));
			return Value{ p.is_absolute() };
		};
		(*pathPkg)["isAbsolute"] = Value{ isAbsFn };

		(*pathPkg)["sep"] = Value{ std::string(1, fs::path::preferred_separator) };
	});
}

} // namespace asul
