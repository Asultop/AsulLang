#pragma once

#include <memory>
#include <vector>
#include <stdexcept>
#include <variant>
#include <functional>
#include <sstream>
#include <string>

// 这个头文件包含os package的模板实现
// 它会被ALangEngine.cpp包含，然后在installBuiltins函数中实例化

namespace AsulModule {
    namespace Os {
        // 模板函数，用于初始化os package
        // 这个函数会在ALangEngine.cpp中实例化，这样就能访问ALangEngine.cpp中匿名命名空间的类型
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr osPkg);
    }
}

// 模板函数的实现
namespace AsulModule {
    namespace Os {
        template<typename FunctionPtr, typename ValueType, typename EnvironmentPtr, typename ObjectPtr, typename ArrayPtr>
        void initialize(ObjectPtr osPkg) {
            // 注意：os.call函数需要访问ALangEngine的内部状态，暂时保留在ALangEngine.cpp中
            // 这里只实现不需要访问内部状态的函数

            // getEnv(name) -> string | null
            auto getEnvFn = std::make_shared<typename FunctionPtr::element_type>();
            getEnvFn->isBuiltin = true;
            getEnvFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.empty()) throw std::runtime_error("getEnv expects name");
                std::string name = std::get<std::string>(args[0]);
                const char* val = std::getenv(name.c_str());
                if (val) return ValueType{ std::string(val) };
                return ValueType{ std::monostate{} };
            };
            (*osPkg)["getEnv"] = ValueType{ getEnvFn };

            // setEnv(name, value) -> boolean
            auto setEnvFn = std::make_shared<typename FunctionPtr::element_type>();
            setEnvFn->isBuiltin = true;
            setEnvFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                if (args.size() != 2) throw std::runtime_error("setEnv expects name, value");
                std::string name = std::get<std::string>(args[0]);
                std::string val = std::get<std::string>(args[1]);
                setenv(name.c_str(), val.c_str(), 1);
                return ValueType{ true };
            };
            (*osPkg)["setEnv"] = ValueType{ setEnvFn };

            // exit(code) -> never
            auto exitFn = std::make_shared<typename FunctionPtr::element_type>();
            exitFn->isBuiltin = true;
            exitFn->builtin = [](const std::vector<ValueType>& args, EnvironmentPtr)->ValueType {
                int code = 0;
                if (!args.empty()) {
                    // 简化实现，只处理数字类型
                    if (std::holds_alternative<double>(args[0])) {
                        code = static_cast<int>(std::get<double>(args[0]));
                    } else {
                        throw std::runtime_error("exit code must be a number");
                    }
                }
                std::exit(code);
                return ValueType{ std::monostate{} };
            };
            (*osPkg)["exit"] = ValueType{ exitFn };

            // platform() -> string
            auto platformFn = std::make_shared<typename FunctionPtr::element_type>();
            platformFn->isBuiltin = true;
            platformFn->builtin = [](const std::vector<ValueType>&, EnvironmentPtr)->ValueType {
                #ifdef __linux__
                return ValueType{ std::string("linux") };
                #elif _WIN32
                return ValueType{ std::string("windows") };
                #elif __APPLE__
                return ValueType{ std::string("darwin") };
                #else
                return ValueType{ std::string("unknown") };
                #endif
            };
            (*osPkg)["platform"] = ValueType{ platformFn };

            // arch() -> string
            auto archFn = std::make_shared<typename FunctionPtr::element_type>();
            archFn->isBuiltin = true;
            archFn->builtin = [](const std::vector<ValueType>&, EnvironmentPtr)->ValueType {
                if (sizeof(void*) == 8) {
                    return ValueType{ std::string("x64") };
                } else {
                    return ValueType{ std::string("x86") };
                }
            };
            (*osPkg)["arch"] = ValueType{ archFn };
        }
    }
}