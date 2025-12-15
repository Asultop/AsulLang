#ifndef STD_UUID_H
#define STD_UUID_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.uuid package with the interpreter
void registerStdUuidPackage(Interpreter& interp);
PackageMeta getStdUuidPackageMeta();

} // namespace asul

#endif // STD_UUID_H
