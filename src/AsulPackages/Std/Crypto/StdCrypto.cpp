#include "StdCrypto.h"
#include "../../../AsulInterpreter.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef ASUL_HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
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

#ifdef ASUL_HAS_OPENSSL
        // AES-256-CBC encryption
        // aes.encrypt(plaintext: string, key: string, iv: string) -> base64 string
        auto aesObj = std::make_shared<Object>();
        auto aesEncryptFn = std::make_shared<Function>(); aesEncryptFn->isBuiltin = true;
        aesEncryptFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 3) throw std::runtime_error("aes.encrypt expects (plaintext, key, iv)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1]) || 
                !std::holds_alternative<std::string>(args[2])) {
                throw std::runtime_error("aes.encrypt expects all string arguments");
            }
            std::string plaintext = std::get<std::string>(args[0]);
            std::string key = std::get<std::string>(args[1]);
            std::string iv = std::get<std::string>(args[2]);
            
            if (key.size() != 32) throw std::runtime_error("AES-256 key must be 32 bytes");
            if (iv.size() != 16) throw std::runtime_error("AES IV must be 16 bytes");
            
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx) throw std::runtime_error("Failed to create cipher context");
            
            std::vector<unsigned char> ciphertext(plaintext.size() + AES_BLOCK_SIZE);
            int len = 0, ciphertext_len = 0;
            
            try {
                if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, 
                    reinterpret_cast<const unsigned char*>(key.data()),
                    reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
                    throw std::runtime_error("EVP_EncryptInit_ex failed");
                }
                
                if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                    reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()) != 1) {
                    throw std::runtime_error("EVP_EncryptUpdate failed");
                }
                ciphertext_len = len;
                
                if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
                    throw std::runtime_error("EVP_EncryptFinal_ex failed");
                }
                ciphertext_len += len;
                
                EVP_CIPHER_CTX_free(ctx);
                
                // Convert to base64
                BIO* b64 = BIO_new(BIO_f_base64());
                BIO* bio = BIO_new(BIO_s_mem());
                bio = BIO_push(b64, bio);
                BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
                BIO_write(bio, ciphertext.data(), ciphertext_len);
                BIO_flush(bio);
                
                BUF_MEM* bufferPtr;
                BIO_get_mem_ptr(bio, &bufferPtr);
                std::string result(bufferPtr->data, bufferPtr->length);
                BIO_free_all(bio);
                
                return Value{ result };
            } catch (...) {
                EVP_CIPHER_CTX_free(ctx);
                throw;
            }
        };
        (*aesObj)["encrypt"] = Value{ aesEncryptFn };
        
        // aes.decrypt(ciphertext: base64 string, key: string, iv: string) -> string
        auto aesDecryptFn = std::make_shared<Function>(); aesDecryptFn->isBuiltin = true;
        aesDecryptFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 3) throw std::runtime_error("aes.decrypt expects (ciphertext, key, iv)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1]) || 
                !std::holds_alternative<std::string>(args[2])) {
                throw std::runtime_error("aes.decrypt expects all string arguments");
            }
            std::string ciphertext_b64 = std::get<std::string>(args[0]);
            std::string key = std::get<std::string>(args[1]);
            std::string iv = std::get<std::string>(args[2]);
            
            if (key.size() != 32) throw std::runtime_error("AES-256 key must be 32 bytes");
            if (iv.size() != 16) throw std::runtime_error("AES IV must be 16 bytes");
            
            // Decode base64
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO* bio = BIO_new_mem_buf(ciphertext_b64.data(), ciphertext_b64.size());
            bio = BIO_push(b64, bio);
            BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
            
            std::vector<unsigned char> ciphertext(ciphertext_b64.size());
            int decoded_len = BIO_read(bio, ciphertext.data(), ciphertext.size());
            BIO_free_all(bio);
            
            if (decoded_len < 0) throw std::runtime_error("Base64 decode failed");
            
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx) throw std::runtime_error("Failed to create cipher context");
            
            std::vector<unsigned char> plaintext(decoded_len + AES_BLOCK_SIZE);
            int len = 0, plaintext_len = 0;
            
            try {
                if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                    reinterpret_cast<const unsigned char*>(key.data()),
                    reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
                    throw std::runtime_error("EVP_DecryptInit_ex failed");
                }
                
                if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                    ciphertext.data(), decoded_len) != 1) {
                    throw std::runtime_error("EVP_DecryptUpdate failed");
                }
                plaintext_len = len;
                
                if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
                    throw std::runtime_error("EVP_DecryptFinal_ex failed");
                }
                plaintext_len += len;
                
                EVP_CIPHER_CTX_free(ctx);
                
                return Value{ std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len) };
            } catch (...) {
                EVP_CIPHER_CTX_free(ctx);
                throw;
            }
        };
        (*aesObj)["decrypt"] = Value{ aesDecryptFn };
        
        // aes.generateKey() -> 32-byte hex string
        auto aesGenKeyFn = std::make_shared<Function>(); aesGenKeyFn->isBuiltin = true;
        aesGenKeyFn->builtin = [hexOut](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
            unsigned char key[32];
            if (RAND_bytes(key, 32) != 1) throw std::runtime_error("RAND_bytes failed");
            return Value{ hexOut(key, 32) };
        };
        (*aesObj)["generateKey"] = Value{ aesGenKeyFn };
        
        // aes.generateIV() -> 16-byte hex string
        auto aesGenIVFn = std::make_shared<Function>(); aesGenIVFn->isBuiltin = true;
        aesGenIVFn->builtin = [hexOut](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
            unsigned char iv[16];
            if (RAND_bytes(iv, 16) != 1) throw std::runtime_error("RAND_bytes failed");
            return Value{ hexOut(iv, 16) };
        };
        (*aesObj)["generateIV"] = Value{ aesGenIVFn };
        
        (*pkg)["aes"] = Value{ aesObj };
        
        // RSA-2048 functions
        auto rsaObj = std::make_shared<Object>();
        
        // rsa.generateKeyPair() -> {publicKey: string(PEM), privateKey: string(PEM)}
        auto rsaGenKeyPairFn = std::make_shared<Function>(); rsaGenKeyPairFn->isBuiltin = true;
        rsaGenKeyPairFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
            EVP_PKEY* pkey = nullptr;
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
            if (!ctx) throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
            
            try {
                if (EVP_PKEY_keygen_init(ctx) <= 0) throw std::runtime_error("EVP_PKEY_keygen_init failed");
                if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) throw std::runtime_error("EVP_PKEY_CTX_set_rsa_keygen_bits failed");
                if (EVP_PKEY_keygen(ctx, &pkey) <= 0) throw std::runtime_error("EVP_PKEY_keygen failed");
                
                EVP_PKEY_CTX_free(ctx);
                
                // Export public key
                BIO* pubBio = BIO_new(BIO_s_mem());
                if (!PEM_write_bio_PUBKEY(pubBio, pkey)) {
                    BIO_free(pubBio);
                    EVP_PKEY_free(pkey);
                    throw std::runtime_error("PEM_write_bio_PUBKEY failed");
                }
                BUF_MEM* pubBuf;
                BIO_get_mem_ptr(pubBio, &pubBuf);
                std::string publicKey(pubBuf->data, pubBuf->length);
                BIO_free(pubBio);
                
                // Export private key
                BIO* privBio = BIO_new(BIO_s_mem());
                if (!PEM_write_bio_PrivateKey(privBio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
                    BIO_free(privBio);
                    EVP_PKEY_free(pkey);
                    throw std::runtime_error("PEM_write_bio_PrivateKey failed");
                }
                BUF_MEM* privBuf;
                BIO_get_mem_ptr(privBio, &privBuf);
                std::string privateKey(privBuf->data, privBuf->length);
                BIO_free(privBio);
                
                EVP_PKEY_free(pkey);
                
                auto result = std::make_shared<Object>();
                (*result)["publicKey"] = Value{ publicKey };
                (*result)["privateKey"] = Value{ privateKey };
                return Value{ result };
            } catch (...) {
                EVP_PKEY_CTX_free(ctx);
                if (pkey) EVP_PKEY_free(pkey);
                throw;
            }
        };
        (*rsaObj)["generateKeyPair"] = Value{ rsaGenKeyPairFn };
        
        // rsa.encrypt(plaintext: string, publicKeyPEM: string) -> base64 string
        auto rsaEncryptFn = std::make_shared<Function>(); rsaEncryptFn->isBuiltin = true;
        rsaEncryptFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 2) throw std::runtime_error("rsa.encrypt expects (plaintext, publicKeyPEM)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) {
                throw std::runtime_error("rsa.encrypt expects all string arguments");
            }
            std::string plaintext = std::get<std::string>(args[0]);
            std::string pubKeyPEM = std::get<std::string>(args[1]);
            
            BIO* bio = BIO_new_mem_buf(pubKeyPEM.data(), pubKeyPEM.size());
            EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (!pkey) throw std::runtime_error("Failed to parse public key PEM");
            
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
            if (!ctx) {
                EVP_PKEY_free(pkey);
                throw std::runtime_error("EVP_PKEY_CTX_new failed");
            }
            
            try {
                if (EVP_PKEY_encrypt_init(ctx) <= 0) throw std::runtime_error("EVP_PKEY_encrypt_init failed");
                if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) throw std::runtime_error("EVP_PKEY_CTX_set_rsa_padding failed");
                
                size_t outlen;
                if (EVP_PKEY_encrypt(ctx, nullptr, &outlen, 
                    reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()) <= 0) {
                    throw std::runtime_error("EVP_PKEY_encrypt (size) failed");
                }
                
                std::vector<unsigned char> ciphertext(outlen);
                if (EVP_PKEY_encrypt(ctx, ciphertext.data(), &outlen,
                    reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()) <= 0) {
                    throw std::runtime_error("EVP_PKEY_encrypt failed");
                }
                
                EVP_PKEY_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                
                // Convert to base64
                BIO* b64 = BIO_new(BIO_f_base64());
                BIO* outBio = BIO_new(BIO_s_mem());
                outBio = BIO_push(b64, outBio);
                BIO_set_flags(outBio, BIO_FLAGS_BASE64_NO_NL);
                BIO_write(outBio, ciphertext.data(), outlen);
                BIO_flush(outBio);
                
                BUF_MEM* bufferPtr;
                BIO_get_mem_ptr(outBio, &bufferPtr);
                std::string result(bufferPtr->data, bufferPtr->length);
                BIO_free_all(outBio);
                
                return Value{ result };
            } catch (...) {
                EVP_PKEY_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                throw;
            }
        };
        (*rsaObj)["encrypt"] = Value{ rsaEncryptFn };
        
        // rsa.decrypt(ciphertext: base64 string, privateKeyPEM: string) -> string
        auto rsaDecryptFn = std::make_shared<Function>(); rsaDecryptFn->isBuiltin = true;
        rsaDecryptFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 2) throw std::runtime_error("rsa.decrypt expects (ciphertext, privateKeyPEM)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) {
                throw std::runtime_error("rsa.decrypt expects all string arguments");
            }
            std::string ciphertext_b64 = std::get<std::string>(args[0]);
            std::string privKeyPEM = std::get<std::string>(args[1]);
            
            // Decode base64
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO* bio = BIO_new_mem_buf(ciphertext_b64.data(), ciphertext_b64.size());
            bio = BIO_push(b64, bio);
            BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
            
            std::vector<unsigned char> ciphertext(ciphertext_b64.size());
            int decoded_len = BIO_read(bio, ciphertext.data(), ciphertext.size());
            BIO_free_all(bio);
            if (decoded_len < 0) throw std::runtime_error("Base64 decode failed");
            
            BIO* keyBio = BIO_new_mem_buf(privKeyPEM.data(), privKeyPEM.size());
            EVP_PKEY* pkey = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
            BIO_free(keyBio);
            if (!pkey) throw std::runtime_error("Failed to parse private key PEM");
            
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
            if (!ctx) {
                EVP_PKEY_free(pkey);
                throw std::runtime_error("EVP_PKEY_CTX_new failed");
            }
            
            try {
                if (EVP_PKEY_decrypt_init(ctx) <= 0) throw std::runtime_error("EVP_PKEY_decrypt_init failed");
                if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) throw std::runtime_error("EVP_PKEY_CTX_set_rsa_padding failed");
                
                size_t outlen;
                if (EVP_PKEY_decrypt(ctx, nullptr, &outlen, ciphertext.data(), decoded_len) <= 0) {
                    throw std::runtime_error("EVP_PKEY_decrypt (size) failed");
                }
                
                std::vector<unsigned char> plaintext(outlen);
                if (EVP_PKEY_decrypt(ctx, plaintext.data(), &outlen, ciphertext.data(), decoded_len) <= 0) {
                    throw std::runtime_error("EVP_PKEY_decrypt failed");
                }
                
                EVP_PKEY_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                
                return Value{ std::string(reinterpret_cast<char*>(plaintext.data()), outlen) };
            } catch (...) {
                EVP_PKEY_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                throw;
            }
        };
        (*rsaObj)["decrypt"] = Value{ rsaDecryptFn };
        
        // rsa.sign(message: string, privateKeyPEM: string) -> base64 string
        auto rsaSignFn = std::make_shared<Function>(); rsaSignFn->isBuiltin = true;
        rsaSignFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 2) throw std::runtime_error("rsa.sign expects (message, privateKeyPEM)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) {
                throw std::runtime_error("rsa.sign expects all string arguments");
            }
            std::string message = std::get<std::string>(args[0]);
            std::string privKeyPEM = std::get<std::string>(args[1]);
            
            BIO* bio = BIO_new_mem_buf(privKeyPEM.data(), privKeyPEM.size());
            EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (!pkey) throw std::runtime_error("Failed to parse private key PEM");
            
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) {
                EVP_PKEY_free(pkey);
                throw std::runtime_error("EVP_MD_CTX_new failed");
            }
            
            try {
                if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0) {
                    throw std::runtime_error("EVP_DigestSignInit failed");
                }
                
                if (EVP_DigestSignUpdate(ctx, message.data(), message.size()) <= 0) {
                    throw std::runtime_error("EVP_DigestSignUpdate failed");
                }
                
                size_t siglen;
                if (EVP_DigestSignFinal(ctx, nullptr, &siglen) <= 0) {
                    throw std::runtime_error("EVP_DigestSignFinal (size) failed");
                }
                
                std::vector<unsigned char> signature(siglen);
                if (EVP_DigestSignFinal(ctx, signature.data(), &siglen) <= 0) {
                    throw std::runtime_error("EVP_DigestSignFinal failed");
                }
                
                EVP_MD_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                
                // Convert to base64
                BIO* b64 = BIO_new(BIO_f_base64());
                BIO* outBio = BIO_new(BIO_s_mem());
                outBio = BIO_push(b64, outBio);
                BIO_set_flags(outBio, BIO_FLAGS_BASE64_NO_NL);
                BIO_write(outBio, signature.data(), siglen);
                BIO_flush(outBio);
                
                BUF_MEM* bufferPtr;
                BIO_get_mem_ptr(outBio, &bufferPtr);
                std::string result(bufferPtr->data, bufferPtr->length);
                BIO_free_all(outBio);
                
                return Value{ result };
            } catch (...) {
                EVP_MD_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                throw;
            }
        };
        (*rsaObj)["sign"] = Value{ rsaSignFn };
        
        // rsa.verify(message: string, signature: base64 string, publicKeyPEM: string) -> bool
        auto rsaVerifyFn = std::make_shared<Function>(); rsaVerifyFn->isBuiltin = true;
        rsaVerifyFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 3) throw std::runtime_error("rsa.verify expects (message, signature, publicKeyPEM)");
            if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1]) ||
                !std::holds_alternative<std::string>(args[2])) {
                throw std::runtime_error("rsa.verify expects all string arguments");
            }
            std::string message = std::get<std::string>(args[0]);
            std::string signature_b64 = std::get<std::string>(args[1]);
            std::string pubKeyPEM = std::get<std::string>(args[2]);
            
            // Decode base64 signature
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO* bio = BIO_new_mem_buf(signature_b64.data(), signature_b64.size());
            bio = BIO_push(b64, bio);
            BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
            
            std::vector<unsigned char> signature(signature_b64.size());
            int decoded_len = BIO_read(bio, signature.data(), signature.size());
            BIO_free_all(bio);
            if (decoded_len < 0) throw std::runtime_error("Base64 decode failed");
            
            BIO* keyBio = BIO_new_mem_buf(pubKeyPEM.data(), pubKeyPEM.size());
            EVP_PKEY* pkey = PEM_read_bio_PUBKEY(keyBio, nullptr, nullptr, nullptr);
            BIO_free(keyBio);
            if (!pkey) throw std::runtime_error("Failed to parse public key PEM");
            
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) {
                EVP_PKEY_free(pkey);
                throw std::runtime_error("EVP_MD_CTX_new failed");
            }
            
            try {
                if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0) {
                    throw std::runtime_error("EVP_DigestVerifyInit failed");
                }
                
                if (EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) <= 0) {
                    throw std::runtime_error("EVP_DigestVerifyUpdate failed");
                }
                
                int result = EVP_DigestVerifyFinal(ctx, signature.data(), decoded_len);
                
                EVP_MD_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                
                return Value{ result == 1 };
            } catch (...) {
                EVP_MD_CTX_free(ctx);
                EVP_PKEY_free(pkey);
                throw;
            }
        };
        (*rsaObj)["verify"] = Value{ rsaVerifyFn };
        
        (*pkg)["rsa"] = Value{ rsaObj };
        
        // Streaming hash support
        auto createHashFn = std::make_shared<Function>(); createHashFn->isBuiltin = true;
        createHashFn->builtin = [hexOut](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
            if (args.size() < 1) throw std::runtime_error("createHash expects algorithm name");
            if (!std::holds_alternative<std::string>(args[0])) {
                throw std::runtime_error("createHash expects string algorithm name");
            }
            std::string algo = std::get<std::string>(args[0]);
            
            const EVP_MD* md = EVP_get_digestbyname(algo.c_str());
            if (!md) throw std::runtime_error("Unknown hash algorithm: " + algo);
            
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
            
            if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestInit_ex failed");
            }
            
            auto hashObj = std::make_shared<Object>();
            
            // Store context as a raw pointer in a shared_ptr<void*> wrapper
            // This approach allows us to:
            // 1. Share the EVP_MD_CTX pointer between update() and digest() closures
            // 2. Track whether digest() has been called (nullptr check)
            // 3. Properly clean up OpenSSL resources when digest() is called
            // Note: The context is freed in digest(), preventing reuse after finalization
            auto ctxPtr = std::make_shared<void*>(static_cast<void*>(ctx));
            
            // update(data: string) -> this
            auto updateFn = std::make_shared<Function>(); updateFn->isBuiltin = true;
            updateFn->builtin = [ctxPtr, hashObj](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
                if (args.size() < 1) throw std::runtime_error("hash.update expects data");
                if (!std::holds_alternative<std::string>(args[0])) {
                    throw std::runtime_error("hash.update expects string data");
                }
                std::string data = std::get<std::string>(args[0]);
                
                EVP_MD_CTX* ctx = static_cast<EVP_MD_CTX*>(*ctxPtr);
                if (!ctx) {
                    throw std::runtime_error("Hash already finalized - cannot update after digest() called");
                }
                if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
                    throw std::runtime_error("EVP_DigestUpdate failed");
                }
                
                return Value{ hashObj };
            };
            (*hashObj)["update"] = Value{ updateFn };
            
            // digest() -> hex string
            auto digestFn = std::make_shared<Function>(); digestFn->isBuiltin = true;
            digestFn->builtin = [ctxPtr, hexOut](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
                EVP_MD_CTX* ctx = static_cast<EVP_MD_CTX*>(*ctxPtr);
                
                if (!ctx) {
                    throw std::runtime_error("Hash already finalized - digest() can only be called once");
                }
                
                unsigned char hash[EVP_MAX_MD_SIZE];
                unsigned int hash_len = 0;
                
                if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
                    throw std::runtime_error("EVP_DigestFinal_ex failed");
                }
                
                EVP_MD_CTX_free(ctx);
                *ctxPtr = nullptr; // Mark as finalized
                
                return Value{ hexOut(hash, hash_len) };
            };
            (*hashObj)["digest"] = Value{ digestFn };
            
            return Value{ hashObj };
        };
        (*pkg)["createHash"] = Value{ createHashFn };
#endif
    });
}

} // namespace asul
