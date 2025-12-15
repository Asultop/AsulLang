#include "StdEncoding.h"
#include "../../../AsulInterpreter.h"
#include <iomanip>
#include <sstream>

namespace asul {

void registerStdEncodingPackage(Interpreter& interp) {
	auto encPkg = interp.ensurePackage("std.encoding");
	
	// Base64
	auto base64Obj = std::make_shared<Object>();
	(*encPkg)["base64"] = Value{base64Obj};
	
	// base64.encode(str)
	auto b64enc = std::make_shared<Function>(); b64enc->isBuiltin = true;
	b64enc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("base64.encode 需要字符串参数");
		std::string in = toString(args[0]);
		static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		int val = 0, valb = -6;
		for (unsigned char c : in) {
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0) {
				out.push_back(chars[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		if (valb > -6) out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
		while (out.size() % 4) out.push_back('=');
		return Value{out};
	};
	(*base64Obj)["encode"] = Value{b64enc};

	// base64.decode(str)
	auto b64dec = std::make_shared<Function>(); b64dec->isBuiltin = true;
	b64dec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("base64.decode 需要字符串参数");
		std::string in = toString(args[0]);
		static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::vector<int> T(256, -1);
		for (int i=0; i<64; i++) T[chars[i]] = i;
		
		std::string out;
		int val = 0, valb = -8;
		for (unsigned char c : in) {
			if (T[c] == -1) break;
			val = (val << 6) + T[c];
			valb += 6;
			if (valb >= 0) {
				out.push_back(char((val >> valb) & 0xFF));
				valb -= 8;
			}
		}
		return Value{out};
	};
	(*base64Obj)["decode"] = Value{b64dec};

	// Base64URL (URL-safe variant)
	auto base64urlObj = std::make_shared<Object>();
	(*encPkg)["base64url"] = Value{base64urlObj};
	
	// base64url.encode(str)
	auto b64urlenc = std::make_shared<Function>(); b64urlenc->isBuiltin = true;
	b64urlenc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("base64url.encode 需要字符串参数");
		std::string in = toString(args[0]);
		static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		int val = 0, valb = -6;
		for (unsigned char c : in) {
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0) {
				out.push_back(chars[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		if (valb > -6) out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
		// Base64URL typically doesn't use padding
		return Value{out};
	};
	(*base64urlObj)["encode"] = Value{b64urlenc};

	// base64url.decode(str)
	auto b64urldec = std::make_shared<Function>(); b64urldec->isBuiltin = true;
	b64urldec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("base64url.decode 需要字符串参数");
		std::string in = toString(args[0]);
		static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::vector<int> T(256, -1);
		for (int i=0; i<64; i++) T[chars[i]] = i;
		
		std::string out;
		int val = 0, valb = -8;
		for (unsigned char c : in) {
			if (T[c] == -1) break;
			val = (val << 6) + T[c];
			valb += 6;
			if (valb >= 0) {
				out.push_back(char((val >> valb) & 0xFF));
				valb -= 8;
			}
		}
		return Value{out};
	};
	(*base64urlObj)["decode"] = Value{b64urldec};

	// bytesToString(arr): convert array of numeric byte values to a string
	auto bytesToStringFn = std::make_shared<Function>(); bytesToStringFn->isBuiltin = true;
	bytesToStringFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.size() != 1) throw std::runtime_error("bytesToString 需要1个数组参数");
		if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("bytesToString 参数必须是数组");
		auto arr = std::get<std::shared_ptr<Array>>(args[0]);
		if (!arr) return Value{std::string("")};
		std::string out;
		out.reserve(arr->size());
		for (auto &v : *arr) {
			double d = getNumber(v, "bytesToString element");
			unsigned char c = static_cast<unsigned char>(static_cast<int>(d));
			out.push_back(static_cast<char>(c));
		}
		return Value{out};
	};
	(*encPkg)["bytesToString"] = Value{bytesToStringFn};

	// Hex
	auto hexObj = std::make_shared<Object>();
	(*encPkg)["hex"] = Value{hexObj};
	
	// hex.encode(str)
	auto hexenc = std::make_shared<Function>(); hexenc->isBuiltin = true;
	hexenc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("hex.encode 需要字符串参数");
		std::string in = toString(args[0]);
		std::ostringstream oss;
		for (unsigned char c : in) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
		return Value{oss.str()};
	};
	(*hexObj)["encode"] = Value{hexenc};
	
