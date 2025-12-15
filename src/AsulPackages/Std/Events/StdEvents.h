#ifndef STD_EVENTS_H
#define STD_EVENTS_H

#include "../../PackageMeta.h"

namespace asul {

class Interpreter;

// Register the std.events package with the interpreter
void registerStdEventsPackage(Interpreter& interp);
PackageMeta getStdEventsPackageMeta();

} // namespace asul

#endif // STD_EVENTS_H
