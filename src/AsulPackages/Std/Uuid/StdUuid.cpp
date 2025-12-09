#include "StdUuid.h"
#include "../../../AsulInterpreter.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace asul {

static std::string generate_uuid_v4() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

void registerStdUuidPackage(Interpreter& interp) {
    interp.registerLazyPackage("std.uuid", [](std::shared_ptr<Object> pkg) {
        auto v4Fn = std::make_shared<Function>();
        v4Fn->isBuiltin = true;
        v4Fn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
            return Value{ generate_uuid_v4() };
        };
        (*pkg)["v4"] = v4Fn;
    });
}

} // namespace asul
