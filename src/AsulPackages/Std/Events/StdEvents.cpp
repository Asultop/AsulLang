#include "StdEvents.h"
#include "../../../AsulInterpreter.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

namespace asul {

// Native handle for AsulObject to store signal-slot connections
struct NativeEventObject {
    // Map from signal name to list of connected slots (callbacks)
    std::unordered_map<std::string, std::vector<Value>> slots;
};

void registerStdEventsPackage(Interpreter& interp) {
    auto globals = interp.globalsEnv();
    Interpreter* interpPtr = &interp;
    
    // Create AsulObject class
    auto asulObjectClass = std::make_shared<ClassInfo>();
    asulObjectClass->name = "AsulObject";
    asulObjectClass->isNative = true;
    
    // Helper to extract InstanceExt from closure
    auto getThisInstanceExt = [](std::shared_ptr<Environment> clos) -> InstanceExt* {
        if (!clos) throw std::runtime_error("internal: instance method called without closure");
        Value tv = clos->get("this");
        if (!std::holds_alternative<std::shared_ptr<Instance>>(tv))
            throw std::runtime_error("internal: invalid 'this' value");
        auto pins = std::get<std::shared_ptr<Instance>>(tv);
        if (!pins) throw std::runtime_error("internal: null 'this'");
        return static_cast<InstanceExt*>(pins.get());
    };
    
    // Constructor for AsulObject
    auto constructor = std::make_shared<Function>();
    constructor->isBuiltin = true;
    constructor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment> clos) -> Value {
        Value thisVal = clos->get("this");
        if (!std::holds_alternative<std::shared_ptr<Instance>>(thisVal))
            throw std::runtime_error("AsulObject.constructor: 'this' is not an instance");
        auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
        auto instExt = static_cast<InstanceExt*>(inst.get());
        
        // Allocate native event object handle with exception safety
        std::unique_ptr<NativeEventObject> neo(new NativeEventObject());
        instExt->nativeHandle = neo.release();
        instExt->nativeDestructor = [](void* p) { delete static_cast<NativeEventObject*>(p); };
        
        return Value{std::monostate{}};
    };
    asulObjectClass->methods["constructor"] = constructor;
    
    // emit(signal, ...args) - emit a signal with optional arguments
    auto emitFn = std::make_shared<Function>();
    emitFn->isBuiltin = true;
    emitFn->builtin = [getThisInstanceExt, interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> clos) -> Value {
        if (args.empty()) throw std::runtime_error("emit expects at least 1 argument (signal name)");
        
        InstanceExt* ie = getThisInstanceExt(clos);
        auto neo = static_cast<NativeEventObject*>(ie->nativeHandle);
        if (!neo) throw std::runtime_error("AsulObject: native handle missing");
        
        // First argument is signal name
        std::string signal = toString(args[0]);
        
        // Get signal arguments (all args after the signal name)
        std::vector<Value> signalArgs;
        for (size_t i = 1; i < args.size(); i++) {
            signalArgs.push_back(args[i]);
        }
        
        // Find and call all slots connected to this signal
        auto it = neo->slots.find(signal);
        if (it != neo->slots.end()) {
            for (const auto& slot : it->second) {
                // Each slot should be a function
                if (auto fn = std::get_if<std::shared_ptr<Function>>(&slot)) {
                    // Call the slot function with signal arguments
                    if ((*fn)->isBuiltin) {
                        (*fn)->builtin(signalArgs, (*fn)->closure);
                    } else {
                        // Handle non-builtin functions (lambdas)
                        // Check if arguments match
                        size_t expectedParams = (*fn)->params.size();
                        size_t providedArgs = signalArgs.size();
                        
                        // Handle rest parameters
                        if ((*fn)->restParamIndex >= 0) {
                            // Rest param allows any number of args >= non-rest params
                            size_t minArgs = (*fn)->restParamIndex;
                            if (providedArgs < minArgs) {
                                throw std::runtime_error("emit: slot function expects at least " + 
                                    std::to_string(minArgs) + " arguments but got " + std::to_string(providedArgs));
                            }
                        } else if (providedArgs != expectedParams) {
                            // For regular functions, arg count must match exactly
                            throw std::runtime_error("emit: slot function expects " + 
                                std::to_string(expectedParams) + " arguments but got " + std::to_string(providedArgs));
                        }
                        
                        // Create new environment for function execution
                        auto local = std::make_shared<Environment>((*fn)->closure);
                        
                        // Bind regular parameters
                        for (size_t i = 0; i < expectedParams && i < providedArgs; ++i) {
                            if ((*fn)->restParamIndex >= 0 && static_cast<int>(i) >= (*fn)->restParamIndex) {
                                // This is the rest parameter - collect remaining args into an array
                                auto restArr = std::make_shared<Array>();
                                for (size_t j = i; j < providedArgs; ++j) {
                                    restArr->push_back(signalArgs[j]);
                                }
                                local->define((*fn)->params[i], Value{restArr});
                                break;
                            } else {
                                local->define((*fn)->params[i], signalArgs[i]);
                            }
                        }
                        
                        // Execute function body
                        try {
                            interpPtr->executeBlock((*fn)->body, local);
                        } catch (const ReturnSignal& rs) {
                            // Ignore return value from slots
                            (void)rs;
                        }
                    }
                }
            }
        }
        
        return Value{std::monostate{}};
    };
    asulObjectClass->methods["emit"] = emitFn;
    
