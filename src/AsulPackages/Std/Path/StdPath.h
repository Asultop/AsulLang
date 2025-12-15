#ifndef STD_PATH_H
#define STD_PATH_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.path package with the interpreter
void registerStdPathPackage(Interpreter& interp);
PackageMeta getStdPathPackageMeta();

} // namespace asul

#endif // STD_PATH_H
