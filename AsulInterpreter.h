#ifndef ASUL_INTERPRETER_H
#define ASUL_INTERPRETER_H

#include "ALangEngine.h"
#include <string>
#include <vector>

class AsulInterpreter {
public:
    AsulInterpreter();
    ~AsulInterpreter();

    void initialize();
    void execute(const std::string& code);
    void execute();
    void setSource(const std::string& code);
    void setImportBaseDir(const std::string& dir);
    void runEventLoopUntilIdle();

    using NativeValue = ALangEngine::NativeValue;
    using HostValue = ALangEngine::HostValue;

    void setGlobal(const std::string& name, const NativeValue& value);
    void setGlobalValue(const std::string& name, const HostValue& value);
    void registerFunction(const std::string& name, ALangEngine::NativeFunc func);

private:
    ALangEngine engine;
};

#endif // ASUL_INTERPRETER_H
