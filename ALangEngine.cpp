#include "ALangEngine.h"

// Include external modules
#include "AsulLexer.h"
#include "AsulRuntime.h"
#include "AsulAst.h"
#include "AsulParser.h"
#include "AsulInterpreter.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <regex>
#include <utility>
#include <variant>
#include <vector>
#include <queue>
#include <filesystem>
#include <fstream>
#include <optional>
#include <cstring>
#include <random>
#include <csignal>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <process.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/wait.h>
#endif
#include <atomic>
#include "AsulFormatString/AsulFormatString.h"

// Use the asul namespace for external modules
using namespace asul;

// Global mutex for timezone operations (setenv/tzset are not thread-safe)
std::mutex tzMutex;

// ----------- ALangEngine facade -----------

struct ALangEngine::Impl {
	std::string source;
	Interpreter interpreter;
};

ALangEngine::ALangEngine() : impl(new Impl()) {}
ALangEngine::~ALangEngine() { delete impl; }

void ALangEngine::initialize() {
	// 安装 AsulFormatString 色彩与日志标签（用于错误美化输出）
	try {
		auto& afs = asul_formatter();
		afs.installColorFormatAdapter();
		afs.installLogLabelAdapter();
		afs.installResetLabelAdapter();
	} catch (...) {
		// 忽略格式器初始化异常，保持回退到纯文本错误输出
	}
}

// 全局可配置的错误配色映射：{"header","code","caret"}
static std::unordered_map<std::string, std::string> g_errorColorMap;

void ALangEngine::setSource(const std::string& code) { impl->source = code; }

void ALangEngine::setErrorColorMap(const std::unordered_map<std::string, std::string>& colorMap) {
	g_errorColorMap = colorMap;
}

void ALangEngine::execute() {
	if (impl->source.empty()) return;
	execute(impl->source);
}

static std::string colorize(const std::string& key, const std::string& text, const char* defColor) {
    std::string color = defColor;
    auto it = g_errorColorMap.find(key);
    if (it != g_errorColorMap.end() && !it->second.empty()) color = it->second;
    return f("{" + color + "}", text);
}

static std::string sanitizeHeaderMsg(const std::string& msg) {
	// 统一头部信息：去掉位置信息（at line / column / length），只保留简短描述
	std::string s = msg;
	// 仅处理第一行作为头部
	size_t nl = s.find('\n');
	if (nl != std::string::npos) s = s.substr(0, nl);

	auto removeKeyNumber = [&](const char* key){
		size_t p = 0;
		while ((p = s.find(key, p)) != std::string::npos) {
			size_t start = p;
			size_t end = p + std::strlen(key);
			while (end < s.size() && isspace(static_cast<unsigned char>(s[end]))) end++;
			while (end < s.size() && isdigit(static_cast<unsigned char>(s[end]))) end++;
			// 去掉前导的", "
			if (start >= 2 && s[start-2] == ',' && s[start-1] == ' ') start -= 2;
			s.erase(start, end - start);
		}
	};

	// 去掉 "at line N"（含可选前导空格）
	{
		size_t p = 0;
		while ((p = s.find(" at line ", p)) != std::string::npos) {
			size_t start = p;
			p += 9; // len(" at line ")
			size_t end = p;
			while (end < s.size() && isdigit(static_cast<unsigned char>(s[end]))) end++;
			s.erase(start, end - start);
		}
	}

	// 去掉 ", column N" 与 ", length M"
	removeKeyNumber("column");
	removeKeyNumber("length");

	// 修剪多余空格
	while (!s.empty() && (s.back() == ' ' || s.back() == ',')) s.pop_back();
	return s;
}

