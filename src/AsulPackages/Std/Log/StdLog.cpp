#include "StdLog.h"
#include "../../../AsulInterpreter.h"
#include "../../../AsulRuntime.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace asul {

// ANSI color codes
static const char* RESET = "\033[0m";
static const char* RED = "\033[31m";
static const char* YELLOW = "\033[33m";
static const char* BLUE = "\033[34m";
static const char* GRAY = "\033[90m";
static const char* WHITE = "\033[37m";

// Global log state
static int globalLogLevel = 1; // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
static bool colorsEnabled = true;

static std::string getCurrentTimestamp() {
	auto now = std::time(nullptr);
	auto tm = *std::localtime(&now);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

static std::string formatLogMessage(const std::string& level, const std::string& message, bool useColor, const char* color) {
	std::ostringstream oss;
	oss << "[" << getCurrentTimestamp() << "] ";
	if (useColor && colorsEnabled) {
		oss << color << "[" << level << "]" << RESET << " " << message;
	} else {
		oss << "[" << level << "] " << message;
	}
	return oss.str();
}

void registerStdLogPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.log", [](std::shared_ptr<Object> logPkg) {
		
		// setLevel(level) - 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
		auto setLevelFn = std::make_shared<Function>();
		setLevelFn->isBuiltin = true;
		setLevelFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 1) throw std::runtime_error("setLevel expects level argument (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)");
			int level = static_cast<int>(getNumber(args[0], "setLevel level"));
			if (level < 0) level = 0;
			if (level > 3) level = 3;
			globalLogLevel = level;
			return Value{std::monostate{}};
		};
		(*logPkg)["setLevel"] = Value{setLevelFn};

		// getLevel() - returns current log level
		auto getLevelFn = std::make_shared<Function>();
		getLevelFn->isBuiltin = true;
		getLevelFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
			return Value{static_cast<double>(globalLogLevel)};
		};
		(*logPkg)["getLevel"] = Value{getLevelFn};

		// setColors(enabled) - enable/disable color output
		auto setColorsFn = std::make_shared<Function>();
		setColorsFn->isBuiltin = true;
		setColorsFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 1) throw std::runtime_error("setColors expects boolean argument");
			colorsEnabled = isTruthy(args[0]);
			return Value{std::monostate{}};
		};
		(*logPkg)["setColors"] = Value{setColorsFn};

		// debug(message, ...) - log debug message
		auto debugFn = std::make_shared<Function>();
		debugFn->isBuiltin = true;
		debugFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (globalLogLevel > 0) return Value{std::monostate{}}; // Skip if level too high
			std::ostringstream oss;
			for (size_t i = 0; i < args.size(); ++i) {
				if (i > 0) oss << " ";
				oss << toString(args[i]);
			}
			std::cout << formatLogMessage("DEBUG", oss.str(), true, GRAY) << std::endl;
			return Value{std::monostate{}};
		};
		(*logPkg)["debug"] = Value{debugFn};

		// info(message, ...) - log info message
		auto infoFn = std::make_shared<Function>();
		infoFn->isBuiltin = true;
		infoFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (globalLogLevel > 1) return Value{std::monostate{}};
			std::ostringstream oss;
			for (size_t i = 0; i < args.size(); ++i) {
				if (i > 0) oss << " ";
				oss << toString(args[i]);
			}
			std::cout << formatLogMessage("INFO", oss.str(), true, BLUE) << std::endl;
			return Value{std::monostate{}};
		};
		(*logPkg)["info"] = Value{infoFn};

		// warn(message, ...) - log warning message
		auto warnFn = std::make_shared<Function>();
		warnFn->isBuiltin = true;
		warnFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (globalLogLevel > 2) return Value{std::monostate{}};
			std::ostringstream oss;
			for (size_t i = 0; i < args.size(); ++i) {
				if (i > 0) oss << " ";
				oss << toString(args[i]);
			}
			std::cout << formatLogMessage("WARN", oss.str(), true, YELLOW) << std::endl;
			return Value{std::monostate{}};
		};
		(*logPkg)["warn"] = Value{warnFn};

		// error(message, ...) - log error message
		auto errorFn = std::make_shared<Function>();
		errorFn->isBuiltin = true;
		errorFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (globalLogLevel > 3) return Value{std::monostate{}};
			std::ostringstream oss;
			for (size_t i = 0; i < args.size(); ++i) {
				if (i > 0) oss << " ";
				oss << toString(args[i]);
			}
			std::cerr << formatLogMessage("ERROR", oss.str(), true, RED) << std::endl;
			return Value{std::monostate{}};
		};
		(*logPkg)["error"] = Value{errorFn};

		// json(level, obj) - structured JSON logging
		auto jsonFn = std::make_shared<Function>();
		jsonFn->isBuiltin = true;
		jsonFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) throw std::runtime_error("json expects (level, object) arguments");
			int level = static_cast<int>(getNumber(args[0], "json level"));
			if (level < globalLogLevel) return Value{std::monostate{}};
			
			std::ostringstream oss;
			oss << "{\"timestamp\":\"" << getCurrentTimestamp() << "\",";
			oss << "\"level\":" << level << ",";
			oss << "\"data\":" << toString(args[1]) << "}";
			
			std::cout << oss.str() << std::endl;
			return Value{std::monostate{}};
		};
		(*logPkg)["json"] = Value{jsonFn};

		// Log level constants
		(*logPkg)["DEBUG"] = Value{0.0};
		(*logPkg)["INFO"] = Value{1.0};
		(*logPkg)["WARN"] = Value{2.0};
		(*logPkg)["ERROR"] = Value{3.0};
	});
}

} // namespace asul
