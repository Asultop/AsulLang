#ifndef STD_OS_H
#define STD_OS_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.os package with the interpreter
void registerStdOsPackage(Interpreter& interp);
PackageMeta getStdOsPackageMeta();

} // namespace asul

#endif // STD_OS_H
