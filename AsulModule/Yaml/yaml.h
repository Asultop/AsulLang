#pragma once

#include <memory>
#include <vector>
#include <stdexcept>
#include <variant>
#include <functional>
#include <cctype>
#include <sstream>
#include <string>

// 这个头文件包含yaml package的模板实现
// 它会被ALangEngine.cpp包含，然后在installBuiltins函数中实例化

namespace AsulModule {
    namespace Yaml {
        // 模板函数，用于初始化yaml package
        // 这个函数会在ALangEngine.cpp中实例化，这样就能访问ALangEngine.cpp中匿名命名空间的类型
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr yamlPkg);
    }
}

// 模板函数的实现
namespace AsulModule {
    namespace Yaml {
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr yamlPkg) {
            // yaml.parse(text) -> ALang Value (object/array/scalars) for a small subset
            auto parseFn = std::make_shared<typename FunctionPtr::element_type>();
            parseFn->isBuiltin = true;
            parseFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() != 1) throw std::runtime_error("yaml.parse expects 1 argument (string)");
                if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("yaml.parse argument must be string");
                std::string s = std::get<std::string>(args[0]);
                std::vector<std::string> lines; { 
                    std::istringstream iss(s); std::string line; while (std::getline(iss, line)) { if (!line.empty() && line.back()=='\r') line.pop_back(); lines.push_back(line); }
                }
                struct Ctx { int indent; ValueType value; bool isSeq; std::shared_ptr<typename ObjectPtr::element_type> parentMap; std::string keyInParent; }; 
                std::vector<Ctx> stack;
                auto newMap = [](){ return ValueType{ std::make_shared<typename ObjectPtr::element_type>() }; };
                auto newSeq = [](){ return ValueType{ std::make_shared<typename ArrayPtr::element_type>() }; };
                auto asMap = [](ValueType& v)->std::shared_ptr<typename ObjectPtr::element_type>{ return std::get<std::shared_ptr<typename ObjectPtr::element_type>>(v); };
                auto asSeq = [](ValueType& v)->std::shared_ptr<typename ArrayPtr::element_type>{ return std::get<std::shared_ptr<typename ArrayPtr::element_type>>(v); };
                auto parseScalar = [](const std::string& t)->ValueType{
                    if (t == "null" || t == "~" || t == "Null" || t == "NULL") return ValueType{ std::monostate{} };
                    if (t == "true" || t == "True" || t == "TRUE") return ValueType{ true };
                    if (t == "false" || t == "False" || t == "FALSE") return ValueType{ false };
                    // number
                    char* end=nullptr; double dv = std::strtod(t.c_str(), &end); if (end && *end=='\0' && !t.empty()) return ValueType{ dv };
                    return ValueType{ t };
                };
                auto currentIndent = [](const std::string& l){ int k=0; for(char c: l){ if(c==' ') k++; else break; } return k; };
                ValueType root{ std::make_shared<typename ObjectPtr::element_type>() };
                stack.push_back(Ctx{ -1, root, false, nullptr, std::string() });
                for (size_t idx=0; idx<lines.size(); ++idx) {
                    std::string line = lines[idx]; 
                    if (line.find_first_not_of(' ') == std::string::npos) continue; // skip empty
                    int ind = currentIndent(line); 
                    std::string trimmed = line.substr(ind);
                    // pop to matching indent
                    while (!stack.empty() && ind <= stack.back().indent) stack.pop_back();
                    if (stack.empty()) throw std::runtime_error("yaml: bad indentation");
                    // sequence item
                    if (trimmed.rfind("- ", 0) == 0) {
                        ValueType* container = &stack.back().value; 
                        if (!stack.back().isSeq) {
                            // If current context is a map created for a key, convert that key's value to a sequence
                            if (stack.back().parentMap) {
                                ValueType seq = newSeq();
                                (*stack.back().parentMap)[stack.back().keyInParent] = seq;
                                stack.back().value = seq;
                                stack.back().isSeq = true;
                            } else {
                                // Otherwise, create a new sequence context (e.g., at root)
                                stack.push_back(Ctx{ ind, newSeq(), true, nullptr, std::string() });
                                container = &stack.back().value;
                            }
                        }
                        auto itemText = trimmed.substr(2);
                        auto seq = asSeq(*container);
                        // if item ends with ':' then nested map follows
                        if (!itemText.empty() && itemText.back() == ':') {
                            ValueType m = newMap(); 
                            seq->push_back(m); 
                            stack.push_back(Ctx{ ind, m, false });
                        } else {
                            seq->push_back(parseScalar(itemText));
                        }
                        continue;
                    }
                    // mapping: key: value or key:
                    size_t colon = trimmed.find(':'); 
                    if (colon == std::string::npos) throw std::runtime_error("yaml: expected ':'");
                    std::string key = trimmed.substr(0, colon); // no unescape
                    std::string rest = trimmed.substr(colon+1); 
                    if (!rest.empty() && rest[0]==' ') rest.erase(0,1);
                    auto parent = asMap(stack.back().value);
                    if (rest.empty()) {
                        // nested block (placeholder map; may convert to sequence if '-' items follow)
                        ValueType m = newMap(); 
                        (*parent)[key] = m; 
                        stack.push_back(Ctx{ ind, m, false, parent, key });
                    } else if (rest == "|") {
                        // literal block scalar
                        std::ostringstream oss; 
                        size_t j = idx+1; 
                        int base = -1; 
                        for (; j<lines.size(); ++j) { 
                            int ind2 = currentIndent(lines[j]); 
                            if (ind2 <= ind) break; 
                            if (base<0) base=ind2; 
                            std::string t = lines[j].substr(base); 
                            oss << t; 
                            if (j+1<lines.size()) oss << "\n"; 
                        }
                        (*parent)[key] = ValueType{ oss.str() }; 
                        idx = j-1;
                    } else if (rest == ">") {
                        // folded block scalar
                        std::ostringstream oss; 
                        size_t j = idx+1; 
                        int base=-1; 
                        for (; j<lines.size(); ++j) { 
                            int ind2=currentIndent(lines[j]); 
                            if (ind2 <= ind) break; 
                            if (base<0) base=ind2; 
                            std::string t = lines[j].substr(base); 
                            if (oss.tellp()>0) oss << ' '; 
                            oss << t; 
                        }
                        (*parent)[key] = ValueType{ oss.str() }; 
                        idx = j-1;
                    } else {
                        (*parent)[key] = parseScalar(rest);
                    }
                }
                return stack.front().value;
            };
            (*yamlPkg)["parse"] = ValueType{ parseFn };
        }
    }
}