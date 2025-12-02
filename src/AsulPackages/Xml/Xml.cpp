#include "Xml.h"
#include "../../AsulInterpreter.h"

namespace asul {

void registerXmlPackage(Interpreter& interp) {
	interp.registerLazyPackage("xml", [](std::shared_ptr<Object> xmlPkg) {

		// xml.parse(text) -> Node object { name, attrs: object, children: array }
		auto parseFn = std::make_shared<Function>();
		parseFn->isBuiltin = true;
		parseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("xml.parse expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("xml.parse argument must be string");
			std::string s = std::get<std::string>(args[0]);
			size_t i = 0; auto n = s.size();
			auto skipWS = [&](){ while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) i++; };
			auto parseName = [&]() -> std::string { size_t st = i; while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i]=='_' || s[i]=='-' || s[i]==':' )) i++; if (i==st) throw std::runtime_error("xml: expected name"); return s.substr(st, i-st); };
			auto parseAttrVal = [&]() -> std::string { skipWS(); if (i>=n || (s[i] != '"' && s[i] != '\'')) throw std::runtime_error("xml: expected quote for attribute value"); char q = s[i++]; std::string out; while (i<n && s[i] != q) { out.push_back(s[i++]); } if (i>=n) throw std::runtime_error("xml: unterminated attribute value"); i++; return out; };
			auto parseAttrs = [&]() -> std::shared_ptr<Object> { auto obj = std::make_shared<Object>(); for(;;){ skipWS(); if (i>=n) break; if (s[i]=='/' || s[i]=='>') break; std::string k = parseName(); skipWS(); if (i>=n || s[i] != '=') throw std::runtime_error("xml: expected '=' after attribute name"); i++; std::string v = parseAttrVal(); (*obj)[k] = Value{ v }; }
				return obj; };
			std::function<std::shared_ptr<Object>()> parseElement = [&]() -> std::shared_ptr<Object> {
				skipWS(); if (i>=n || s[i] != '<') throw std::runtime_error("xml: expected '<'"); i++;
				if (i<n && s[i]=='?') { // skip processing instruction
					while (i+1<n && !(s[i]=='?' && s[i+1]=='>')) i++; i+=2; return parseElement();
				}
				if (i+3<n && s[i]=='!' && s[i+1]=='-' && s[i+2]=='-') { // comment
					i+=3; while (i+2<n && !(s[i]=='-'&&s[i+1]=='-'&&s[i+2]=='>')) i++; i+=3; return parseElement();
				}
				std::string name = parseName();
				auto attrs = parseAttrs();
				skipWS(); bool selfClose=false; if (i<n && s[i]=='/') { selfClose=true; i++; }
				if (i>=n || s[i] != '>') throw std::runtime_error("xml: expected '>'"); i++;
				auto node = std::make_shared<Object>();
				(*node)["name"] = Value{ name };
				(*node)["attrs"] = Value{ attrs };
				auto children = std::make_shared<Array>();
				if (!selfClose) {
					// parse children (text or elements) until </name>
					for(;;){
						skipWS(); if (i>=n) break;
						if (i<n && s[i]=='<' && i+1<n && s[i+1]=='/') {
							i+=2; std::string endName = parseName(); skipWS(); if (i>=n || s[i] != '>') throw std::runtime_error("xml: expected '>' in end tag"); i++;
							if (endName != name) throw std::runtime_error("xml: mismatched end tag");
							break;
						} else if (i<n && s[i]=='<') {
							children->push_back(Value{ parseElement() });
						} else {
							// text node
							size_t st=i; while (i<n && s[i] != '<') i++; std::string text = s.substr(st, i-st);
							// trim only CRLF around
							if (!text.empty()) children->push_back(Value{ text });
						}
					}
				}
				(*node)["children"] = Value{ children };
				return node;
			};
			return Value{ parseElement() };
		};
		(*xmlPkg)["parse"] = Value{ parseFn };
	});
}

} // namespace asul
