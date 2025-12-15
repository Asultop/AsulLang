#ifndef STD_NETWORK_H
#define STD_NETWORK_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.network package with the interpreter
// This package uses the AsulAsync interface for async operations
void registerStdNetworkPackage(Interpreter& interp);
PackageMeta getStdNetworkPackageMeta();

} // namespace asul

#endif // STD_NETWORK_H
