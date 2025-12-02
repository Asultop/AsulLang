#ifndef JSON_PKG_H
#define JSON_PKG_H

namespace asul {

class Interpreter;

// Register the json package with the interpreter
void registerJsonPackage(Interpreter& interp);

} // namespace asul

#endif // JSON_PKG_H
