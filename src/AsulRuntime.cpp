#include "AsulRuntime.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace asul {

// ----------- Value Helper Functions -----------
std::string typeOf(const Value& v) {
	switch (v.index()) {
	case 0: return "null";
	case 1: return "number";
	case 2: return "string";
	case 3: return "boolean";
	case 4: return "function";
	case 5: return "array";
	case 6: return "object";
	case 7: return "class";
	case 8: return "instance";
	case 9: return "promise";
	default: return "unknown";
	}
}

bool isTruthy(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return false;
	if (auto b = std::get_if<bool>(&v)) return *b;
	if (auto n = std::get_if<double>(&v)) return *n != 0.0;
	if (auto s = std::get_if<std::string>(&v)) return !s->empty();
	if (std::holds_alternative<std::shared_ptr<Array>>(v)) return true;
	if (std::holds_alternative<std::shared_ptr<Object>>(v)) return true;
	return true;
}

bool valueEqual(const Value& a, const Value& b) {
	if (a.index() != b.index()) return false;
	switch (a.index()) {
		case 0: return true; // null
		case 1: return std::get<double>(a) == std::get<double>(b);
		case 2: return std::get<std::string>(a) == std::get<std::string>(b);
		case 3: return std::get<bool>(a) == std::get<bool>(b);
		case 4: return std::get<std::shared_ptr<Function>>(a).get() == std::get<std::shared_ptr<Function>>(b).get();
		case 5: return std::get<std::shared_ptr<Array>>(a).get() == std::get<std::shared_ptr<Array>>(b).get();
		case 6: return std::get<std::shared_ptr<Object>>(a).get() == std::get<std::shared_ptr<Object>>(b).get();
		case 7: return std::get<std::shared_ptr<ClassInfo>>(a).get() == std::get<std::shared_ptr<ClassInfo>>(b).get();
		case 8: return std::get<std::shared_ptr<Instance>>(a).get() == std::get<std::shared_ptr<Instance>>(b).get();
		case 9: return std::get<std::shared_ptr<PromiseState>>(a).get() == std::get<std::shared_ptr<PromiseState>>(b).get();
		default: return false;
	}
}

size_t valueHash(const Value& v) {
	std::hash<std::string> sh;
	std::hash<double> dh;
	switch (v.index()) {
		case 0: return 0x9e3779b97f4a7c15ULL; // null const
		case 1: { // number
			double d = std::get<double>(v);
			return dh(d) ^ (0x9e3779b97f4a7c15ULL + (dh(d)<<6) + (dh(d)>>2));
		}
		case 2: { // string
			const std::string &s = std::get<std::string>(v);
			return sh(s);
		}
		case 3: { // bool
			bool b = std::get<bool>(v);
			return b ? 1231u : 3413u;
		}
		case 4: { auto p = std::get<std::shared_ptr<Function>>(v).get(); return reinterpret_cast<size_t>(p); }
		case 5: { auto p = std::get<std::shared_ptr<Array>>(v).get(); return reinterpret_cast<size_t>(p); }
		case 6: { auto p = std::get<std::shared_ptr<Object>>(v).get(); return reinterpret_cast<size_t>(p); }
		case 7: { auto p = std::get<std::shared_ptr<ClassInfo>>(v).get(); return reinterpret_cast<size_t>(p); }
		case 8: { auto p = std::get<std::shared_ptr<Instance>>(v).get(); return reinterpret_cast<size_t>(p); }
		case 9: { auto p = std::get<std::shared_ptr<PromiseState>>(v).get(); return reinterpret_cast<size_t>(p); }
		default: return 0;
	}
}

