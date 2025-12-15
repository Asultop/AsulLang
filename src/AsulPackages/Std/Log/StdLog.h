#pragma once
#include "../../../AsulInterpreter.h"
#include "../../PackageMeta.h"

namespace asul {
	void registerStdLogPackage(Interpreter& interp);
    PackageMeta getStdLogPackageMeta();
}
