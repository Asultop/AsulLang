#ifndef ASUL_CSV_H
#define ASUL_CSV_H

namespace asul {

class Interpreter;

// Register the csv package with the interpreter (independent module)
void registerCsvPackage(Interpreter& interp);

} // namespace asul

#endif // ASUL_CSV_H
