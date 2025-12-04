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

		// replaceAll(str, search, replacement)
		auto replaceAllFn = std::make_shared<Function>();
		replaceAllFn->isBuiltin = true;
		replaceAllFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 3 || !std::holds_alternative<std::string>(args[0]) ||
			    !std::holds_alternative<std::string>(args[1]) || !std::holds_alternative<std::string>(args[2])) {
				throw std::runtime_error("replaceAll expects 3 string arguments (str, search, replacement)");
			}
			std::string str = std::get<std::string>(args[0]);
			std::string search = std::get<std::string>(args[1]);
			std::string replacement = std::get<std::string>(args[2]);
			
			if (search.empty()) {
				return Value{str};
			}
			
			std::string result;
			size_t pos = 0;
			size_t lastPos = 0;
			while ((pos = str.find(search, lastPos)) != std::string::npos) {
				result.append(str, lastPos, pos - lastPos);
				result.append(replacement);
				lastPos = pos + search.length();
			}
			result.append(str, lastPos, str.length() - lastPos);
			return Value{result};
		};
		(*stringPkg)["replaceAll"] = Value{replaceAllFn};

		// repeat(str, count)
		auto repeatFn = std::make_shared<Function>();
		repeatFn->isBuiltin = true;
		repeatFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
			    !std::holds_alternative<double>(args[1])) {
				throw std::runtime_error("repeat expects (string, number) arguments");
			}
			std::string str = std::get<std::string>(args[0]);
			int count = static_cast<int>(std::get<double>(args[1]));
			
			if (count < 0) {
				throw std::runtime_error("repeat count must be non-negative");
			}
			if (count == 0 || str.empty()) {
				return Value{std::string("")};
			}
			
			std::string result;
			result.reserve(str.length() * count);
			for (int i = 0; i < count; ++i) {
				result.append(str);
			}
			return Value{result};
		};
		(*stringPkg)["repeat"] = Value{repeatFn};

		// localeCompare(str1, str2)
		auto localeCompareFn = std::make_shared<Function>();
		localeCompareFn->isBuiltin = true;
		localeCompareFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
			    !std::holds_alternative<std::string>(args[1])) {
				throw std::runtime_error("localeCompare expects 2 string arguments");
			}
			std::string str1 = std::get<std::string>(args[0]);
			std::string str2 = std::get<std::string>(args[1]);
			
			// Simple lexicographical comparison (C locale)
			// For full locale support, would need std::locale or ICU library
			int result = str1.compare(str2);
			if (result < 0) return Value{-1.0};
			if (result > 0) return Value{1.0};
			return Value{0.0};
		};
		(*stringPkg)["localeCompare"] = Value{localeCompareFn};
	});
}

} // namespace asul
