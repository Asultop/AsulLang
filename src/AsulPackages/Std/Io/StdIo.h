#ifndef ASUL_STD_IO_H
#define ASUL_STD_IO_H

namespace asul {
class Interpreter;

// Register std.io package (Stream class with << >> overload)
void registerStdIoPackage(Interpreter& interp);
}

#endif // ASUL_STD_IO_H
