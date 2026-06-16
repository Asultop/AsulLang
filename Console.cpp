// Console.cpp
// A small reusable console/REPL front-end for ALangEngine.
// Provides runConsole(argc, argv) which supports:
//  - --help / -h
//  - --version / -v
//  - -f / --file <path>
//  - -e / --eval <code>
//  - -i    (interactive mode / drop into REPL after file/eval)

#include "ALangEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>
#include <optional>

#if defined(USE_READLINE)
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace console {

int runConsole(int argc, char* argv[]) {
    std::cout << "DEBUG: runConsole argc=" << argc << std::endl;
    const char* VERSION = "ALang 0.1.0";

    bool showHelp = false;
    bool showVersion = false;
    bool interactive = false;
    std::string runFile;
    std::string evalCode;

    // Simple option parsing
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") showHelp = true;
        else if (a == "--version" || a == "-v") showVersion = true;
        else if (a == "-i") interactive = true;
        else if (a == "-f" || a == "--file") {
            if (i + 1 < argc) runFile = argv[++i];
        } else if (a == "-e" || a == "--eval") {
            if (i + 1 < argc) evalCode = argv[++i];
        } else if (runFile.empty() && a.size() > 0 && a[0] != '-') {
            // treat positional as file
            runFile = a;
        }
    }

    if (showHelp) {
        std::cout << "Usage: alang [options]\n"
                  << "Options:\n"
                  << "  -h, --help        Show this help\n"
                  << "  -v, --version     Show version\n"
                  << "  -f, --file <path> Execute file and exit (use -i to drop into REPL after)\n"
                  << "  -e, --eval <code> Execute code string and exit (use -i to drop into REPL after)\n"
                  << "  -i                Interactive: REPL mode (or after file/eval)\n";
        return 0;
    }

    if (showVersion) {
        std::cout << VERSION << std::endl;
        return 0;
    }

    ALangEngine engine;
    engine.initialize();

    // Optional error colors and small helper registration (keeps parity with examples)
    engine.setErrorColorMap({
        {"header", "RED"}, {"code", "DARK_GRAY"}, {"caret", "RED"},
        {"token", "RED"}, {"lineLabel", "YELLOW"}, {"lineValue", "CYAN"}
    });

    // Register a tiny Math class so REPL users have something useful
    engine.registerClass(
        "Math",
        [](const std::vector<ALangEngine::NativeValue>& /*args*/, void* /*thisHandle*/) -> ALangEngine::NativeValue {
            return ALangEngine::NativeValue{std::in_place_type<std::monostate>};
        },
        std::unordered_map<std::string, ALangEngine::NativeFunc>{
            {"sum", [](const std::vector<ALangEngine::NativeValue>& args, void*) {
                double a = 0, b = 0;
                if (args.size() > 0) if (auto p = std::get_if<double>(&args[0])) a = *p;
                if (args.size() > 1) if (auto p = std::get_if<double>(&args[1])) b = *p;
                return ALangEngine::NativeValue{std::in_place_type<double>, a + b};
            }},
            {"abs", [](const std::vector<ALangEngine::NativeValue>& args, void*) {
                double x = 0; if (!args.empty()) if (auto p = std::get_if<double>(&args[0])) x = *p;
                return ALangEngine::NativeValue{ std::in_place_type<double>, std::fabs(x) };
            }}
        }
    );

    auto run_code = [&](const std::string& code, bool showRuntimeError = true)->bool{
        try {
            engine.execute(code);
            engine.runEventLoopUntilIdle();
            return true;
        } catch (const std::exception& ex) {
            if (showRuntimeError) {
                std::cerr << "Runtime error: " << ex.what() << std::endl;
            }
        } catch (...) {
            if (showRuntimeError) {
                std::cerr << "Unknown runtime error" << std::endl;
            }
        }
        return false;
    };

    // If eval provided, run it first
    if (!evalCode.empty()) {
        if (!run_code(evalCode)) return 1;
    }

    // If file provided, run it
    auto expandPath = [&](const std::string &p)->std::string{
        if (p.size() > 0 && p[0] == '~') {
            const char* home = getenv("HOME");
            if (!home) home = getenv("USERPROFILE");
            if (home) return std::string(home) + p.substr(1);
        }
        return p;
    };

    auto trim = [&](const std::string &s)->std::string{
        size_t a = 0; while (a < s.size() && isspace((unsigned char)s[a])) ++a;
        size_t b = s.size(); while (b > a && isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b-a);
    };

    auto tryOpenFile = [&](const std::string &orig, std::string &outPath)->std::optional<std::string> {
        std::vector<std::filesystem::path> candidates;
        std::string ep = expandPath(orig);
        std::filesystem::path p(ep);
        if (p.is_absolute()) candidates.push_back(p);
        else {
            candidates.push_back(std::filesystem::current_path() / p);
            candidates.push_back(p);
        }
        for (auto &c : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(c, ec) && !std::filesystem::is_directory(c, ec)) {
                outPath = c.string();
                return outPath;
            }
        }
        return std::nullopt;
    };

    if (!runFile.empty()) {
        std::string resolved;
        auto found = tryOpenFile(runFile, resolved);
        if (!found) {
            std::cerr << "Cannot open file: " << runFile << "\n";
            std::cerr << "Tried paths:\n";
            std::string ep = expandPath(runFile);
            std::filesystem::path p(ep);
            if (p.is_absolute()) std::cerr << "  " << p << "\n";
            else {
                std::cerr << "  " << (std::filesystem::current_path() / p) << "\n";
                std::cerr << "  " << p << "\n";
            }
            return 1;
        }
        std::ifstream in(resolved);
        if (!in) { std::cerr << "Cannot open resolved file: " << resolved << std::endl; return 1; }
        std::ostringstream ss; ss << in.rdbuf(); std::string code = ss.str();
        // set import base dir to file dir
        try {
            std::filesystem::path p(runFile);
            std::filesystem::path base = p.has_parent_path() ? p.parent_path() : std::filesystem::current_path();
            base = std::filesystem::absolute(base);
            engine.setImportBaseDir(base.string());
            std::filesystem::current_path(base);
        } catch (...) {}
        if (!run_code(code, false)) return 1;
    }

    // If not interactive and we already executed something, exit
    if (!interactive && (!evalCode.empty() || !runFile.empty())) return 0;

    // Enter interactive REPL
    std::cout << "ALang REPL (type .help for commands)." << std::endl;
