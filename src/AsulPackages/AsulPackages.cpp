#include "AsulPackages.h"

namespace asul {

const std::vector<PackageMeta>& getPackageMetadata() {
    static std::vector<PackageMeta> packages;
    if (!packages.empty()) return packages;

    // std.crypto
    {
        PackageMeta pkg;
        pkg.name = "std.crypto";
        pkg.exports = { "randomUUID", "getRandomValues", "md5", "sha1", "sha256" };
        packages.push_back(pkg);
    }

    // std.time
    {
        PackageMeta pkg;
        pkg.name = "std.time";
        pkg.exports = { "now", "nowISO", "dateFromEpoch", "parse" };
        
        ClassMeta dateClass;
        dateClass.name = "Date";
        dateClass.methods.push_back({"constructor"});
        dateClass.methods.push_back({"toISO"});
        dateClass.methods.push_back({"getYear"});
        dateClass.methods.push_back({"getMonth"});
        dateClass.methods.push_back({"getDay"});
        dateClass.methods.push_back({"getHour"});
        dateClass.methods.push_back({"getMinute"});
        dateClass.methods.push_back({"getSecond"});
        dateClass.methods.push_back({"getMillisecond"});
        dateClass.methods.push_back({"getEpochMillis"});
        pkg.classes.push_back(dateClass);
        
        packages.push_back(pkg);
    }

    // std.csv
    {
        PackageMeta pkg;
        pkg.name = "std.csv";
        pkg.exports = { "parse", "stringify", "read", "write" };
        packages.push_back(pkg);
    }

    // std.json
    {
        PackageMeta pkg;
        pkg.name = "std.json";
        pkg.exports = { "parse", "stringify" };
        packages.push_back(pkg);
    }

    // std.io
    {
        PackageMeta pkg;
        pkg.name = "std.io";
        pkg.exports = { "stdin", "stdout", "stderr", "mkdir", "rmdir", "stat", "copy", "move", "chmod", "walk" };

        ClassMeta fileStreamClass;
        fileStreamClass.name = "FileStream";
        fileStreamClass.methods.push_back({"constructor"});
        fileStreamClass.methods.push_back({"read"});
        fileStreamClass.methods.push_back({"write"});
        fileStreamClass.methods.push_back({"eof"});
        fileStreamClass.methods.push_back({"close"});
        pkg.classes.push_back(fileStreamClass);

        ClassMeta fileClass;
        fileClass.name = "File";
        fileClass.methods.push_back({"read"});
        fileClass.methods.push_back({"write"});
        fileClass.methods.push_back({"append"});
        fileClass.methods.push_back({"exists"});
        fileClass.methods.push_back({"delete"});
        fileClass.methods.push_back({"rename"});
        fileClass.methods.push_back({"stat"});
        fileClass.methods.push_back({"copy"});
        pkg.classes.push_back(fileClass);

        ClassMeta dirClass;
        dirClass.name = "Dir";
        dirClass.methods.push_back({"list"});
        dirClass.methods.push_back({"exists"});
        dirClass.methods.push_back({"create"});
        dirClass.methods.push_back({"delete"});
        dirClass.methods.push_back({"rename"});
        dirClass.methods.push_back({"walk"});
        pkg.classes.push_back(dirClass);

        packages.push_back(pkg);
    }

    // std.os
    {
        PackageMeta pkg;
        pkg.name = "std.os";
        pkg.exports = { "platform", "arch", "cpus", "totalmem", "freemem", "homedir", "tmpdir", "hostname", "release", "type", "uptime", "userInfo", "eol", "exec" };
        packages.push_back(pkg);
    }

    // std.net
    {
        PackageMeta pkg;
        pkg.name = "std.net";
        pkg.exports = { "createServer", "connect", "isIP", "isIPv4", "isIPv6" };
        
        ClassMeta socketClass;
        socketClass.name = "Socket";
        socketClass.methods.push_back({"connect"});
        socketClass.methods.push_back({"write"});
        socketClass.methods.push_back({"on"});
        socketClass.methods.push_back({"close"});
        pkg.classes.push_back(socketClass);

        ClassMeta serverClass;
        serverClass.name = "Server";
        serverClass.methods.push_back({"listen"});
        serverClass.methods.push_back({"close"});
        serverClass.methods.push_back({"on"});
        pkg.classes.push_back(serverClass);

        packages.push_back(pkg);
    }
    
    // std.http
    {
        PackageMeta pkg;
        pkg.name = "std.http";
        pkg.exports = { "createServer", "get", "post", "request" };
        
        ClassMeta serverClass;
        serverClass.name = "Server";
        serverClass.methods.push_back({"listen"});
        serverClass.methods.push_back({"on"});
        serverClass.methods.push_back({"close"});
        pkg.classes.push_back(serverClass);
        
        ClassMeta clientRequestClass;
        clientRequestClass.name = "ClientRequest";
        clientRequestClass.methods.push_back({"write"});
        clientRequestClass.methods.push_back({"end"});
        clientRequestClass.methods.push_back({"on"});
        pkg.classes.push_back(clientRequestClass);

        packages.push_back(pkg);
    }

    return packages;
}

} // namespace asul
