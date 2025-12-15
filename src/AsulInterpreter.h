#ifndef ASUL_INTERPRETER_H
#define ASUL_INTERPRETER_H

#include "AsulAst.h"
#include "AsulRuntime.h"
#include "AsulParser.h"
#include "AsulAsync.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
    // Windows networking and process headers
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <process.h>
    typedef int socklen_t;
#else
    // Unix/Linux/macOS networking and process headers
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/wait.h>
#endif

#include "AsulFormatString/AsulFormatString.h"

// Forward declare external package registration
namespace asul { class Interpreter; }
void registerExternalPackages(asul::Interpreter& interp);

// Global mutex for timezone operations
extern std::mutex tzMutex;

namespace asul {

// Control flow signals
struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};
struct ExceptionSignal { Value value; std::vector<std::string> stackTrace = {}; };

// Signal handling globals
extern std::atomic<int> g_pendingSignals[32];
void globalSignalHandler(int sig);

class Interpreter : public AsulAsync {
public:
	Interpreter() { globals = std::make_shared<Environment>(); env = globals; installBuiltins(); }

	void registerPackageSymbol(const std::string& pkgName, const std::string& symbol, const Value& value);
	std::shared_ptr<Object> ensurePackage(const std::string& name);
	void importPackageSymbols(const std::string& name);

	// Package registration for external packages
	void registerLazyPackage(const std::string& name, std::function<void(std::shared_ptr<Object>)> init);

	// Signal handler setter for external packages
	void setSignalHandler(int sig, const Value& callback) { signalHandlers[sig] = callback; }

	void setImportBaseDir(const std::string& base) {
		try { importBaseDir = std::filesystem::path(base); }
		catch (...) { importBaseDir.clear(); }
	}

	void execute(const std::vector<StmtPtr>& stmts) {
		for (auto& s : stmts) execute(s);
	}

	// 事件循环：用于分发 then/catch 与 go 任务
	void postTask(std::function<void()> fn) override {
		{
			std::lock_guard<std::mutex> lk(loopMutex);
			taskQueue.push(std::move(fn));
		}
		loopCv.notify_one();
	}
	
	// AsulAsync interface implementation
	std::shared_ptr<PromiseState> createPromise() override {
		auto p = std::make_shared<PromiseState>();
		p->loopPtr = this;
		return p;
	}
	
	void resolve(std::shared_ptr<PromiseState> promise, const Value& value) override {
		settlePromise(promise, false, value);
	}
	
	void reject(std::shared_ptr<PromiseState> promise, const Value& error) override {
		settlePromise(promise, true, error);
	}
	
	// Get the async interface for external packages
	AsulAsync& getAsyncInterface() { return *this; }
	
	void runEventLoopUntilIdle() {
		for (;;) {
			std::function<void()> fn;
			{
				std::unique_lock<std::mutex> lk(loopMutex);
				if (taskQueue.empty()) break;
				fn = std::move(taskQueue.front()); taskQueue.pop();
			}
			if (fn) fn();
		}
	}

	// Import external file: resolve path, read, parse and execute in isolated env, then return module object
	std::shared_ptr<Object> importFilePath(const std::string& rawPath) {
		// capture context for error pretty-printing + import chain
		std::string ctxCode; std::string ctxFile;
		try {
			namespace fs = std::filesystem;
			fs::path p(rawPath);
			// Try both as-is and with .alang suffix if no extension
			auto resolveCandidate = [&](const fs::path& cand, fs::path& out)->bool{
				std::error_code ec{};
				fs::path base = importBaseDir.empty() ? fs::current_path(ec) : importBaseDir;
				fs::path abs = cand.is_absolute() ? cand : (base / cand);
				if (!ec && fs::exists(abs)) { out = fs::weakly_canonical(abs, ec); return true; }
				return false;
			};
			fs::path finalPath;
			bool found = false;
			if (p.has_extension()) {
				found = resolveCandidate(p, finalPath);
			} else {
				// try without extension first, then add .alang
				found = resolveCandidate(p, finalPath);
				if (!found) {
					fs::path withExt = p.string() + ".alang";
					found = resolveCandidate(withExt, finalPath);
				}
			}
			if (!found) {
				throw std::runtime_error(std::string("Import file not found: ") + rawPath);
			}
			std::string key = finalPath.string();
			ctxFile = key;
			if (importedModules.find(key) != importedModules.end()) return importedModules[key];

			// Read file content
			std::ifstream in(key, std::ios::in | std::ios::binary);
			if (!in) throw std::runtime_error(std::string("Cannot open import file: ") + key);
			std::ostringstream ss; ss << in.rdbuf();
			std::string code = ss.str();
			ctxCode = code;

			// push import chain
			importStack.push_back(key);
			struct ImportGuard { std::vector<std::string>& st; ~ImportGuard(){ st.pop_back(); } } guard{importStack};

			// Lex/parse
			Lexer lx(code);
			auto tokens = lx.scanTokens();
			Parser ps(tokens, code);
			auto stmts = ps.parse();

			// Execute in an isolated environment that can see globals (builtins/classes)
			auto fileEnv = std::make_shared<Environment>(globals);
			// Run
			executeBlock(stmts, fileEnv);

			// Create module object with exported symbols
			auto modObj = std::make_shared<Object>();
			// Rule: Import only if (Explicitly Exported) OR (Starts with Uppercase)
			for (const auto& kv : fileEnv->values) {
				const std::string& name = kv.first;
				bool isExplicit = fileEnv->explicitExports.find(name) != fileEnv->explicitExports.end();
				bool isImplicit = !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
				
				if (isExplicit || isImplicit) {
					(*modObj)[name] = kv.second;
				}
			}

			// Cache and return
			importedModules[key] = modObj;
			return modObj;
		} catch (const ExceptionSignal& ex) {
			// Record error source for upper-level pretty printing
			lastErrorSource = ctxCode; lastErrorFilename = ctxFile;
			// attach import chain
			std::ostringstream oss; oss << toString(ex.value);
			if (!importStack.empty()) {
				oss << " | import chain: ";
				for (size_t i=0;i<importStack.size();++i) { if (i) oss << " -> "; oss << importStack[i]; }
			}
			throw std::runtime_error(oss.str());
		} catch (const std::exception& ex) {
			lastErrorSource = ctxCode; lastErrorFilename = ctxFile;
			std::ostringstream oss; oss << ex.what();
			if (!importStack.empty()) {
				oss << " | import chain: ";
				for (size_t i=0;i<importStack.size();++i) { if (i) oss << " -> "; oss << importStack[i]; }
			}
			throw std::runtime_error(oss.str());
		}
	}

