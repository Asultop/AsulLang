#pragma once
#include "../../../AsulInterpreter.h"
#include "../../PackageMeta.h"

namespace asul {
	void registerStdTestPackage(Interpreter& interp);
    PackageMeta getStdTestPackageMeta();
}
