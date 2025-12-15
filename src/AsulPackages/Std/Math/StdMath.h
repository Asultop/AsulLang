#ifndef STD_MATH_H
#define STD_MATH_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.math package with the interpreter
void registerStdMathPackage(Interpreter& interp);
PackageMeta getStdMathPackageMeta();

} // namespace asul

#endif // STD_MATH_H