	Value evaluate(const ExprPtr& expr) {
		if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) return lit->value;
			if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
				try {
					return env->get(var->name);
				} catch (const std::exception& ex) {
					std::ostringstream oss;
					oss << ex.what() << " at line " << var->line << ", column " << var->column << ", length " << var->length;
					throw std::runtime_error(oss.str());
				}
			}
			if (auto asg = std::dynamic_pointer_cast<AssignExpr>(expr)) {
				Value v = evaluate(asg->value);
				if (!env->assign(asg->name, v)) throw std::runtime_error("Undefined variable '" + asg->name + "' at line " + std::to_string(asg->line));
				return v;
			}
			if (auto dasg = std::dynamic_pointer_cast<DestructuringAssignExpr>(expr)) {
				Value v = evaluate(dasg->value);
				destructurePattern(dasg->pattern, v);
				return v;
			}
		if (auto arr = std::dynamic_pointer_cast<ArrayLiteralExpr>(expr)) {
			auto av = std::make_shared<Array>();
			av->reserve(arr->elements.size());
			for (auto& e : arr->elements) {
				if (auto sp = std::dynamic_pointer_cast<SpreadExpr>(e)) {
					Value v = evaluate(sp->expr);
					if (auto a = std::get_if<std::shared_ptr<Array>>(&v)) {
						if (!*a) continue;
						for (auto &it : **a) av->push_back(it);
					} else {
						std::ostringstream oss; oss << "Spread element is not an array at line " << sp->line << ", column " << sp->column << ", length " << sp->length; throw std::runtime_error(oss.str());
					}
				} else {
					av->push_back(evaluate(e));
				}
			}
			return Value{std::shared_ptr<Array>(av)};
		}
		if (auto obj = std::dynamic_pointer_cast<ObjectLiteralExpr>(expr)) {
			auto ov = std::make_shared<Object>();
			for (auto& pr : obj->props) {
				if (pr.isSpread) {
					Value v = evaluate(pr.value);
					if (auto o = std::get_if<std::shared_ptr<Object>>(&v)) {
						if (!*o) continue;
						for (auto &kv : **o) {
							(*ov)[kv.first] = kv.second;
						}
					} else {
						std::ostringstream oss; oss << "Spread value is not an object at line " << pr.line << ", column " << pr.column << ", length " << pr.length; throw std::runtime_error(oss.str());
					}
				} else {
					std::string key;
					if (pr.computed) {
						Value kv = evaluate(pr.keyExpr);
						key = keyFromValue(kv);
					} else {
						key = pr.name;
					}
					(*ov)[key] = evaluate(pr.value);
				}
			}
			return Value{std::shared_ptr<Object>(ov)};
		}
		if (auto gp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
			// Special-case property access on a VariableExpr to provide reflection helpers
			if (auto ve = std::dynamic_pointer_cast<VariableExpr>(gp->object)) {
				if (gp->name == "type") {
					auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
					std::string vname = ve->name;
					fn->builtin = [vname](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
						try {
							auto dt = clos->getDeclaredType(vname);
							if (dt) return Value{ *dt };
							// try runtime value
							try { Value rv = clos->get(vname); return Value{ typeOf(rv) }; } catch(...) { return Value{ std::string("undefined") }; }
						} catch(...) { return Value{ std::string("undefined") }; }
					};
					return fn;
				}
				if (gp->name == "literal") {
					auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->closure = env;
					std::string vname = ve->name;
					fn->builtin = [vname](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ vname }; };
					return fn;
				}
			}
			Value o = evaluate(gp->object);
			try {
				return getProperty(o, gp->name);
			} catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << gp->line << ", column " << gp->column << ", length " << gp->length; throw std::runtime_error(oss.str());
			}
		}
		if (auto oc = std::dynamic_pointer_cast<OptionalChainingExpr>(expr)) {
			// Optional chaining: obj?.prop returns null if obj is null/undefined
			Value o = evaluate(oc->object);
			if (std::holds_alternative<std::monostate>(o)) {
				return Value{std::monostate{}};
			}
			try {
				return getProperty(o, oc->name);
			} catch (const std::exception&) {
				// If property doesn't exist, return null instead of throwing
				return Value{std::monostate{}};
			}
		}
		if (auto ix = std::dynamic_pointer_cast<IndexExpr>(expr)) {
			Value o = evaluate(ix->object);
			Value k = evaluate(ix->index);
			try { return getIndex(o, k); }
			catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << ix->line << ", column " << ix->column << ", length " << ix->length; throw std::runtime_error(oss.str());
			}
		}
		if (auto sp = std::dynamic_pointer_cast<SetPropExpr>(expr)) {
			Value& ov = *[&]()->Value*{
				try { return &ensureObjectRef(sp->object); }
				catch (const std::exception& ex) {
					std::ostringstream oss; oss << ex.what() << " at line " << sp->line << ", column " << sp->column << ", length " << sp->length; throw std::runtime_error(oss.str());
				}
			}();
			Value v = evaluate(sp->value);
			if (auto pobj = std::get_if<std::shared_ptr<Object>>(&ov)) {
				(**pobj)[sp->name] = v;
				return v;
			}
			if (auto pins = std::get_if<std::shared_ptr<Instance>>(&ov)) {
				(**pins).fields[sp->name] = v;
				return v;
			}
			return v;
		}
		if (auto si = std::dynamic_pointer_cast<SetIndexExpr>(expr)) {
			try {
				Value& ov = evaluateRef(si->object);
				Value idxv = evaluate(si->index);
				Value v = evaluate(si->value);
				if (auto parr = std::get_if<std::shared_ptr<Array>>(&ov)) {
					size_t idx = indexFromValue(idxv);
					auto& vec = **parr;
					if (idx >= vec.size()) {
						std::ostringstream oss; oss << "Array index out of range at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
					}
					vec[idx] = v; return v;
				}
				if (auto pobj = std::get_if<std::shared_ptr<Object>>(&ov)) {
					std::string key = keyFromValue(idxv);
					(**pobj)[key] = v; return v;
				}
				if (auto pins = std::get_if<std::shared_ptr<Instance>>(&ov)) {
					std::string key = keyFromValue(idxv);
					(*pins)->fields[key] = v; return v;
				}
				{
					std::ostringstream oss; oss << "Index assignment on non-array/object at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
				}
			} catch (const std::exception& ex) {
				std::string s = ex.what();
				if (s.find("line ") == std::string::npos) {
					std::ostringstream oss; oss << s << " at line " << si->line << ", column " << si->column << ", length " << si->length; throw std::runtime_error(oss.str());
				}
				throw;
			}
		}
		if (auto un = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
			Value r = evaluate(un->right);
			try {
				switch (un->op.type) {
				case TokenType::Bang: return Value{!isTruthy(r)};
				case TokenType::Minus: {
					double rv = getNumber(r, "unary '-'");
					return Value{-rv};
				}
				case TokenType::Tilde: {
					// Bitwise NOT on 64-bit integer view
					double rv = getNumber(r, "unary '~' ");
					long long iv = static_cast<long long>(rv);
					long long res = ~iv;
					return Value{ static_cast<double>(res) };
				}
				default: break;
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << un->op.line << ", column " << un->op.column << ", length " << std::max(1, un->op.length); throw std::runtime_error(oss.str());
			}
		}
		if (auto update = std::dynamic_pointer_cast<UpdateExpr>(expr)) {
			// 处理递增/递减运算符：++x, --x, x++, x--
			try {
				// 获取当前值
				Value oldValue;
				std::string varName;
				std::shared_ptr<Object> objPtr;
				std::string propName;
				std::shared_ptr<Array> arrPtr;
				Value indexValue;
				bool isVar = false, isProp = false, isIndex = false;
				
				if (auto var = std::dynamic_pointer_cast<VariableExpr>(update->operand)) {
					oldValue = env->get(var->name);
					varName = var->name;
					isVar = true;
				} else if (auto getp = std::dynamic_pointer_cast<GetPropExpr>(update->operand)) {
					Value obj = evaluate(getp->object);
					if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
						objPtr = *po;
						propName = getp->name;
						if (objPtr && objPtr->find(propName) != objPtr->end()) {
							oldValue = (*objPtr)[propName];
						} else {
							oldValue = Value{std::monostate{}};
						}
						isProp = true;
					} else if (auto inst = std::get_if<std::shared_ptr<Instance>>(&obj)) {
						if (*inst) {
							auto it = (*inst)->fields.find(getp->name);
							if (it != (*inst)->fields.end()) {
								oldValue = it->second;
							} else {
								oldValue = Value{std::monostate{}};
							}
							// 处理实例字段更新需要特殊处理
							throw std::runtime_error("Update operators on instance properties not yet fully supported");
						}
					} else {
						throw std::runtime_error("Cannot apply update operator to non-object property");
					}
				} else if (auto idx = std::dynamic_pointer_cast<IndexExpr>(update->operand)) {
					Value obj = evaluate(idx->object);
					indexValue = evaluate(idx->index);
					if (auto pa = std::get_if<std::shared_ptr<Array>>(&obj)) {
						arrPtr = *pa;
						if (!arrPtr) throw std::runtime_error("Cannot index null array");
						int i = static_cast<int>(getNumber(indexValue, "array index"));
						if (i < 0 || i >= static_cast<int>(arrPtr->size())) {
							throw std::runtime_error("Array index out of range");
						}
						oldValue = (*arrPtr)[i];
						isIndex = true;
					} else if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
						objPtr = *po;
						propName = keyFromValue(indexValue);
						if (objPtr && objPtr->find(propName) != objPtr->end()) {
							oldValue = (*objPtr)[propName];
						} else {
							oldValue = Value{std::monostate{}};
						}
						isProp = true;
					} else {
						throw std::runtime_error("Cannot apply update operator to non-indexable value");
					}
				} else {
					throw std::runtime_error("Invalid operand for update operator");
				}
				
				// 计算新值
				double numValue = getNumber(oldValue, "update operator");
				double newNumValue = (update->op.type == TokenType::PlusPlus) ? numValue + 1 : numValue - 1;
				Value newValue{newNumValue};
				
				// 更新值
				if (isVar) {
					env->assign(varName, newValue);
				} else if (isProp && objPtr) {
					(*objPtr)[propName] = newValue;
				} else if (isIndex && arrPtr) {
					int i = static_cast<int>(getNumber(indexValue, "array index"));
					(*arrPtr)[i] = newValue;
				}
				
				// 返回值：前置返回新值，后置返回旧值
				return update->isPrefix ? newValue : Value{numValue};
			} catch (const std::exception& ex) {
				std::ostringstream oss;
				oss << ex.what() << " at line " << update->line << ", column " << update->column << ", length " << update->length;
				throw std::runtime_error(oss.str());
			}
		}
		if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
			Value l = evaluate(bin->left); Value r = evaluate(bin->right);
			try {
				switch (bin->op.type) {
				case TokenType::Plus:
					if (auto ln = std::get_if<double>(&l)) {
						if (auto rn = std::get_if<double>(&r)) return Value{*ln + *rn};
						if (auto rs = std::get_if<std::string>(&r)) return Value{toString(l) + *rs};
					}
					if (auto ls = std::get_if<std::string>(&l)) return Value{*ls + toString(r)};
					// Operator overloading: __add__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__add__");
							if (m) {
								std::vector<Value> args{r};
								// For builtin methods of instances, 'this' is usually bound when method is retrieved via getProperty.
								// But here we retrieved it from class. We need to bind 'this'.
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								// User defined
								if (args.size() != m->params.size()) { /* handle mismatch? */ }
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					throw std::runtime_error("'+' requires numbers or strings");
				case TokenType::Minus:
					// Operator overloading: __sub__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__sub__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, "left of '-' ") - getNumber(r, "right of '-' ")};
				case TokenType::Star:
					// Operator overloading: __mul__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__mul__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, "left of '*' ") * getNumber(r, "right of '*' ")};
				case TokenType::Slash:
					// Operator overloading: __div__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__div__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					{
						double denom = getNumber(r, "right of '/' ");
						return Value{getNumber(l, "left of '/' ") / denom};
					}
				case TokenType::Percent:
					// Operator overloading: __mod__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__mod__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					{
						double rv = getNumber(r, "right of '%' ");
						return Value{std::fmod(getNumber(l, "left of '%' "), rv)};
					}
				case TokenType::Greater:
					// Operator overloading: __gt__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__gt__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, ">") > getNumber(r, ">")};
				case TokenType::GreaterEqual:
					// Operator overloading: __ge__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__ge__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, ">=") >= getNumber(r, ">=")};
				case TokenType::Less:
					// Operator overloading: __lt__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__lt__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, "<") < getNumber(r, "<")};
				case TokenType::LessEqual:
					// Operator overloading: __le__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__le__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{getNumber(l, "<=") <= getNumber(r, "<=")};
				case TokenType::EqualEqual:
					// Operator overloading: __eq__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__eq__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{isJSEqual(l, r)};
				case TokenType::BangEqual:
					// Operator overloading: __ne__ (or use negation of __eq__)
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__ne__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					return Value{!isJSEqual(l, r)};
				case TokenType::StrictEqual: return Value{isStrictEqual(l, r)};
				case TokenType::StrictNotEqual: return Value{!isStrictEqual(l, r)};
				case TokenType::Ampersand:
					// Operator overloading: __and__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__and__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					{
						long long lv = static_cast<long long>(getNumber(l, "& left"));
						long long rv = static_cast<long long>(getNumber(r, "& right"));
						return Value{ static_cast<double>(lv & rv) };
					}
				case TokenType::Pipe:
					// Operator overloading: __or__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__or__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					{
						long long lv = static_cast<long long>(getNumber(l, "| left"));
						long long rv = static_cast<long long>(getNumber(r, "| right"));
						return Value{ static_cast<double>(lv | rv) };
					}
				case TokenType::Caret:
					// Operator overloading: __xor__
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__xor__");
							if (m) {
								std::vector<Value> args{r};
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size();++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					{
						long long lv = static_cast<long long>(getNumber(l, "^ left"));
						long long rv = static_cast<long long>(getNumber(r, "^ right"));
						return Value{ static_cast<double>(lv ^ rv) };
					}
				case TokenType::ShiftLeft: {
					// Operator overloading: __shl__ (for stream-like operations)
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__shl__");
							if (m) {
								std::vector<Value> args{ r };
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size(); ++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					long long lv = static_cast<long long>(getNumber(l, "<< left"));
					long long rv = static_cast<long long>(getNumber(r, "<< right"));
					return Value{ static_cast<double>(lv << rv) };
				}
				case TokenType::ShiftRight: {
					// Operator overloading: __shr__ (for stream-like operations)
					if (auto lInst = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (*lInst && (*lInst)->klass) {
							auto m = findMethod((*lInst)->klass, "__shr__");
							if (m) {
								std::vector<Value> args{ r };
								auto boundEnv = std::make_shared<Environment>(m->closure);
								boundEnv->define("this", l);
								if (m->isBuiltin) return m->builtin(args, boundEnv);
								auto local = std::make_shared<Environment>(boundEnv);
								for (size_t i=0;i<args.size() && i<m->params.size(); ++i) local->define(m->params[i], args[i]);
								try { executeBlock(m->body, local); } catch (const ReturnSignal& rs) { return rs.value; }
								return Value{std::monostate{}};
							}
						}
					}
					long long lv = static_cast<long long>(getNumber(l, ">> left"));
					long long rv = static_cast<long long>(getNumber(r, ">> right"));
					return Value{ static_cast<double>(lv >> rv) };
				}
				case TokenType::MatchInterface: {
					// Interface/class descriptor matching via '=~=' operator
					if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(r)) {
						throw std::runtime_error("'=~=' right-hand side must be an interface/class descriptor");
					}
					auto target = std::get<std::shared_ptr<ClassInfo>>(r);
					// Instance: verify all required methods exist in its class chain
					if (auto pins = std::get_if<std::shared_ptr<Instance>>(&l)) {
						if (!*pins || !(*pins)->klass) return Value{false};
						for (auto &kv : target->methods) {
							const std::string& mname = kv.first;
							auto f = findMethod((*pins)->klass, mname);
							if (!f) return Value{false};
						}
						return Value{true};
					}
					// Plain object: check it contains all interface method names as properties
					if (auto po = std::get_if<std::shared_ptr<Object>>(&l)) {
						if (!*po) return Value{false};
						for (auto &kv : target->methods) {
							const std::string& mname = kv.first;
							if ((**po).find(mname) == (**po).end()) return Value{false};
						}
						return Value{true};
					}
					return Value{false};
				}
				default: break;
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss; oss << ex.what() << " at line " << bin->op.line << ", column " << bin->op.column << ", length " << std::max(1, bin->op.length); throw std::runtime_error(oss.str());
			}
		}
		if (auto lg = std::dynamic_pointer_cast<LogicalExpr>(expr)) {
			Value l = evaluate(lg->left);
			if (lg->op.type == TokenType::OrOr) {
				return isTruthy(l) ? l : evaluate(lg->right);
			} else if (lg->op.type == TokenType::AndAnd) {
				return !isTruthy(l) ? l : evaluate(lg->right);
			} else if (lg->op.type == TokenType::QuestionQuestion) {
				// Nullish coalescing: only use right if left is null/undefined
				if (std::holds_alternative<std::monostate>(l)) {
					return evaluate(lg->right);
				}
				return l;
			}
			return l;
		}
		if (auto cond = std::dynamic_pointer_cast<ConditionalExpr>(expr)) {
			// 三元运算符：condition ? thenBranch : elseBranch
			try {
				Value condValue = evaluate(cond->condition);
				if (isTruthy(condValue)) {
					return evaluate(cond->thenBranch);
				} else {
					return evaluate(cond->elseBranch);
				}
			} catch (const std::exception& ex) {
				std::ostringstream oss;
				oss << ex.what() << " in ternary operator at line " << cond->line << ", column " << cond->column;
				throw std::runtime_error(oss.str());
			}
		}
		if (auto aw = std::dynamic_pointer_cast<AwaitExpr>(expr)) {
			Value v = evaluate(aw->expr);
			if (!std::holds_alternative<std::shared_ptr<PromiseState>>(v)) {
				std::ostringstream oss; oss << "await expects a Promise at line " << aw->line << ", column " << aw->column << ", length " << aw->length; throw std::runtime_error(oss.str());
			}
			auto p = std::get<std::shared_ptr<PromiseState>>(v);
			if (!p) return Value{std::monostate{}};
			std::unique_lock<std::mutex> lk(p->mtx);
			p->cv.wait(lk, [&]{ return p->settled; });
			if (p->rejected) throw ExceptionSignal{ p->result };
			return p->result;
		}
		if (auto yieldExpr = std::dynamic_pointer_cast<YieldExpr>(expr)) {
			// For now, yield simply evaluates to the value (basic implementation)
			// Full generator support would require coroutine-like state management
			if (yieldExpr->value) {
				return evaluate(yieldExpr->value);
			}
			return Value{std::monostate{}};
		}
		if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
			// derive callee name for stack trace
			auto deriveName = [&](const ExprPtr& e)->std::string{
				if (auto v = std::dynamic_pointer_cast<VariableExpr>(e)) return v->name;
				if (auto gp = std::dynamic_pointer_cast<GetPropExpr>(e)) return gp->name;
				return std::string("call");
			};
			std::string calleeDesc = deriveName(call->callee);
			// push call frame
			callStack.push_back(calleeDesc + std::string(" at line ") + std::to_string(call->line));
			struct FrameGuard { std::vector<std::string>& st; ~FrameGuard(){ st.pop_back(); } } _fg{callStack};
			try {
				Value cal = evaluate(call->callee);
			if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) {
				std::ostringstream oss; oss << "Can only call functions at line " << call->line << ", column " << call->column << ", length " << call->length; throw std::runtime_error(oss.str());
			}
			auto fn = std::get<std::shared_ptr<Function>>(cal);
			std::vector<Value> args; args.reserve(call->args.size());
			for (auto& a : call->args) args.push_back(evaluate(a));
			if (fn->isBuiltin) {
				try { return fn->builtin(args, fn->closure); }
				catch (const ExceptionSignal& ex) {
					// 已经是脚本异常，补充栈
					ExceptionSignal enriched = ex;
					if (enriched.stackTrace.empty()) enriched.stackTrace = callStack;
					throw enriched;
				}
				catch (const std::exception& ex) {
					// 将 native 异常转换为脚本异常对象
					Value ev = ensureExceptionValue(Value{ std::string(ex.what()) }, call->line, call->column, call->length);
					ExceptionSignal es; es.value = ev; es.stackTrace = callStack; throw es;
				}
			}
			// 支持调用由 FunctionExpr 生成的普通函数
			if (fn->isAsync) {
				// 返回一个 Promise，并将函数体作为任务投递
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				postTask([this, fn, args, p]{
					// 在闭包环境基础上创建局部环境并执行
					auto local = std::make_shared<Environment>(fn->closure);
					
					// Handle rest parameters
					if (fn->restParamIndex >= 0) {
						// Bind normal parameters
						for (int i = 0; i < fn->restParamIndex && i < static_cast<int>(args.size()); ++i) {
							local->define(fn->params[i], args[i]);
						}
						// For parameters with defaults between provided args and rest param
						for (int i = static_cast<int>(args.size()); i < fn->restParamIndex; ++i) {
							if (fn->defaultValues[i]) {
								local->define(fn->params[i], evaluate(fn->defaultValues[i]));
							} else {
								local->define(fn->params[i], Value{std::monostate{}});
							}
						}
						// Collect rest parameters into array
						auto restArray = std::make_shared<std::vector<Value>>();
						for (size_t i = fn->restParamIndex; i < args.size(); ++i) {
							restArray->push_back(args[i]);
						}
						local->define(fn->params[fn->restParamIndex], Value{restArray});
					} else {
						// No rest parameter, bind provided arguments
						for (size_t i = 0; i < args.size() && i < fn->params.size(); ++i) {
							local->define(fn->params[i], args[i]);
						}
						// Fill missing parameters with default values
						for (size_t i = args.size(); i < fn->params.size(); ++i) {
							if (fn->defaultValues[i]) {
								local->define(fn->params[i], evaluate(fn->defaultValues[i]));
							} else {
								// No default value provided for missing parameter - error
								local->define(fn->params[i], Value{std::monostate{}});
							}
						}
					}
					
					Value ret{std::monostate{}};
					try {
						executeBlock(fn->body, local);
					} catch (const ReturnSignal& rs) {
						ret = rs.value;
					} catch (const ExceptionSignal& ex) {
						Value v = ex.value;
						// 若无栈，补充
						if (ex.stackTrace.empty()) {
							Value wrapped = ensureExceptionValue(v);
							settlePromise(p, true, wrapped);
						} else {
							settlePromise(p, true, v);
						}
						return;
					} catch (const std::exception& exNative) {
						Value ev = ensureExceptionValue(Value{ std::string(exNative.what()) });
						settlePromise(p, true, ev); return;
					}
					settlePromise(p, false, ret);
				});
				return Value{p};
			}
			
			// Synchronous function call
			// Handle rest parameters
			if (fn->restParamIndex >= 0) {
				// Calculate minimum required parameters (those before rest param without defaults)
				int minParams = 0;
				for (int i = 0; i < fn->restParamIndex; ++i) {
					if (!fn->defaultValues[i]) minParams = i + 1;
				}
				
				if (static_cast<int>(args.size()) < minParams) {
					std::ostringstream oss; 
					oss << "Expected at least " << minParams << " arguments but got " << args.size() 
					    << " at line " << call->line << ", column " << call->column << ", length " << call->length; 
					throw std::runtime_error(oss.str());
				}
				
				auto local = std::make_shared<Environment>(fn->closure);
				// Bind provided normal parameters
				for (int i = 0; i < fn->restParamIndex && i < static_cast<int>(args.size()); ++i) {
					local->define(fn->params[i], args[i]);
				}
				// Fill missing parameters before rest param with defaults
				for (int i = static_cast<int>(args.size()); i < fn->restParamIndex; ++i) {
					if (fn->defaultValues[i]) {
						local->define(fn->params[i], evaluate(fn->defaultValues[i]));
					} else {
						local->define(fn->params[i], Value{std::monostate{}});
					}
				}
				// Collect rest parameters into array
				auto restArray = std::make_shared<std::vector<Value>>();
				for (size_t i = fn->restParamIndex; i < args.size(); ++i) {
					restArray->push_back(args[i]);
				}
				local->define(fn->params[fn->restParamIndex], Value{restArray});
				
				try {
					executeBlock(fn->body, local);
				} catch (const ReturnSignal& rs) { return rs.value; }
				catch (const ExceptionSignal& ex) {
					ExceptionSignal enriched = ex;
					if (enriched.stackTrace.empty()) enriched.stackTrace = callStack;
					throw enriched;
				}
				catch (const std::exception& exNative) {
					Value ev = ensureExceptionValue(Value{ std::string(exNative.what()) }, call->line, call->column, call->length);
					ExceptionSignal es; es.value = ev; es.stackTrace = callStack; throw es;
				}
				return Value{std::monostate{}};
			}
			
			// No rest parameter - check parameter count considering defaults
			int minRequired = 0;
			for (size_t i = 0; i < fn->params.size(); ++i) {
				if (!fn->defaultValues[i]) minRequired = static_cast<int>(i + 1);
			}
			
			if (args.size() < static_cast<size_t>(minRequired) || args.size() > fn->params.size()) {
				std::ostringstream oss; 
				oss << "Expected ";
				if (minRequired == static_cast<int>(fn->params.size())) {
					oss << fn->params.size();
				} else {
					oss << minRequired << "-" << fn->params.size();
				}
				oss << " arguments but got " << args.size() 
				    << " at line " << call->line << ", column " << call->column << ", length " << call->length; 
				throw std::runtime_error(oss.str());
			}
			
			auto local = std::make_shared<Environment>(fn->closure);
			// Bind provided arguments
			for (size_t i = 0; i < args.size(); ++i) {
				local->define(fn->params[i], args[i]);
			}
			// Fill missing parameters with default values
			for (size_t i = args.size(); i < fn->params.size(); ++i) {
				if (fn->defaultValues[i]) {
					local->define(fn->params[i], evaluate(fn->defaultValues[i]));
				} else {
					local->define(fn->params[i], Value{std::monostate{}});
				}
			}
			
			try {
				executeBlock(fn->body, local);
			} catch (const ReturnSignal& rs) { return rs.value; }
			return Value{std::monostate{}};
			} catch (const ExceptionSignal&) { throw; }
			catch (const std::exception& ex) {
				// Attach call stack if not present
				std::string msg = ex.what();
				if (msg.find("Stack:") == std::string::npos && !callStack.empty()) {
					std::ostringstream oss; oss << msg << "\n" << "Stack:";
					for (int i = static_cast<int>(callStack.size()) - 1; i >= 0; --i) {
						oss << "\n  -> " << callStack[static_cast<size_t>(i)];
					}
					throw std::runtime_error(oss.str());
				}
				throw;
			}
		}
		if (auto fexpr = std::dynamic_pointer_cast<FunctionExpr>(expr)) {
			auto fn = std::make_shared<Function>();
			// extract parameter names (ignore optional types at runtime)
			fn->params.clear();
			fn->defaultValues.clear();
			fn->restParamIndex = -1;
			for (size_t i = 0; i < fexpr->params.size(); ++i) {
				fn->params.push_back(fexpr->params[i].name);
				fn->defaultValues.push_back(fexpr->params[i].defaultValue);
				if (fexpr->params[i].isRest) {
					fn->restParamIndex = static_cast<int>(i);
				}
			}
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(fexpr->body)) fn->body = innerBlock->statements; else fn->body = { fexpr->body };
			fn->closure = env; // 关闭环境捕获
			fn->isGenerator = fexpr->isGenerator;
			return Value{fn};
		}
		if (auto nw = std::dynamic_pointer_cast<NewExpr>(expr)) {
			Value cal = evaluate(nw->callee);
			if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(cal)) {
				std::ostringstream oss; oss << "new: target is not a class at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
			}
			auto klass = std::get<std::shared_ptr<ClassInfo>>(cal);
			std::shared_ptr<Instance> inst;
			if (klass->isNative) {
				inst = std::make_shared<InstanceExt>();
			} else {
				inst = std::make_shared<Instance>();
			}
			inst->klass = klass;
			// constructor (lookup super chain)
			auto ctor = findMethod(klass, "constructor");
			if (ctor) {
				std::vector<Value> args; args.reserve(nw->args.size());
				for (auto& a : nw->args) args.push_back(evaluate(a));
				// bind this
				auto bound = std::make_shared<Function>(*ctor);
				auto thisEnv = std::make_shared<Environment>(bound->closure);
				thisEnv->define("this", inst);
				bound->closure = thisEnv;
				if (bound->isBuiltin) {
					try { (void)bound->builtin(args, bound->closure); }
					catch (const std::exception& ex) {
						std::string s = ex.what();
						if (s.find("line ") == std::string::npos) {
							std::ostringstream oss; oss << s << " at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
						}
						throw;
					}
				} else {
					if (args.size() != bound->params.size()) {
						std::ostringstream oss; oss << "Arity mismatch at line " << nw->line << ", column " << nw->column << ", length " << nw->length; throw std::runtime_error(oss.str());
					}
					auto local = std::make_shared<Environment>(bound->closure);
					for (size_t i=0;i<args.size();++i) local->define(bound->params[i], args[i]);
					try { executeBlock(bound->body, local); } catch (const ReturnSignal&) {}
				}
			}
			return inst;
		}
		throw std::runtime_error("Unknown expression type");
	}

	Value callValue(const Value& cal, const std::vector<Value>& args) {
		if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) return Value{std::monostate{}};
		auto fn = std::get<std::shared_ptr<Function>>(cal);
		if (fn->isBuiltin) {
			return fn->builtin(args, fn->closure);
		}
		auto local = std::make_shared<Environment>(fn->closure);
		for (size_t i=0; i<args.size() && i<fn->params.size(); ++i) local->define(fn->params[i], args[i]);
		try {
			executeBlock(fn->body, local);
		} catch (const ReturnSignal& rs) {
			return rs.value;
		}
		return Value{std::monostate{}};
	}

	void checkSignals() {
		for (int i = 1; i < 32; ++i) {
			if (g_pendingSignals[i].exchange(0)) {
				auto it = signalHandlers.find(i);
				if (it != signalHandlers.end()) {
					try {
						callValue(it->second, { Value{static_cast<double>(i)} });
					} catch (...) { }
				}
			}
		}
	}

	void execute(const StmtPtr& stmt) {
		checkSignals();
		if (auto e = std::dynamic_pointer_cast<ExprStmt>(stmt)) { (void)evaluate(e->expr); return; }
		if (std::dynamic_pointer_cast<EmptyStmt>(stmt)) { return; }
		if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) {
			for (auto& ent : imp->entries) {
				if (ent.isFile) {
					try {
						auto modObj = importFilePath(ent.filePath);
						// If a specific symbol was requested (from "file" import name), bind that symbol
						if (!ent.symbol.empty()) {
							auto fit = modObj->find(ent.symbol);
							if (fit == modObj->end()) {
								std::ostringstream oss; oss << "Module '" << ent.filePath << "' has no symbol '" << ent.symbol << "'";
								oss << " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
								throw std::runtime_error(oss.str());
							}
							std::string varName = ent.alias.has_value() ? ent.alias.value() : ent.symbol;
							env->define(varName, fit->second);
						} else if (ent.alias.has_value()) {
							// import "file" as alias
							env->define(ent.alias.value(), Value{modObj});
						} else {
							// import "file" (merge symbols)
							for (auto& kv : *modObj) {
								env->define(kv.first, kv.second);
							}
						}
					}
					catch (const std::exception& ex) {
						std::ostringstream oss; oss << ex.what();
						// 附加 import 语句位置
						oss << " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
						throw std::runtime_error(oss.str());
					}
					continue;
				}
				auto it = packages.find(ent.packageName);
				if (it == packages.end()) {
					if (loadLazyPackage(ent.packageName)) {
						it = packages.find(ent.packageName);
					}
				}
				if (it == packages.end()) {
					std::ostringstream oss; oss << "Unknown package: " << ent.packageName
						<< " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
					throw std::runtime_error(oss.str());
				}
				auto pobj = it->second;
				if (!pobj) continue;
				if (ent.symbol == "__module__") {
					// Bind the package object itself to a variable named by the last segment
					std::string pkg = ent.packageName;
					size_t p = pkg.rfind('.');
					std::string varName = (p == std::string::npos) ? pkg : pkg.substr(p+1);
					if (ent.alias.has_value()) varName = ent.alias.value();
					env->define(varName, Value{pobj});
				} else if (ent.symbol == "*") {
					// Load all lazy sub-packages
					std::string prefix = ent.packageName + ".";
					std::vector<std::string> toLoad;
					for (auto& lp : lazyPackages) {
						if (lp.first.rfind(prefix, 0) == 0) {
							toLoad.push_back(lp.first);
						}
					}
					for (const auto& name : toLoad) loadLazyPackage(name);

					for (auto& kv : *pobj) env->define(kv.first, kv.second);
				} else {
					auto fit = pobj->find(ent.symbol);
					if (fit == pobj->end()) {
						// Check if it is a sub-package import
						std::string subPkgName = ent.packageName + "." + ent.symbol;
						loadLazyPackage(subPkgName);
						auto subIt = packages.find(subPkgName);
						if (subIt != packages.end()) {
							std::string varName = ent.symbol;
							if (ent.alias.has_value()) varName = ent.alias.value();
							env->define(varName, Value{subIt->second});
							continue;
						}

						std::ostringstream oss; oss << "Package '" << ent.packageName << "' has no symbol '" << ent.symbol << "'"
							<< " at line " << ent.line << ", column " << ent.column << ", length " << std::max(1, ent.length);
						throw std::runtime_error(oss.str());
					}
					std::string varName = ent.symbol;
					if (ent.alias.has_value()) varName = ent.alias.value();
					env->define(varName, fit->second);
				}
			}
			return;
		}
		if (auto v = std::dynamic_pointer_cast<VarDecl>(stmt)) {
			Value init = v->init ? evaluate(v->init) : Value{std::monostate{}};
			// resolve declared type if a type expression was provided
			std::optional<std::string> declaredName = std::nullopt;
			if (v->typeExpr) {
				try {
					Value tv = evaluate(v->typeExpr);
					if (auto ps = std::get_if<std::string>(&tv)) {
						declaredName = *ps;
					} else if (auto po = std::get_if<std::shared_ptr<Object>>(&tv)) {
						if (*po) {
							auto it = (**po).find("declaredType");
							if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) declaredName = std::get<std::string>(it->second);
							else {
								auto it2 = (**po).find("runtimeType");
								if (it2 != (**po).end() && std::holds_alternative<std::string>(it2->second)) declaredName = std::get<std::string>(it2->second);
							}
						}
					} else {
						declaredName = typeOf(tv);
					}
				} catch(...) { /* ignore and leave declaredName nullopt */ }
			}
			if (declaredName) env->defineWithType(v->name, init, declaredName); else env->define(v->name, init);
			if (v->isExported) env->explicitExports.insert(v->name);
			return;
		}
		if (auto vd = std::dynamic_pointer_cast<VarDeclDestructuring>(stmt)) {
			Value init = vd->init ? evaluate(vd->init) : Value{std::monostate{}};
			destructurePattern(vd->pattern, init);
			return;
		}
		if (auto b = std::dynamic_pointer_cast<BlockStmt>(stmt)) { executeBlock(b->statements, std::make_shared<Environment>(env)); return; }
		if (auto i = std::dynamic_pointer_cast<IfStmt>(stmt)) {
			if (isTruthy(evaluate(i->cond))) execute(i->thenB); else if (i->elseB) execute(i->elseB);
			return;
		}
		if (auto w = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
			while (isTruthy(evaluate(w->cond))) {
				try { execute(w->body); }
				catch (const ContinueSignal&) { /* continue loop */ }
				catch (const BreakSignal&) { break; }
			}
			return;
		}
		if (auto dw = std::dynamic_pointer_cast<DoWhileStmt>(stmt)) {
			do {
				try { execute(dw->body); }
				catch (const ContinueSignal&) { /* continue loop */ }
				catch (const BreakSignal&) { break; }
			} while (isTruthy(evaluate(dw->cond)));
			return;
		}
		if (auto f = std::dynamic_pointer_cast<ForStmt>(stmt)) {
			if (f->init) execute(f->init);
			for (;;) {
				if (f->cond) { if (!isTruthy(evaluate(f->cond))) break; }
				try { execute(f->body); }
				catch (const ContinueSignal&) { /* go to post */ }
				catch (const BreakSignal&) { break; }
				if (f->post) (void)evaluate(f->post);
			}
			return;
		}
		if (auto fe = std::dynamic_pointer_cast<ForEachStmt>(stmt)) {
			// foreach (varName in iterable) body
			Value iterableValue = evaluate(fe->iterable);
			
			// 创建新的作用域用于循环变量
			auto loopEnv = std::make_shared<Environment>(env);
			loopEnv->define(fe->varName, Value{std::monostate{}});
			
			// 根据 iterable 类型进行迭代
			if (auto arr = std::get_if<std::shared_ptr<std::vector<Value>>>(&iterableValue)) {
				// 数组：遍历每个元素
				for (const auto& elem : **arr) {
					loopEnv->assign(fe->varName, elem);
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else if (auto obj = std::get_if<std::shared_ptr<std::unordered_map<std::string,Value>>>(&iterableValue)) {
				// 对象：遍历每个键
				for (const auto& [key, value] : **obj) {
					loopEnv->assign(fe->varName, Value{key});
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else if (auto str = std::get_if<std::string>(&iterableValue)) {
				// 字符串：遍历每个字符
				for (char ch : *str) {
					loopEnv->assign(fe->varName, Value{std::string(1, ch)});
					try {
						auto prevEnv = env;
						env = loopEnv;
						execute(fe->body);
						env = prevEnv;
					}
					catch (const ContinueSignal&) { /* continue to next iteration */ }
					catch (const BreakSignal&) { break; }
				}
			} else {
				throw std::runtime_error("foreach requires an iterable (array, object, or string)");
			}
			return;
		}
		if (auto sw = std::dynamic_pointer_cast<SwitchStmt>(stmt)) {
			// switch (expr) { case val: ... default: ... }
			Value switchValue = evaluate(sw->expr);
			
			bool matched = false;
			bool fallThrough = false;
			
			try {
				for (const auto& caseClause : sw->cases) {
					// Check if this is a matching case or if we're in fall-through mode
					if (!matched && !fallThrough) {
						if (caseClause.value == nullptr) {
							// This is the default case - matches if no previous case matched
							matched = true;
						} else {
							// Evaluate case value and compare
							Value caseValue = evaluate(caseClause.value);
							if (isStrictEqual(switchValue, caseValue)) {
								matched = true;
							}
						}
					}
					
					// Execute case body if matched or in fall-through
					if (matched || fallThrough) {
						for (const auto& s : caseClause.body) {
							execute(s);
						}
						fallThrough = true; // Enable fall-through for subsequent cases
					}
				}
			} catch (const BreakSignal&) {
				// Break out of switch
			}
			return;
		}
		if (auto m = std::dynamic_pointer_cast<MatchStmt>(stmt)) {
			// match (expr) { case pattern => body, ... }
			Value matchValue = evaluate(m->expr);
			
			for (const auto& arm : m->arms) {
				bool matches = false;
				
				// Check if pattern matches
				if (arm.pattern == nullptr) {
					// Default/catchall pattern
					matches = true;
				} else {
					// Evaluate pattern and check equality
					Value patternValue = evaluate(arm.pattern);
					matches = isStrictEqual(matchValue, patternValue);
				}
				
				// If pattern matches, check guard (if any)
				if (matches && arm.guard != nullptr) {
					Value guardResult = evaluate(arm.guard);
					matches = isTruthy(guardResult);
				}
				
				// Execute body if matched
				if (matches) {
					try {
						execute(arm.body);
					} catch (const BreakSignal&) {
						// Break out of match
					}
					// Match is exhaustive - stop after first match
					return;
				}
			}
			// No pattern matched and no default - this is allowed
			return;
		}
		if (auto r = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
			Value val = r->value ? evaluate(r->value) : Value{std::monostate{}};
			throw ReturnSignal{val};
		}
		if (auto t = std::dynamic_pointer_cast<ThrowStmt>(stmt)) {
			Value raw = t->value ? evaluate(t->value) : Value{std::monostate{}};
			Value wrapped = ensureExceptionValue(raw);
			ExceptionSignal ex; ex.value = wrapped; ex.stackTrace = callStack; // capture current stack
			throw ex;
		}
		if (auto tc = std::dynamic_pointer_cast<TryCatchStmt>(stmt)) {
			bool exceptionCaught = false;
			try {
				execute(tc->tryBlock);
			} catch (const ExceptionSignal& ex) {
				exceptionCaught = true;
				auto local = std::make_shared<Environment>(env);
				local->define(tc->catchName, ex.value);
				// 在新的局部环境中执行 catch 块
				if (auto block = std::dynamic_pointer_cast<BlockStmt>(tc->catchBlock)) {
					executeBlock(block->statements, local);
				} else {
					executeBlock(std::vector<StmtPtr>{ tc->catchBlock }, local);
				}
			} catch (const std::exception& ex) {
				exceptionCaught = true;
				// Catch C++ runtime errors and expose them as ALang exceptions
				auto local = std::make_shared<Environment>(env);
				Value errVal = buildExceptionValue(ex.what());
				local->define(tc->catchName, errVal);
				if (auto block = std::dynamic_pointer_cast<BlockStmt>(tc->catchBlock)) {
					executeBlock(block->statements, local);
				} else {
					executeBlock(std::vector<StmtPtr>{ tc->catchBlock }, local);
				}
			}
			// Execute finally block if present (always runs)
			if (tc->finallyBlock) {
				execute(tc->finallyBlock);
			}
			return;
		}
		if (std::dynamic_pointer_cast<BreakStmt>(stmt)) { throw BreakSignal{}; }
		if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) { throw ContinueSignal{}; }
		if (auto dec = std::dynamic_pointer_cast<DecoratorStmt>(stmt)) {
			// Execute the target (function or class declaration)
			execute(dec->target);
			
			// Get the name of the declared function/class
			std::string targetName;
			if (auto f = std::dynamic_pointer_cast<FunctionStmt>(dec->target)) {
				targetName = f->name;
			} else if (auto c = std::dynamic_pointer_cast<ClassStmt>(dec->target)) {
				targetName = c->name;
			} else {
				throw std::runtime_error("Decorators can only be applied to functions or classes");
			}
			
			// Get the original value
			Value originalValue = env->get(targetName);
			
			// Apply decorators in reverse order (bottom-up)
			Value decoratedValue = originalValue;
			for (auto it = dec->decorators.rbegin(); it != dec->decorators.rend(); ++it) {
				Value decoratorFunc = evaluate(*it);
				
				// Check if decorator is a function
				if (!std::holds_alternative<std::shared_ptr<Function>>(decoratorFunc)) {
					throw std::runtime_error("Decorator must be a function");
				}
				
				auto fn = std::get<std::shared_ptr<Function>>(decoratorFunc);
				std::vector<Value> args = {decoratedValue};
				
				// Call the decorator function with the current value
				if (fn->isBuiltin) {
					decoratedValue = fn->builtin(args, fn->closure);
				} else {
					// Create new environment for decorator call
					auto callEnv = std::make_shared<Environment>(fn->closure);
					if (args.size() != fn->params.size() && fn->restParamIndex == -1) {
						throw std::runtime_error("Decorator expects " + std::to_string(fn->params.size()) + " arguments");
					}
					for (size_t i = 0; i < fn->params.size() && i < args.size(); ++i) {
						callEnv->define(fn->params[i], args[i]);
					}
					
					// Execute decorator function body
					try {
						auto prevEnv = env;
						env = callEnv;
						for (auto& s : fn->body) execute(s);
						env = prevEnv;
						decoratedValue = Value{std::monostate{}}; // No return value
					} catch (const ReturnSignal& ret) {
						decoratedValue = ret.value;
					}
					// env = env; // Restore environment is already handled
				}
			}
			
			// Update the binding with the decorated value
			env->assign(targetName, decoratedValue);
			return;
		}
		if (auto f = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
			auto fn = std::make_shared<Function>();
			// extract parameter names (ignore optional types at runtime)
			fn->params.clear();
			fn->defaultValues.clear();
			fn->restParamIndex = -1;
			for (size_t i = 0; i < f->params.size(); ++i) {
				fn->params.push_back(f->params[i].name);
				fn->defaultValues.push_back(f->params[i].defaultValue);
				if (f->params[i].isRest) {
					fn->restParamIndex = static_cast<int>(i);
				}
			}
			// 将语句体包装成块：函数体如果是单个语句，处理成block便于复用
			if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(f->body)) fn->body = innerBlock->statements;
			else fn->body = { f->body };
			fn->closure = env;
			fn->isAsync = f->isAsync;
			fn->isGenerator = f->isGenerator;
			env->define(f->name, fn);
			if (f->isExported) env->explicitExports.insert(f->name);
			return;
		}
		if (auto c = std::dynamic_pointer_cast<ClassStmt>(stmt)) {
			auto klass = std::make_shared<ClassInfo>();
			klass->name = c->name;
			// 解析多父类
			for (auto& sname : c->superNames) {
				Value sv = env->get(sname);
				if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(sv)) throw std::runtime_error("Base must be a class: " + sname);
				klass->supers.push_back(std::get<std::shared_ptr<ClassInfo>>(sv));
			}
			// methods
			for (auto& m : c->methods) {
				auto fn = std::make_shared<Function>();
				fn->params.clear(); 
				fn->defaultValues.clear();
				for (auto &p : m->params) {
					fn->params.push_back(p.name);
					fn->defaultValues.push_back(p.defaultValue);
				}
				if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(m->body)) fn->body = innerBlock->statements; else fn->body = { m->body };
				fn->closure = env;
				fn->isAsync = m->isAsync;
				fn->isGenerator = m->isGenerator;
				
				// Apply decorators
				Value methodVal = Value{fn};
				for (auto it = m->decorators.rbegin(); it != m->decorators.rend(); ++it) {
					Value decoratorFunc = evaluate(*it);
					if (!std::holds_alternative<std::shared_ptr<Function>>(decoratorFunc)) {
						throw std::runtime_error("Decorator must be a function");
					}
					auto decFn = std::get<std::shared_ptr<Function>>(decoratorFunc);
					std::vector<Value> args = {methodVal};
					
					if (decFn->isBuiltin) {
						methodVal = decFn->builtin(args, decFn->closure);
					} else {
						auto callEnv = std::make_shared<Environment>(decFn->closure);
						for (size_t i = 0; i < decFn->params.size() && i < args.size(); ++i) {
							callEnv->define(decFn->params[i], args[i]);
						}
						try {
							auto prevEnv = env;
							env = callEnv;
							for (auto& s : decFn->body) execute(s);
							env = prevEnv;
							methodVal = Value{std::monostate{}}; 
						} catch (const ReturnSignal& ret) {
							methodVal = ret.value;
						}
					}
				}
				
				if (std::holds_alternative<std::shared_ptr<Function>>(methodVal)) {
					fn = std::get<std::shared_ptr<Function>>(methodVal);
				} else if (!m->decorators.empty() && !std::holds_alternative<std::monostate>(methodVal)) {
					throw std::runtime_error("Method decorator must return a function");
				}

				if (m->isStatic) {
					klass->staticMethods[m->name] = fn;
				} else {
					klass->methods[m->name] = fn;
				}
			}
			
			// 验证是否实现了所有 interface 方法
			for (auto& super : klass->supers) {
				// 检查父类的每个方法是否为 interface 方法占位符（nullptr）
				for (auto& [methodName, methodFunc] : super->methods) {
					if (methodFunc == nullptr) {
						// 这是一个 interface 方法，检查子类是否实现
						bool implemented = false;
						// 检查当前类是否实现
						if (klass->methods.find(methodName) != klass->methods.end() && klass->methods[methodName] != nullptr) {
							implemented = true;
						}
						// 检查其他父类是否提供实现
						if (!implemented) {
							for (auto& otherSuper : klass->supers) {
								if (otherSuper != super && otherSuper->methods.find(methodName) != otherSuper->methods.end() && otherSuper->methods[methodName] != nullptr) {
									implemented = true;
									break;
								}
							}
						}
						if (!implemented) {
							std::ostringstream oss;
							oss << "Class '" << c->name << "' must implement interface method '" << methodName << "' from '" << super->name << "'";
							throw std::runtime_error(oss.str());
						}
					}
				}
			}
			
			env->define(c->name, Value{klass});
			if (c->isExported) env->explicitExports.insert(c->name);
			return;
		}
		if (auto ext = std::dynamic_pointer_cast<ExtendStmt>(stmt)) {
			Value cv = env->get(ext->name);
			if (!std::holds_alternative<std::shared_ptr<ClassInfo>>(cv)) throw std::runtime_error("extends: target is not a class: " + ext->name);
			auto klass = std::get<std::shared_ptr<ClassInfo>>(cv);
			for (auto& m : ext->methods) {
				auto fn = std::make_shared<Function>();
				fn->params.clear(); 
				fn->defaultValues.clear();
				for (auto &p : m->params) {
					fn->params.push_back(p.name);
					fn->defaultValues.push_back(p.defaultValue);
				}
				if (auto innerBlock = std::dynamic_pointer_cast<BlockStmt>(m->body)) fn->body = innerBlock->statements; else fn->body = { m->body };
				fn->closure = env;
				fn->isAsync = m->isAsync;
				fn->isGenerator = m->isGenerator;
				klass->methods[m->name] = fn; // 覆盖或新增
			}
			return;
		}
		if (auto itf = std::dynamic_pointer_cast<InterfaceStmt>(stmt)) {
			// 将 interface 作为空方法集合的 ClassInfo 注入环境，可作为多继承的父类使用
			auto klass = std::make_shared<ClassInfo>();
			klass->name = itf->name;
			// 可选：记录方法名（不作校验）
			for (auto& mn : itf->methodNames) {
				if (klass->methods.find(mn) == klass->methods.end()) {
					klass->methods[mn] = nullptr; // 占位
				}
			}
			env->define(itf->name, klass);
			if (itf->isExported) env->explicitExports.insert(itf->name);
			return;
		}
		if (auto go = std::dynamic_pointer_cast<GoStmt>(stmt)) {
			// 调度一个任务在事件循环中执行表达式（通常为调用表达式）
			auto exprCopy = go->call;
			auto envSnap = env;
			postTask([this, exprCopy, envSnap]{
				auto prev = env;
				env = envSnap;
				try { (void) evaluate(exprCopy); } catch (...) { /* 丢弃 go 任务中的异常 */ }
				env = prev;
			});
			return;
		}
		throw std::runtime_error("Unknown statement type");
	}

	void executeBlock(const std::vector<StmtPtr>& stmts, std::shared_ptr<Environment> newEnv) {
		auto previous = env;
		env = newEnv;
		try {
			for (auto& s : stmts) execute(s);
		} catch (...) {
			env = previous; throw;
		}
		env = previous;
	}

	std::shared_ptr<Environment> globalsEnv() const { return globals; }
	std::shared_ptr<Environment> currentEnv() const { return env; }
	void setCurrentEnv(std::shared_ptr<Environment> newEnv) { env = newEnv; }

	// 供宿主侧调用全局函数的便捷方法
	Value callFunction(const std::string& name, const std::vector<Value>& args) {
		Value cal = globals->get(name);
		if (!std::holds_alternative<std::shared_ptr<Function>>(cal)) throw std::runtime_error("callFunction: target is not a function: " + name);
		auto fn = std::get<std::shared_ptr<Function>>(cal);
		if (fn->isBuiltin) {
			return fn->builtin(args, fn->closure);
		}
		if (args.size() != fn->params.size()) throw std::runtime_error("callFunction: arity mismatch");
		auto local = std::make_shared<Environment>(fn->closure);
		for (size_t i=0;i<args.size();++i) local->define(fn->params[i], args[i]);
		try {
			executeBlock(fn->body, local);
		} catch (const ReturnSignal& rs) {
			return rs.value;
		}
		return Value{std::monostate{}};
	}

