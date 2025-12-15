#ifndef STD_TIME_H
#define STD_TIME_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.time package with the interpreter
void registerStdTimePackage(Interpreter& interp);
PackageMeta getStdTimePackageMeta();

} // namespace asul

#endif // STD_TIME_H
