#ifndef STD_REGEX_H
#define STD_REGEX_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.regex package with the interpreter
void registerStdRegexPackage(Interpreter& interp);
PackageMeta getStdRegexPackageMeta();

} // namespace asul

#endif // STD_REGEX_H