private:
	std::shared_ptr<Environment> globals;
	std::shared_ptr<Environment> env;
	std::mutex loopMutex;
	std::condition_variable loopCv;
	std::queue<std::function<void()>> taskQueue;
	std::unordered_map<std::string, std::shared_ptr<Object>> packages;
	std::shared_ptr<Object> stdRoot;
	std::unordered_map<std::string, std::shared_ptr<Object>> importedModules; // cache for file imports
	std::filesystem::path importBaseDir;

	// Signal handlers map: signal number -> callback function
	std::unordered_map<int, Value> signalHandlers;

	// Error context for pretty printing from imported files
	std::string lastErrorSource;
	std::string lastErrorFilename;
	std::vector<std::string> importStack;
	std::vector<std::string> callStack;

	// Lazy loading support
	std::map<std::string, std::function<void(std::shared_ptr<Object>)>> lazyPackages;

	bool loadLazyPackage(const std::string& name) {
		auto it = lazyPackages.find(name);
		if (it != lazyPackages.end()) {
			auto pkg = ensurePackage(name);
			it->second(pkg);
			lazyPackages.erase(it);
			return true;
		}
		return false;
	}

	// 构建带有增强信息的异常对象：{ message, line, column, length, stack: [...], type: "Error" }
	Value buildExceptionValue(const std::string& msg, int line = -1, int column = -1, int length = -1) {
		// 如果已有对象则补充 stack
		auto obj = std::make_shared<Object>();
		(*obj)["message"] = Value{ msg };
		if (line >= 0) (*obj)["line"] = Value{ static_cast<double>(line) };
		if (column >= 0) (*obj)["column"] = Value{ static_cast<double>(column) };
		if (length >= 0) (*obj)["length"] = Value{ static_cast<double>(length) };
		(*obj)["type"] = Value{ std::string("Error") };
		// stack 数组
		auto arr = std::make_shared<Array>();
		for (auto &f : callStack) arr->push_back(Value{ f });
		(*obj)["stack"] = Value{ arr };
		return Value{ obj };
	}
	// 若用户 throw 自定义值，包装为对象（string 转对象；object 未包含 stack 时补充）
	Value ensureExceptionValue(Value v, int line = -1, int column = -1, int length = -1) {
		// string -> { message: str, ... }
		if (std::holds_alternative<std::string>(v)) {
			return buildExceptionValue(std::get<std::string>(v), line, column, length);
		}
		// object: 若没有 stack 添加；若没有 message 生成 message = toString(value)
		if (auto po = std::get_if<std::shared_ptr<Object>>(&v)) {
			if (*po) {
				bool hasStack = false; bool hasMsg = false;
				if ((**po).find("stack") != (**po).end()) hasStack = true;
				if ((**po).find("message") != (**po).end()) hasMsg = true;
				if (!hasStack) {
					auto arr = std::make_shared<Array>();
					for (auto &f : callStack) arr->push_back(Value{ f });
					(**po)["stack"] = Value{ arr };
				}
				if (!hasMsg) {
					(**po)["message"] = Value{ std::string("Object thrown") };
				}
				if (line >= 0 && (**po).find("line") == (**po).end()) (**po)["line"] = Value{ static_cast<double>(line) };
				if (column >= 0 && (**po).find("column") == (**po).end()) (**po)["column"] = Value{ static_cast<double>(column) };
				if (length >= 0 && (**po).find("length") == (**po).end()) (**po)["length"] = Value{ static_cast<double>(length) };
				if ((**po).find("type") == (**po).end()) (**po)["type"] = Value{ std::string("Error") };
				return v;
			}
		}
		// 其它类型 -> 包装成字符串表示
		return buildExceptionValue(typeOf(v), line, column, length);
	}

