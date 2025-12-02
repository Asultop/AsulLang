#ifndef STD_NETWORK_H
#define STD_NETWORK_H

namespace asul {

class Interpreter;

// Register the std.network package with the interpreter
// This package uses the AsulAsync interface for async operations
void registerStdNetworkPackage(Interpreter& interp);

} // namespace asul

#endif // STD_NETWORK_H