std::string toString(const Value& v) {
	if (std::holds_alternative<std::monostate>(v)) return "null";
	if (auto n = std::get_if<double>(&v)) {
		std::ostringstream oss; oss << *n; return oss.str();
	}
	if (auto s = std::get_if<std::string>(&v)) return *s;
	if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
	if (std::holds_alternative<std::shared_ptr<Function>>(v)) return "[Function]";
	if (auto arr = std::get_if<std::shared_ptr<Array>>(&v)) {
		std::ostringstream oss; oss << "[";
		if (*arr) {
			for (size_t i=0;i<(*arr)->size();++i) {
				if (i) oss << ", ";
				oss << toString((**arr)[i]);
			}
		}
		oss << "]"; return oss.str();
	}
	if (auto obj = std::get_if<std::shared_ptr<Object>>(&v)) {
		std::ostringstream oss; oss << "{"; bool first=true;
		if (*obj) {
			for (auto& kv : **obj) {
				if (!first) oss << ", "; first=false;
				oss << kv.first << ": " << toString(kv.second);
			}
		}
		oss << "}"; return oss.str();
	}
	if (std::holds_alternative<std::shared_ptr<ClassInfo>>(v)) return "[Class]";
	if (auto inst = std::get_if<std::shared_ptr<Instance>>(&v)) {
		if (*inst && (*inst)->klass) {
			if ((*inst)->klass->name == "Date") {
				auto it = (*inst)->fields.find("iso");
				if (it != (*inst)->fields.end() && std::holds_alternative<std::string>(it->second)) {
					return std::get<std::string>(it->second);
				}
			}
			if ((*inst)->klass->name == "Duration") {
				auto it = (*inst)->fields.find("milliseconds");
				if (it != (*inst)->fields.end() && std::holds_alternative<double>(it->second)) {
					std::ostringstream oss; oss << "Duration(" << std::get<double>(it->second) << "ms)";
					return oss.str();
				}
			}
		}
		return "[Object]";
	}
	if (std::holds_alternative<std::shared_ptr<PromiseState>>(v)) return "[Promise]";
	return "unknown";
}

// ----------- Environment -----------
void Environment::define(const std::string& name, const Value& val) { values[name] = val; }

void Environment::defineWithType(const std::string& name, const Value& val, const std::optional<std::string>& typeName) {
	values[name] = val;
	if (typeName && !typeName->empty()) declaredTypes[name] = *typeName;
}

std::optional<std::string> Environment::getDeclaredType(const std::string& name) {
	auto it = declaredTypes.find(name);
	if (it != declaredTypes.end()) return it->second;
	if (parent) return parent->getDeclaredType(name);
	return std::nullopt;
}

bool Environment::assign(const std::string& name, const Value& val) {
	if (values.find(name) != values.end()) { values[name] = val; return true; }
	if (parent) return parent->assign(name, val);
	return false;
}

Value Environment::get(const std::string& name) {
	auto it = values.find(name);
	if (it != values.end()) return it->second;
	if (parent) return parent->get(name);
	throw std::runtime_error("Undefined variable '" + name + "'");
}

// ----------- Stream Wrappers -----------
size_t FStreamWrapper::read(char* buf, size_t n) {
	fs.read(buf, n);
	return static_cast<size_t>(fs.gcount());
}

void FStreamWrapper::write(const char* buf, size_t n) {
	fs.write(buf, n);
}

void FStreamWrapper::close() { fs.close(); }

bool FStreamWrapper::eof() { return fs.eof(); }

size_t StdinWrapper::read(char* buf, size_t n) {
	std::cin.read(buf, n);
	return static_cast<size_t>(std::cin.gcount());
}

void StdinWrapper::write(const char* buf, size_t n) { }

void StdinWrapper::close() { }

bool StdinWrapper::eof() { return std::cin.eof(); }

size_t StdoutWrapper::read(char* buf, size_t n) { return 0; }

void StdoutWrapper::write(const char* buf, size_t n) {
	std::cout.write(buf, n);
	std::cout.flush();
}

void StdoutWrapper::close() { }

size_t StderrWrapper::read(char* buf, size_t n) { return 0; }

void StderrWrapper::write(const char* buf, size_t n) {
	std::cerr.write(buf, n);
	std::cerr.flush();
}

void StderrWrapper::close() { }

size_t FilePtrWrapper::read(char* buf, size_t n) {
	return fread(buf, 1, n, fp);
}

void FilePtrWrapper::write(const char* buf, size_t n) {
	fwrite(buf, 1, n, fp);
}

void FilePtrWrapper::close() {
	if (fp && closer) {
		closer(fp);
		fp = nullptr;
	}
}

bool FilePtrWrapper::eof() { return feof(fp); }

} // namespace asul
