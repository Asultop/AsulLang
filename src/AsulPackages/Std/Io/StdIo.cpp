#include "StdIo.h"
#include "../../../AsulInterpreter.h"
#include <sstream>

namespace asul {

static std::shared_ptr<ClassInfo> makeStreamClass(std::shared_ptr<Environment> env) {
    auto klass = std::make_shared<ClassInfo>();
    klass->name = "Stream";

    // fields: buffer:string, pos:number
    // constructor(optional init string)
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
        fn->params = { "init" }; fn->defaultValues = { nullptr };
        fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this");
            if (!std::holds_alternative<std::shared_ptr<Instance>>(thisV)) return Value{std::monostate{}};
            auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            inst->fields["buffer"] = Value{ std::string( args.size()>=1 && std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "" ) };
            inst->fields["pos"] = Value{ 0.0 };
            return Value{std::monostate{}};
        };
        klass->methods["constructor"] = fn;
    }

    // write(value): append string representation
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env; fn->params = { "value" };
        fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            std::string cur = std::get<std::string>(inst->fields["buffer"]);
            cur += toString(args.empty()? Value{std::monostate{}} : args[0]);
            inst->fields["buffer"] = Value{ cur };
            return thisV;
        };
        klass->methods["write"] = fn;
    }

    // readToken(): read until whitespace or end from current pos
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
        fn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            std::string cur = std::get<std::string>(inst->fields["buffer"]);
            size_t pos = static_cast<size_t>(Interpreter::getNumber(inst->fields["pos"], "pos"));
            while (pos < cur.size() && std::isspace(static_cast<unsigned char>(cur[pos]))) pos++;
            size_t start = pos;
            while (pos < cur.size() && !std::isspace(static_cast<unsigned char>(cur[pos]))) pos++;
            std::string tok = cur.substr(start, pos-start);
            inst->fields["pos"] = Value{ static_cast<double>(pos) };
            return Value{ tok };
        };
        klass->methods["readToken"] = fn;
    }

    // readLine(): read until \n
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
        fn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            std::string cur = std::get<std::string>(inst->fields["buffer"]);
            size_t pos = static_cast<size_t>(Interpreter::getNumber(inst->fields["pos"], "pos"));
            size_t start = pos;
            while (pos < cur.size() && cur[pos] != '\n') pos++;
            std::string line = cur.substr(start, pos-start);
            if (pos < cur.size() && cur[pos] == '\n') pos++;
            inst->fields["pos"] = Value{ static_cast<double>(pos) };
            return Value{ line };
        };
        klass->methods["readLine"] = fn;
    }

    // __shl__(value): streaming write (<<)
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env; fn->params = { "value" };
        fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            std::string cur = std::get<std::string>(inst->fields["buffer"]);
            cur += toString(args.empty()? Value{std::monostate{}} : args[0]);
            inst->fields["buffer"] = Value{ cur };
            return thisV;
        };
        klass->methods["__shl__"] = fn;
    }

    // __shr__(target): streaming read (>>) -> returns read token; if target provided and is object with 'value' assigns
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env; fn->params = { "target" };
        fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            std::string cur = std::get<std::string>(inst->fields["buffer"]);
            size_t pos = static_cast<size_t>(Interpreter::getNumber(inst->fields["pos"], "pos"));
            while (pos < cur.size() && std::isspace(static_cast<unsigned char>(cur[pos]))) pos++;
            size_t start = pos;
            while (pos < cur.size() && !std::isspace(static_cast<unsigned char>(cur[pos]))) pos++;
            std::string tok = cur.substr(start, pos-start);
            inst->fields["pos"] = Value{ static_cast<double>(pos) };
            Value ret{ tok };
            if (!args.empty()) {
                if (auto pobj = std::get_if<std::shared_ptr<Object>>(&args[0])) {
                    if (*pobj) { (**pobj)["value"] = ret; }
                }
            }
            return ret;
        };
        klass->methods["__shr__"] = fn;
    }

    // toString(): returns buffer
    {
        auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
        fn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
            auto thisV = clos->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisV);
            return Value{ std::get<std::string>(inst->fields["buffer"]) };
        };
        klass->methods["toString"] = fn;
    }

    return klass;
}

void registerStdIoPackage(Interpreter& interp) {
    interp.registerLazyPackage("std.io", [&interp](std::shared_ptr<Object> pkg){
        // Expose Stream class
        auto klass = makeStreamClass(interp.globalsEnv());
        (*pkg)["Stream"] = Value{ klass };
    });
}

} // namespace asul
