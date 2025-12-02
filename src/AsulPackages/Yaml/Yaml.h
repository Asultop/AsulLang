#ifndef YAML_PKG_H
#define YAML_PKG_H

namespace asul {

class Interpreter;

// Register the yaml package with the interpreter
void registerYamlPackage(Interpreter& interp);

} // namespace asul

#endif // YAML_PKG_H