static void printErrorWithContext(const std::string& src, const std::string& msg, const std::string& filename = std::string()) {
	// 分离首行（头部）与后续详细信息（如 Stack 或提示）
	std::string cleanMsg = msg;
	std::string headerLine = cleanMsg;
	std::string extraLines;
	size_t firstNl = cleanMsg.find('\n');
	if (firstNl != std::string::npos) {
		headerLine = cleanMsg.substr(0, firstNl);
		extraLines = cleanMsg.substr(firstNl + 1);
	}

	// 从头部提取行列信息（支持 "line N, column M" 或 "at line N"）
	int line = -1, col = 1, width = 1;
	size_t p = headerLine.find("line ");
	if (p != std::string::npos) {
		p += 5;
		size_t q = p;
		while (q < headerLine.size() && isdigit(static_cast<unsigned char>(headerLine[q]))) q++;
		if (q > p) {
			line = std::stoi(headerLine.substr(p, q - p));
			size_t cpos = headerLine.find("column ", q);
			if (cpos != std::string::npos) {
				cpos += 7;
				size_t r = cpos;
				while (r < headerLine.size() && isdigit(static_cast<unsigned char>(headerLine[r]))) r++;
				if (r > cpos) col = std::stoi(headerLine.substr(cpos, r - cpos));
			}
			// try parse length
			size_t lpos = headerLine.find("length ", q);
			if (lpos != std::string::npos) {
				lpos += 7;
				size_t r2 = lpos;
				while (r2 < headerLine.size() && isdigit(static_cast<unsigned char>(headerLine[r2]))) r2++;
				if (r2 > lpos) width = std::max(1, std::stoi(headerLine.substr(lpos, r2 - lpos)));
			}
		}
	}
	if (line >= 1) {
		// Extract line text
		int curLine = 1;
		size_t i = 0, startIdx = 0;
		for (; i < src.size(); ++i) { if (curLine == line) { startIdx = i; break; } if (src[i] == '\n') curLine++; }
		size_t j = startIdx; while (j < src.size() && src[j] != '\n' && src[j] != '\r') j++;
		std::string lineStr = (curLine == line) ? src.substr(startIdx, j - startIdx) : std::string();
		std::string head = colorize("header", std::string("[ALang Error]"), "RED");
		// Render token inside the code line
		int c0 = std::max(1, col) - 1; int w = std::max(1, width);
		if (c0 > static_cast<int>(lineStr.size())) c0 = static_cast<int>(lineStr.size());
		int endPos = std::min(static_cast<int>(lineStr.size()), c0 + w);
		std::string before = lineStr.substr(0, c0);
		std::string mid = lineStr.substr(c0, endPos - c0);
		std::string after = lineStr.substr(endPos);
		std::string codeLine = colorize("code", before, "LIGHT_GRAY")
							 + colorize("token", mid, "RED")
							 + colorize("code", after, "LIGHT_GRAY");
		// Build caret: ^~~~ style
		std::string caretStr;
		if (width == 1) {
			caretStr = colorize("caret", "^", "RED");
		} else {
			caretStr = colorize("caret", "^" + std::string(width - 1, '~'), "RED");
		}
		// line prefix (colored): optional filename + "line N: "
		std::string filePrefix;
		if (!filename.empty()) {
			filePrefix = colorize("fileLabel", std::string("file "), "YELLOW")
					   + colorize("fileValue", filename, "CYAN")
					   + colorize("lineLabel", std::string(", "), "YELLOW");
		}
		std::string linePrefix = colorize("lineLabel", std::string("line "), "YELLOW")
							   + colorize("lineValue", std::to_string(line), "CYAN")
							   + colorize("lineLabel", std::string(": "), "YELLOW");
		int prefixLen = 5 + static_cast<int>(std::to_string(line).size()) + 2; // "line " + digits + ": "
		std::cerr << head << " " << sanitizeHeaderMsg(headerLine) << "\n"
				  << (filePrefix.empty() ? std::string() : filePrefix) << linePrefix << codeLine << "\n"
				  << std::string((int)(filePrefix.empty() ? 0 : 0) + prefixLen + (col > 1 ? col - 1 : 0), ' ') << caretStr
				  << std::endl;
		if (!extraLines.empty()) {
			// 若附加消息包含 '^'（可能自带代码/插入符），避免与我们生成的上下文重复输出
			if (extraLines.find('^') == std::string::npos) {
				std::cerr << extraLines << std::endl;
			}
		}
		return;
	}
	// Fallback
	std::string head = colorize("header", std::string("[ALang Error]"), "RED");
	std::cerr << head << " " << sanitizeHeaderMsg(headerLine) << std::endl;
}

void ALangEngine::execute(const std::string& code) {
	try {
		Lexer lx(code);
		auto tokens = lx.scanTokens();
		Parser ps(tokens, code);
		auto stmts = ps.parse();
		impl->interpreter.execute(stmts);
	} catch (const ExceptionSignal& ex) {
		// Prefer imported-file context if any
		std::string altSrc, altFile;
		if (impl->interpreter.takeErrorContext(altSrc, altFile)) {
			printErrorWithContext(altSrc, toString(ex.value), altFile);
		} else {
			printErrorWithContext(code, toString(ex.value));
		}
		throw;
	} catch (const std::exception& ex) {
		std::string altSrc, altFile;
		if (impl->interpreter.takeErrorContext(altSrc, altFile)) {
			printErrorWithContext(altSrc, ex.what(), altFile);
		} else {
			printErrorWithContext(code, ex.what());
		}
		throw; // 也可选择吞掉错误，根据需要
	}
}

void ALangEngine::registerModule(const char* /*moduleName*/, std::function<void()> initFunc) {
	if (initFunc) initFunc();
}