public:
	bool takeErrorContext(std::string& outSrc, std::string& outFile) {
		if (lastErrorSource.empty()) return false;
		outSrc = lastErrorSource; outFile = lastErrorFilename;
		lastErrorSource.clear(); lastErrorFilename.clear();
		return true;
	}

	void settlePromise(std::shared_ptr<PromiseState> p, bool rejected, const Value& result) override {
		{
			std::lock_guard<std::mutex> lk(p->mtx);
			p->settled = true; p->rejected = rejected; p->result = result;
		}
		p->cv.notify_all();
		dispatchPromiseCallbacks(p);
	}

	void dispatchPromiseCallbacks(std::shared_ptr<PromiseState> p) override {
		if (!p->loopPtr) return;
		auto loop = static_cast<Interpreter*>(p->loopPtr);
		if (!p->rejected) {
			for (auto& pair : p->thenCallbacks) {
				auto cb = pair.first; auto nextP = pair.second;
				loop->postTask([this, cb, nextP, p]() {
					try {
						std::vector<Value> a{ p->result };
						Value ret{std::monostate{}};
						if (cb->isBuiltin) ret = cb->builtin(a, cb->closure);
						else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (a.size() != cb->params.size()) {
								if (!cb->params.empty()) local->define(cb->params[0], p->result);
							} else {
								for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]);
							}
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
						}
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
							auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
							// 链接：inner 完成后再 settle nextP
							{
								std::lock_guard<std::mutex> lk(inner->mtx);
								inner->loopPtr = this;
								inner->thenCallbacks.push_back({ makeResolver(), nextP });
								inner->catchCallbacks.push_back({ makeRejecter(), nextP });
							}
							if (inner->settled) dispatchPromiseCallbacks(inner);
						} else {
							settlePromise(nextP, false, ret);
						}
					} catch (const ExceptionSignal& ex) {
						settlePromise(nextP, true, ex.value);
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					} catch (...) {
						settlePromise(nextP, true, Value{ std::string("error") });
					}
				});
			}
		} else {
			for (auto& pair : p->catchCallbacks) {
				auto cb = pair.first; auto nextP = pair.second;
				loop->postTask([this, cb, nextP, p]() {
					try {
						std::vector<Value> a{ p->result };
						Value ret{std::monostate{}};
						if (cb->isBuiltin) ret = cb->builtin(a, cb->closure);
						else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (a.size() != cb->params.size()) {
								if (!cb->params.empty()) local->define(cb->params[0], p->result);
							} else {
								for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]);
							}
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
						}
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
							auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
							{
								std::lock_guard<std::mutex> lk(inner->mtx);
								inner->loopPtr = this;
								inner->thenCallbacks.push_back({ makeResolver(), nextP });
								inner->catchCallbacks.push_back({ makeRejecter(), nextP });
							}
							if (inner->settled) dispatchPromiseCallbacks(inner);
						} else {
							settlePromise(nextP, false, ret);
						}
					} catch (const ExceptionSignal& ex) {
						settlePromise(nextP, true, ex.value);
					} catch (const std::exception& ex) {
						settlePromise(nextP, true, Value{ std::string(ex.what()) });
					} catch (...) {
						settlePromise(nextP, true, Value{ std::string("error") });
					}
				});
			}
		}
	}

	// 生成一个 resolver/rejecter 回调函数（形如 x => x 或 e => throw e）用于链接
	std::shared_ptr<Function> makeResolver() {
		auto f = std::make_shared<Function>();
		f->isBuiltin = true;
		f->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty()) return Value{std::monostate{}};
			return args[0];
		};
		return f;
	}
	std::shared_ptr<Function> makeRejecter() {
		auto f = std::make_shared<Function>();
		f->isBuiltin = true;
		f->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			// 通过抛异常传播拒绝
			if (args.empty()) throw std::runtime_error("Promise rejected");
			std::string msg = toString(args[0]);
			throw std::runtime_error(msg);
		};
		return f;
	}

	// Strict equality: types must match; primitives compare by value; non-primitives compare by pointer
	static bool isStrictEqual(const Value& a, const Value& b) {
		if (a.index() != b.index()) {
			return false;
		}
		if (std::holds_alternative<std::monostate>(a)) return true;
		if (auto na = std::get_if<double>(&a)) return *na == std::get<double>(b);
		if (auto sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
		if (auto ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
		if (std::holds_alternative<std::shared_ptr<Function>>(a)) return std::get<std::shared_ptr<Function>>(a).get() == std::get<std::shared_ptr<Function>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Array>>(a)) return std::get<std::shared_ptr<Array>>(a).get() == std::get<std::shared_ptr<Array>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Object>>(a)) return std::get<std::shared_ptr<Object>>(a).get() == std::get<std::shared_ptr<Object>>(b).get();
		if (std::holds_alternative<std::shared_ptr<ClassInfo>>(a)) return std::get<std::shared_ptr<ClassInfo>>(a).get() == std::get<std::shared_ptr<ClassInfo>>(b).get();
		if (std::holds_alternative<std::shared_ptr<Instance>>(a)) return std::get<std::shared_ptr<Instance>>(a).get() == std::get<std::shared_ptr<Instance>>(b).get();
		if (std::holds_alternative<std::shared_ptr<PromiseState>>(a)) return std::get<std::shared_ptr<PromiseState>>(a).get() == std::get<std::shared_ptr<PromiseState>>(b).get();
		return false;
	}

	// Convert various types to a numeric value similar to JS ToNumber
	static double toNumberPrimitive(const Value& v, bool& ok) {
		ok = true;
		if (std::holds_alternative<std::monostate>(v)) return 0.0; // null -> +0
		if (auto n = std::get_if<double>(&v)) return *n;
		if (auto s = std::get_if<std::string>(&v)) {
			char* end = nullptr; double d = std::strtod(s->c_str(), &end);
			if (end && *end == '\0') return d;
			ok = false; return std::numeric_limits<double>::quiet_NaN();
		}
		if (auto b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
		// Objects/functions/arrays -> ToPrimitive then ToNumber: use string coercion via toString()
		std::string s = toString(v);
		char* end = nullptr; double d = std::strtod(s.c_str(), &end);
		if (end && *end == '\0') return d;
		ok = false; return std::numeric_limits<double>::quiet_NaN();
	}

	// ToPrimitive fallback: use toString for non-primitive values
	static Value toPrimitive(const Value& v) {
		if (std::holds_alternative<std::monostate>(v) || std::get_if<double>(&v) || std::get_if<std::string>(&v) || std::get_if<bool>(&v)) return v;
		return Value{toString(v)};
	}

	// Abstract equality algorithm (JS-like == with coercion)
	static bool isJSEqual(const Value& x, const Value& y) {
		// If same type, use strict equality
		if (x.index() == y.index()) return isStrictEqual(x, y);

		// Number == String
		if (std::get_if<double>(&x) && std::get_if<std::string>(&y)) {
			bool ok; double yn = toNumberPrimitive(y, ok); if (!ok) return false; return std::get<double>(x) == yn;
		}
		if (std::get_if<std::string>(&x) && std::get_if<double>(&y)) {
			bool ok; double xn = toNumberPrimitive(x, ok); if (!ok) return false; return xn == std::get<double>(y);
		}

		// Boolean -> convert to number
		if (std::get_if<bool>(&x)) {
			bool ok; double xn = toNumberPrimitive(x, ok); return isJSEqual(Value{xn}, y);
		}
		if (std::get_if<bool>(&y)) {
			bool ok; double yn = toNumberPrimitive(y, ok); return isJSEqual(x, Value{yn});
		}

		// Object to primitive then compare
		auto isObjectType = [](const Value& v)->bool{
			return std::holds_alternative<std::shared_ptr<Object>>(v) || std::holds_alternative<std::shared_ptr<Array>>(v) || std::holds_alternative<std::shared_ptr<Instance>>(v) || std::holds_alternative<std::shared_ptr<ClassInfo>>(v) || std::holds_alternative<std::shared_ptr<Function>>(v) || std::holds_alternative<std::shared_ptr<PromiseState>>(v);
		};
		if (isObjectType(x) && (std::get_if<std::string>(&y) || std::get_if<double>(&y))) {
			Value px = toPrimitive(x);
			return isJSEqual(px, y);
		}
		if (isObjectType(y) && (std::get_if<std::string>(&x) || std::get_if<double>(&x))) {
			Value py = toPrimitive(y);
			return isJSEqual(x, py);
		}

		return false;
	}

	static double getNumber(const Value& v, const char* where) {
		if (auto n = std::get_if<double>(&v)) return *n;
		if (auto s = std::get_if<std::string>(&v)) {
			char* end = nullptr; double d = std::strtod(s->c_str(), &end); if (end && *end=='\0') return d;
		}
		throw std::runtime_error(std::string("Expected number at ") + where);
	}

	// Helpers for object/array/class/instance access
	static std::shared_ptr<Function> findMethod(std::shared_ptr<ClassInfo> k, const std::string& name) {
		if (!k) return nullptr;
		auto it = k->methods.find(name);
		if (it != k->methods.end()) return it->second;
		// 多继承：按声明顺序递归线性查找
		for (auto& s : k->supers) {
			auto f = findMethod(s, name);
			if (f) return f;
		}
		return nullptr;
	}
	static std::shared_ptr<Function> findStaticMethod(std::shared_ptr<ClassInfo> k, const std::string& name) {
		if (!k) return nullptr;
		auto it = k->staticMethods.find(name);
		if (it != k->staticMethods.end()) return it->second;
		for (auto& s : k->supers) {
			auto f = findStaticMethod(s, name);
			if (f) return f;
		}
		return nullptr;
	}
	Value getProperty(const Value& obj, const std::string& name) {
		// Instance: fields then methods
		if (auto pins = std::get_if<std::shared_ptr<Instance>>(&obj)) {
			if (*pins) {
				auto fit = (*pins)->fields.find(name);
				if (fit != (*pins)->fields.end()) return fit->second;
				if ((*pins)->klass) {
					auto m = findMethod((*pins)->klass, name);
					if (m) {
						auto bound = std::make_shared<Function>(*m);
						auto thisEnv = std::make_shared<Environment>(bound->closure);
						thisEnv->define("this", obj);
						bound->closure = thisEnv;
						return bound;
					}
				}
				return Value{std::monostate{}};
			}
		}
		// Class (static methods)
		if (auto pcls = std::get_if<std::shared_ptr<ClassInfo>>(&obj)) {
			if (*pcls) {
				auto m = findStaticMethod(*pcls, name);
				if (m) return m;
			}
		}
		// Object
		if (auto po = std::get_if<std::shared_ptr<Object>>(&obj)) {
			auto it = (**po).find(name);
			if (it != (**po).end()) return it->second;
			// synthetic len()
			if (name == "len") {
				auto lenFn = std::make_shared<Function>(); lenFn->isBuiltin = true;
				auto o = *po; lenFn->builtin = [o](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(o ? o->size() : 0) }; };
				return lenFn;
			}
			return Value{std::monostate{}};
		}
		// Array synthetic methods
		if (auto parr = std::get_if<std::shared_ptr<Array>>(&obj)) {
			if (name == "len") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(a ? a->size() : 0) }; };
				return fn;
			}
			if (name == "push") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{0.0}; for (auto& v: args) a->push_back(v); return Value{ static_cast<double>(a->size()) }; };
				return fn;
			}
			if (name == "pop") { auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a=*parr; fn->builtin=[a](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { if (!a||a->empty()) return Value{std::monostate{}}; Value v=a->back(); a->pop_back(); return v; }; return fn; }
			if (name == "shift") { auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a=*parr; fn->builtin=[a](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { if (!a||a->empty()) return Value{std::monostate{}}; Value v=(*a)[0]; a->erase(a->begin()); return v; }; return fn; }
			if (name == "unshift") { auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a=*parr; fn->builtin=[a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{0.0}; a->insert(a->begin(), args.begin(), args.end()); return Value{ static_cast<double>(a->size()) }; }; return fn; }
			if (name == "slice") { auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a=*parr; fn->builtin=[a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{std::monostate{}}; double n=static_cast<double>(a->size()); double s=0; double e=n; if (!args.empty()) s=getNumber(args[0], "slice start"); if (args.size()>=2) e=getNumber(args[1], "slice end"); if (s<0) s+=n; if (e<0) e+=n; if (s<0) s=0; if (e<0) e=0; size_t si=static_cast<size_t>(s); size_t ei=static_cast<size_t>(e); if (si>a->size()) si=a->size(); if (ei>a->size()) ei=a->size(); if (ei<si) ei=si; auto out=std::make_shared<Array>(); for (size_t i=si;i<ei;++i) out->push_back((*a)[i]); return Value{out}; }; return fn; }
			if (name == "indexOf") { auto fn=std::make_shared<Function>(); fn->isBuiltin=true; auto a=*parr; fn->builtin=[a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{-1.0}; if (args.empty()) throw std::runtime_error("indexOf expects value argument"); size_t start=0; if (args.size()>=2) { double sd=getNumber(args[1], "indexOf start"); if (sd<0) sd+=a->size(); if (sd<0) sd=0; start=static_cast<size_t>(sd); if (start>a->size()) return Value{-1.0}; } for (size_t i=start;i<a->size();++i) { if (valueEqual((*a)[i], args[0])) return Value{ static_cast<double>(i) }; } return Value{-1.0}; }; return fn; }
			if (name == "join") { auto fn=std::make_shared<Function>(); fn->isBuiltin=true; auto a=*parr; fn->builtin=[a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { std::string delim=""; if (!args.empty()) delim=toString(args[0]); if (!a||a->empty()) return Value{ std::string("") }; std::ostringstream oss; for (size_t i=0;i<a->size();++i) { if (i) oss<<delim; oss<<toString((*a)[i]); } return Value{ oss.str() }; }; return fn; }
			if (name == "reverse") { auto fn=std::make_shared<Function>(); fn->isBuiltin=true; auto a=*parr; fn->builtin=[a](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { if (a) std::reverse(a->begin(), a->end()); return Value{a}; }; return fn; }
			if (name == "sort") { auto fn=std::make_shared<Function>(); fn->isBuiltin=true; auto a=*parr; fn->builtin=[this,a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{std::monostate{}}; std::shared_ptr<Function> cmp; if (!args.empty()) { if (!std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("sort comparator must be function"); cmp=std::get<std::shared_ptr<Function>>(args[0]); }
				std::stable_sort(a->begin(), a->end(), [this,cmp](const Value& lhs, const Value& rhs){ if (cmp) { Value ret{std::monostate{}}; if (cmp->isBuiltin) { std::vector<Value> carg{lhs,rhs}; ret=cmp->builtin(carg, cmp->closure); } else { auto local=std::make_shared<Environment>(cmp->closure); if (cmp->params.size()>0) local->define(cmp->params[0], lhs); if (cmp->params.size()>1) local->define(cmp->params[1], rhs); try { executeBlock(cmp->body, local); } catch (const ReturnSignal& rs) { ret=rs.value; } } return isTruthy(ret); }
				// default compare: numbers numeric asc, else string lexicographical
				auto ln=std::get_if<double>(&lhs); auto rn=std::get_if<double>(&rhs); if (ln && rn) return *ln < *rn; std::string ls=toString(lhs); std::string rs=toString(rhs); return ls < rs; }); return Value{a}; }; return fn; }
			if (name == "splice") { auto fn=std::make_shared<Function>(); fn->isBuiltin=true; auto a=*parr; fn->builtin=[a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (!a) return Value{std::monostate{}}; if (args.empty()) throw std::runtime_error("splice expects start index"); double startD=getNumber(args[0], "splice start"); if (startD<0) startD+=a->size(); if (startD<0) startD=0; size_t start = static_cast<size_t>(startD); if (start>a->size()) start=a->size(); size_t deleteCount=0; size_t insertFrom=1; if (args.size()>=2) { double delD=getNumber(args[1], "splice deleteCount"); if (delD<0) delD=0; deleteCount=static_cast<size_t>(delD); insertFrom=2; } if (start+deleteCount>a->size()) deleteCount = a->size()-start; auto removed=std::make_shared<Array>(); for (size_t i=0;i<deleteCount;++i) removed->push_back((*a)[start+i]); a->erase(a->begin()+start, a->begin()+start+deleteCount); // insert new items
				for (size_t i=insertFrom;i<args.size();++i) a->insert(a->begin()+start+(i-insertFrom), args[i]); return Value{removed}; }; return fn; }
			if (name == "map") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{std::monostate{}};
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("map expects a single function argument");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto out = std::make_shared<Array>();
					for (size_t i = 0; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], elem);
							if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 2) local->define(cb->params[2], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						out->push_back(res);
					}
					return Value{out};
				};
				return fn;
			}
			if (name == "filter") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{std::monostate{}};
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("filter expects a single function argument");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto out = std::make_shared<Array>();
					for (size_t i = 0; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], elem);
							if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 2) local->define(cb->params[2], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						if (isTruthy(res)) out->push_back(elem);
					}
					return Value{out};
				};
				return fn;
			}
			if (name == "reduce") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{std::monostate{}};
					if (args.size() < 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("reduce expects a function and optional initial value");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					Value acc;
					size_t start = 0;
					if (args.size() >= 2) { acc = args[1]; } else { if (a->empty()) throw std::runtime_error("reduce of empty array with no initial value"); acc = (*a)[0]; start = 1; }
					for (size_t i = start; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ acc, elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], acc);
							if (cb->params.size() > 1) local->define(cb->params[1], elem);
							if (cb->params.size() > 2) local->define(cb->params[2], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 3) local->define(cb->params[3], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						acc = res;
					}
					return acc;
				};
				return fn;
			}
			if (name == "find") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{std::monostate{}};
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("find expects a single function argument");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					for (size_t i = 0; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], elem);
							if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 2) local->define(cb->params[2], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						if (isTruthy(res)) return elem;
					}
					return Value{std::monostate{}};
				};
				return fn;
			}
			if (name == "some") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{false};
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("some expects a single function argument");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					for (size_t i = 0; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], elem);
							if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 2) local->define(cb->params[2], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						if (isTruthy(res)) return Value{true};
					}
					return Value{false};
				};
				return fn;
			}
			if (name == "every") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value {
					if (!a) return Value{true};
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("every expects a single function argument");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					for (size_t i = 0; i < a->size(); ++i) {
						Value elem = (*a)[i];
						Value res{std::monostate{}};
						if (cb->isBuiltin) {
							std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{a} };
							res = cb->builtin(carg, cb->closure);
						} else {
							auto local = std::make_shared<Environment>(cb->closure);
							if (cb->params.size() > 0) local->define(cb->params[0], elem);
							if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
							if (cb->params.size() > 2) local->define(cb->params[2], Value{a});
							try { executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
						}
						if (!isTruthy(res)) return Value{false};
					}
					return Value{true};
				};
				return fn;
			}
			if (name == "includes") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true; auto a = *parr;
				fn->builtin = [this,a](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (!a) return Value{false};
					if (args.size() < 1) throw std::runtime_error("includes expects at least 1 argument");
					for (size_t i = 0; i < a->size(); ++i) { if (valueEqual((*a)[i], args[0])) return Value{true}; }
					return Value{false};
				};
				return fn;
			}
			return Value{std::monostate{}};
		}
		// String synthetic methods
		if (auto ps = std::get_if<std::string>(&obj)) {
			if (name == "len") { auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ static_cast<double>(s.size()) }; }; return fn; }
			// Added extended string methods
			if (name == "trim") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
					size_t start = 0, end = s.size();
					while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) start++;
					while (end > start && std::isspace(static_cast<unsigned char>(s[end-1]))) end--;
					return Value{ s.substr(start, end - start) };
				}; return fn;
			}
			if (name == "trimLeft") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
					size_t start = 0;
					while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
					return Value{ s.substr(start) };
				}; return fn;
			}
			if (name == "trimRight") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
					size_t end = s.size();
					while (end > 0 && std::isspace(static_cast<unsigned char>(s[end-1]))) end--;
					return Value{ s.substr(0, end) };
				}; return fn;
			}
			if (name == "toLowerCase") { auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { std::string out=s; for (auto &c: out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return Value{out}; }; return fn; }
			if (name == "toUpperCase") { auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true; fn->builtin = [s](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { std::string out=s; for (auto &c: out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return Value{out}; }; return fn; }
			if (name == "startsWith") { auto s=*ps; auto fn=std::make_shared<Function>(); fn->isBuiltin=true; fn->builtin=[s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("startsWith expects 1 string arg"); std::string pre=std::get<std::string>(args[0]); return Value{ s.rfind(pre,0)==0 }; }; return fn; }
			if (name == "endsWith") { auto s=*ps; auto fn=std::make_shared<Function>(); fn->isBuiltin=true; fn->builtin=[s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("endsWith expects 1 string arg"); std::string suf=std::get<std::string>(args[0]); if (suf.size()>s.size()) return Value{false}; return Value{ std::equal(suf.rbegin(), suf.rend(), s.rbegin()) }; }; return fn; }
			if (name == "includes") { auto s=*ps; auto fn=std::make_shared<Function>(); fn->isBuiltin=true; fn->builtin=[s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (args.size()!=1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("includes expects 1 string arg"); std::string sub=std::get<std::string>(args[0]); return Value{ s.find(sub)!=std::string::npos }; }; return fn; }
			if (name == "indexOf") { auto s=*ps; auto fn=std::make_shared<Function>(); fn->isBuiltin=true; fn->builtin=[s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value { if (args.size()<1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("indexOf expects search string and optional start index"); std::string search=std::get<std::string>(args[0]); size_t start=0; if (args.size()>=2) start = static_cast<size_t>(std::max(0, static_cast<int>(getNumber(args[1], "indexOf start")))); auto pos = s.find(search, start); if (pos==std::string::npos) return Value{ -1.0 }; return Value{ static_cast<double>(pos) }; }; return fn; }
			if (name == "split") {
				auto s = *ps;
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 1) throw std::runtime_error("split expects a delimiter string");
					if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("split delimiter must be a string");
					std::string delim = std::get<std::string>(args[0]);
					auto out = std::make_shared<Array>();
					if (delim.empty()) { // split into chars
						for (char c : s) { out->push_back(Value{ std::string(1, c) }); }
						return Value{out};
					}
					size_t pos = 0, found;
					while ((found = s.find(delim, pos)) != std::string::npos) {
						out->push_back(Value{ s.substr(pos, found - pos) });
						pos = found + delim.size();
					}
					out->push_back(Value{ s.substr(pos) });
					return Value{out};
				};
				return fn;
			}
			if (name == "substring") {
				auto s = *ps;
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 1) throw std::runtime_error("substring expects start and optional end");
					double start = getNumber(args[0], "substring start");
					size_t si = static_cast<size_t>(std::max(0, static_cast<int>(start)));
					size_t len = s.size();
					if (args.size() >= 2) {
						double end = getNumber(args[1], "substring end");
						size_t ei = static_cast<size_t>(std::max(0, static_cast<int>(end)));
						if (ei > len) ei = len;
						if (si >= ei) return Value{ std::string("") };
						return Value{ s.substr(si, ei - si) };
					}
					if (si >= len) return Value{ std::string("") };
					return Value{ s.substr(si) };
				};
				return fn;
			}
			if (name == "replace") {
				auto s = *ps;
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 2) throw std::runtime_error("replace expects search and replacement strings");
					if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) throw std::runtime_error("replace expects string arguments");
					std::string search = std::get<std::string>(args[0]);
					std::string repl = std::get<std::string>(args[1]);
					if (search.empty()) return Value{ s };
					size_t pos = s.find(search);
					if (pos == std::string::npos) return Value{ s };
					std::string out = s.substr(0, pos) + repl + s.substr(pos + search.size());
					return Value{ out };
				};
				return fn;
			}
			if (name == "lastIndexOf") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 1 || !std::holds_alternative<std::string>(args[0])) throw std::runtime_error("lastIndexOf expects search string");
					std::string search = std::get<std::string>(args[0]);
					size_t pos = std::string::npos;
					if (args.size() >= 2) {
						double d = getNumber(args[1], "lastIndexOf position");
						if (d >= 0) pos = static_cast<size_t>(d);
					}
					auto found = s.rfind(search, pos);
					if (found == std::string::npos) return Value{ -1.0 };
					return Value{ static_cast<double>(found) };
				}; return fn;
			}
			if (name == "slice") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					double start = 0, end = static_cast<double>(s.size());
					if (args.size() >= 1) start = getNumber(args[0], "slice start");
					if (args.size() >= 2) end = getNumber(args[1], "slice end");
					if (start < 0) start += s.size();
					if (end < 0) end += s.size();
					if (start < 0) start = 0;
					if (end < 0) end = 0;
					size_t si = static_cast<size_t>(start);
					size_t ei = static_cast<size_t>(end);
					if (si > s.size()) si = s.size();
					if (ei > s.size()) ei = s.size();
					if (ei < si) return Value{ std::string("") };
					return Value{ s.substr(si, ei - si) };
				}; return fn;
			}
			if (name == "padStart") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 1) throw std::runtime_error("padStart expects target length");
					size_t targetLen = static_cast<size_t>(std::max(0.0, getNumber(args[0], "padStart length")));
					if (s.size() >= targetLen) return Value{s};
					std::string pad = " ";
					if (args.size() >= 2) pad = toString(args[1]);
					if (pad.empty()) return Value{s};
					std::string out;
					size_t padLen = targetLen - s.size();
					while (out.size() < padLen) out += pad;
					out = out.substr(0, padLen);
					return Value{ out + s };
				}; return fn;
			}
			if (name == "padEnd") {
				auto s = *ps; auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [s](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() < 1) throw std::runtime_error("padEnd expects target length");
					size_t targetLen = static_cast<size_t>(std::max(0.0, getNumber(args[0], "padEnd length")));
					if (s.size() >= targetLen) return Value{s};
					std::string pad = " ";
					if (args.size() >= 2) pad = toString(args[1]);
					if (pad.empty()) return Value{s};
					std::string out = s;
					while (out.size() < targetLen) out += pad;
					return Value{ out.substr(0, targetLen) };
				}; return fn;
			}
			return Value{std::string("undefined")};
		}
		// For numbers and other primitives, return "undefined" instead of null
		if (std::get_if<double>(&obj) || std::get_if<bool>(&obj)) {
			return Value{std::string("undefined")};
		}
		// Promise synthetic methods: then/catch
		if (auto p = std::get_if<std::shared_ptr<PromiseState>>(&obj)) {
			if (name == "then") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				auto ps = *p;
				fn->builtin = [ps](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("then expects a function");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto nextP = std::make_shared<PromiseState>();
					nextP->loopPtr = ps->loopPtr;
					{
						std::lock_guard<std::mutex> lk(ps->mtx);
						if (ps->settled && !ps->rejected) {
							if (ps->loopPtr) {
								auto loop = static_cast<Interpreter*>(ps->loopPtr);
								loop->postTask([ps, cb, nextP, loop]{
									try {
										std::vector<Value> a{ ps->result };
										Value ret{std::monostate{}};
										if (cb->isBuiltin) ret = cb->builtin(a, cb->closure); else {
											auto local = std::make_shared<Environment>(cb->closure);
											if (a.size() != cb->params.size()) { if (!cb->params.empty()) local->define(cb->params[0], ps->result); }
											else { for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]); }
											try { loop->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
										}
										if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
											auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
											{
												std::lock_guard<std::mutex> lk2(inner->mtx);
												inner->loopPtr = loop;
												inner->thenCallbacks.push_back({ loop->makeResolver(), nextP });
												inner->catchCallbacks.push_back({ loop->makeRejecter(), nextP });
											}
											if (inner->settled) loop->dispatchPromiseCallbacks(inner);
										} else {
											loop->settlePromise(nextP, false, ret);
										}
									} catch (const ExceptionSignal& ex) {
										loop->settlePromise(nextP, true, ex.value);
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									} catch (...) {
										loop->settlePromise(nextP, true, Value{ std::string("error") });
									}
								});
							}
						} else {
							ps->thenCallbacks.push_back({ cb, nextP });
						}
					}
					return Value{nextP};
				};
				return fn;
			}
			if (name == "catch") {
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				auto ps = *p;
				fn->builtin = [ps](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.size() != 1 || !std::holds_alternative<std::shared_ptr<Function>>(args[0])) throw std::runtime_error("catch expects a function");
					auto cb = std::get<std::shared_ptr<Function>>(args[0]);
					auto nextP = std::make_shared<PromiseState>();
					nextP->loopPtr = ps->loopPtr;
					{
						std::lock_guard<std::mutex> lk(ps->mtx);
						if (ps->settled && ps->rejected) {
							if (ps->loopPtr) {
								auto loop = static_cast<Interpreter*>(ps->loopPtr);
								loop->postTask([ps, cb, nextP, loop]{
									try {
										std::vector<Value> a{ ps->result };
										Value ret{std::monostate{}};
										if (cb->isBuiltin) ret = cb->builtin(a, cb->closure); else {
											auto local = std::make_shared<Environment>(cb->closure);
											if (a.size() != cb->params.size()) { if (!cb->params.empty()) local->define(cb->params[0], ps->result); }
											else { for (size_t i=0;i<a.size();++i) local->define(cb->params[i], a[i]); }
											try { loop->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { ret = rs.value; }
										}
										if (std::holds_alternative<std::shared_ptr<PromiseState>>(ret)) {
											auto inner = std::get<std::shared_ptr<PromiseState>>(ret);
											{
												std::lock_guard<std::mutex> lk2(inner->mtx);
												inner->loopPtr = loop;
												inner->thenCallbacks.push_back({ loop->makeResolver(), nextP });
												inner->catchCallbacks.push_back({ loop->makeRejecter(), nextP });
											}
											if (inner->settled) loop->dispatchPromiseCallbacks(inner);
										} else {
											loop->settlePromise(nextP, false, ret);
										}
									} catch (const ExceptionSignal& ex) {
										loop->settlePromise(nextP, true, ex.value);
									} catch (const std::exception& ex) {
										loop->settlePromise(nextP, true, Value{ std::string(ex.what()) });
									} catch (...) {
										loop->settlePromise(nextP, true, Value{ std::string("error") });
									}
								});
							}
						} else {
							ps->catchCallbacks.push_back({ cb, nextP });
						}
					}
					return Value{nextP};
				};
				return fn;
			}
			return Value{std::monostate{}};
		}
		// Class / others: no properties
		return Value{std::monostate{}};
	}
	static Value getIndex(const Value& obj, const Value& key) {
		if (auto parr = std::get_if<std::shared_ptr<Array>>(&obj)) {
			size_t idx = indexFromValue(key);
			auto& vec = **parr;
			if (idx >= vec.size()) throw std::runtime_error("Array index out of range");
			return vec[idx];
		}
		if (auto pins = std::get_if<std::shared_ptr<Instance>>(&obj)) {
			std::string k = keyFromValue(key);
			auto it = (*pins)->fields.find(k);
			if (it != (*pins)->fields.end()) return it->second;
			return Value{std::monostate{}};
		}
		if (auto pobj = std::get_if<std::shared_ptr<Object>>(&obj)) {
			std::string k = keyFromValue(key);
			auto it = (**pobj).find(k);
			if (it == (**pobj).end()) return Value{std::monostate{}};
			return it->second;
		}
		throw std::runtime_error("Index access on non-array/object");
	}
	static size_t indexFromValue(const Value& v) {
		double d = getNumber(v, "array index");
		if (d < 0) throw std::runtime_error("Negative index");
		size_t idx = static_cast<size_t>(d);
		if (static_cast<double>(idx) != d) throw std::runtime_error("Index must be integer");
		return idx;
	}
	static std::string keyFromValue(const Value& v) {
		if (auto s = std::get_if<std::string>(&v)) return *s;
		if (auto n = std::get_if<double>(&v)) { std::ostringstream oss; oss << *n; return oss.str(); }
		if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
		if (std::holds_alternative<std::monostate>(v)) return "null";
		throw std::runtime_error("Unsupported key type");
	}

	// Evaluate to a reference-like concept: here we just ensure object is object and return Value& by storing object evaluated value back? For simplicity, we evaluate then require it's object/array and return a reference to the held shared_ptr so we can mutate its contents.
	Value& ensureObjectRef(const ExprPtr& objExpr) {
		tempStorage = evaluate(objExpr);
		if (!std::holds_alternative<std::shared_ptr<Object>>(tempStorage) && !std::holds_alternative<std::shared_ptr<Instance>>(tempStorage)) throw std::runtime_error("Target is not an object");
		return tempStorage;
	}
	Value& evaluateRef(const ExprPtr& objExpr) {
		tempStorage = evaluate(objExpr);
		if (std::holds_alternative<std::shared_ptr<Array>>(tempStorage) || std::holds_alternative<std::shared_ptr<Object>>(tempStorage) || std::holds_alternative<std::shared_ptr<Instance>>(tempStorage)) return tempStorage;
		throw std::runtime_error("Target is not indexable");
	}

	Value tempStorage; // used to hold temporary during Set* operations

	// Helper function to destructure patterns
	void destructurePattern(const PatternPtr& pattern, const Value& value) {
		if (auto idPat = std::dynamic_pointer_cast<IdentifierPattern>(pattern)) {
			// Simple identifier pattern
			Value val = value;
			if (std::holds_alternative<std::monostate>(value) && idPat->defaultValue) {
				val = evaluate(idPat->defaultValue);
			}
			env->define(idPat->name, val);
		} else if (auto arrPat = std::dynamic_pointer_cast<ArrayPattern>(pattern)) {
			// Array destructuring pattern
			auto arrPtr = std::get_if<std::shared_ptr<Array>>(&value);
			if (!arrPtr) {
				std::ostringstream oss;
				oss << "Cannot destructure non-array value (got " << typeOf(value) << ") in array pattern";
				throw std::runtime_error(oss.str());
			}
			auto& arr = **arrPtr;
			size_t i = 0;
			for (auto& elem : arrPat->elements) {
				if (i < arr.size()) {
					destructurePattern(elem, arr[i]);
				} else {
					// Use default value if provided
					destructurePattern(elem, Value{std::monostate{}});
				}
				i++;
			}
			// Handle rest element
			if (arrPat->hasRest) {
				auto restArr = std::make_shared<Array>();
				for (size_t j = i; j < arr.size(); j++) {
					restArr->push_back(arr[j]);
				}
				env->define(arrPat->restName, Value{restArr});
			}
		} else if (auto objPat = std::dynamic_pointer_cast<ObjectPattern>(pattern)) {
			// Object destructuring pattern
			auto objPtr = std::get_if<std::shared_ptr<Object>>(&value);
			if (!objPtr) {
				std::ostringstream oss;
				oss << "Cannot destructure non-object value (got " << typeOf(value) << ") in object pattern";
				throw std::runtime_error(oss.str());
			}
			auto& obj = **objPtr;
			std::unordered_set<std::string> extractedKeys;
			
			for (auto& prop : objPat->properties) {
				auto it = obj.find(prop.key);
				Value propValue = (it != obj.end()) ? it->second : Value{std::monostate{}};
				
				// Use default value if property doesn't exist
				if (std::holds_alternative<std::monostate>(propValue) && prop.defaultValue) {
					propValue = evaluate(prop.defaultValue);
				}
				
				destructurePattern(prop.pattern, propValue);
				extractedKeys.insert(prop.key);
			}
			
			// Handle rest element
			if (objPat->hasRest) {
				auto restObj = std::make_shared<Object>();
				for (auto& [key, val] : obj) {
					if (extractedKeys.find(key) == extractedKeys.end()) {
						(*restObj)[key] = val;
					}
				}
				env->define(objPat->restName, Value{restObj});
			}
		}
	}

	void installBuiltins() {
		stdRoot = std::make_shared<Object>();
		globals->define("std", Value{stdRoot});
		// Define 'undefined' as a global variable equivalent to null (monostate)
		globals->define("undefined", Value{std::monostate{}});
		packages["std"] = stdRoot;

		// Register all external packages
		// This includes: std.path, std.string, std.math, std.time, std.os, std.regex,
		// std.encoding, std.network, std.crypto, std.io (with fileSystem and fileStream),
		// std.builtin (global functions like len, push, typeof, eval, quote, sleep, Promise),
		// std.collections (Map, Set, Deque, Stack, PriorityQueue and utility functions),
		// json, xml, yaml, os, csv packages
		registerExternalPackages(*this);
	}
};

} // namespace asul

#endif // ASUL_INTERPRETER_H
