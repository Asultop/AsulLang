#ifndef STD_URL_H
#define STD_URL_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.url package with the interpreter
void registerStdUrlPackage(Interpreter& interp);
PackageMeta getStdUrlPackageMeta();

} // namespace asul

#endif // STD_URL_H
