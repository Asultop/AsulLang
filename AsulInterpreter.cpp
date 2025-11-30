#include "AsulInterpreter.h"

AsulInterpreter::AsulInterpreter() = default;
AsulInterpreter::~AsulInterpreter() = default;

void AsulInterpreter::initialize() { engine.initialize(); }
void AsulInterpreter::execute(const std::string& code) { engine.setSource(code); engine.execute(); }
void AsulInterpreter::execute() { engine.execute(); }
void AsulInterpreter::setSource(const std::string& code) { engine.setSource(code); }
void AsulInterpreter::setImportBaseDir(const std::string& dir) { engine.setImportBaseDir(dir); }
void AsulInterpreter::runEventLoopUntilIdle() { engine.runEventLoopUntilIdle(); }
void AsulInterpreter::setGlobal(const std::string& name, const NativeValue& value) { engine.setGlobal(name, value); }
void AsulInterpreter::setGlobalValue(const std::string& name, const HostValue& value) { engine.setGlobalValue(name, value); }
void AsulInterpreter::registerFunction(const std::string& name, ALangEngine::NativeFunc func) { engine.registerFunction(name, func); }
