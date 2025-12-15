#ifndef ASUL_PACKAGES_H
#define ASUL_PACKAGES_H

#include <string>
#include <vector>
#include <map>

#include "PackageMeta.h"

// Include all package headers
#include "Std/Path/StdPath.h"
#include "Std/String/StdString.h"
#include "Std/Math/StdMath.h"
#include "Std/Time/StdTime.h"
#include "Std/Os/StdOs.h"
#include "Std/Regex/StdRegex.h"
#include "Std/Encoding/StdEncoding.h"
#include "Std/Network/StdNetwork.h"
#include "Std/Crypto/StdCrypto.h"
#include "Std/Io/StdIo.h"
#include "Std/Builtin/StdBuiltin.h"
#include "Std/Collections/StdCollections.h"
#include "Std/Array/StdArray.h"
#include "Std/Log/StdLog.h"
#include "Std/Test/StdTest.h"
#include "Std/Ffi/StdFfi.h"
#include "Std/Uuid/StdUuid.h"
#include "Std/Url/StdUrl.h"
#include "Std/Events/StdEvents.h"
#include "Csv/Csv.h"
#include "Json/Json.h"
#include "Xml/Xml.h"
#include "Yaml/Yaml.h"
#include "Os/Os.h"

namespace asul {
    class Interpreter;

    // Registry function to get metadata for all packages
    const std::vector<PackageMeta>& getPackageMetadata();
}

// Declaration only - implementation in AsulInterpreter.cpp
void registerExternalPackages(asul::Interpreter& interp);

#endif // ASUL_PACKAGES_H
