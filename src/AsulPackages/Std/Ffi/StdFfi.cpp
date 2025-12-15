#include "StdFfi.h"
#include "../../../AsulInterpreter.h"
#include <cstring>
#include <unordered_map>

#ifdef _WIN32
    #include <windows.h>
    // Define Unix-like constants for Windows
    #define RTLD_LAZY    0x00001
    #define RTLD_NOW     0x00002
    #define RTLD_GLOBAL  0x00100
    #define RTLD_LOCAL   0x00000
#else
    #include <dlfcn.h>
#endif

namespace asul {

// Store loaded libraries with reference counting
static std::unordered_map<std::string, void*> g_loadedLibraries;
static std::unordered_map<void*, int> g_libraryRefCount;

void registerStdFfiPackage(Interpreter& interp) {
    // Get interpreter pointer for lambdas
    Interpreter* interpPtr = &interp;
    
    interp.registerLazyPackage("std.ffi", [interpPtr](std::shared_ptr<Object> pkg) {
        
        // dlopen(path, mode) - Load a dynamic library
        auto dlopenFn = std::make_shared<Function>();
        dlopenFn->isBuiltin = true;
        dlopenFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.empty()) {
                throw std::runtime_error("dlopen requires at least 1 argument: path [, mode]");
            }
            if (!std::holds_alternative<std::string>(args[0])) {
                throw std::runtime_error("dlopen: path must be a string");
            }
            
            std::string path = std::get<std::string>(args[0]);
            int mode = RTLD_LAZY | RTLD_LOCAL; // Default mode
            
            if (args.size() >= 2) {
                if (!std::holds_alternative<double>(args[1])) {
                    throw std::runtime_error("dlopen: mode must be a number");
                }
                mode = static_cast<int>(std::get<double>(args[1]));
            }
            
#ifdef _WIN32
            // Windows: Use LoadLibrary
            HMODULE handle = LoadLibraryA(path.c_str());
            void* libHandle = reinterpret_cast<void*>(handle);
            if (!libHandle) {
                DWORD error = GetLastError();
                throw std::runtime_error("dlopen failed: Error code " + std::to_string(error));
            }
#else
            // Unix/Linux/macOS: Use dlopen
            void* libHandle = dlopen(path.c_str(), mode);
            if (!libHandle) {
                const char* error = dlerror();
                throw std::runtime_error(std::string("dlopen failed: ") + (error ? error : "unknown error"));
            }
#endif
            
            // Store the library handle
            g_loadedLibraries[path] = libHandle;
            g_libraryRefCount[libHandle]++;
            
            // Return library handle as opaque pointer (stored as number)
            return Value{static_cast<double>(reinterpret_cast<uintptr_t>(libHandle))};
        };
        (*pkg)["dlopen"] = Value{dlopenFn};
        
        // dlsym(handle, symbol) - Get function pointer from library
        auto dlsymFn = std::make_shared<Function>();
        dlsymFn->isBuiltin = true;
        dlsymFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() != 2) {
                throw std::runtime_error("dlsym requires 2 arguments: handle, symbol");
            }
            if (!std::holds_alternative<double>(args[0])) {
                throw std::runtime_error("dlsym: handle must be a number");
            }
            if (!std::holds_alternative<std::string>(args[1])) {
                throw std::runtime_error("dlsym: symbol must be a string");
            }
            
            void* libHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(std::get<double>(args[0])));
            std::string symbol = std::get<std::string>(args[1]);
            
#ifdef _WIN32
            // Windows: Use GetProcAddress
            FARPROC funcPtr = GetProcAddress(reinterpret_cast<HMODULE>(libHandle), symbol.c_str());
            void* symPtr = reinterpret_cast<void*>(funcPtr);
            if (!symPtr) {
                DWORD error = GetLastError();
                throw std::runtime_error("dlsym failed: Symbol '" + symbol + "' not found (Error " + std::to_string(error) + ")");
            }
