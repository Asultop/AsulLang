#ifndef STD_BUILTIN_H
#define STD_BUILTIN_H

namespace asul {

class Interpreter;

// Register the std.builtin package and global builtins with the interpreter
void registerStdBuiltinPackage(Interpreter& interp);

} // namespace asul

#endif // STD_BUILTIN_H
