#ifndef ASUL_PACKAGES_H
#define ASUL_PACKAGES_H

// Include all package headers
#include "AsulPackages/Std/Path/StdPath.h"
#include "AsulPackages/Std/String/StdString.h"
#include "AsulPackages/Std/Math/StdMath.h"
#include "AsulPackages/Std/Time/StdTime.h"
#include "AsulPackages/Std/Os/StdOs.h"
#include "AsulPackages/Std/Regex/StdRegex.h"
#include "AsulPackages/Std/Encoding/StdEncoding.h"
#include "AsulPackages/Std/Network/StdNetwork.h"
#include "AsulPackages/Json/Json.h"
#include "AsulPackages/Xml/Xml.h"
#include "AsulPackages/Yaml/Yaml.h"
#include "AsulPackages/Os/Os.h"

// Declaration only - implementation in AsulInterpreter.cpp
void registerExternalPackages(asul::Interpreter& interp);

#endif // ASUL_PACKAGES_H