	// hex.decode(str)
	auto hexdec = std::make_shared<Function>(); hexdec->isBuiltin = true;
	hexdec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("hex.decode 需要字符串参数");
		std::string in = toString(args[0]);
		if (in.size() % 2 != 0) throw std::runtime_error("十六进制字符串长度无效");
		std::string out;
		for (size_t i=0; i<in.size(); i+=2) {
			std::string byteStr = in.substr(i, 2);
			char c = (char)strtol(byteStr.c_str(), nullptr, 16);
			out.push_back(c);
		}
		return Value{out};
	};
	(*hexObj)["decode"] = Value{hexdec};

	// hex.toBytes(hexStr) - convert hex string to byte array
	auto hexToBytes = std::make_shared<Function>(); hexToBytes->isBuiltin = true;
	hexToBytes->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("hex.toBytes 需要字符串参数");
		std::string in = toString(args[0]);
		if (in.size() % 2 != 0) throw std::runtime_error("十六进制字符串长度无效");
		auto arr = std::make_shared<Array>();
		for (size_t i=0; i<in.size(); i+=2) {
			std::string byteStr = in.substr(i, 2);
			int byte = strtol(byteStr.c_str(), nullptr, 16);
			arr->push_back(Value{ static_cast<double>(byte) });
		}
		return Value{arr};
	};
	(*hexObj)["toBytes"] = Value{hexToBytes};

	// hex.fromBytes(arr) - convert byte array to hex string
	auto hexFromBytes = std::make_shared<Function>(); hexFromBytes->isBuiltin = true;
	hexFromBytes->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.size() != 1) throw std::runtime_error("hex.fromBytes 需要1个数组参数");
		if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("hex.fromBytes 参数必须是数组");
		auto arr = std::get<std::shared_ptr<Array>>(args[0]);
		if (!arr) return Value{std::string("")};
		std::ostringstream oss;
		for (auto &v : *arr) {
			double d = getNumber(v, "hex.fromBytes element");
			int byte = static_cast<int>(d) & 0xFF;
			oss << std::hex << std::setw(2) << std::setfill('0') << byte;
		}
		return Value{oss.str()};
	};
	(*hexObj)["fromBytes"] = Value{hexFromBytes};

	// URL
	auto urlObj = std::make_shared<Object>();
	(*encPkg)["url"] = Value{urlObj};
	
	// url.encode(str)
	auto urlenc = std::make_shared<Function>(); urlenc->isBuiltin = true;
	urlenc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("url.encode 需要字符串参数");
		std::string in = toString(args[0]);
		std::ostringstream oss;
		for (unsigned char c : in) {
			if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') oss << c;
			else oss << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
		}
		return Value{oss.str()};
	};
	(*urlObj)["encode"] = Value{urlenc};
	
	// url.decode(str)
	auto urldec = std::make_shared<Function>(); urldec->isBuiltin = true;
	urldec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
		if (args.empty()) throw std::runtime_error("url.decode 需要字符串参数");
		std::string in = toString(args[0]);
		std::string out;
		for (size_t i=0; i<in.size(); ++i) {
			if (in[i] == '%') {
				if (i + 2 < in.size()) {
					std::string hex = in.substr(i+1, 2);
					char c = (char)strtol(hex.c_str(), nullptr, 16);
					out.push_back(c);
					i += 2;
				} else {
					out.push_back('%');
				}
			} else if (in[i] == '+') {
				out.push_back(' ');
			} else {
				out.push_back(in[i]);
			}
		}
		return Value{out};
	};
	(*urlObj)["decode"] = Value{urldec};
}

} // namespace asul