#if defined(USE_READLINE)
    // Use readline for history and arrow keys
    while (true) {
        char* input = readline(">>> ");
        if (!input) break; // EOF (Ctrl-D)
        std::string line(input);
        free(input);
        if (line.empty()) continue;
        if (line == ".exit" || line == ".quit" || line == "exit" || line == "quit") break;
        if (line == ".help") { std::cout << ".help .exit .version .load <file> .clear" << std::endl; continue; }
        if (line == ".version") { std::cout << VERSION << std::endl; continue; }
        if (line.rfind(".load ", 0) == 0) {
                std::string f = trim(line.substr(6));
                if (f.empty()) { std::cerr << "Usage: .load <file>" << std::endl; continue; }
            std::string resolved;
            auto found = tryOpenFile(f, resolved);
            if (!found) {
                std::cerr << "Cannot open file: " << f << "\nTried: \n";
                std::string ep = expandPath(f);
                std::filesystem::path p(ep);
                if (p.is_absolute()) std::cerr << "  " << p << "\n";
                else {
                    std::cerr << "  " << (std::filesystem::current_path() / p) << "\n";
                    std::cerr << "  " << p << "\n";
                }
                continue;
            }
            std::ifstream in(resolved);
            if (!in) { std::cerr << "Cannot open resolved file: " << resolved << std::endl; continue; }
            std::ostringstream ss; ss << in.rdbuf(); std::string code = ss.str();
            run_code(code);
            continue;
        }
        if (line == ".clear") { for (int i=0;i<50;++i) std::cout << std::endl; continue; }
        if (!line.empty()) add_history(line.c_str());
        run_code(line);
    }
#else
    std::string line;
    while (true) {
        std::cout << ">>> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == ".exit" || line == ".quit" || line == "exit" || line == "quit") break;
        if (line == ".help") { std::cout << ".help .exit .version .load <file> .clear" << std::endl; continue; }
        if (line == ".version") { std::cout << VERSION << std::endl; continue; }
        if (line.rfind(".load ", 0) == 0) {
            std::string f = line.substr(6);
            std::ifstream in(f);
            if (!in) { std::cerr << "Cannot open file: " << f << std::endl; continue; }
            std::ostringstream ss; ss << in.rdbuf(); std::string code = ss.str();
            run_code(code);
            continue;
        }
        if (line == ".clear") { for (int i=0;i<50;++i) std::cout << std::endl; continue; }

        run_code(line);
    }
#endif

    return 0;
}

} // namespace console
