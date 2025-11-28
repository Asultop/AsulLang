#pragma once

// 这个头文件包含json package的模板实现
// 它会被ALangEngine.cpp包含，然后在installBuiltins函数中实例化

namespace AsulModule {
    namespace Json {
        // 模板函数，用于初始化json package
        // 这个函数会在ALangEngine.cpp中实例化，这样就能访问ALangEngine.cpp中匿名命名空间的类型
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr jsonPkg) {
            auto parseFn = std::make_shared<typename FunctionPtr::element_type>();
            parseFn->isBuiltin = true;
            parseFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() != 1) throw std::runtime_error("parse expects 1 argument (json string)");
                if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("parse argument must be string");
                std::string s = std::get<std::string>(args[0]);
                size_t i = 0;
                auto skip = [&](void){ while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; };
                std::function<ValueType()> parseValue;
                std::function<std::string()> parseString = [&]() -> std::string {
                    if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON parse error: expected '\"' at position " + std::to_string(i));
                    i++; std::string out;
                    while (i < s.size()) {
                        char c = s[i++];
                        if (c == '"') return out;
                        if (c == '\\') {
                            if (i >= s.size()) break;
                            char e = s[i++];
                            switch (e) {
                            case '"': out.push_back('"'); break;
                            case '\\': out.push_back('\\'); break;
                            case '/': out.push_back('/'); break;
                            case 'b': out.push_back('\b'); break;
                            case 'f': out.push_back('\f'); break;
                            case 'n': out.push_back('\n'); break;
                            case 'r': out.push_back('\r'); break;
                            case 't': out.push_back('\t'); break;
                            case 'u': {
                                if (i + 4 > s.size()) throw std::runtime_error("JSON parse error: invalid unicode escape");
                                unsigned int code = 0;
                                for (int k=0;k<4;k++) { char h = s[i++]; code <<= 4; if (h>='0'&&h<='9') code += h-'0'; else if (h>='a'&&h<='f') code += 10 + (h-'a'); else if (h>='A'&&h<='F') code += 10 + (h-'A'); else throw std::runtime_error("JSON parse error: invalid unicode hex"); }
                                if (code <= 0x7F) out.push_back((char)code);
                                else if (code <= 0x7FF) { out.push_back((char)(0xC0 | ((code >> 6) & 0x1F))); out.push_back((char)(0x80 | (code & 0x3F))); }
                                else { out.push_back((char)(0xE0 | ((code >> 12) & 0x0F))); out.push_back((char)(0x80 | ((code >> 6) & 0x3F))); out.push_back((char)(0x80 | (code & 0x3F))); }
                                break;
                            }
                            default: out.push_back(e); break;
                            }
                        } else {
                            out.push_back(c);
                        }
                    }
                    throw std::runtime_error("JSON parse error: unterminated string");
                };
                std::function<ValueType()> parseNumber = [&]() -> ValueType {
                    size_t start = i; if (s[i] == '-') i++; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++; if (i < s.size() && s[i] == '.') { i++; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++; } if (i < s.size() && (s[i]=='e' || s[i]=='E')) { i++; if (s[i]=='+'||s[i]=='-') i++; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++; }
                    double val = 0.0; try { val = std::stod(s.substr(start, i - start)); } catch(...) { throw std::runtime_error("JSON parse error: invalid number"); }
                    return ValueType{ val };
                };
                std::function<ValueType()> parseArray = [&]() -> ValueType {
                    i++; skip(); auto arr = std::make_shared<typename ArrayPtr::element_type>(); if (i < s.size() && s[i] == ']') { i++; return ValueType{arr}; }
                    for (;;) { skip(); arr->push_back(parseValue()); skip(); if (i < s.size() && s[i] == ',') { i++; continue; } if (i < s.size() && s[i] == ']') { i++; break; } throw std::runtime_error("JSON parse error: expected ',' or ']' in array"); }
                    return ValueType{arr};
                };
                std::function<ValueType()> parseObject = [&]() -> ValueType {
                    i++; skip(); auto obj = std::make_shared<typename ObjectPtr::element_type>(); if (i < s.size() && s[i] == '}') { i++; return ValueType{obj}; }
                    for (;;) { skip(); if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON parse error: expected string key"); std::string key = parseString(); skip(); if (i >= s.size() || s[i] != ':') throw std::runtime_error("JSON parse error: expected ':' after key"); i++; skip(); ValueType v = parseValue(); (*obj)[key] = v; skip(); if (i < s.size() && s[i] == ',') { i++; continue; } if (i < s.size() && s[i] == '}') { i++; break; } throw std::runtime_error("JSON parse error: expected ',' or '}' in object"); }
                    return ValueType{obj};
                };
                parseValue = [&]() -> ValueType { skip(); if (i >= s.size()) throw std::runtime_error("JSON parse error: empty input"); char c = s[i]; if (c == '{') return parseObject(); if (c == '[') return parseArray(); if (c == '"') return ValueType{ parseString() }; if (c == 't' && s.substr(i,4)=="true") { i+=4; return ValueType{ true }; } if (c == 'f' && s.substr(i,5)=="false") { i+=5; return ValueType{ false }; } if (c == 'n' && s.substr(i,4)=="null") { i+=4; return ValueType{ std::monostate{} }; } if (c=='-'||std::isdigit(static_cast<unsigned char>(c))) return parseNumber(); throw std::runtime_error(std::string("JSON parse error at position ")+std::to_string(i)); };
                ValueType res = parseValue(); skip(); if (i != s.size()) throw std::runtime_error("JSON parse error: trailing characters"); return res;
            };
            (*jsonPkg)["parse"] = ValueType{ parseFn };

            // stringify(value) -> string (basic)
            auto stringifyFn = std::make_shared<typename FunctionPtr::element_type>();
            stringifyFn->isBuiltin = true;
            stringifyFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() < 1) throw std::runtime_error("stringify expects at least 1 argument (value)");
                std::function<std::string(const ValueType&)> emit;
                auto escapeStr = [](const std::string& in)->std::string { std::string out; out.push_back('"'); for (char c : in) { switch(c){ case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\b': out += "\\b"; break; case '\f': out += "\\f"; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break; default: if ((unsigned char)c < 0x20) { char buf[8]; std::snprintf(buf,sizeof(buf),"\\u%04x",(unsigned char)c); out += buf; } else out.push_back(c); break; } } out.push_back('"'); return out; };
                emit = [&](const ValueType& v)->std::string {
                    if (std::holds_alternative<std::monostate>(v)) return "null";
                    if (auto n = std::get_if<double>(&v)) { std::ostringstream o; o<<*n; return o.str(); }
                    if (auto s = std::get_if<std::string>(&v)) return escapeStr(*s);
                    if (auto b = std::get_if<bool>(&v)) return *b?"true":"false";
                    if (auto arr = std::get_if<ArrayPtr>(&v)) { auto a=*arr; if(!a) return "[]"; std::ostringstream o; o<<"["; for(size_t i=0;i<a->size();++i){ if(i) o<<","; o<<emit((*a)[i]); } o<<"]"; return o.str(); }
                    if (auto obj = std::get_if<ObjectPtr>(&v)) { auto oobj=*obj; if(!oobj) return "{}"; std::ostringstream o; o<<"{"; bool first=true; for(auto &kv : *oobj){ if(!first) o<<","; first=false; o<<escapeStr(kv.first)<<":"<<emit(kv.second);} o<<"}"; return o.str(); }
                    return "null";
                };
                return ValueType{ emit(args[0]) };
            };
            (*jsonPkg)["stringify"] = ValueType{ stringifyFn };
        }
    }
}
