#include "StdRegex.h"
#include "../../../AsulInterpreter.h"
#include <regex>

namespace asul {

void registerStdRegexPackage(Interpreter& interp) {
	auto stdRoot = interp.ensurePackage("std");
	
	interp.registerLazyPackage("std.regex", [stdRoot](std::shared_ptr<Object> regexPkg) {
		auto regexClass = std::make_shared<ClassInfo>();
		regexClass->name = "Regex";
		
		// constructor(pattern)
		auto ctor = std::make_shared<Function>(); ctor->isBuiltin = true;
		ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			if (args.empty()) throw ExceptionSignal{ Value{ std::string("Regex constructor expects pattern") } };
			std::string pattern;
			if (auto p = std::get_if<std::string>(&args[0])) pattern = *p;
			else throw ExceptionSignal{ Value{ std::string("Regex pattern must be string") } };
			
			// Validate pattern
			try {
				std::regex re(pattern);
			} catch (const std::exception& e) {
				throw ExceptionSignal{ Value{ std::string("Regex error: ") + e.what() } };
			}

			// Store pattern in instance field "_pattern"
			if (!clos) return Value{std::monostate{}};
			Value tv = clos->get("this");
			if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) {
				if (*pins) (*pins)->fields["_pattern"] = Value{pattern};
			}
			return Value{std::monostate{}};
		};
		regexClass->methods["constructor"] = ctor;

		// match(str) -> [full, group1, ...] or null
		auto matchFn = std::make_shared<Function>(); matchFn->isBuiltin = true;
		matchFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			if (args.empty()) throw ExceptionSignal{ Value{ std::string("Regex.match expects string") } };
			std::string text;
			if (auto p = std::get_if<std::string>(&args[0])) text = *p;
			else throw ExceptionSignal{ Value{ std::string("Regex.match argument must be string") } };

			std::string pattern;
			if (clos) {
				Value tv = clos->get("this");
				if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) {
					if (*pins) {
						auto it = (*pins)->fields.find("_pattern");
						if (it != (*pins)->fields.end() && std::holds_alternative<std::string>(it->second)) {
							pattern = std::get<std::string>(it->second);
						}
					}
				}
			}
			if (pattern.empty()) throw ExceptionSignal{ Value{ std::string("Regex instance has no pattern") } };

			try {
				std::regex re(pattern);
				std::smatch sm;
				if (std::regex_search(text, sm, re)) {
					auto arr = std::make_shared<Array>();
					for (const auto& m : sm) {
						arr->push_back(Value{m.str()});
					}
					return Value{arr};
				}
			} catch (const std::exception& e) {
				throw ExceptionSignal{ Value{ std::string("Regex error: ") + e.what() } };
			}
			return Value{std::monostate{}};
		};
		regexClass->methods["match"] = matchFn;

		// test(str) -> bool
		auto testFn = std::make_shared<Function>(); testFn->isBuiltin = true;
		testFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			if (args.empty()) throw ExceptionSignal{ Value{ std::string("Regex.test expects string") } };
			std::string text;
			if (auto p = std::get_if<std::string>(&args[0])) text = *p;
			else throw ExceptionSignal{ Value{ std::string("Regex.test argument must be string") } };

			std::string pattern;
			if (clos) {
				Value tv = clos->get("this");
				if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) {
					if (*pins) {
						auto it = (*pins)->fields.find("_pattern");
						if (it != (*pins)->fields.end() && std::holds_alternative<std::string>(it->second)) {
							pattern = std::get<std::string>(it->second);
						}
					}
				}
			}
			if (pattern.empty()) throw ExceptionSignal{ Value{ std::string("Regex instance has no pattern") } };

			try {
				std::regex re(pattern);
				return Value{std::regex_search(text, re)};
			} catch (const std::exception& e) {
				throw ExceptionSignal{ Value{ std::string("Regex error: ") + e.what() } };
			}
		};
		regexClass->methods["test"] = testFn;

		// replace(str, replacement) -> string
		auto replaceFn = std::make_shared<Function>(); replaceFn->isBuiltin = true;
		replaceFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			if (args.size() < 2) throw ExceptionSignal{ Value{ std::string("Regex.replace expects string and replacement") } };
			std::string text;
			if (auto p = std::get_if<std::string>(&args[0])) text = *p;
			else throw ExceptionSignal{ Value{ std::string("Regex.replace first argument must be string") } };
			
			std::string replacement;
			if (auto p = std::get_if<std::string>(&args[1])) replacement = *p;
			else throw ExceptionSignal{ Value{ std::string("Regex.replace second argument must be string") } };

			std::string pattern;
			if (clos) {
				Value tv = clos->get("this");
				if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) {
					if (*pins) {
						auto it = (*pins)->fields.find("_pattern");
						if (it != (*pins)->fields.end() && std::holds_alternative<std::string>(it->second)) {
							pattern = std::get<std::string>(it->second);
						}
					}
				}
			}
			if (pattern.empty()) throw ExceptionSignal{ Value{ std::string("Regex instance has no pattern") } };

			try {
				std::regex re(pattern);
				std::string res = std::regex_replace(text, re, replacement);
				return Value{res};
			} catch (const std::exception& e) {
				throw ExceptionSignal{ Value{ std::string("Regex error: ") + e.what() } };
			}
		};
		regexClass->methods["replace"] = replaceFn;

		(*regexPkg)["Regex"] = Value{regexClass};
		(*stdRoot)["regex"] = Value{regexClass};
	});
}

} // namespace asul
