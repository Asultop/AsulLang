#pragma once
#include "../../PackageMeta.h"

namespace asul {
    class Interpreter;
    void registerStdFfiPackage(Interpreter& interp);
    PackageMeta getStdFfiPackageMeta();
}
