#ifndef ASUL_STD_IO_H
#define ASUL_STD_IO_H

#include "../../PackageMeta.h"

namespace asul {
class Interpreter;

// Register std.io package (Stream class with << >> overload)
void registerStdIoPackage(Interpreter& interp);
PackageMeta getStdIoPackageMeta();
}

#endif // ASUL_STD_IO_H
