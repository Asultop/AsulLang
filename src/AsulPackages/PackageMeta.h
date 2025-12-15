#ifndef ASUL_PACKAGE_META_H
#define ASUL_PACKAGE_META_H

#include <string>
#include <vector>
#include <map>

namespace asul {

    struct MethodMeta {
        std::string name;
        int minParams{-1};
        int maxParams{-1}; // -1 for varargs/unknown
    };

    struct ClassMeta {
        std::string name;
        std::vector<MethodMeta> methods;
    };

    struct PackageMeta {
        std::string name;
        std::vector<std::string> exports; // Function names
        std::vector<ClassMeta> classes;
    };

} // namespace asul

#endif // ASUL_PACKAGE_META_H
