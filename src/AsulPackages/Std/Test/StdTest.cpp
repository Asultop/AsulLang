#include "StdTest.h"
#include "../../../AsulInterpreter.h"
#include "../../../AsulRuntime.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

namespace asul {

// Global test state
struct TestState {
	int totalTests = 0;
	int passedTests = 0;
	int failedTests = 0;
	std::vector<std::string> failures;
	std::string currentSuite = "";
	bool inDescribe = false;
};

static TestState globalTestState;

void registerStdTestPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.test", [](std::shared_ptr<Object> testPkg) {
		
		// assert(condition, message) - basic assertion
		auto assertFn = std::make_shared<Function>();
		assertFn->isBuiltin = true;
		assertFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty()) throw std::runtime_error("assert expects at least 1 argument");
			bool condition = isTruthy(args[0]);
			if (!condition) {
				std::string message = args.size() > 1 ? toString(args[1]) : "Assertion failed";
				throw std::runtime_error(message);
			}
			return Value{std::monostate{}};
		};
		(*testPkg)["assert"] = Value{assertFn};

		// assertEqual(actual, expected, message) - equality assertion
		auto assertEqualFn = std::make_shared<Function>();
		assertEqualFn->isBuiltin = true;
		assertEqualFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) throw std::runtime_error("assertEqual expects (actual, expected) arguments");
			std::string actual = toString(args[0]);
			std::string expected = toString(args[1]);
			if (actual != expected) {
				std::ostringstream oss;
				oss << "Expected " << expected << " but got " << actual;
				if (args.size() > 2) {
					oss << " - " << toString(args[2]);
				}
				throw std::runtime_error(oss.str());
			}
			return Value{std::monostate{}};
		};
		(*testPkg)["assertEqual"] = Value{assertEqualFn};

		// assertNotEqual(actual, expected, message)
		auto assertNotEqualFn = std::make_shared<Function>();
		assertNotEqualFn->isBuiltin = true;
		assertNotEqualFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) throw std::runtime_error("assertNotEqual expects (actual, expected) arguments");
			std::string actual = toString(args[0]);
			std::string expected = toString(args[1]);
			if (actual == expected) {
				std::ostringstream oss;
				oss << "Expected values to be different, but both are " << actual;
				if (args.size() > 2) {
					oss << " - " << toString(args[2]);
				}
				throw std::runtime_error(oss.str());
			}
			return Value{std::monostate{}};
		};
		(*testPkg)["assertNotEqual"] = Value{assertNotEqualFn};

		// getStats() - get test statistics
		auto getStatsFn = std::make_shared<Function>();
		getStatsFn->isBuiltin = true;
		getStatsFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
			auto stats = std::make_shared<Object>();
			(*stats)["total"] = Value{static_cast<double>(globalTestState.totalTests)};
			(*stats)["passed"] = Value{static_cast<double>(globalTestState.passedTests)};
			(*stats)["failed"] = Value{static_cast<double>(globalTestState.failedTests)};
			return Value{stats};
		};
		(*testPkg)["getStats"] = Value{getStatsFn};

		// resetStats() - reset test statistics
		auto resetStatsFn = std::make_shared<Function>();
		resetStatsFn->isBuiltin = true;
		resetStatsFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
			globalTestState.totalTests = 0;
			globalTestState.passedTests = 0;
			globalTestState.failedTests = 0;
			globalTestState.failures.clear();
			globalTestState.currentSuite = "";
			globalTestState.inDescribe = false;
			return Value{std::monostate{}};
		};
		(*testPkg)["resetStats"] = Value{resetStatsFn};

		// pass() - mark test as passed
		auto passFn = std::make_shared<Function>();
		passFn->isBuiltin = true;
		passFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
			globalTestState.totalTests++;
			globalTestState.passedTests++;
			return Value{std::monostate{}};
		};
		(*testPkg)["pass"] = Value{passFn};

		// fail(message) - mark test as failed
		auto failFn = std::make_shared<Function>();
		failFn->isBuiltin = true;
		failFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			globalTestState.totalTests++;
			globalTestState.failedTests++;
			std::string message = args.empty() ? "Test failed" : toString(args[0]);
			globalTestState.failures.push_back(message);
			return Value{std::monostate{}};
		};
		(*testPkg)["fail"] = Value{failFn};

		// printSummary() - print test summary
		auto printSummaryFn = std::make_shared<Function>();
		printSummaryFn->isBuiltin = true;
		printSummaryFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
			std::cout << "\n========================================" << std::endl;
			std::cout << "Test Summary" << std::endl;
			std::cout << "========================================" << std::endl;
			std::cout << "Total:  " << globalTestState.totalTests << std::endl;
			std::cout << "Passed: " << globalTestState.passedTests << std::endl;
			std::cout << "Failed: " << globalTestState.failedTests << std::endl;
			
			if (!globalTestState.failures.empty()) {
				std::cout << "\nFailures:" << std::endl;
				for (const auto& failure : globalTestState.failures) {
					std::cout << "  - " << failure << std::endl;
				}
			}
			
			std::cout << "========================================" << std::endl;
			return Value{std::monostate{}};
		};
		(*testPkg)["printSummary"] = Value{printSummaryFn};
	});
}

} // namespace asul