// 宿主类注册：使用内置Function包装，将NativeValue与内部Value互转
static Value nativeToValue(const ALangEngine::NativeValue& nv) {
	switch (nv.index()) {
		case 0: return Value{std::monostate{}};
		case 1: return Value{std::get<double>(nv)};
		case 2: return Value{std::get<std::string>(nv)};
		case 3: return Value{std::get<bool>(nv)};
	}
	return Value{std::monostate{}};
}
static ALangEngine::NativeValue valueToNative(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return ALangEngine::NativeValue{std::monostate{}};
	if (auto d = std::get_if<double>(&v)) return ALangEngine::NativeValue{*d};
	if (auto s = std::get_if<std::string>(&v)) return ALangEngine::NativeValue{*s};
	if (auto b = std::get_if<bool>(&v)) return ALangEngine::NativeValue{*b};
	// 非基元类型一律视作 null
	return ALangEngine::NativeValue{std::monostate{}};
}

// Host <-> internal value marshaling for HostValue bridge
static ALangEngine::HostValue valueToHost(const Value& v) {
	using HV = ALangEngine::HostValue;
	if (std::holds_alternative<std::monostate>(v)) return HV::Null();
	if (auto d = std::get_if<double>(&v)) return HV::Number(*d);
	if (auto s = std::get_if<std::string>(&v)) return HV::String(*s);
	if (auto b = std::get_if<bool>(&v)) return HV::Bool(*b);
	// For complex types, expose an opaque pointer to the underlying Value
	return HV::Opaque((void*)&v);
}

static Value hostToValue(const ALangEngine::HostValue& hv) {
	using HV = ALangEngine::HostValue;
	switch (hv.type()) {
		case HV::Type::Null: return Value{std::monostate{}};
		case HV::Type::Number: return Value{hv.asNumber()};
		case HV::Type::String: return Value{hv.asString()};
		case HV::Type::Bool: return Value{hv.asBool()};
		case HV::Type::Opaque: {
			void* p = hv.asOpaque();
			if (!p) return Value{std::monostate{}};
			// Assume opaque points to a Value allocated/managed by the engine
			return *reinterpret_cast<Value*>(p);
		}
	}
	return Value{std::monostate{}};
}

static ALangEngine::NativeValue hostValueToNative(const ALangEngine::HostValue& hv) {
	Value v = hostToValue(hv);
	return valueToNative(v);
}

static ALangEngine::HostValue nativeToHostValue(const ALangEngine::NativeValue& nv) {
	Value v = nativeToValue(nv);
	return valueToHost(v);
}

void ALangEngine::registerClass(
	const std::string& className,
	NativeFunc constructor,
	const std::unordered_map<std::string, NativeFunc>& methods,
	const std::vector<std::string>& baseClasses
) {
	// 构造ClassInfo
	auto& interp = impl->interpreter;
	// 访问私有类型：此处位于同一翻译单元，直接构建ClassInfo
	auto klass = std::make_shared<ClassInfo>();
	klass->name = className;
	for (auto& bn : baseClasses) {
		try {
			Value bv = interp.globalsEnv()->get(bn);
			if (std::holds_alternative<std::shared_ptr<ClassInfo>>(bv)) {
				klass->supers.push_back(std::get<std::shared_ptr<ClassInfo>>(bv));
			}
		} catch (...) { /* 忽略缺失的基类 */ }
	}

	// 构造器
	if (constructor) {
		auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
		fn->builtin = [constructor](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			std::vector<NativeValue> na; na.reserve(args.size());
			for (auto& a : args) na.push_back(valueToNative(a));
			void* thisHandle = nullptr;
			if (clos) {
				try {
					Value tv = clos->get("this");
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) thisHandle = pins->get();
				} catch (...) {}
			}
			auto ret = constructor(na, thisHandle);
			return nativeToValue(ret);
		};
		klass->methods["constructor"] = fn;
	}
	// 方法
	for (auto& kv : methods) {
		auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
		auto native = kv.second;
		fn->builtin = [native](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
			std::vector<NativeValue> na; na.reserve(args.size());
			for (auto& a : args) na.push_back(valueToNative(a));
			void* thisHandle = nullptr;
			if (clos) {
				try {
					Value tv = clos->get("this");
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&tv)) thisHandle = pins->get();
				} catch (...) {}
			}
			auto ret = native(na, thisHandle);
			return nativeToValue(ret);
		};
		klass->methods[kv.first] = fn;
	}

		// 注入全局
		impl->interpreter.globalsEnv()->define(className, klass);
		// impl->interpreter.registerPackageSymbol("std.math", className, klass);
}

