// TestRunner.cpp
// Cross-platform test runner for ALang
// Replaces errorTest.sh, funcTest.sh, and runAllTests.sh

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include "AsulFormatString/AsulFormatString.h"

#ifdef _WIN32
    #include <windows.h>
    #define popen _popen
    #define pclose _pclose
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

// Initialize formatter with colors
void initFormatter() {
#ifdef _WIN32
    // Enable ANSI escape sequences on Windows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif
    asul_formatter().installColorFormatAdapter();
    asul_formatter().installResetLabelAdapter();
}

// Error example files
std::vector<std::string> errorFiles = {
    "assign_undefined.alang",
    // "await_non_promise.alang",
    "call_non_function.alang",
    "expect_property_name.alang",
    "import_not_found.alang",
    "import_private_symbol.alang",
    "missing_import_math.alang",
    "index_assignment_non_array.alang",
    "index_non_array.alang",
    "index_out_of_range.alang",
    "interface_with_body.alang",
    "invalid_assignment_target.alang",
    "invalid_interpolation.alang",
    "missing_interface_method.alang",
    "missing_multiple_interface.alang",
    // "property_access_non_object.alang",
    "spread_element_not_array.alang",
    "spread_value_not_object.alang",
    "undefined_variable.alang",
    "unterminated_string.alang"
};

// Functional example files
std::vector<std::string> funcFiles = {
    "builtins_test.alang",
    "comment_examples.alang",
    "computedProps.alang",
    "defaultParamsExample.alang",
    "doWhileExample.alang",
    "emptySemicolons.alang",
    "array_methods_test.alang",
    "evalExample.alang",
    "example.alang",
    "export_test.alang",
    "fileImportExample.alang",
    "foreachExample.alang",
    "foreachAdvanced.alang",
    "goExample.alang",
    "importExample.alang",
    "mathExample.alang",
    "networkExample.alang",
    "incrementExample.alang",
    "incrementEdgeCases.alang",
    "interfaceExample.alang",
    "interfaceValidationTest.alang",
    "interfaceUsageGuide.alang",
    "interpolationExample.alang",
    "lambdaExample.alang",
    "overloadTest.alang",
    "overrideTest.alang",
    "quoteExample.alang",
    "quote_complex.alang",
    "quote_edit_apply.alang",
    "reflection_test.alang",
    "restParamsExample.alang",
    "restParamsAdvanced.alang",
    "spread_examples.alang",
    "switchExample.alang",
    "switchAdvanced.alang",
    "ternaryExample.alang",
    "try_catchExample.alang",
    "type_and_match_example.alang",
    "type_comparison.alang",
    "map_example.alang",
    "containers_example.alang",
    "STLExample.alang",
    "staticMethodExample.alang",
    "bitwiseExample.alang",
    "fileIOExample.alang",
    "fileIOClassExample.alang",
    "fileIOAdvancedExample.alang",
    "fileSystem_test.alang",
    "fs_import_check.alang",
    "dateTimeExample.alang",
    "dateTime_extended.alang",
    "timezone_test.alang",
    "jsonExample.alang",
    "OSExample.alang",
    "io_os_test.alang",
    "signal_test.alang",
    "stringExample.alang",
    "test_lazy.alang",
    "test_wildcard.alang",
    "test_wildcard_std.alang",
    "setExample.alang",
    "stackExample.alang",
    "priorityQueueExample.alang",
    "binarySearchExample.alang",
    "string_methods_extended.alang",
    "encoding_test.alang",
    "socket_test.alang",
    "xml_yaml_example.alang",
    "http_test.alang",
    "http_sendfail_test.alang",
    "http_methods_test.alang",
    "http_fixes_test.alang",
    "http_client_enhanced_test.alang",
    "http_enhanced_integration_test.alang",
    "crypto_example.alang",
    "crypto_hash_demo.alang",
    "stream_example.alang",
    "csvExample.alang",
    "array_select_methods.alang",
    "string_methods_test.alang",
    "math_methods_test.alang",
    "object_methods_test.alang",
    "path_enhancements_test.alang",
    "encoding_enhancements_test.alang",
    "promise_utilities_test.alang",
    "log_test.alang",
    "test_framework_test.alang",
    "crypto_enhancements_test.alang",
    "language_runtime_test.alang",
    "type_system_iterator_test.alang",
    "operator_overload_test.alang",
    "ffi_test.alang",
    "regexExample.alang",
    "simpleDefault.alang",
    "enhancedExceptionExample.alang",
    "destructuring_test.alang",
    "optional_chaining_test.alang",
    "pattern_matching_test.alang",
    "yield_test.alang"
};

