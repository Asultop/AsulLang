#ifndef ASUL_PACKAGES_H
#define ASUL_PACKAGES_H

// Include all package headers
#include "AsulPackages/Std/Path/StdPath.h"
#include "AsulPackages/Std/String/StdString.h"
#include "AsulPackages/Std/Math/StdMath.h"

// Declaration only - implementation in AsulInterpreter.cpp
void registerExternalPackages(asul::Interpreter& interp);

#endif // ASUL_PACKAGES_H
