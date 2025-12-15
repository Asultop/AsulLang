#include "AsulPackages.h"

namespace asul {

const std::vector<PackageMeta>& getPackageMetadata() {
    static std::vector<PackageMeta> packages;
    if (!packages.empty()) return packages;

    // Each package now provides its own metadata via get*PackageMeta() functions
    // defined in their respective source files. This centralizes the aggregation
    // while allowing each package to define its own symbols.
    
    // Note: We aggregate metadata here rather than calling individual functions
    // to avoid linking issues with the LSP target which doesn't include full runtime.
    // The individual metadata functions are still available for direct use.

    // std.path
    {
        PackageMeta pkg;
        pkg.name = "std.path";
        pkg.exports = { "join", "resolve", "dirname", "basename", "extname", "isAbsolute", "normalize", "relative", "sep" };
        packages.push_back(pkg);
    }

    // std.string
    {
        PackageMeta pkg;
        pkg.name = "std.string";
        pkg.exports = { "toUpperCase", "toLowerCase", "trim", "replaceAll", "repeat", "localeCompare" };
        packages.push_back(pkg);
    }

    // std.math
    {
        PackageMeta pkg;
        pkg.name = "std.math";
        pkg.exports = { "abs", "sin", "cos", "tan", "sqrt", "exp", "log", "pow", "ceil", "floor", "round", "min", "max", "random", "clamp", "lerp", "approxEqual", "pi", "e" };
        packages.push_back(pkg);
    }

    // std.time
    {
        PackageMeta pkg;
        pkg.name = "std.time";
        pkg.exports = { "Duration", "Date", "nowEpochMillis", "nowEpochSeconds", "nowISO", "now", "dateFromEpoch", "parse" };
        
        ClassMeta dateClass;
        dateClass.name = "Date";
        dateClass.methods = { {"constructor"}, {"toISO"}, {"getYear"}, {"getMonth"}, {"getDay"}, {"getHour"}, {"getMinute"}, {"getSecond"}, {"getMillisecond"}, {"getEpochMillis"}, {"format"}, {"__add__"}, {"__sub__"} };
        pkg.classes.push_back(dateClass);

        ClassMeta durationClass;
        durationClass.name = "Duration";
        durationClass.methods = { {"constructor"} };
        pkg.classes.push_back(durationClass);
        
        packages.push_back(pkg);
    }

    // std.os
    {
        PackageMeta pkg;
        pkg.name = "std.os";
        pkg.exports = { "system", "getenv", "setenv", "signal", "kill", "raise", "getpid", "popen", "platform" };
        packages.push_back(pkg);
    }

    // std.regex
    {
        PackageMeta pkg;
        pkg.name = "std.regex";
        pkg.exports = { "Regex" };

        ClassMeta regexClass;
        regexClass.name = "Regex";
        regexClass.methods = { {"constructor"}, {"match"}, {"test"}, {"replace"} };
        pkg.classes.push_back(regexClass);

        packages.push_back(pkg);
    }

    // std.io
    {
        PackageMeta pkg;
        pkg.name = "std.io";
        pkg.exports = { "stdin", "stdout", "stderr", "mkdir", "rmdir", "stat", "copy", "move", "chmod", "walk", "writeFile", "appendFile", "readFile" };

        ClassMeta fileStreamClass;
        fileStreamClass.name = "FileStream";
        fileStreamClass.methods = { {"constructor"}, {"read"}, {"write"}, {"eof"}, {"close"} };
        pkg.classes.push_back(fileStreamClass);

        ClassMeta fileClass;
        fileClass.name = "File";
        fileClass.methods = { {"read"}, {"write"}, {"append"}, {"exists"}, {"delete"}, {"rename"}, {"stat"}, {"copy"} };
        pkg.classes.push_back(fileClass);

        ClassMeta dirClass;
        dirClass.name = "Dir";
        dirClass.methods = { {"list"}, {"exists"}, {"create"}, {"delete"}, {"rename"}, {"walk"} };
        pkg.classes.push_back(dirClass);

        packages.push_back(pkg);
    }

    // std.network (includes http features)
    {
        PackageMeta pkg;
        pkg.name = "std.network";
        pkg.exports = { "parseHeaders", "fetch", "get", "post", "put", "delete", "patch", "head", "request", "http" };
        
        ClassMeta socketClass;
        socketClass.name = "Socket";
        socketClass.methods = { {"constructor"}, {"bind"}, {"listen"}, {"connect"}, {"accept"}, {"read"}, {"write"}, {"close"} };
        pkg.classes.push_back(socketClass);

        ClassMeta urlClass;
        urlClass.name = "URL";
        urlClass.methods = { {"constructor"}, {"parseQuery"} };
        pkg.classes.push_back(urlClass);

        packages.push_back(pkg);
    }

    // std.log
    {
        PackageMeta pkg;
        pkg.name = "std.log";
        pkg.exports = { "setLevel", "getLevel", "setColors", "debug", "info", "warn", "error", "json", "DEBUG", "INFO", "WARN", "ERROR" };
        packages.push_back(pkg);
    }

    // std.test
    {
        PackageMeta pkg;
        pkg.name = "std.test";
        pkg.exports = { "assert", "assertEqual", "assertNotEqual", "getStats", "resetStats", "pass", "fail", "printSummary" };
        packages.push_back(pkg);
    }

    // std.ffi
    {
        PackageMeta pkg;
        pkg.name = "std.ffi";
        pkg.exports = { "dlopen", "dlsym", "dlclose", "call", "RTLD_LAZY", "RTLD_NOW", "RTLD_GLOBAL", "RTLD_LOCAL" };
        packages.push_back(pkg);
    }

    // std.uuid
    {
        PackageMeta pkg;
        pkg.name = "std.uuid";
        pkg.exports = { "v4" };
        packages.push_back(pkg);
    }

    // std.url
    {
        PackageMeta pkg;
        pkg.name = "std.url";
        pkg.exports = { "URL" };
        
        ClassMeta urlClass;
        urlClass.name = "URL";
        urlClass.methods = { {"constructor"}, {"parseQuery"} };
        pkg.classes.push_back(urlClass);

        packages.push_back(pkg);
    }

    // std.events
    {
        PackageMeta pkg;
        pkg.name = "std.events";
        pkg.exports = { "connect" };
        
        ClassMeta asulObjClass;
        asulObjClass.name = "AsulObject";
        asulObjClass.methods = { {"on"}, {"off"}, {"emit"}, {"receive"} };
        pkg.classes.push_back(asulObjClass);

        packages.push_back(pkg);
    }

    // std.crypto
    {
        PackageMeta pkg;
        pkg.name = "std.crypto";
        pkg.exports = { "randomUUID", "getRandomValues", "md5", "sha1", "sha256" };
        packages.push_back(pkg);
    }

    // csv
    {
        PackageMeta pkg;
        pkg.name = "csv";
        pkg.exports = { "parse", "stringify", "read", "write" };
        packages.push_back(pkg);
    }

    // json
    {
        PackageMeta pkg;
        pkg.name = "json";
        pkg.exports = { "parse", "stringify" };
        packages.push_back(pkg);
    }

    return packages;
}

} // namespace asul
