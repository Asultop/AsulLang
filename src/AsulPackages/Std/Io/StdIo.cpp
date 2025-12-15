#include "StdIo.h"
#include "../../../AsulInterpreter.h"
#include <sstream>
#include <fstream>
#include <filesystem>

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
	// Get interpreter pointer for lambdas that need it
	Interpreter* interpPtr = &interp;
	
	auto ioPkg = interp.ensurePackage("std.io");
	{
		// Populate std.io package directly (not lazy)
		// Expose Stream class
		auto klass = makeStreamClass(interp.globalsEnv());
		(*ioPkg)["Stream"] = Value{ klass };
		
		// Add I/O functions and File/Dir/FileStream classes
		auto fsPkg = interp.ensurePackage("std.io.fileSystem");
		
				auto printFn = std::make_shared<Function>();
				printFn->isBuiltin = true;
				printFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					for (auto& v : args) std::cout << toString(v);
					return Value{std::monostate{}};
				};
				(*ioPkg)["print"] = Value{printFn};

				auto printlnFn = std::make_shared<Function>();
				printlnFn->isBuiltin = true;
				printlnFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					for (auto& v : args) std::cout << toString(v);
					std::cout << std::endl;
					return Value{std::monostate{}};
				};
				(*ioPkg)["println"] = Value{printlnFn};

				// File I/O helpers: readFile(path), writeFile(path, data), appendFile(path, data), exists(path), listDir(path)
				auto readFileFn = std::make_shared<Function>();
				readFileFn->isBuiltin = true;
				readFileFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 1) throw std::runtime_error("readFile 需要1个参数 (path string)");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("readFile path must be string");
					std::string path = std::get<std::string>(args[0]);
					std::ifstream in(path, std::ios::in | std::ios::binary);
					if (!in) {
						std::ostringstream oss; oss << "Failed to open file for reading: " << path; throw std::runtime_error(oss.str());
					}
					std::ostringstream buffer; buffer << in.rdbuf();
					return Value{buffer.str()};
				};
				(*ioPkg)["readFile"] = Value{readFileFn};

				auto writeFileFn = std::make_shared<Function>();
				writeFileFn->isBuiltin = true;
				writeFileFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 2) throw std::runtime_error("writeFile 需要2个参数 (path, data)");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("writeFile path must be string");
					std::string path = std::get<std::string>(args[0]);
					std::string data = toString(args[1]);
					std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
					if (!out) { std::ostringstream oss; oss << "Failed to open file for writing: " << path; throw std::runtime_error(oss.str()); }
					out << data;
					return Value{true};
				};
				(*ioPkg)["writeFile"] = Value{writeFileFn};

				auto appendFileFn = std::make_shared<Function>();
				appendFileFn->isBuiltin = true;
				appendFileFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 2) throw std::runtime_error("appendFile 需要2个参数 (path, data)");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("appendFile path must be string");
					std::string path = std::get<std::string>(args[0]);
					std::string data = toString(args[1]);
					std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::app);
					if (!out) { std::ostringstream oss; oss << "Failed to open file for appending: " << path; throw std::runtime_error(oss.str()); }
					out << data;
					return Value{true};
				};
				(*ioPkg)["appendFile"] = Value{appendFileFn};

				auto existsFn = std::make_shared<Function>();
				existsFn->isBuiltin = true;
				existsFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 1) throw std::runtime_error("exists expects 1 argument (path string)");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("exists path must be string");
					std::string path = std::get<std::string>(args[0]);
					return Value{ std::filesystem::exists(path) };
				};
				(*ioPkg)["exists"] = Value{existsFn};

				auto listDirFn = std::make_shared<Function>();
				listDirFn->isBuiltin = true;
				listDirFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 1) throw std::runtime_error("listDir expects 1 argument (path string)");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("listDir path must be string");
					std::string path = std::get<std::string>(args[0]);
					std::error_code ec;
					if (!std::filesystem::exists(path, ec)) throw std::runtime_error("Directory does not exist: " + path);
					if (!std::filesystem::is_directory(path, ec)) throw std::runtime_error("Not a directory: " + path);
					auto arr = std::make_shared<Array>();
					for (auto &entry : std::filesystem::directory_iterator(path, ec)) {
						if (ec) break;
						std::string name = entry.path().filename().string();
						arr->push_back(Value{name});
					}
					return Value{arr};
				};
				(*ioPkg)["listDir"] = Value{listDirFn};

				// Built-in File class
				{
					auto fileClass = std::make_shared<ClassInfo>();
					fileClass->name = "File";
					// constructor(path)
					auto ctor = std::make_shared<Function>();
					ctor->isBuiltin = true;
					ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (args.size() != 1) throw std::runtime_error("File.constructor expects 1 argument (path)");
						if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("File path must be string");
						std::string path = std::get<std::string>(args[0]);
						// store path in interpPtr.fields["path"]
						Value thisVal = closure->get("this");
						if (!std::holds_alternative<std::shared_ptr<Instance>>(thisVal)) throw std::runtime_error("interpPtr is not instance in File.constructor");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						inst->fields["path"] = Value{path};
						return Value{std::monostate{}};
					};
					fileClass->methods["constructor"] = ctor;
					// read()
					auto readM = std::make_shared<Function>(); readM->isBuiltin = true; readM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("File.read expects 0 arguments");
						Value thisVal = closure->get("this");
						auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::ifstream in(path, std::ios::in | std::ios::binary); if (!in) throw std::runtime_error("File.read cannot open: " + path);
						std::ostringstream buf; buf << in.rdbuf(); return Value{buf.str()};
					}; fileClass->methods["read"] = readM;
					// write(data)
					auto writeM = std::make_shared<Function>(); writeM->isBuiltin = true; writeM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (args.size() != 1) throw std::runtime_error("File.write expects 1 argument (data)");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::string data = toString(args[0]);
						std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc); if (!out) throw std::runtime_error("File.write cannot open: " + path);
						out << data; return Value{true};
					}; fileClass->methods["write"] = writeM;
					// append(data)
					auto appendM = std::make_shared<Function>(); appendM->isBuiltin = true; appendM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (args.size() != 1) throw std::runtime_error("File.append expects 1 argument (data)");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::string data = toString(args[0]);
						std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::app); if (!out) throw std::runtime_error("File.append cannot open: " + path);
						out << data; return Value{true};
					}; fileClass->methods["append"] = appendM;
					// exists()
					auto existsM = std::make_shared<Function>(); existsM->isBuiltin = true; existsM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("File.exists expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						return Value{ std::filesystem::exists(path) };
					}; fileClass->methods["exists"] = existsM;
					// size()
					auto sizeM = std::make_shared<Function>(); sizeM->isBuiltin = true; sizeM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("File.size expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::error_code ec; auto sz = std::filesystem::file_size(path, ec); if (ec) return Value{ -1.0 }; return Value{ static_cast<double>(sz) };
					}; fileClass->methods["size"] = sizeM;
					// delete()
					auto deleteM = std::make_shared<Function>(); deleteM->isBuiltin = true; deleteM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (!args.empty()) throw std::runtime_error("File.delete expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::error_code ec; bool ok = std::filesystem::remove(path, ec); if (ec) return Value{false}; return Value{ok};
					}; fileClass->methods["delete"] = deleteM;
					// rename(newPath)
					auto renameM = std::make_shared<Function>(); renameM->isBuiltin = true; renameM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("File.rename expects 1 argument (newPath)");
						if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("File.rename newPath must be string");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string oldPath = std::get<std::string>(inst->fields["path"]); std::string newPath = std::get<std::string>(args[0]);
						std::error_code ec; std::filesystem::rename(oldPath, newPath, ec); if (ec) return Value{false}; inst->fields["path"] = Value{newPath}; return Value{true};
					}; fileClass->methods["rename"] = renameM;
					// readBytes()
					auto readBytesM = std::make_shared<Function>(); readBytesM->isBuiltin = true; readBytesM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (!args.empty()) throw std::runtime_error("File.readBytes expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::ifstream in(path, std::ios::binary); if (!in) throw std::runtime_error("File.readBytes cannot open: " + path);
						std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						auto arr = std::make_shared<Array>(); arr->reserve(buf.size());
						for (auto c : buf) arr->push_back(Value{ static_cast<double>(c) });
						return Value{arr};
					}; fileClass->methods["readBytes"] = readBytesM;
					// writeBytes(array)
					auto writeBytesM = std::make_shared<Function>(); writeBytesM->isBuiltin = true; writeBytesM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("File.writeBytes expects 1 argument (array of byte numbers)");
						if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("File.writeBytes expects array");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						auto arr = std::get<std::shared_ptr<Array>>(args[0]);
						std::ofstream out(path, std::ios::binary | std::ios::trunc); if (!out) throw std::runtime_error("File.writeBytes cannot open: " + path);
						for (auto &v : *arr) {
							double d = getNumber(v, "File.writeBytes element"); unsigned char c = static_cast<unsigned char>(static_cast<int>(d) & 0xFF); out.put(static_cast<char>(c));
						}
						return Value{true};
					}; fileClass->methods["writeBytes"] = writeBytesM;
					// appendBytes(array)
					auto appendBytesM = std::make_shared<Function>(); appendBytesM->isBuiltin = true; appendBytesM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("File.appendBytes expects 1 argument (array)");
						if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("File.appendBytes expects array");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						auto arr = std::get<std::shared_ptr<Array>>(args[0]);
						std::ofstream out(path, std::ios::binary | std::ios::app); if (!out) throw std::runtime_error("File.appendBytes cannot open: " + path);
						for (auto &v : *arr) { double d = getNumber(v, "File.appendBytes element"); unsigned char c = static_cast<unsigned char>(static_cast<int>(d) & 0xFF); out.put(static_cast<char>(c)); }
						return Value{true};
					}; fileClass->methods["appendBytes"] = appendBytesM;
					// open(mode) -> FileStream instance
					auto openM = std::make_shared<Function>(); openM->isBuiltin = true; openM->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("File.open expects 1 argument (mode)");
						if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("File.open mode must be string");
						std::string mode = std::get<std::string>(args[0]);
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);

						auto ioPkgLocal = interpPtr->ensurePackage("std.io");
						auto itFS = ioPkgLocal->find("FileStream");
						if (itFS == ioPkgLocal->end() || !std::holds_alternative<std::shared_ptr<ClassInfo>>(itFS->second)) throw std::runtime_error("FileStream class not found");
						auto streamClass = std::get<std::shared_ptr<ClassInfo>>(itFS->second);

						std::ios_base::openmode modeFlags = std::ios::binary;
						if (mode == "r") modeFlags |= std::ios::in;
						else if (mode == "w") modeFlags |= std::ios::out | std::ios::trunc;
						else if (mode == "a") modeFlags |= std::ios::out | std::ios::app;
						else if (mode == "rw") modeFlags |= std::ios::in | std::ios::out;
						else throw std::runtime_error("File.open invalid mode: " + mode);

						auto fsInst = std::make_shared<InstanceExt>(); 
						fsInst->klass = streamClass; 
						fsInst->fields["path"] = Value{path}; 
						fsInst->fields["mode"] = Value{mode}; 
						fsInst->fields["closed"] = Value{false}; 

						auto* wrapper = new FStreamWrapper(path, modeFlags);
						if (!wrapper->fs) {
							delete wrapper;
							throw std::runtime_error("File.open failed: " + path);
						}

						fsInst->nativeHandle = wrapper;
						fsInst->nativeDestructor = [](void* ptr) { 
							delete static_cast<StreamWrapper*>(ptr); 
						};

						return Value{std::shared_ptr<Instance>(fsInst)};
					}; fileClass->methods["open"] = openM;
					(*fsPkg)["File"] = Value{fileClass};
				}

				// Built-in Dir class
				{
					auto dirClass = std::make_shared<ClassInfo>();
					dirClass->name = "Dir";
					auto ctor = std::make_shared<Function>(); ctor->isBuiltin = true; ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (args.size() != 1) throw std::runtime_error("Dir.constructor expects 1 argument (path)");
						if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("Dir path must be string");
						std::string path = std::get<std::string>(args[0]);
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						inst->fields["path"] = Value{path}; return Value{std::monostate{}};
					}; dirClass->methods["constructor"] = ctor;
					// list()
					auto listM = std::make_shared<Function>(); listM->isBuiltin = true; listM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("Dir.list expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::error_code ec; if (!std::filesystem::exists(path, ec)) throw std::runtime_error("Dir.list path does not exist: " + path);
						if (!std::filesystem::is_directory(path, ec)) throw std::runtime_error("Dir.list not a directory: " + path);
						auto arr = std::make_shared<Array>();
						for (auto &entry : std::filesystem::directory_iterator(path, ec)) { if (ec) break; arr->push_back(Value{ entry.path().filename().string() }); }
						return Value{arr};
					}; dirClass->methods["list"] = listM;
					// exists()
					auto existsM = std::make_shared<Function>(); existsM->isBuiltin = true; existsM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("Dir.exists expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						return Value{ std::filesystem::exists(path) };
					}; dirClass->methods["exists"] = existsM;
					// create()
					auto createM = std::make_shared<Function>(); createM->isBuiltin = true; createM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
						if (!args.empty()) throw std::runtime_error("Dir.create expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]);
						std::error_code ec; bool ok = std::filesystem::create_directories(path, ec); if (ec) throw std::runtime_error("Dir.create failed: " + path); return Value{ ok };
					}; dirClass->methods["create"] = createM;
					// delete() recursive
					auto deleteDirM = std::make_shared<Function>(); deleteDirM->isBuiltin = true; deleteDirM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (!args.empty()) throw std::runtime_error("Dir.delete expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string path = std::get<std::string>(inst->fields["path"]); std::error_code ec; auto count = std::filesystem::remove_all(path, ec); if (ec) return Value{ -1.0 }; return Value{ static_cast<double>(count) };
					}; dirClass->methods["delete"] = deleteDirM;
					// rename(newPath)
					auto renameDirM = std::make_shared<Function>(); renameDirM->isBuiltin = true; renameDirM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("Dir.rename expects 1 argument (newPath)");
						if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("Dir.rename newPath must be string");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string oldPath = std::get<std::string>(inst->fields["path"]); std::string newPath = std::get<std::string>(args[0]); std::error_code ec; std::filesystem::rename(oldPath, newPath, ec); if (ec) return Value{false}; inst->fields["path"] = Value{newPath}; return Value{true};
					}; dirClass->methods["rename"] = renameDirM;
					// walk() recursive list
					auto walkM = std::make_shared<Function>(); walkM->isBuiltin = true; walkM->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (!args.empty()) throw std::runtime_error("Dir.walk expects 0 arguments");
						Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
						std::string base = std::get<std::string>(inst->fields["path"]); std::error_code ec; if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) throw std::runtime_error("Dir.walk invalid directory: " + base);
						auto arr = std::make_shared<Array>();
						for (auto &entry : std::filesystem::recursive_directory_iterator(base, ec)) { if (ec) break; std::string rel = std::filesystem::relative(entry.path(), base, ec).string(); arr->push_back(Value{rel}); }
						return Value{arr};
					}; dirClass->methods["walk"] = walkM;
					(*fsPkg)["Dir"] = Value{dirClass};
				}

				// Alias File and Dir to std.io for convenience
				(*ioPkg)["File"] = (*fsPkg)["File"];
				(*ioPkg)["Dir"] = (*fsPkg)["Dir"];

				// FileStream class (buffered, stateful)
				{
					auto fsClass = std::make_shared<ClassInfo>(); fsClass->name = "FileStream";

					// read(n)
					auto fsRead = std::make_shared<Function>(); fsRead->isBuiltin = true; fsRead->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						Value thisVal = closure->get("this"); 
						auto inst = std::dynamic_pointer_cast<InstanceExt>(std::get<std::shared_ptr<Instance>>(thisVal));
						if (!inst || !inst->nativeHandle) throw std::runtime_error("FileStream: invalid handle");

						StreamWrapper* stream = static_cast<StreamWrapper*>(inst->nativeHandle);

						size_t n = 0;
						if (!args.empty()) {
							double dn = getNumber(args[0], "FileStream.read n");
							if (dn < 0) throw std::runtime_error("FileStream.read n must be >=0");
							n = static_cast<size_t>(dn);
						} else {
							throw std::runtime_error("FileStream.read expects 1 argument (n bytes)");
						}

						std::vector<char> buf(n);
						size_t got = stream->read(buf.data(), n);
						buf.resize(got);

						auto arr = std::make_shared<Array>();
						arr->reserve(got);
						for (char c : buf) arr->push_back(Value{ static_cast<double>(static_cast<unsigned char>(c)) });
						return Value{arr};
					}; 
					fsClass->methods["read"] = fsRead;

					// write(data)
					auto fsWrite = std::make_shared<Function>(); fsWrite->isBuiltin = true; fsWrite->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						if (args.size() != 1) throw std::runtime_error("FileStream.write 需要1个参数");
						Value thisVal = closure->get("this"); 
						auto inst = std::dynamic_pointer_cast<InstanceExt>(std::get<std::shared_ptr<Instance>>(thisVal));
						if (!inst || !inst->nativeHandle) throw std::runtime_error("FileStream: invalid handle");

						StreamWrapper* stream = static_cast<StreamWrapper*>(inst->nativeHandle);

						if (std::holds_alternative<std::string>(args[0])) {
							std::string s = std::get<std::string>(args[0]);
							stream->write(s.data(), s.size());
						} else if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
							auto arr = std::get<std::shared_ptr<Array>>(args[0]);
							std::vector<char> buf;
							buf.reserve(arr->size());
							for (auto &v : *arr) {
								double d = getNumber(v, "FileStream.write element");
								buf.push_back(static_cast<char>(static_cast<unsigned char>(d)));
							}
							stream->write(buf.data(), buf.size());
						} else {
							throw std::runtime_error("FileStream.write expects string or byte array");
						}
						return Value{true};
					};
					fsClass->methods["write"] = fsWrite;

					// eof()
					auto fsEof = std::make_shared<Function>(); fsEof->isBuiltin = true; fsEof->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						Value thisVal = closure->get("this"); 
						auto inst = std::dynamic_pointer_cast<InstanceExt>(std::get<std::shared_ptr<Instance>>(thisVal));
						if (!inst || !inst->nativeHandle) return Value{true};
						StreamWrapper* stream = static_cast<StreamWrapper*>(inst->nativeHandle);
						return Value{stream->eof()};
					};
					fsClass->methods["eof"] = fsEof;

					// close()
					auto fsClose = std::make_shared<Function>(); fsClose->isBuiltin = true; fsClose->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
						Value thisVal = closure->get("this"); 
						auto inst = std::dynamic_pointer_cast<InstanceExt>(std::get<std::shared_ptr<Instance>>(thisVal));
						if (inst && inst->nativeHandle) {
							StreamWrapper* stream = static_cast<StreamWrapper*>(inst->nativeHandle);
							stream->close();
							if (inst->nativeDestructor) inst->nativeDestructor(inst->nativeHandle);
							inst->nativeHandle = nullptr;
							inst->nativeDestructor = nullptr;
						}
						inst->fields["closed"] = Value{true};
						return Value{true};
					};
					fsClass->methods["close"] = fsClose;

					(*ioPkg)["FileStream"] = Value{fsClass};
				}

				// Standard Streams
				{
					auto ioPkg = interpPtr->ensurePackage("std.io");
					auto itFS = ioPkg->find("FileStream");
					if (itFS != ioPkg->end() && std::holds_alternative<std::shared_ptr<ClassInfo>>(itFS->second)) {
						auto streamClass = std::get<std::shared_ptr<ClassInfo>>(itFS->second);

						// stdin
						auto stdinInst = std::make_shared<InstanceExt>();
						stdinInst->klass = streamClass;
						stdinInst->fields["path"] = Value{std::string("stdin")};
						stdinInst->fields["mode"] = Value{std::string("r")};
						stdinInst->fields["closed"] = Value{false};
						stdinInst->nativeHandle = new StdinWrapper();
						stdinInst->nativeDestructor = [](void* ptr) { delete static_cast<StreamWrapper*>(ptr); };
						(*ioPkg)["stdin"] = Value{std::shared_ptr<Instance>(stdinInst)};

						// stdout
						auto stdoutInst = std::make_shared<InstanceExt>();
						stdoutInst->klass = streamClass;
						stdoutInst->fields["path"] = Value{std::string("stdout")};
						stdoutInst->fields["mode"] = Value{std::string("w")};
						stdoutInst->fields["closed"] = Value{false};
						stdoutInst->nativeHandle = new StdoutWrapper();
						stdoutInst->nativeDestructor = [](void* ptr) { delete static_cast<StreamWrapper*>(ptr); };
						(*ioPkg)["stdout"] = Value{std::shared_ptr<Instance>(stdoutInst)};

						// stderr
						auto stderrInst = std::make_shared<InstanceExt>();
						stderrInst->klass = streamClass;
						stderrInst->fields["path"] = Value{std::string("stderr")};
						stderrInst->fields["mode"] = Value{std::string("w")};
						stderrInst->fields["closed"] = Value{false};
						stderrInst->nativeHandle = new StderrWrapper();
						stderrInst->nativeDestructor = [](void* ptr) { delete static_cast<StreamWrapper*>(ptr); };
						(*ioPkg)["stderr"] = Value{std::shared_ptr<Instance>(stderrInst)};
					}
				}

				// Extended File System Operations (std.io.fileSystem)
				{
					// mkdir(path)
					auto mkdirFn = std::make_shared<Function>(); mkdirFn->isBuiltin=true; mkdirFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("mkdir expects path string");
						std::string path = std::get<std::string>(args[0]);
						std::error_code ec; bool ok = std::filesystem::create_directories(path, ec);
						if(ec) return Value{false}; return Value{true};
					};
					(*fsPkg)["mkdir"] = Value{mkdirFn};

					// rmdir(path) - recursive remove
					auto rmdirFn = std::make_shared<Function>(); rmdirFn->isBuiltin=true; rmdirFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("rmdir expects path string");
						std::string path = std::get<std::string>(args[0]);
						std::error_code ec; auto n = std::filesystem::remove_all(path, ec);
						if(ec) return Value{false}; return Value{true};
					};
					(*fsPkg)["rmdir"] = Value{rmdirFn};

					// stat(path)
					auto statFn = std::make_shared<Function>(); statFn->isBuiltin=true; statFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("stat expects path string");
						std::string path = std::get<std::string>(args[0]);
						std::error_code ec; auto s = std::filesystem::status(path, ec);
						if(ec) return Value{std::monostate{}};
						auto obj = std::make_shared<Object>();
						(*obj)["isFile"] = Value{std::filesystem::is_regular_file(s)};
						(*obj)["isDir"] = Value{std::filesystem::is_directory(s)};
						(*obj)["size"] = Value{static_cast<double>(std::filesystem::file_size(path, ec))};
						auto perms = s.permissions();
						(*obj)["permissions"] = Value{static_cast<double>(static_cast<int>(perms))};
						auto ftime = std::filesystem::last_write_time(path, ec);
						auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
						std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
						(*obj)["mtime"] = Value{static_cast<double>(tt)};
						return Value{obj};
					};
					(*fsPkg)["stat"] = Value{statFn};

					// copy(src, dest)
					auto copyFn = std::make_shared<Function>(); copyFn->isBuiltin=true; copyFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=2) throw std::runtime_error("copy expects src, dest");
						std::string src = toString(args[0]); std::string dest = toString(args[1]);
						std::error_code ec; std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
						if(ec) return Value{false}; return Value{true};
					};
					(*fsPkg)["copy"] = Value{copyFn};

					// move(src, dest)
					auto moveFn = std::make_shared<Function>(); moveFn->isBuiltin=true; moveFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=2) throw std::runtime_error("move expects src, dest");
						std::string src = toString(args[0]); std::string dest = toString(args[1]);
						std::error_code ec; std::filesystem::rename(src, dest, ec);
						if(ec) return Value{false}; return Value{true};
					};
					(*fsPkg)["move"] = Value{moveFn};

					// chmod(path, mode)
					auto chmodFn = std::make_shared<Function>(); chmodFn->isBuiltin=true; chmodFn->builtin=[](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=2) throw std::runtime_error("chmod expects path, mode");
						std::string path = toString(args[0]); double mode = getNumber(args[1], "chmod mode");
						std::error_code ec; std::filesystem::permissions(path, static_cast<std::filesystem::perms>(static_cast<int>(mode)), ec);
						if(ec) return Value{false}; return Value{true};
					};
					(*fsPkg)["chmod"] = Value{chmodFn};

					// walk(path, callback)
					auto walkFn = std::make_shared<Function>(); walkFn->isBuiltin=true; walkFn->builtin=[interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
						if(args.size()!=2) throw std::runtime_error("walk expects path, callback");
						std::string path = toString(args[0]);
						if(!std::holds_alternative<std::shared_ptr<Function>>(args[1])) throw std::runtime_error("walk callback must be function");
						auto cb = std::get<std::shared_ptr<Function>>(args[1]);
						std::error_code ec;
						for(auto& entry: std::filesystem::recursive_directory_iterator(path, ec)) {
							if(ec) break;
							std::string p = entry.path().string();
							bool isDir = entry.is_directory();
							Value res{std::monostate{}};
							if(cb->isBuiltin) {
								std::vector<Value> cargs{Value{p}, Value{isDir}};
								res = cb->builtin(cargs, cb->closure);
							} else {
								auto local = std::make_shared<Environment>(cb->closure);
								if(cb->params.size()>0) local->define(cb->params[0], Value{p});
								if(cb->params.size()>1) local->define(cb->params[1], Value{isDir});
								try { interpPtr->executeBlock(cb->body, local); } catch(const ReturnSignal& rs) { res = rs.value; }
							}
							if(std::holds_alternative<bool>(res) && std::get<bool>(res)==false) break;
						}
						return Value{std::monostate{}};
					};
					(*fsPkg)["walk"] = Value{walkFn};
				}

	}
	
	// Import std.io symbols to global scope
	interp.importPackageSymbols("std.io");
}

PackageMeta getStdIoPackageMeta() {
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

    return pkg;
}

} // namespace asul
