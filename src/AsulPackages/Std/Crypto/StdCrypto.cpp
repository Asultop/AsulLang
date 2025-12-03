#include "StdCrypto.h"
#include "../../../AsulInterpreter.h"
#include <random>
#include <sstream>
#include <iomanip>

#ifdef ASUL_HAS_OPENSSL
#include <openssl/evp.h>
#endif

namespace asul {

static std::string generateUUIDv4() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist64;
    uint64_t a = dist64(gen);
    uint64_t b = dist64(gen);
    unsigned char bytes[16];
    for (int i=0;i<8;++i) bytes[i] = (unsigned char)((a >> (8*(7-i))) & 0xFF);
    for (int i=0;i<8;++i) bytes[8+i] = (unsigned char)((b >> (8*(7-i))) & 0xFF);
    // Set version (4) and variant (RFC 4122)
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80); // variant 10xxxxxx
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0');
    for (int i=0;i<16;++i) {
        oss << std::setw(2) << (int)bytes[i];
        if (i==3||i==5||i==7||i==9) oss << '-';
    }
    return oss.str();
}

void registerStdCryptoPackage(Interpreter& interp) {
    interp.registerLazyPackage("std.crypto", [](std::shared_ptr<Object> pkg){
        // crypto.randomUUID() -> string
        auto uuidFn = std::make_shared<Function>(); uuidFn->isBuiltin = true;
        uuidFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
            return Value{ generateUUIDv4() };
        }; (*pkg)["randomUUID"] = Value{ uuidFn };

        // crypto.getRandomValues(n) -> Array<number 0..255>
        auto grvFn = std::make_shared<Function>(); grvFn->isBuiltin = true;
        grvFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 1) throw std::runtime_error("getRandomValues expects length argument");
            int n = static_cast<int>(Interpreter::getNumber(args[0], "getRandomValues length"));
            if (n < 0) n = 0;
            std::random_device rd;
            std::uniform_int_distribution<int> dist(0,255);
            auto arr = std::make_shared<Array>();
            for (int i=0;i<n;++i) arr->push_back(Value{ static_cast<double>(dist(rd)) });
            return Value{arr};
        }; (*pkg)["getRandomValues"] = Value{ grvFn };

        // Hash functions: md5/sha1/sha256
        auto hexOut = [](const unsigned char* data, size_t len){
            std::ostringstream oss; oss << std::hex << std::nouppercase << std::setfill('0');
            for (size_t i=0;i<len;++i) oss << std::setw(2) << (int)data[i];
            return oss.str();
        };

#ifdef ASUL_HAS_OPENSSL
        auto digestHex = [hexOut](const std::string& algo, const std::string& data)->std::string {
            const EVP_MD* md = EVP_get_digestbyname(algo.c_str());
            if (!md) throw std::runtime_error("OpenSSL: unknown digest algo '" + algo + "'");
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) throw std::runtime_error("OpenSSL: EVP_MD_CTX_new failed");
            unsigned char out[EVP_MAX_MD_SIZE]; unsigned int outLen = 0;
            std::string hex;
            try {
                if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) throw std::runtime_error("OpenSSL: DigestInit failed");
                if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) throw std::runtime_error("OpenSSL: DigestUpdate failed");
                if (EVP_DigestFinal_ex(ctx, out, &outLen) != 1) throw std::runtime_error("OpenSSL: DigestFinal failed");
                hex = hexOut(out, outLen);
            } catch(...) {
                EVP_MD_CTX_free(ctx);
                throw;
            }
            EVP_MD_CTX_free(ctx);
            return hex;
        };

        auto md5Fn = std::make_shared<Function>(); md5Fn->isBuiltin = true;
        md5Fn->builtin = [digestHex](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 1 || !std::holds_alternative<std::string>(args[0]))
                throw std::runtime_error("md5 expects a string argument");
            return Value{ digestHex("MD5", std::get<std::string>(args[0])) };
        }; (*pkg)["md5"] = Value{ md5Fn };

        auto sha1Fn = std::make_shared<Function>(); sha1Fn->isBuiltin = true;
        sha1Fn->builtin = [digestHex](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 1 || !std::holds_alternative<std::string>(args[0]))
                throw std::runtime_error("sha1 expects a string argument");
            return Value{ digestHex("SHA1", std::get<std::string>(args[0])) };
        }; (*pkg)["sha1"] = Value{ sha1Fn };

        auto sha256Fn = std::make_shared<Function>(); sha256Fn->isBuiltin = true;
        sha256Fn->builtin = [digestHex](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 1 || !std::holds_alternative<std::string>(args[0]))
                throw std::runtime_error("sha256 expects a string argument");
            return Value{ digestHex("SHA256", std::get<std::string>(args[0])) };
        }; (*pkg)["sha256"] = Value{ sha256Fn };
#else
        auto mkStub = [](const char* name){
            auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
            fn->builtin = [name](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
                if (args.size() < 1 || !std::holds_alternative<std::string>(args[0]))
                    throw std::runtime_error(std::string(name) + " expects a string argument");
                throw std::runtime_error(std::string(name) + " not implemented yet");
            }; return fn;
        };
        (*pkg)["md5"] = Value{ mkStub("md5") };
        (*pkg)["sha1"] = Value{ mkStub("sha1") };
        (*pkg)["sha256"] = Value{ mkStub("sha256") };
#endif
    });
}

} // namespace asul