// Run a command and return exit code
int runCommand(const std::string& alangPath, const std::string& filePath) {
#ifdef _WIN32
    // Use CreateProcess on Windows for better path handling
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    std::string cmdLine = "\"" + alangPath + "\" \"" + filePath + "\"";
    
    // CreateProcess needs a mutable string
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');
    
    BOOL success = CreateProcessA(
        alangPath.c_str(),  // Application name
        cmdBuf.data(),      // Command line
        NULL,               // Process security attributes
        NULL,               // Thread security attributes
        FALSE,              // Inherit handles
        0,                  // Creation flags
        NULL,               // Environment
        NULL,               // Current directory
        &si,
        &pi
    );
    
    if (!success) {
        return -1;
    }
    
    // Wait for the process to complete
    WaitForSingleObject(pi.hProcess, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return static_cast<int>(exitCode);
#else
    std::string cmd = "\"" + alangPath + "\" \"" + filePath + "\" 2>/dev/null";
    int status = std::system(cmd.c_str());
    return WEXITSTATUS(status);
#endif
}

// Convert path to native string with proper escaping
std::string pathToCmd(const fs::path& p) {
#ifdef _WIN32
    // Use forward slashes or properly escape backslashes for Windows
    std::string s = p.string();
    // Replace backslashes with forward slashes (works in most cases on Windows)
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return s;
#else
    return p.string();
#endif
}

// Get the path to alang executable
fs::path getAlangPath(const fs::path& baseDir) {
#ifdef _WIN32
    // Try Debug first, then Release, then base directory
    fs::path debugPath = baseDir / "build" / "Debug" / "alang.exe";
    fs::path releasePath = baseDir / "build" / "Release" / "alang.exe";
    fs::path basePath = baseDir / "build" / "alang.exe";
    
    if (fs::exists(debugPath)) return debugPath;
    if (fs::exists(releasePath)) return releasePath;
    if (fs::exists(basePath)) return basePath;
    return debugPath; // Default
#else
    return baseDir / "build" / "alang";
#endif
}

// Print separator line
void printSeparator(char c = '=', int width = 50) {
    std::cout << std::string(width, c) << std::endl;
}

// Run error tests
int runErrorTests(const fs::path& baseDir, bool verbose = true) {
    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\n" +
                       "Running ALang Error Examples\n" +
                       std::string(50, '=')) << std::endl << std::endl;

    fs::path alangPath = getAlangPath(baseDir);
    fs::path errorDir = baseDir / "Example" / "ErrorExample";

    int total = 0;
    int errorsCaught = 0;

    for (const auto& file : errorFiles) {
        fs::path filePath = errorDir / file;
        
        if (!fs::exists(filePath)) {
            if (verbose) {
                std::cout << f("{YELLOW}", "Skipping (not found): " + file) << std::endl;
            }
            continue;
        }

        if (verbose) {
            std::cout << "----------------------------------------" << std::endl;
            std::cout << "Testing: " << file << std::endl;
            std::cout << "----------------------------------------" << std::endl;
        }

        std::string alangStr = pathToCmd(alangPath);
        std::string fileStr = pathToCmd(filePath);

        int exitCode = runCommand(alangStr, fileStr);
        total++;

        if (exitCode != 0) {
            if (verbose) {
                std::cout << f("{GREEN}", "[PASS] Expected error caught") << std::endl;
            }
            errorsCaught++;
        } else {
            if (verbose) {
                std::cout << f("{RED}", "[FAIL] No error (unexpected)") << std::endl;
            }
        }

        if (verbose) std::cout << std::endl;
    }

    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\nError Test Summary") << std::endl;
    printSeparator('=', 50);
    std::cout << "Total files tested: " << total << std::endl;
    std::cout << f("{GREEN}", "Errors caught: " + std::to_string(errorsCaught)) << std::endl;
    if (total - errorsCaught > 0) {
        std::cout << f("{RED}", "No errors: " + std::to_string(total - errorsCaught)) << std::endl;
    } else {
        std::cout << f("{GREEN}", "No errors: " + std::to_string(total - errorsCaught)) << std::endl;
    }
    printSeparator();
    std::cout << std::endl;

    return (errorsCaught == total) ? 0 : 1;
}

// Run functional tests
int runFuncTests(const fs::path& baseDir, bool verbose = true) {
    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\n" +
                       "Running ALang Function Examples\n" +
                       std::string(50, '=')) << std::endl << std::endl;

    fs::path alangPath = getAlangPath(baseDir);
    fs::path exampleDir = baseDir / "Example";

    // Create test file list with platform-specific files
    std::vector<std::string> testFiles = funcFiles;
#ifdef _WIN32
    testFiles.push_back("ffi_test_windows.alang");
