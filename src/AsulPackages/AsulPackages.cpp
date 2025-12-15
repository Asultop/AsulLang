#include "AsulPackages.h"

namespace asul {

const std::vector<PackageMeta>& getPackageMetadata() {
    static std::vector<PackageMeta> packages;
    if (!packages.empty()) return packages;

    // Collect metadata from all packages that have metadata functions
    packages.push_back(getStdTimePackageMeta());
    packages.push_back(getStdIoPackageMeta());
    packages.push_back(getStdOsPackageMeta());
    packages.push_back(getStdNetworkPackageMeta());
    packages.push_back(getStdPathPackageMeta());
    packages.push_back(getStdStringPackageMeta());
    packages.push_back(getStdMathPackageMeta());
    packages.push_back(getStdRegexPackageMeta());
    packages.push_back(getStdLogPackageMeta());
    packages.push_back(getStdTestPackageMeta());
    packages.push_back(getStdFfiPackageMeta());
    packages.push_back(getStdUuidPackageMeta());
    packages.push_back(getStdUrlPackageMeta());
    packages.push_back(getStdEventsPackageMeta());

    // TODO: Add remaining packages that don't have metadata functions yet:
    // - std.crypto
    // - std.csv (or csv)
    // - std.json (or json)
    // - std.http (part of network?)
    // - std.array
    // - std.builtin
    // - std.collections
    // - std.encoding
    // - xml, yaml, os (top-level packages)

    return packages;
}

} // namespace asul