#else
            // Unix/Linux/macOS: Use dlsym
            dlerror(); // Clear any previous error
            void* symPtr = dlsym(libHandle, symbol.c_str());
            const char* error = dlerror();
            if (error) {
                throw std::runtime_error(std::string("dlsym failed: ") + error);
            }
#endif
            
            // Return function pointer as opaque value
            return Value{static_cast<double>(reinterpret_cast<uintptr_t>(symPtr))};
        };
        (*pkg)["dlsym"] = Value{dlsymFn};
        
        // dlclose(handle) - Close a dynamic library
        auto dlcloseFn = std::make_shared<Function>();
        dlcloseFn->isBuiltin = true;
        dlcloseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() != 1) {
                throw std::runtime_error("dlclose requires 1 argument: handle");
            }
            if (!std::holds_alternative<double>(args[0])) {
                throw std::runtime_error("dlclose: handle must be a number");
            }
            
            void* libHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(std::get<double>(args[0])));
            
            // Decrement reference count
            auto it = g_libraryRefCount.find(libHandle);
            if (it != g_libraryRefCount.end()) {
                it->second--;
                if (it->second <= 0) {
#ifdef _WIN32
                    FreeLibrary(reinterpret_cast<HMODULE>(libHandle));
#else
                    dlclose(libHandle);
#endif
                    g_libraryRefCount.erase(it);
                    
                    // Remove from loaded libraries
                    for (auto libIt = g_loadedLibraries.begin(); libIt != g_loadedLibraries.end(); ) {
                        if (libIt->second == libHandle) {
                            libIt = g_loadedLibraries.erase(libIt);
                        } else {
                            ++libIt;
                        }
                    }
                }
            }
            
            return Value{std::monostate{}};
        };
        (*pkg)["dlclose"] = Value{dlcloseFn};
        
        // call(funcPtr, returnType, args...) - Call a C function
        // returnType: "void", "int", "double", "pointer", "string"
        auto callFn = std::make_shared<Function>();
        callFn->isBuiltin = true;
        callFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() < 2) {
                throw std::runtime_error("call requires at least 2 arguments: funcPtr, returnType [, args...]");
            }
            if (!std::holds_alternative<double>(args[0])) {
                throw std::runtime_error("call: funcPtr must be a number");
            }
            if (!std::holds_alternative<std::string>(args[1])) {
                throw std::runtime_error("call: returnType must be a string");
            }
            
            void* funcPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(std::get<double>(args[0])));
            std::string returnType = std::get<std::string>(args[1]);
            
            // WARNING: This is a simplified FFI implementation for basic use cases
            // Production FFI would use libffi for proper calling conventions
            // This version supports up to 6 arguments
            
            union FFIArg {
                long long i64;
                double f64;
                void* ptr;
                const char* str;
            };
            
            std::vector<FFIArg> ffiArgs;
            std::vector<std::string> stringHolders; // Keep strings alive
            
            for (size_t i = 2; i < args.size() && i < 8; ++i) {
                FFIArg val;
                const Value& arg = args[i];
                
                if (std::holds_alternative<double>(arg)) {
                    val.i64 = static_cast<long long>(std::get<double>(arg));
                    val.f64 = std::get<double>(arg);
                } else if (std::holds_alternative<std::string>(arg)) {
                    stringHolders.push_back(std::get<std::string>(arg));
                    val.str = stringHolders.back().c_str();
                    val.ptr = const_cast<char*>(stringHolders.back().c_str());
                } else if (std::holds_alternative<std::monostate>(arg)) {
                    val.ptr = nullptr;
                } else {
                    val.ptr = nullptr;
                }
                ffiArgs.push_back(val);
            }
            
            // Call the function based on argument count and return type
            if (returnType == "void") {
                typedef void (*func0_t)();
                typedef void (*func1_t)(long long);
                typedef void (*func2_t)(long long, long long);
                typedef void (*func3_t)(long long, long long, long long);
                typedef void (*func4_t)(long long, long long, long long, long long);
                typedef void (*func5_t)(long long, long long, long long, long long, long long);
                typedef void (*func6_t)(long long, long long, long long, long long, long long, long long);
                
                switch (ffiArgs.size()) {
                    case 0: ((func0_t)funcPtr)(); break;
                    case 1: ((func1_t)funcPtr)(ffiArgs[0].i64); break;
                    case 2: ((func2_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64); break;
                    case 3: ((func3_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64); break;
                    case 4: ((func4_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64); break;
                    case 5: ((func5_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64, ffiArgs[4].i64); break;
                    case 6: ((func6_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64, ffiArgs[4].i64, ffiArgs[5].i64); break;
                    default: throw std::runtime_error("call: too many arguments (max 6)");
                }
                return Value{std::monostate{}};
            } else if (returnType == "int") {
                typedef int (*func0_t)();
                typedef int (*func1_t)(long long);
                typedef int (*func2_t)(long long, long long);
                typedef int (*func3_t)(long long, long long, long long);
                typedef int (*func4_t)(long long, long long, long long, long long);
                typedef int (*func5_t)(long long, long long, long long, long long, long long);
                typedef int (*func6_t)(long long, long long, long long, long long, long long, long long);
                
                int ret;
                switch (ffiArgs.size()) {
                    case 0: ret = ((func0_t)funcPtr)(); break;
                    case 1: ret = ((func1_t)funcPtr)(ffiArgs[0].i64); break;
                    case 2: ret = ((func2_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64); break;
                    case 3: ret = ((func3_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64); break;
                    case 4: ret = ((func4_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64); break;
                    case 5: ret = ((func5_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64, ffiArgs[4].i64); break;
                    case 6: ret = ((func6_t)funcPtr)(ffiArgs[0].i64, ffiArgs[1].i64, ffiArgs[2].i64, ffiArgs[3].i64, ffiArgs[4].i64, ffiArgs[5].i64); break;
                    default: throw std::runtime_error("call: too many arguments (max 6)");
                }
                return Value{static_cast<double>(ret)};
            } else if (returnType == "double") {
                typedef double (*func0_t)();
                typedef double (*func1_t)(double);
                typedef double (*func2_t)(double, double);
                typedef double (*func3_t)(double, double, double);
                typedef double (*func4_t)(double, double, double, double);
                typedef double (*func5_t)(double, double, double, double, double);
                typedef double (*func6_t)(double, double, double, double, double, double);
                
                double ret;
                switch (ffiArgs.size()) {
                    case 0: ret = ((func0_t)funcPtr)(); break;
                    case 1: ret = ((func1_t)funcPtr)(ffiArgs[0].f64); break;
                    case 2: ret = ((func2_t)funcPtr)(ffiArgs[0].f64, ffiArgs[1].f64); break;
                    case 3: ret = ((func3_t)funcPtr)(ffiArgs[0].f64, ffiArgs[1].f64, ffiArgs[2].f64); break;
                    case 4: ret = ((func4_t)funcPtr)(ffiArgs[0].f64, ffiArgs[1].f64, ffiArgs[2].f64, ffiArgs[3].f64); break;
                    case 5: ret = ((func5_t)funcPtr)(ffiArgs[0].f64, ffiArgs[1].f64, ffiArgs[2].f64, ffiArgs[3].f64, ffiArgs[4].f64); break;
                    case 6: ret = ((func6_t)funcPtr)(ffiArgs[0].f64, ffiArgs[1].f64, ffiArgs[2].f64, ffiArgs[3].f64, ffiArgs[4].f64, ffiArgs[5].f64); break;
                    default: throw std::runtime_error("call: too many arguments (max 6)");
                }
                return Value{ret};
            } else if (returnType == "pointer") {
                typedef void* (*func0_t)();
                typedef void* (*func1_t)(void*);
                typedef void* (*func2_t)(void*, void*);
                typedef void* (*func3_t)(void*, void*, void*);
                typedef void* (*func4_t)(void*, void*, void*, void*);
                typedef void* (*func5_t)(void*, void*, void*, void*, void*);
                typedef void* (*func6_t)(void*, void*, void*, void*, void*, void*);
                
                void* ret;
                switch (ffiArgs.size()) {
                    case 0: ret = ((func0_t)funcPtr)(); break;
                    case 1: ret = ((func1_t)funcPtr)(ffiArgs[0].ptr); break;
                    case 2: ret = ((func2_t)funcPtr)(ffiArgs[0].ptr, ffiArgs[1].ptr); break;
                    case 3: ret = ((func3_t)funcPtr)(ffiArgs[0].ptr, ffiArgs[1].ptr, ffiArgs[2].ptr); break;
                    case 4: ret = ((func4_t)funcPtr)(ffiArgs[0].ptr, ffiArgs[1].ptr, ffiArgs[2].ptr, ffiArgs[3].ptr); break;
                    case 5: ret = ((func5_t)funcPtr)(ffiArgs[0].ptr, ffiArgs[1].ptr, ffiArgs[2].ptr, ffiArgs[3].ptr, ffiArgs[4].ptr); break;
                    case 6: ret = ((func6_t)funcPtr)(ffiArgs[0].ptr, ffiArgs[1].ptr, ffiArgs[2].ptr, ffiArgs[3].ptr, ffiArgs[4].ptr, ffiArgs[5].ptr); break;
                    default: throw std::runtime_error("call: too many arguments (max 6)");
                }
                return Value{static_cast<double>(reinterpret_cast<uintptr_t>(ret))};
            } else if (returnType == "string") {
                typedef const char* (*func0_t)();
                typedef const char* (*func1_t)(const char*);
                typedef const char* (*func2_t)(const char*, const char*);
                typedef const char* (*func3_t)(const char*, const char*, const char*);
                typedef const char* (*func4_t)(const char*, const char*, const char*, const char*);
                typedef const char* (*func5_t)(const char*, const char*, const char*, const char*, const char*);
                typedef const char* (*func6_t)(const char*, const char*, const char*, const char*, const char*, const char*);
                
                const char* ret;
                switch (ffiArgs.size()) {
                    case 0: ret = ((func0_t)funcPtr)(); break;
                    case 1: ret = ((func1_t)funcPtr)(ffiArgs[0].str); break;
                    case 2: ret = ((func2_t)funcPtr)(ffiArgs[0].str, ffiArgs[1].str); break;
                    case 3: ret = ((func3_t)funcPtr)(ffiArgs[0].str, ffiArgs[1].str, ffiArgs[2].str); break;
                    case 4: ret = ((func4_t)funcPtr)(ffiArgs[0].str, ffiArgs[1].str, ffiArgs[2].str, ffiArgs[3].str); break;
                    case 5: ret = ((func5_t)funcPtr)(ffiArgs[0].str, ffiArgs[1].str, ffiArgs[2].str, ffiArgs[3].str, ffiArgs[4].str); break;
                    case 6: ret = ((func6_t)funcPtr)(ffiArgs[0].str, ffiArgs[1].str, ffiArgs[2].str, ffiArgs[3].str, ffiArgs[4].str, ffiArgs[5].str); break;
                    default: throw std::runtime_error("call: too many arguments (max 6)");
                }
                return Value{std::string(ret ? ret : "")};
            } else {
                throw std::runtime_error("call: unsupported return type '" + returnType + "'. Supported: void, int, double, pointer, string");
            }
        };
        (*pkg)["call"] = Value{callFn};
        
        // Constants for dlopen modes
        (*pkg)["RTLD_LAZY"] = Value{static_cast<double>(RTLD_LAZY)};
        (*pkg)["RTLD_NOW"] = Value{static_cast<double>(RTLD_NOW)};
        (*pkg)["RTLD_GLOBAL"] = Value{static_cast<double>(RTLD_GLOBAL)};
        (*pkg)["RTLD_LOCAL"] = Value{static_cast<double>(RTLD_LOCAL)};
    });
}

} // namespace asul
