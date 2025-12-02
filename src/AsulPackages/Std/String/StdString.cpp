#include "StdString.h"
#include "../../../AsulInterpreter.h"
#include <algorithm>
#include <cctype>

namespace asul {

void registerStdStringPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.string", [](std::shared_ptr<Object> stringPkg) {

		// toUpperCase(str)
		auto toUpperCaseFn = std::make_shared<Function>();
		toUpperCaseFn->isBuiltin = true;
		toUpperCaseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
				throw std::runtime_error("toUpperCase expects 1 string argument");
			}
			std::string input = std::get<std::string>(args[0]);
			std::transform(input.begin(), input.end(), input.begin(), ::toupper);
			return Value{input};
		};
		(*stringPkg)["toUpperCase"] = Value{toUpperCaseFn};

		// toLowerCase(str)
		auto toLowerCaseFn = std::make_shared<Function>();
		toLowerCaseFn->isBuiltin = true;
		toLowerCaseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
				throw std::runtime_error("toLowerCase expects 1 string argument");
			}
			std::string input = std::get<std::string>(args[0]);
			std::transform(input.begin(), input.end(), input.begin(), ::tolower);
			return Value{input};
		};
		(*stringPkg)["toLowerCase"] = Value{toLowerCaseFn};

		// trim(str)
		auto trimFn = std::make_shared<Function>();
		trimFn->isBuiltin = true;
		trimFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
				throw std::runtime_error("trim expects 1 string argument");
			}
			std::string input = std::get<std::string>(args[0]);
			auto start = input.find_first_not_of(" \t\n\r");
			auto end = input.find_last_not_of(" \t\n\r");
			if (start == std::string::npos || end == std::string::npos) {
				return Value{std::string("")};
			}
			return Value{input.substr(start, end - start + 1)};
		};
		(*stringPkg)["trim"] = Value{trimFn};
	});
}

} // namespace asul
