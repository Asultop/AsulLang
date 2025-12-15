#ifndef STD_STRING_H
#define STD_STRING_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.string package with the interpreter
void registerStdStringPackage(Interpreter& interp);
PackageMeta getStdStringPackageMeta();

} // namespace asul

#endif // STD_STRING_H
