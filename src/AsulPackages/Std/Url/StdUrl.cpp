#include "StdUrl.h"
#include "../../../AsulInterpreter.h"
#include <string>
#include <vector>
#include <memory>

namespace asul {

void registerStdUrlPackage(Interpreter& interp) {
    interp.registerLazyPackage("std.url", [](std::shared_ptr<Object> pkg) {
        // URL class: new URL(str) -> fields: protocol, host, port, path, query
        auto urlClass = std::make_shared<ClassInfo>();
        urlClass->name = "URL";
        auto urlCtor = std::make_shared<Function>();
        urlCtor->isBuiltin = true;
        urlCtor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
            if (args.size() != 1) throw std::runtime_error("URL constructor expects 1 argument (string)");
            std::string u = toString(args[0]);
            std::string protocol; std::string host; int port = -1; std::string path = "/"; std::string query;
            size_t schemePos = u.find("://");
            if (schemePos != std::string::npos) { protocol = u.substr(0, schemePos); }
            size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
            size_t pathStart = u.find('/', hostStart);
            if (pathStart == std::string::npos) { pathStart = u.size(); }
            // host[:port]
            {
                size_t hpEnd = pathStart;
                // Check for user:pass@host
                size_t atSign = u.find('@', hostStart);
                if (atSign != std::string::npos && atSign < hpEnd) {
                    hostStart = atSign + 1;
                }

                size_t colon = u.find(':', hostStart);
                if (colon != std::string::npos && colon < hpEnd) {
                    host = u.substr(hostStart, colon - hostStart);
                    std::string pstr = u.substr(colon + 1, hpEnd - (colon + 1));
                    try { port = std::stoi(pstr); } catch (...) { port = -1; }
                } else {
                    host = u.substr(hostStart, hpEnd - hostStart);
                }
            }
            // path?query
            if (pathStart < u.size()) {
                size_t qmark = u.find('?', pathStart);
                if (qmark == std::string::npos) { path = u.substr(pathStart); }
                else { path = u.substr(pathStart, qmark - pathStart); query = u.substr(qmark + 1); }
            }
            if (port < 0) { if (protocol == "http") port = 80; else if (protocol == "https") port = 443; }
            Value thisVal = closure->get("this");
            auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
            inst->fields["protocol"] = Value{ protocol };
            inst->fields["host"] = Value{ host };
            inst->fields["port"] = Value{ static_cast<double>(port) };
            inst->fields["path"] = Value{ path };
            inst->fields["query"] = Value{ query };
            return Value{ std::monostate{} };
        };
        urlClass->methods["constructor"] = urlCtor;

        // parseQuery() method - returns an object with query parameters
        auto parseQueryFn = std::make_shared<Function>();
        parseQueryFn->isBuiltin = true;
        parseQueryFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
            Value thisVal = closure->get("this");
            auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
            std::string query = toString(inst->fields["query"]);
            
            auto params = std::make_shared<Object>();
            if (query.empty()) return Value{params};
            
            // Parse query string: key1=value1&key2=value2
            size_t pos = 0;
            while (pos < query.size()) {
                size_t ampPos = query.find('&', pos);
                if (ampPos == std::string::npos) ampPos = query.size();
                
                std::string pair = query.substr(pos, ampPos - pos);
                size_t eqPos = pair.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = pair.substr(0, eqPos);
                    std::string value = pair.substr(eqPos + 1);
                    (*params)[key] = Value{value};
                } else {
                    (*params)[pair] = Value{std::string("")};
                }
                pos = ampPos + 1;
            }
            return Value{params};
        };
        urlClass->methods["parseQuery"] = parseQueryFn;

        (*pkg)["URL"] = Value{ urlClass };
    });
}

} // namespace asul
