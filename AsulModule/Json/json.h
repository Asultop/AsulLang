#pragma once

#include <memory>
#include <vector>
#include <stdexcept>
#include <variant>
#include <cmath>
#include <functional>

// 这个头文件包含json package的模板实现
// 它会被ALangEngine.cpp包含，然后在installBuiltins函数中实例化

namespace AsulModule {
    namespace Json {
        // 模板函数，用于初始化json package
        // 这个函数会在ALangEngine.cpp中实例化，这样就能访问ALangEngine.cpp中匿名命名空间的类型
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr jsonPkg) {
            // json.parse(text) -> object/array
            auto parseFn = std::make_shared<typename FunctionPtr::element_type>();
            parseFn->isBuiltin = true;
            parseFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() != 1) throw std::runtime_error("json.parse expects 1 argument (string)");
                if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("json.parse argument must be string");
                std::string s = std::get<std::string>(args[0]);
                size_t i = 0; auto n = s.size();
                auto skipWS = [&](){ while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) i++; };
                auto parseString = [&]() -> std::string {
                    if (i >= n || s[i] != '"') throw std::runtime_error("json: expected '\"'");
                    i++; std::string out;
                    while (i < n) {
                        if (s[i] == '\\') {
                            i++; if (i >= n) throw std::runtime_error("json: unexpected end after '\\'");
                            switch (s[i]) {
                                case '"': out += '"'; break;
                                case '\\': out += '\\'; break;
                                case '/': out += '/'; break;
                                case 'b': out += '\b'; break;
                                case 'f': out += '\f'; break;
                                case 'n': out += '\n'; break;
                                case 'r': out += '\r'; break;
                                case 't': out += '\t'; break;
                                case 'u': {
                                    i++; std::string hex;
                                    for (int j=0; j<4; j++) { if (i >= n) throw std::runtime_error("json: unexpected end in unicode escape"); hex += s[i++]; }
                                    try { out += static_cast<char>(std::stoul(hex, nullptr, 16)); }
                                    catch (...) { throw std::runtime_error("json: invalid unicode escape"); }
                                    break;
                                }
                                default: throw std::runtime_error("json: invalid escape sequence");
                            }
                        } else if (s[i] == '"') { i++; break; }
                        else out += s[i];
                        i++;
                    }
                    return out;
                };
                std::function<ValueType()> parseValue;
                auto parseArray = [&]() -> ArrayPtr {
                    if (i >= n || s[i] != '[') throw std::runtime_error("json: expected '['");
                    i++; auto arr = std::make_shared<typename ArrayPtr::element_type>();
                    skipWS(); if (i < n && s[i] == ']') { i++; return arr; }
                    for (;;) {
                        arr->push_back(parseValue());
                        skipWS(); if (i >= n) throw std::runtime_error("json: unexpected end in array");
                        if (s[i] == ']') { i++; return arr; }
                        if (s[i] != ',') throw std::runtime_error("json: expected ',' or ']'");
                        i++;
                    }
                };
                auto parseObject = [&]() -> ObjectPtr {
                    if (i >= n || s[i] != '{') throw std::runtime_error("json: expected '{'");
                    i++; auto obj = std::make_shared<typename ObjectPtr::element_type>();
                    skipWS(); if (i < n && s[i] == '}') { i++; return obj; }
                    for (;;) {
                        skipWS(); std::string key = parseString();
                        skipWS(); if (i >= n || s[i] != ':') throw std::runtime_error("json: expected ':'");
                        i++; (*obj)[key] = parseValue();
                        skipWS(); if (i >= n) throw std::runtime_error("json: unexpected end in object");
                        if (s[i] == '}') { i++; return obj; }
                        if (s[i] != ',') throw std::runtime_error("json: expected ',' or '}'");
                        i++;
                    }
                };
                parseValue = [&]() -> ValueType {
                    skipWS(); if (i >= n) throw std::runtime_error("json: unexpected end");
                    if (s[i] == '{') return ValueType{ parseObject() };
                    if (s[i] == '[') return ValueType{ parseArray() };
                    if (s[i] == '"') return ValueType{ parseString() };
                    if (s[i] == 't' && i+3 < n && s.substr(i,4) == "true") { i +=4; return ValueType{ true }; }
                    if (s[i] == 'f' && i+4 < n && s.substr(i,5) == "false") { i +=5; return ValueType{ false }; }
                    if (s[i] == 'n' && i+3 < n && s.substr(i,4) == "null") { i +=4; return ValueType{}; }
                    if (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-') {
                        size_t st = i;
                        if (s[i] == '-') i++;
                        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
                        if (i < n && s[i] == '.') { i++; while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) i++; }
                        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
                            i++; if (i < n && (s[i] == '+' || s[i] == '-')) i++;
                            while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
                        }
                        std::string numStr = s.substr(st, i-st);
                        try {
                            double d = std::stod(numStr);
                            return ValueType{ d };
                        } catch (...) { throw std::runtime_error("json: invalid number"); }
                    }
                    throw std::runtime_error("json: unexpected character");
                };
                auto result = parseValue();
                skipWS(); if (i < n) throw std::runtime_error("json: unexpected trailing characters");
                return result;
            };
            (*jsonPkg)["parse"] = ValueType{ parseFn };

            // json.stringify(value) -> string
            auto stringifyFn = std::make_shared<typename FunctionPtr::element_type>();
            stringifyFn->isBuiltin = true;
            stringifyFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() != 1) throw std::runtime_error("json.stringify expects 1 argument");
                std::string out;
                auto stringify = [&](const ValueType& v, auto&& stringify_ref) -> void {
                    if (std::holds_alternative<std::monostate>(v)) {
                        out += "null";
                    } else if (std::holds_alternative<bool>(v)) {
                        out += std::get<bool>(v) ? "true" : "false";
                    } else if (std::holds_alternative<double>(v)) {
                        double d = std::get<double>(v);
                        if (std::isnan(d) || std::isinf(d)) throw std::runtime_error("json: NaN/Infinity not allowed");
                        std::string s = std::to_string(d);
                        // Remove trailing .0 if present
                        if (s.find('.') != std::string::npos) {
                            s.erase(s.find_last_not_of('0') + 1);
                            if (s.back() == '.') s.pop_back();
                        }
                        out += s;
                    } else if (std::holds_alternative<std::string>(v)) {
                        out += '"';
                        for (char c : std::get<std::string>(v)) {
                            switch (c) {
                                case '"': out += '\\'; out += '"'; break;
                                case '\\': out += '\\'; out += '\\'; break;
                                case '\b': out += '\\'; out += 'b'; break;
                                case '\f': out += '\\'; out += 'f'; break;
                                case '\n': out += '\\'; out += 'n'; break;
                                case '\r': out += '\\'; out += 'r'; break;
                                case '\t': out += '\\'; out += 't'; break;
                                default: out += c; break;
                            }
                        }
                        out += '"';
                    } else if (std::holds_alternative<ObjectPtr>(v)) {
                        auto obj = std::get<ObjectPtr>(v);
                        out += '{';
                        bool first = true;
                        for (const auto& [k, val] : *obj) {
                            if (!first) out += ',';
                            first = false;
                            out += '"'; out += k; out += '"'; out += ':';
                            stringify_ref(val, stringify_ref);
                        }
                        out += '}';
                    } else if (std::holds_alternative<ArrayPtr>(v)) {
                        auto arr = std::get<ArrayPtr>(v);
                        out += '[';
                        bool first = true;
                        for (const auto& val : *arr) {
                            if (!first) out += ',';
                            first = false;
                            stringify_ref(val, stringify_ref);
                        }
                        out += ']';
                    }
                };
                stringify(args[0], stringify);
                return ValueType{ out };
            };
            (*jsonPkg)["stringify"] = ValueType{ stringifyFn };
        }
    }
}
