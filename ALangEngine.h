#ifndef ALANGENGINE_H
#define ALANGENGINE_H

#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class ALangEngine {
public:
    ALangEngine();
    ~ALangEngine();
    void initialize();
    void execute(const std::string& code);
    void execute();
    void setSource(const std::string& code);
    void registerModule(const char* moduleName, std::function<void()> initFunc);
    void setErrorColorMap(const std::unordered_map<std::string, std::string>& colorMap);
    void setImportBaseDir(const std::string& dir);

    using NativeValue = std::variant<std::monostate,double,std::string,bool>;
    using NativeFunc = std::function<NativeValue(const std::vector<NativeValue>&, void* thisHandle)>;
    void registerClass(
        const std::string& className,
        NativeFunc constructor,
        const std::unordered_map<std::string, NativeFunc>& methods,
        const std::vector<std::string>& baseClasses = {}
    );

    NativeValue callFunction(
        const std::string& functionName,
        const std::vector<NativeValue>& args
    );

    // Bridge: safe HostValue wrapper for host-facing APIs. Hosts can use
    // HostValue and the HostFunc signatures to interact with engine values
    // without the engine exposing its internal Value layout.
    class HostValue {
    public:
        enum class Type { Null, Number, String, Bool, Opaque };
        HostValue(): t(Type::Null), num(0), str(), b(false), opaque(nullptr) {}
        static HostValue Null() { return HostValue(); }
        static HostValue Number(double v) { HostValue h; h.t = Type::Number; h.num = v; return h; }
        static HostValue String(const std::string& s) { HostValue h; h.t = Type::String; h.str = s; return h; }
        static HostValue Bool(bool v) { HostValue h; h.t = Type::Bool; h.b = v; return h; }
        static HostValue Opaque(void* p) { HostValue h; h.t = Type::Opaque; h.opaque = p; return h; }

        Type type() const { return t; }
        double asNumber() const { return num; }
        const std::string& asString() const { return str; }
        bool asBool() const { return b; }
        void* asOpaque() const { return opaque; }
    private:
        Type t{Type::Null};
        double num{0};
        std::string str;
        bool b{false};
        void* opaque{nullptr};
    };

    using HostFunc = std::function<HostValue(const std::vector<HostValue>&, void* thisHandle)>;

    void registerClassValue(
        const std::string& className,
        HostFunc constructor,
        const std::unordered_map<std::string, HostFunc>& methods,
        const std::vector<std::string>& baseClasses = {}
    );

    HostValue callFunctionValue(
        const std::string& functionName,
        const std::vector<HostValue>& args
    );

    void runEventLoopUntilIdle();

    void setGlobal(const std::string& name, const NativeValue& value);
    // Variant that accepts HostValue bridge for host-facing APIs
    void setGlobalValue(const std::string& name, const HostValue& value);
    void registerFunction(const std::string& name, NativeFunc func);
    void registerFunctionValue(const std::string& name, HostFunc func);
    void registerInterface(const std::string& name, const std::vector<std::string>& methodNames);
private:
    struct Impl;
    Impl* impl;

protected:

};

#endif // ALANGENGINE_H