#include "Yaml.h"
#include "../../AsulInterpreter.h"
#include <sstream>

namespace asul {

void registerYamlPackage(Interpreter& interp) {
	interp.registerLazyPackage("yaml", [](std::shared_ptr<Object> yamlPkg) {
		// yaml.parse(text) -> ALang Value (object/array/scalars) for a small subset
		auto parseFn = std::make_shared<Function>(); parseFn->isBuiltin = true;
		parseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("yaml.parse expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("yaml.parse argument must be string");
			std::string s = std::get<std::string>(args[0]);
			std::vector<std::string> lines; {
				std::istringstream iss(s); std::string line; while (std::getline(iss, line)) { if (!line.empty() && line.back()=='\r') line.pop_back(); lines.push_back(line); }
			}
			struct Ctx { int indent; Value value; bool isSeq; std::shared_ptr<Object> parentMap; std::string keyInParent; }; std::vector<Ctx> stack;
			auto newMap = [](){ return Value{ std::make_shared<Object>() }; };
			auto newSeq = [](){ return Value{ std::make_shared<Array>() }; };
			auto asMap = [](Value& v)->std::shared_ptr<Object>{ return std::get<std::shared_ptr<Object>>(v); };
			auto asSeq = [](Value& v)->std::shared_ptr<Array>{ return std::get<std::shared_ptr<Array>>(v); };
			auto parseScalar = [](const std::string& t)->Value{
				if (t == "null" || t == "~" || t == "Null" || t == "NULL") return Value{ std::monostate{} };
				if (t == "true" || t == "True" || t == "TRUE") return Value{ true };
				if (t == "false" || t == "False" || t == "FALSE") return Value{ false };
				// number
				char* end=nullptr; double dv = std::strtod(t.c_str(), &end); if (end && *end=='\0' && !t.empty()) return Value{ dv };
				return Value{ t };
			};
			auto currentIndent = [](const std::string& l){ int k=0; for(char c: l){ if(c==' ') k++; else break; } return k; };
			Value root{ std::make_shared<Object>() }; stack.push_back(Ctx{ -1, root, false, nullptr, std::string() });
			for (size_t idx=0; idx<lines.size(); ++idx) {
				std::string line = lines[idx]; if (line.find_first_not_of(' ') == std::string::npos) continue; // skip empty
				int ind = currentIndent(line); std::string trimmed = line.substr(ind);
				// pop to matching indent
				while (!stack.empty() && ind <= stack.back().indent) stack.pop_back();
				if (stack.empty()) throw std::runtime_error("yaml: bad indentation");
				// sequence item
				if (trimmed.rfind("- ", 0) == 0) {
					Value* container = &stack.back().value; if (!stack.back().isSeq) {
						// If current context is a map created for a key, convert that key's value to a sequence
						if (stack.back().parentMap) {
							Value seq = newSeq();
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
						Value m = newMap(); seq->push_back(m); stack.push_back(Ctx{ ind, m, false, nullptr, std::string() });
					} else {
						seq->push_back(parseScalar(itemText));
					}
					continue;
				}
				// mapping: key: value or key:
				size_t colon = trimmed.find(':'); if (colon == std::string::npos) throw std::runtime_error("yaml: expected ':'");
				std::string key = trimmed.substr(0, colon); // no unescape
				std::string rest = trimmed.substr(colon+1); if (!rest.empty() && rest[0]==' ') rest.erase(0,1);
				auto parent = asMap(stack.back().value);
				if (rest.empty()) {
					// nested block (placeholder map; may convert to sequence if '-' items follow)
					Value m = newMap(); (*parent)[key] = m; stack.push_back(Ctx{ ind, m, false, parent, key });
				} else if (rest == "|") {
					// literal block scalar
					std::ostringstream oss; size_t j = idx+1; int base = -1; for (; j<lines.size(); ++j) { int ind2 = currentIndent(lines[j]); if (ind2 <= ind) break; if (base<0) base=ind2; std::string t = lines[j].substr(base); oss << t; if (j+1<lines.size()) oss << "\n"; }
					(*parent)[key] = Value{ oss.str() }; idx = j-1;
				} else if (rest == ">") {
					// folded block scalar
					std::ostringstream oss; size_t j = idx+1; int base=-1; for (; j<lines.size(); ++j) { int ind2=currentIndent(lines[j]); if (ind2 <= ind) break; if (base<0) base=ind2; std::string t = lines[j].substr(base); if (oss.tellp()>0) oss << ' '; oss << t; }
					(*parent)[key] = Value{ oss.str() }; idx = j-1;
				} else {
					(*parent)[key] = parseScalar(rest);
				}
			}
			return stack.front().value;
		};
		(*yamlPkg)["parse"] = Value{ parseFn };
	});
}

} // namespace asul
