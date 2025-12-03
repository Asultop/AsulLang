#include "Csv.h"
#include "../../AsulInterpreter.h"
#include <fstream>
#include <sstream>

namespace asul {

void registerCsvPackage(Interpreter& interp) {
    auto init = [](std::shared_ptr<Object> csvPkg) {
        // parse(text) -> Array<Array<string>>
        auto parseFn = std::make_shared<Function>();
        parseFn->isBuiltin = true;
        parseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() < 1 || !std::holds_alternative<std::string>(args[0])) {
                throw std::runtime_error("csv.parse expects 1 string argument");
            }
            std::string input = std::get<std::string>(args[0]);
            auto rows = std::make_shared<Array>();
            std::vector<std::string> currentRow;
            std::string field;
            bool inQuotes = false;
            auto commitField = [&]() { currentRow.push_back(field); field.clear(); };
            auto commitRow = [&]() { auto arr = std::make_shared<Array>(); for (auto& s : currentRow) arr->push_back(Value{std::string(s)}); rows->push_back(Value{arr}); currentRow.clear(); };
            for (size_t i = 0; i < input.size(); ++i) {
                char c = input[i];
                if (inQuotes) {
                    if (c == '"') {
                        if (i + 1 < input.size() && input[i + 1] == '"') { field.push_back('"'); ++i; } else { inQuotes = false; }
                    } else { field.push_back(c); }
                } else {
                    if (c == '"') inQuotes = true; else if (c == ',') commitField(); else if (c == '\n') { commitField(); commitRow(); } else if (c == '\r') { commitField(); commitRow(); if (i + 1 < input.size() && input[i + 1] == '\n') ++i; } else field.push_back(c);
                }
            }
            if (inQuotes) throw std::runtime_error("Unterminated quote in CSV input");
            if (!field.empty() || !currentRow.empty()) { commitField(); commitRow(); }
            return Value{rows};
        };
        (*csvPkg)["parse"] = Value{parseFn};

        // stringify(rows) -> string
        auto stringifyFn = std::make_shared<Function>();
        stringifyFn->isBuiltin = true;
        stringifyFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() < 1 || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
                throw std::runtime_error("csv.stringify expects rows: Array<Array<string|number|bool>>");
            }
            auto rows = std::get<std::shared_ptr<Array>>(args[0]);
            std::ostringstream out;
            auto escapeField = [](const Value& v) {
                std::string s = toString(v);
                bool needQuote = s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos || s.find('\r') != std::string::npos;
                if (!needQuote) return s;
                std::string q; q.reserve(s.size() + 2);
                q.push_back('"');
                for (char c : s) { if (c == '"') { q.push_back('"'); q.push_back('"'); } else { q.push_back(c); } }
                q.push_back('"');
                return q;
            };
            for (size_t i = 0; i < rows->size(); ++i) {
                const Value& rowV = (*rows)[i];
                if (!std::holds_alternative<std::shared_ptr<Array>>(rowV)) throw std::runtime_error("Row must be Array");
                auto row = std::get<std::shared_ptr<Array>>(rowV);
                for (size_t j = 0; j < row->size(); ++j) {
                    if (j) out << ',';
                    out << escapeField((*row)[j]);
                }
                if (i + 1 < rows->size()) out << '\n';
            }
            return Value{out.str()};
        };
        (*csvPkg)["stringify"] = Value{stringifyFn};

        // read(path) -> rows
        auto readFn = std::make_shared<Function>();
        readFn->isBuiltin = true;
        readFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() < 1 || !std::holds_alternative<std::string>(args[0])) {
                throw std::runtime_error("csv.read expects path string");
            }
            std::ifstream in(std::get<std::string>(args[0]));
            if (!in) throw std::runtime_error("Failed to open CSV file");
            std::ostringstream buf; buf << in.rdbuf();
            std::string input = buf.str();
            // reuse parser logic
            auto rows = std::make_shared<Array>();
            std::vector<std::string> currentRow; std::string field; bool inQuotes = false;
            auto commitField = [&]() { currentRow.push_back(field); field.clear(); };
            auto commitRow = [&]() { auto arr = std::make_shared<Array>(); for (auto& s : currentRow) arr->push_back(Value{std::string(s)}); rows->push_back(Value{arr}); currentRow.clear(); };
            for (size_t i = 0; i < input.size(); ++i) {
                char c = input[i];
                if (inQuotes) {
                    if (c == '"') {
                        if (i + 1 < input.size() && input[i + 1] == '"') { field.push_back('"'); ++i; } else { inQuotes = false; }
                    } else { field.push_back(c); }
                } else {
                    if (c == '"') inQuotes = true; else if (c == ',') commitField(); else if (c == '\n') { commitField(); commitRow(); } else if (c == '\r') { commitField(); commitRow(); if (i + 1 < input.size() && input[i + 1] == '\n') ++i; } else field.push_back(c);
                }
            }
            if (inQuotes) throw std::runtime_error("Unterminated quote in CSV");
            if (!field.empty() || !currentRow.empty()) { commitField(); commitRow(); }
            return Value{rows};
        };
        (*csvPkg)["read"] = Value{readFn};

        // write(path, rows)
        auto writeFn = std::make_shared<Function>();
        writeFn->isBuiltin = true;
        writeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
                throw std::runtime_error("csv.write expects (path: string, rows: Array<Array<Value>>)");
            }
            std::string path = std::get<std::string>(args[0]);
            auto rows = std::get<std::shared_ptr<Array>>(args[1]);
            // build CSV text via stringify
            std::ostringstream out;
            auto escapeField = [](const Value& v) {
                std::string s = toString(v);
                bool needQuote = s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos || s.find('\r') != std::string::npos;
                if (!needQuote) return s;
                std::string q; q.reserve(s.size() + 2);
                q.push_back('"');
                for (char c : s) { if (c == '"') { q.push_back('"'); q.push_back('"'); } else { q.push_back(c); } }
                q.push_back('"');
                return q;
            };
            for (size_t i = 0; i < rows->size(); ++i) {
                const Value& rowV = (*rows)[i];
                if (!std::holds_alternative<std::shared_ptr<Array>>(rowV)) throw std::runtime_error("Row must be Array");
                auto row = std::get<std::shared_ptr<Array>>(rowV);
                for (size_t j = 0; j < row->size(); ++j) {
                    if (j) out << ',';
                    out << escapeField((*row)[j]);
                }
                if (i + 1 < rows->size()) out << '\n';
            }
            std::ofstream ofs(path);
            if (!ofs) throw std::runtime_error("Failed to open file for writing");
            ofs << out.str();
            ofs.close();
            return Value{true};
        };
        (*csvPkg)["write"] = Value{writeFn};
    };
    // Register under both names for compatibility
    interp.registerLazyPackage("csv", init);
    // interp.registerLazyPackage("std.csv", init);
}

} // namespace asul