#endif

    int total = 0;
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failedTests;

    for (const auto& file : testFiles) {
        fs::path filePath = exampleDir / file;
        
        if (!fs::exists(filePath)) {
            if (verbose) {
                std::cout << f("{YELLOW}", "Skipping (not found): " + file) << std::endl;
            }
            continue;
        }

        if (verbose) {
            std::cout << "----------------------------------------" << std::endl;
            std::cout << "Testing: " << file << std::endl;
            std::cout << "----------------------------------------" << std::endl;
        }

        std::string alangStr = pathToCmd(alangPath);
        std::string fileStr = pathToCmd(filePath);

        int exitCode = runCommand(alangStr, fileStr);
        total++;

        if (exitCode == 0) {
            if (verbose) {
                std::cout << f("{GREEN}", "[PASS] Test passed") << std::endl;
            }
            passed++;
        } else {
            if (verbose) {
                std::cout << f("{RED}", "[FAIL] Test failed (exit code: " + std::to_string(exitCode) + ")") << std::endl;
            }
            failed++;
            failedTests.push_back(file);
        }

        if (verbose) std::cout << std::endl;
    }

    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\nFunctional Test Summary") << std::endl;
    printSeparator('=', 50);
    std::cout << "Total files tested: " << total << std::endl;
    std::cout << f("{GREEN}", "Passed: " + std::to_string(passed)) << std::endl;
    if (failed > 0) {
        std::cout << f("{RED}", "Failed: " + std::to_string(failed)) << std::endl;
    } else {
        std::cout << f("{GREEN}", "Failed: " + std::to_string(failed)) << std::endl;
    }
    
    if (!failedTests.empty()) {
        std::cout << std::endl << f("{RED}", "Failed tests:") << std::endl;
        for (const auto& t : failedTests) {
            std::cout << "  - " << t << std::endl;
        }
    }
    
    printSeparator();
    std::cout << std::endl;

    return (failed > 0) ? 1 : 0;
}

// Run all tests
int runAllTests(const fs::path& baseDir, bool verbose = true) {
    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\n" +
                       "   Running All ALang Tests\n" +
                       std::string(50, '=')) << std::endl << std::endl;

    std::cout << ">>> Running Error Tests..." << std::endl << std::endl;
    int errorResult = runErrorTests(baseDir, verbose);
    std::cout << std::endl;

    std::cout << ">>> Running Functional Tests..." << std::endl << std::endl;
    int funcResult = runFuncTests(baseDir, verbose);
    std::cout << std::endl;

    // Summary
    std::cout << f("{CYAN}", "\n" + std::string(50, '=') + "\n" +
                       "   Overall Test Summary\n" +
                       std::string(50, '=')) << std::endl;

    if (errorResult == 0) {
        std::cout << f("{GREEN}", "Error Tests: [PASS] PASSED") << std::endl;
    } else {
        std::cout << f("{RED}", "Error Tests: [FAIL] FAILED") << std::endl;
    }

    if (funcResult == 0) {
        std::cout << f("{GREEN}", "Functional Tests: [PASS] PASSED") << std::endl;
    } else {
        std::cout << f("{YELLOW}", "Functional Tests: [FAIL] FAILED (some tests may have known issues)") << std::endl;
    }

    printSeparator('=', 50);

    return (errorResult != 0 || funcResult != 0) ? 1 : 0;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -e, --error     Run error tests only" << std::endl;
    std::cout << "  -f, --func      Run functional tests only" << std::endl;
    std::cout << "  -a, --all       Run all tests (default)" << std::endl;
    std::cout << "  -q, --quiet     Quiet mode (less verbose output)" << std::endl;
    std::cout << "  -h, --help      Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Set UTF-8 console output
    SetConsoleOutputCP(CP_UTF8);
#endif
    // Initialize the AsulFormatString formatter with colors
    initFormatter();

    // Get base directory (where the executable is or current directory)
    fs::path baseDir;
    
    // Try to find the project root by looking for CMakeLists.txt
    fs::path exePath = fs::absolute(argv[0]).parent_path();
    
    // Navigate up from build/Debug or build/Release to find project root
    fs::path searchPath = exePath;
    for (int i = 0; i < 4; i++) {
        if (fs::exists(searchPath / "CMakeLists.txt") && fs::exists(searchPath / "Example")) {
            baseDir = searchPath;
            break;
        }
        searchPath = searchPath.parent_path();
    }
    
    if (baseDir.empty()) {
        baseDir = fs::current_path();
    }

    // Parse arguments
    bool runError = false;
    bool runFunc = false;
    bool verbose = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-e" || arg == "--error") {
            runError = true;
        } else if (arg == "-f" || arg == "--func") {
            runFunc = true;
        } else if (arg == "-a" || arg == "--all") {
            runError = true;
            runFunc = true;
        } else if (arg == "-q" || arg == "--quiet") {
            verbose = false;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Default: run all tests
    if (!runError && !runFunc) {
        return runAllTests(baseDir, verbose);
    }

    int result = 0;
    
    if (runError && runFunc) {
        result = runAllTests(baseDir, verbose);
    } else if (runError) {
        result = runErrorTests(baseDir, verbose);
    } else if (runFunc) {
        result = runFuncTests(baseDir, verbose);
    }

    return result;
}