    // receive(signal, func) - connect a function to a signal
    auto receiveFn = std::make_shared<Function>();
    receiveFn->isBuiltin = true;
    receiveFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos) -> Value {
        if (args.size() != 2)
            throw std::runtime_error("receive expects 2 arguments (signal name, function)");
        
        InstanceExt* ie = getThisInstanceExt(clos);
        auto neo = static_cast<NativeEventObject*>(ie->nativeHandle);
        if (!neo) throw std::runtime_error("AsulObject: native handle missing");
        
        std::string signal = toString(args[0]);
        
        // Verify second argument is a function
        if (!std::holds_alternative<std::shared_ptr<Function>>(args[1]))
            throw std::runtime_error("receive: second argument must be a function");
        
        // Add the function to the signal's slot list
        neo->slots[signal].push_back(args[1]);
        
        return Value{std::monostate{}};
    };
    asulObjectClass->methods["receive"] = receiveFn;
    
    // Register the package
    interp.registerLazyPackage("std.events", [asulObjectClass, interpPtr](std::shared_ptr<Object> pkg) {
        (*pkg)["AsulObject"] = asulObjectClass;
        
        // Global connect function: connect(sender, signal, receiver, slot)
        auto connectFn = std::make_shared<Function>();
        connectFn->isBuiltin = true;
        connectFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
            if (args.size() != 4)
                throw std::runtime_error("connect expects 4 arguments (sender, signal, receiver, slot)");
            
            // sender must be an AsulObject instance
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0]))
                throw std::runtime_error("connect: sender must be an AsulObject instance");
            
            auto sender = std::get<std::shared_ptr<Instance>>(args[0]);
            auto senderExt = static_cast<InstanceExt*>(sender.get());
            auto neo = static_cast<NativeEventObject*>(senderExt->nativeHandle);
            if (!neo) throw std::runtime_error("connect: sender is not an AsulObject");
            
            std::string signal = toString(args[1]);
            
            // receiver must be an AsulObject instance
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[2]))
                throw std::runtime_error("connect: receiver must be an AsulObject instance");
            
            auto receiver = std::get<std::shared_ptr<Instance>>(args[2]);
            
            // slot can be a string (method name) or a function
            Value slotCallback;
            if (auto slotName = std::get_if<std::string>(&args[3])) {
                // If it's a string, look up the method on the receiver
                auto it = receiver->fields.find(*slotName);
                if (it == receiver->fields.end()) {
                    // Try to find it in the class methods
                    if (receiver->klass) {
                        auto methodIt = receiver->klass->methods.find(*slotName);
                        if (methodIt != receiver->klass->methods.end()) {
                            slotCallback = methodIt->second;
                        } else {
                            throw std::runtime_error("connect: receiver does not have method '" + *slotName + "'");
                        }
                    } else {
                        throw std::runtime_error("connect: receiver does not have method '" + *slotName + "'");
                    }
                } else {
                    slotCallback = it->second;
                }
                
                // Bind the method to the receiver instance
                // We need to create a wrapper function that calls the method with 'this' set to receiver
                auto boundFn = std::make_shared<Function>();
                boundFn->isBuiltin = true;
                auto receiverPtr = receiver;
                
                // Make a copy of the slot function (not a pointer to it)
                if (!std::holds_alternative<std::shared_ptr<Function>>(slotCallback))
                    throw std::runtime_error("connect: slot is not a function");
                auto slotFnCopy = std::get<std::shared_ptr<Function>>(slotCallback);
                
                boundFn->builtin = [receiverPtr, slotFnCopy, interpPtr](const std::vector<Value>& callArgs, std::shared_ptr<Environment> env) -> Value {
                    // Create a new environment with 'this' bound to receiver
                    auto boundEnv = std::make_shared<Environment>(slotFnCopy->closure);
                    boundEnv->define("this", receiverPtr);
                    
                    // Call the original function with the bound environment
                    if (slotFnCopy->isBuiltin) {
                        return slotFnCopy->builtin(callArgs, boundEnv);
                    } else {
                        // Handle non-builtin functions (lambdas)
                        size_t expectedParams = slotFnCopy->params.size();
                        size_t providedArgs = callArgs.size();
                        
                        if (slotFnCopy->restParamIndex >= 0) {
                            size_t minArgs = slotFnCopy->restParamIndex;
                            if (providedArgs < minArgs) {
                                throw std::runtime_error("connect: slot function expects at least " + 
                                    std::to_string(minArgs) + " arguments but got " + std::to_string(providedArgs));
                            }
                        } else if (providedArgs != expectedParams) {
                            throw std::runtime_error("connect: slot function expects " + 
                                std::to_string(expectedParams) + " arguments but got " + std::to_string(providedArgs));
                        }
                        
                        // Bind parameters
                        for (size_t i = 0; i < expectedParams && i < providedArgs; ++i) {
                            if (slotFnCopy->restParamIndex >= 0 && static_cast<int>(i) >= slotFnCopy->restParamIndex) {
                                auto restArr = std::make_shared<Array>();
                                for (size_t j = i; j < providedArgs; ++j) {
                                    restArr->push_back(callArgs[j]);
                                }
                                boundEnv->define(slotFnCopy->params[i], Value{restArr});
                                break;
                            } else {
                                boundEnv->define(slotFnCopy->params[i], callArgs[i]);
                            }
                        }
                        
                        // Execute function body
                        try {
                            interpPtr->executeBlock(slotFnCopy->body, boundEnv);
                        } catch (const ReturnSignal& rs) {
                            return rs.value;
                        }
                        return Value{std::monostate{}};
                    }
                };
                
                slotCallback = boundFn;
            } else if (std::holds_alternative<std::shared_ptr<Function>>(args[3])) {
                slotCallback = args[3];
            } else {
                throw std::runtime_error("connect: 槽必须是字符串 (method name) or a function");
            }
            
            // Add the slot to the signal
            neo->slots[signal].push_back(slotCallback);
            
            return Value{std::monostate{}};
        };
        (*pkg)["connect"] = connectFn;
    });
}

PackageMeta getStdEventsPackageMeta() {
    PackageMeta pkg;
    pkg.name = "std.events";
    pkg.exports = { "connect" };
    
    ClassMeta asulObjClass;
    asulObjClass.name = "AsulObject";
    asulObjClass.methods = { {"on"}, {"off"}, {"emit"}, {"receive"} };
    pkg.classes.push_back(asulObjClass);

    return pkg;
}

} // namespace asul
