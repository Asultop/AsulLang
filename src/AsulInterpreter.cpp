#include "AsulInterpreter.h"
#include "AsulPackages.h"
#include <csignal>

namespace asul
{

    // Signal handling implementation
    std::atomic<int> g_pendingSignals[32];

    void globalSignalHandler(int sig)
    {
        if (sig > 0 && sig < 32)
            g_pendingSignals[sig] = 1;
        // Re-register the handler (some platforms reset to default after signal)
        std::signal(sig, globalSignalHandler);
    }

} // namespace asul

// Register all external packages
void registerExternalPackages(asul::Interpreter &interp)
{
    asul::registerStdPathPackage(interp);
    asul::registerStdStringPackage(interp);
    asul::registerStdMathPackage(interp);
    asul::registerStdTimePackage(interp);
    asul::registerStdOsPackage(interp);
    asul::registerStdRegexPackage(interp);
    asul::registerStdEncodingPackage(interp);
    asul::registerStdNetworkPackage(interp);
    asul::registerStdCryptoPackage(interp);
    asul::registerStdIoPackage(interp);
    asul::registerStdBuiltinPackage(interp);
    asul::registerStdCollectionsPackage(interp);
    asul::registerStdArrayPackage(interp);
    asul::registerStdLogPackage(interp);
    asul::registerStdTestPackage(interp);
    asul::registerStdFfiPackage(interp);
    asul::registerCsvPackage(interp);
    asul::registerJsonPackage(interp);
    asul::registerXmlPackage(interp);
    asul::registerYamlPackage(interp);
    asul::registerOsPackage(interp);
}

namespace asul
{

    // Out-of-line method definitions
    void Interpreter::registerLazyPackage(const std::string &name, std::function<void(std::shared_ptr<Object>)> init)
    {
        lazyPackages[name] = init;
    }

    std::shared_ptr<Object> Interpreter::ensurePackage(const std::string &name)
    {
        auto it = packages.find(name);
        if (it != packages.end() && it->second)
            return it->second;
        auto pkg = std::make_shared<Object>();
        packages[name] = pkg;
        if (stdRoot && name.rfind("std.", 0) == 0)
        {
            std::string suffix = name.substr(4);
            auto parent = stdRoot;
            size_t pos = 0;
            while (pos <= suffix.size())
            {
                size_t dot = suffix.find('.', pos);
                std::string part = suffix.substr(pos, dot == std::string::npos ? suffix.size() - pos : dot - pos);
                if (part.empty())
                    break;
                if (dot == std::string::npos)
                {
                    (*parent)[part] = Value{pkg};
                    break;
                }
                auto itPart = parent->find(part);
                std::shared_ptr<Object> next;
                if (itPart != parent->end() && std::holds_alternative<std::shared_ptr<Object>>(itPart->second))
                {
                    next = std::get<std::shared_ptr<Object>>(itPart->second);
                }
                else
                {
                    next = std::make_shared<Object>();
                    (*parent)[part] = Value{next};
                }
                parent = next;
                pos = dot + 1;
            }
        }
        return pkg;
    }

    void Interpreter::importPackageSymbols(const std::string &name)
    {
        auto it = packages.find(name);
        if (it == packages.end() || !it->second)
            return;
        for (auto &kv : *(it->second))
        {
            env->define(kv.first, kv.second);
        }
    }

    void Interpreter::registerPackageSymbol(const std::string &pkgName, const std::string &symbol, const Value &value)
    {
        auto pkg = ensurePackage(pkgName);
        (*pkg)[symbol] = value;
    }

} // namespace asul