void ALangEngine::registerClassValue(
	const std::string& className,
	HostFunc constructor,
	const std::unordered_map<std::string, HostFunc>& methods,
	const std::vector<std::string>& baseClasses
) {
	// Wrap HostFunc into NativeFunc by marshaling
	ALangEngine::NativeFunc ctorWrap = nullptr;
	if (constructor) {
		ctorWrap = [constructor](const std::vector<ALangEngine::NativeValue>& na, void* thisHandle)->ALangEngine::NativeValue {
			std::vector<ALangEngine::HostValue> ha; ha.reserve(na.size());
			for (auto& a : na) ha.push_back(nativeToHostValue(a));
			auto ret = constructor(ha, thisHandle);
			return hostValueToNative(ret);
		};
	}

	std::unordered_map<std::string, ALangEngine::NativeFunc> nativeMethods;
	for (auto& kv : methods) {
		if (!kv.second) continue;
		nativeMethods[kv.first] = [hf = kv.second](const std::vector<ALangEngine::NativeValue>& na, void* thisHandle)->ALangEngine::NativeValue {
			std::vector<ALangEngine::HostValue> ha; ha.reserve(na.size());
			for (auto& a : na) ha.push_back(nativeToHostValue(a));
			auto ret = hf(ha, thisHandle);
			return hostValueToNative(ret);
		};
	}

	// Delegate to existing registerClass
	registerClass(className, ctorWrap, nativeMethods, baseClasses);
}

ALangEngine::HostValue ALangEngine::callFunctionValue(
	const std::string& functionName,
	const std::vector<HostValue>& args
) {
	std::vector<NativeValue> na; na.reserve(args.size());
	for (auto& a : args) na.push_back(hostValueToNative(a));
	NativeValue nv = callFunction(functionName, na);
	return nativeToHostValue(nv);
}

ALangEngine::NativeValue ALangEngine::callFunction(
	const std::string& functionName,
	const std::vector<NativeValue>& args
) {
	try {
		std::vector<Value> va; va.reserve(args.size());
		for (auto& a : args) va.push_back(nativeToValue(a));
		Value ret = impl->interpreter.callFunction(functionName, va);
		return valueToNative(ret);
	} catch (const std::exception& ex) {
		printErrorWithContext(impl->source, std::string("callFunction: ") + ex.what());
		throw;
	}
}

void ALangEngine::runEventLoopUntilIdle() {
	impl->interpreter.runEventLoopUntilIdle();
}

void ALangEngine::setImportBaseDir(const std::string& dir) {
	impl->interpreter.setImportBaseDir(dir);
}

// --- Host registration APIs ---
void ALangEngine::setGlobal(const std::string& name, const NativeValue& value) {
	try {
		Value v = nativeToValue(value);
		impl->interpreter.globalsEnv()->define(name, v);
	} catch (const std::exception& ex) {
		printErrorWithContext(impl->source, std::string("setGlobal: ") + ex.what());
		throw;
	}
}

void ALangEngine::registerFunction(const std::string& name, NativeFunc func) {
	if (!func) return;
	auto fn = std::make_shared<Function>();
	fn->isBuiltin = true;
	fn->builtin = [func](const std::vector<Value>& args, std::shared_ptr<Environment> /*clos*/) -> Value {
		std::vector<NativeValue> na; na.reserve(args.size());
		for (auto& a : args) na.push_back(valueToNative(a));
		auto ret = func(na, nullptr);
		return nativeToValue(ret);
	};
	impl->interpreter.globalsEnv()->define(name, fn);
}

void ALangEngine::setGlobalValue(const std::string& name, const HostValue& value) {
	try {
		Value v = hostToValue(value);
		impl->interpreter.globalsEnv()->define(name, v);
	} catch (const std::exception& ex) {
		printErrorWithContext(impl->source, std::string("setGlobalValue: ") + ex.what());
		throw;
	}
}

void ALangEngine::registerFunctionValue(const std::string& name, HostFunc func) {
	if (!func) return;
	auto fn = std::make_shared<Function>();
	fn->isBuiltin = true;
	fn->builtin = [func](const std::vector<Value>& args, std::shared_ptr<Environment> /*clos*/) -> Value {
		std::vector<ALangEngine::HostValue> ha; ha.reserve(args.size());
		for (auto& a : args) ha.push_back(valueToHost(a));
		auto ret = func(ha, nullptr);
		return hostToValue(ret);
	};
	impl->interpreter.globalsEnv()->define(name, fn);
}

void ALangEngine::registerInterface(const std::string& name, const std::vector<std::string>& methodNames) {
	// Interface is represented as a ClassInfo with method placeholders
	auto klass = std::make_shared<ClassInfo>();
	klass->name = name;
	for (const auto& mn : methodNames) {
		if (klass->methods.find(mn) == klass->methods.end()) {
			klass->methods[mn] = nullptr; // placeholder
		}
	}
	impl->interpreter.globalsEnv()->define(name, klass);
}

