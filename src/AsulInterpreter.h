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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/wait.h>

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
					return Value{getNumber(l, "left of '*' ") * getNumber(r, "right of '*' ")};
				case TokenType::Slash: {
					double denom = getNumber(r, "right of '/' ");
					return Value{getNumber(l, "left of '/' ") / denom};
				}
				case TokenType::Percent: {
					double rv = getNumber(r, "right of '%' ");
					return Value{std::fmod(getNumber(l, "left of '%' "), rv)};
				}
				case TokenType::Greater: return Value{getNumber(l, ">") > getNumber(r, ">")};
				case TokenType::GreaterEqual: return Value{getNumber(l, ">=") >= getNumber(r, ">=")};
				case TokenType::Less: return Value{getNumber(l, "<") < getNumber(r, "<")};
				case TokenType::LessEqual: return Value{getNumber(l, "<=") <= getNumber(r, "<=")};
				case TokenType::EqualEqual: return Value{isJSEqual(l, r)};
				case TokenType::BangEqual: return Value{!isJSEqual(l, r)};
				case TokenType::StrictEqual: return Value{isStrictEqual(l, r)};
				case TokenType::StrictNotEqual: return Value{!isStrictEqual(l, r)};
				case TokenType::Ampersand: {
					long long lv = static_cast<long long>(getNumber(l, "& left"));
					long long rv = static_cast<long long>(getNumber(r, "& right"));
					return Value{ static_cast<double>(lv & rv) };
				}
				case TokenType::Pipe: {
					long long lv = static_cast<long long>(getNumber(l, "| left"));
					long long rv = static_cast<long long>(getNumber(r, "| right"));
					return Value{ static_cast<double>(lv | rv) };
				}
				case TokenType::Caret: {
					long long lv = static_cast<long long>(getNumber(l, "^ left"));
					long long rv = static_cast<long long>(getNumber(r, "^ right"));
					return Value{ static_cast<double>(lv ^ rv) };
				}
				case TokenType::ShiftLeft: {
					long long lv = static_cast<long long>(getNumber(l, "<< left"));
					long long rv = static_cast<long long>(getNumber(r, "<< right"));
					return Value{ static_cast<double>(lv << rv) };
				}
				case TokenType::ShiftRight: {
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
			if (lg->op.type == TokenType::OrOr) return isTruthy(l) ? l : evaluate(lg->right);
			else return !isTruthy(l) ? l : evaluate(lg->right);
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
			try {
				execute(tc->tryBlock);
			} catch (const ExceptionSignal& ex) {
				auto local = std::make_shared<Environment>(env);
				local->define(tc->catchName, ex.value);
				// 在新的局部环境中执行 catch 块
				if (auto block = std::dynamic_pointer_cast<BlockStmt>(tc->catchBlock)) {
					executeBlock(block->statements, local);
				} else {
					executeBlock(std::vector<StmtPtr>{ tc->catchBlock }, local);
				}
			} catch (const std::exception& ex) {
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
			return;
		}
		if (std::dynamic_pointer_cast<BreakStmt>(stmt)) { throw BreakSignal{}; }
		if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) { throw ContinueSignal{}; }
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

	void installBuiltins() {
		stdRoot = std::make_shared<Object>();
		globals->define("std", Value{stdRoot});
		// Define 'undefined' as a global variable equivalent to null (monostate)
		globals->define("undefined", Value{std::monostate{}});
		packages["std"] = stdRoot;

		// Register external packages (std.path, std.string, std.math, etc.)
		registerExternalPackages(*this);



		auto ioPkg = ensurePackage("std.io");
		// Register std.io.fileSystem as a package so it can be imported directly
		// It will also be available as std.io.fileSystem due to ensurePackage logic
		auto fsPkg = ensurePackage("std.io.fileSystem");

		// Encoding Package (std.encoding)
		auto encPkg = ensurePackage("std.encoding");
		
		// Base64
		auto base64Obj = std::make_shared<Object>();
		(*encPkg)["base64"] = Value{base64Obj};
		
		// base64.encode(str)
		auto b64enc = std::make_shared<Function>(); b64enc->isBuiltin = true;
		b64enc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("base64.encode expects string");
			std::string in = toString(args[0]);
			static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			int val = 0, valb = -6;
			for (unsigned char c : in) {
				val = (val << 8) + c;
				valb += 8;
				while (valb >= 0) {
					out.push_back(chars[(val >> valb) & 0x3F]);
					valb -= 6;
				}
			}
			if (valb > -6) out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
			while (out.size() % 4) out.push_back('=');
			return Value{out};
		};
		(*base64Obj)["encode"] = Value{b64enc};

		// base64.decode(str)
		auto b64dec = std::make_shared<Function>(); b64dec->isBuiltin = true;
		b64dec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("base64.decode expects string");
			std::string in = toString(args[0]);
			static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::vector<int> T(256, -1);
			for (int i=0; i<64; i++) T[chars[i]] = i;
			
			std::string out;
			int val = 0, valb = -8;
			for (unsigned char c : in) {
				if (T[c] == -1) break;
				val = (val << 6) + T[c];
				valb += 6;
				if (valb >= 0) {
					out.push_back(char((val >> valb) & 0xFF));
					valb -= 8;
				}
			}
			return Value{out};
		};
		(*base64Obj)["decode"] = Value{b64dec};

		// bytesToString(arr): convert array of numeric byte values to a string
		auto bytesToStringFn = std::make_shared<Function>(); bytesToStringFn->isBuiltin = true;
		bytesToStringFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("bytesToString expects 1 argument (array)");
			if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("bytesToString argument must be array");
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			if (!arr) return Value{std::string("")};
			std::string out;
			out.reserve(arr->size());
			for (auto &v : *arr) {
				double d = getNumber(v, "bytesToString element");
				unsigned char c = static_cast<unsigned char>(static_cast<int>(d));
				out.push_back(static_cast<char>(c));
			}
			return Value{out};
		};
		(*encPkg)["bytesToString"] = Value{bytesToStringFn};

		// Hex
		auto hexObj = std::make_shared<Object>();
		(*encPkg)["hex"] = Value{hexObj};
		
		// hex.encode(str)
		auto hexenc = std::make_shared<Function>(); hexenc->isBuiltin = true;
		hexenc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("hex.encode expects string");
			std::string in = toString(args[0]);
			std::ostringstream oss;
			for (unsigned char c : in) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
			return Value{oss.str()};
		};
		(*hexObj)["encode"] = Value{hexenc};
		
		// hex.decode(str)
		auto hexdec = std::make_shared<Function>(); hexdec->isBuiltin = true;
		hexdec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("hex.decode expects string");
			std::string in = toString(args[0]);
			if (in.size() % 2 != 0) throw std::runtime_error("Invalid hex string length");
			std::string out;
			for (size_t i=0; i<in.size(); i+=2) {
				std::string byteStr = in.substr(i, 2);
				char c = (char)strtol(byteStr.c_str(), nullptr, 16);
				out.push_back(c);
			}
			return Value{out};
		};
		(*hexObj)["decode"] = Value{hexdec};

		// URL
		auto urlObj = std::make_shared<Object>();
		(*encPkg)["url"] = Value{urlObj};
		
		// url.encode(str)
		auto urlenc = std::make_shared<Function>(); urlenc->isBuiltin = true;
		urlenc->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("url.encode expects string");
			std::string in = toString(args[0]);
			std::ostringstream oss;
			for (unsigned char c : in) {
				if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') oss << c;
				else oss << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
			}
			return Value{oss.str()};
		};
		(*urlObj)["encode"] = Value{urlenc};
		
		// url.decode(str)
		auto urldec = std::make_shared<Function>(); urldec->isBuiltin = true;
		urldec->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.empty()) throw std::runtime_error("url.decode expects string");
			std::string in = toString(args[0]);
			std::string out;
			for (size_t i=0; i<in.size(); ++i) {
				if (in[i] == '%') {
					if (i + 2 < in.size()) {
						std::string hex = in.substr(i+1, 2);
						char c = (char)strtol(hex.c_str(), nullptr, 16);
						out.push_back(c);
						i += 2;
					} else {
						out.push_back('%');
					}
				} else if (in[i] == '+') {
					out.push_back(' ');
				} else {
					out.push_back(in[i]);
				}
			}
			return Value{out};
		};
		(*urlObj)["decode"] = Value{urldec};

		// Network Package (std.network)
		registerLazyPackage("std.network", [this](std::shared_ptr<Object> netPkg) {
			
			// Socket Class
			auto socketClass = std::make_shared<ClassInfo>();
			socketClass->name = "Socket";
			socketClass->isNative = true;
			(*netPkg)["Socket"] = Value{socketClass};

			// constructor(domain, type)
			auto ctor = std::make_shared<Function>();
			ctor->isBuiltin = true;
			ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				int domain = AF_INET;
				int type = SOCK_STREAM;
				if (args.size() >= 1) {
					std::string d = toString(args[0]);
					if (d == "inet6") domain = AF_INET6;
				}
				if (args.size() >= 2) {
					std::string t = toString(args[1]);
					if (t == "udp") type = SOCK_DGRAM;
				}
				
				int fd = socket(domain, type, 0);
				if (fd < 0) throw std::runtime_error("socket creation failed");
				
				int opt = 1;
				setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				if (ext) {
					ext->nativeHandle = new int(fd);
					ext->nativeDestructor = [](void* p) {
						int* fdp = static_cast<int*>(p);
						close(*fdp);
						delete fdp;
					};
				}
				return Value{std::monostate{}};
			};
			socketClass->methods["constructor"] = ctor;

			// bind(host, port)
			auto bindFn = std::make_shared<Function>();
			bindFn->isBuiltin = true;
			bindFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				if (args.size() != 2) throw std::runtime_error("bind expects host and port");
				std::string host = toString(args[0]);
				int port = static_cast<int>(getNumber(args[1], "port"));
				
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				if (!ext || !ext->nativeHandle) throw std::runtime_error("Socket not initialized");
				int fd = *static_cast<int*>(ext->nativeHandle);

				struct sockaddr_in addr;
				std::memset(&addr, 0, sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_port = htons(port);
				if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
					throw std::runtime_error("Invalid address");
				}
				
				if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
					throw std::runtime_error("bind failed");
				}
				return Value{true};
			};
			socketClass->methods["bind"] = bindFn;

			// listen(backlog)
			auto listenFn = std::make_shared<Function>();
			listenFn->isBuiltin = true;
			listenFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				int backlog = 5;
				if (!args.empty()) backlog = static_cast<int>(getNumber(args[0], "backlog"));
				
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				int fd = *static_cast<int*>(ext->nativeHandle);
				
				if (listen(fd, backlog) < 0) throw std::runtime_error("listen failed");
				return Value{true};
			};
			socketClass->methods["listen"] = listenFn;

			// connect(host, port) -> Promise
			auto connectFn = std::make_shared<Function>();
			connectFn->isBuiltin = true;
			connectFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				if (args.size() != 2) throw std::runtime_error("connect expects host and port");
				std::string host = toString(args[0]);
				int port = static_cast<int>(getNumber(args[1], "port"));
				
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				int fd = *static_cast<int*>(ext->nativeHandle);

				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				
				std::thread([p, this, fd, host, port]{
					struct sockaddr_in addr;
					std::memset(&addr, 0, sizeof(addr));
					addr.sin_family = AF_INET;
					addr.sin_port = htons(port);
					
					struct hostent* server = gethostbyname(host.c_str());
					if (server == NULL) {
						settlePromise(p, true, Value{std::string("Host resolution failed")});
						return;
					}
					std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

					if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
						settlePromise(p, true, Value{std::string("Connection failed")});
					} else {
						settlePromise(p, false, Value{true});
					}
				}).detach();
				
				return Value{p};
			};
			socketClass->methods["connect"] = connectFn;

			// accept() -> Promise<Socket>
			auto acceptFn = std::make_shared<Function>();
			acceptFn->isBuiltin = true;
			acceptFn->builtin = [this, socketClass](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				int fd = *static_cast<int*>(ext->nativeHandle);

				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;

				std::thread([p, this, fd, socketClass]{
					struct sockaddr_in cli_addr;
					socklen_t clilen = sizeof(cli_addr);
					int newsockfd = accept(fd, (struct sockaddr*)&cli_addr, &clilen);
					if (newsockfd < 0) {
						settlePromise(p, true, Value{std::string("accept failed")});
						return;
					}
					
					auto newInst = std::make_shared<InstanceExt>();
					newInst->klass = socketClass;
					newInst->nativeHandle = new int(newsockfd);
					newInst->nativeDestructor = [](void* p) {
						int* fdp = static_cast<int*>(p);
						close(*fdp);
						delete fdp;
					};
					
					settlePromise(p, false, Value{std::shared_ptr<Instance>(newInst)});
				}).detach();

				return Value{p};
			};
			socketClass->methods["accept"] = acceptFn;

			// write(data) -> Promise
			auto writeFn = std::make_shared<Function>();
			writeFn->isBuiltin = true;
			writeFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				if (args.empty()) throw std::runtime_error("write expects data");
				std::string data = toString(args[0]);
				
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				int fd = *static_cast<int*>(ext->nativeHandle);

				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;

				std::thread([p, this, fd, data]{
					ssize_t n = write(fd, data.c_str(), data.length());
					if (n < 0) {
						settlePromise(p, true, Value{std::string("write failed")});
					} else {
						settlePromise(p, false, Value{static_cast<double>(n)});
					}
				}).detach();

				return Value{p};
			};
			socketClass->methods["write"] = writeFn;

			// read(size) -> Promise<string>
			auto readFn = std::make_shared<Function>();
			readFn->isBuiltin = true;
			readFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				int size = 1024;
				if (!args.empty()) size = static_cast<int>(getNumber(args[0], "size"));
				
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				int fd = *static_cast<int*>(ext->nativeHandle);

				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;

				std::thread([p, this, fd, size]{
					std::vector<char> buf(size);
					ssize_t n = read(fd, buf.data(), size);
					if (n < 0) {
						settlePromise(p, true, Value{std::string("read failed")});
					} else if (n == 0) {
						settlePromise(p, false, Value{std::string("")});
					} else {
						settlePromise(p, false, Value{std::string(buf.data(), n)});
					}
				}).detach();

				return Value{p};
			};
			socketClass->methods["read"] = readFn;

			// close()
			auto closeFn = std::make_shared<Function>();
			closeFn->isBuiltin = true;
			closeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure) -> Value {
				Value thisVal = closure->get("this");
				auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				auto ext = std::dynamic_pointer_cast<InstanceExt>(inst);
				if (ext && ext->nativeHandle) {
					int* fdp = static_cast<int*>(ext->nativeHandle);
					close(*fdp);
					delete fdp;
					ext->nativeHandle = nullptr;
				}
				return Value{true};
			};
			socketClass->methods["close"] = closeFn;

			// URL class: new URL(str) -> fields: protocol, host, port, path, query
			{
				auto urlClass = std::make_shared<ClassInfo>();
				urlClass->name = "URL";
				auto ctor = std::make_shared<Function>();
				ctor->isBuiltin = true;
				ctor->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
					if (args.size() != 1) throw std::runtime_error("URL constructor expects 1 argument (string)");
					std::string u = toString(args[0]);
					std::string protocol; std::string host; int port = -1; std::string path = "/"; std::string query;
					size_t schemePos = u.find("://");
					if (schemePos != std::string::npos) { protocol = u.substr(0, schemePos); }
					size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
					size_t pathStart = u.find('/', hostStart);
					size_t qmark = std::string::npos;
					if (pathStart == std::string::npos) { pathStart = u.size(); }
					// host[:port]
					{
						size_t hpEnd = pathStart;
						size_t colon = u.find(':', hostStart);
						if (colon != std::string::npos && colon < hpEnd) {
							host = u.substr(hostStart, colon - hostStart);
							std::string pstr = u.substr(colon + 1, hpEnd - (colon + 1));
							try { port = std::stoi(pstr); } catch (...) { port = -1; }
						} else {
							host = u.substr(hostStart, hpEnd - hostStart);
						}
					}
					// path?query
					if (pathStart < u.size()) {
						qmark = u.find('?', pathStart);
						if (qmark == std::string::npos) { path = u.substr(pathStart); }
						else { path = u.substr(pathStart, qmark - pathStart); query = u.substr(qmark + 1); }
					}
					if (port < 0) { if (protocol == "http") port = 80; else if (protocol == "https") port = 443; }
					Value thisVal = closure->get("this");
					auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
					inst->fields["protocol"] = Value{ protocol };
					inst->fields["host"] = Value{ host };
					inst->fields["port"] = Value{ static_cast<double>(port) };
					inst->fields["path"] = Value{ path };
					inst->fields["query"] = Value{ query };
					return Value{ std::monostate{} };
				};
				urlClass->methods["constructor"] = ctor;
				(*netPkg)["URL"] = Value{ urlClass };
			}

			// fetch(url[, options]) -> Promise<Response-like>
			{
				auto fetchFn = std::make_shared<Function>();
				fetchFn->isBuiltin = true;
				fetchFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
					if (args.empty()) throw std::runtime_error("fetch expects at least 1 argument (url)");
					std::string url = toString(args[0]);
					std::string method = "GET";
					std::shared_ptr<Object> hdrObj;
					std::string body;
					if (args.size() >= 2) {
						if (!std::holds_alternative<std::shared_ptr<Object>>(args[1])) throw std::runtime_error("fetch options must be object");
						auto opt = std::get<std::shared_ptr<Object>>(args[1]);
						auto itM = opt->find("method"); if (itM != opt->end()) method = toString(itM->second);
						auto itH = opt->find("headers"); if (itH != opt->end() && std::holds_alternative<std::shared_ptr<Object>>(itH->second)) hdrObj = std::get<std::shared_ptr<Object>>(itH->second);
						auto itB = opt->find("body"); if (itB != opt->end()) body = toString(itB->second);
					}
					// Promise
					auto p = std::make_shared<PromiseState>(); p->loopPtr = this;
					std::thread([this, p, url, method, hdrObj, body]{
						try {
							// Parse URL
							std::string proto = "http"; std::string host; int port = 80; std::string path = "/";
							size_t schemePos = url.find("://"); if (schemePos != std::string::npos) proto = url.substr(0, schemePos);
							size_t hostStart = (schemePos == std::string::npos) ? 0 : (schemePos + 3);
							size_t pathStart = url.find('/', hostStart); if (pathStart == std::string::npos) pathStart = url.size();
							size_t colon = url.find(':', hostStart);
							if (colon != std::string::npos && colon < pathStart) { host = url.substr(hostStart, colon - hostStart); port = std::stoi(url.substr(colon + 1, pathStart - colon - 1)); }
							else { host = url.substr(hostStart, pathStart - hostStart); }
							if (pathStart < url.size()) path = url.substr(pathStart);
							if (proto == "https") { settlePromise(p, true, Value{ std::string("HTTPS not supported") }); return; }
							// DNS
							struct hostent* server = gethostbyname(host.c_str());
							if (server == NULL) { settlePromise(p, true, Value{ std::string("No such host: ")+host }); return; }
							int sockfd = socket(AF_INET, SOCK_STREAM, 0); if (sockfd < 0) { settlePromise(p, true, Value{ std::string("socket failed") }); return; }
							struct sockaddr_in serv_addr; std::memset(&serv_addr, 0, sizeof(serv_addr));
							serv_addr.sin_family = AF_INET; std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length); serv_addr.sin_port = htons(port);
							if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) { close(sockfd); settlePromise(p, true, Value{ std::string("connect failed") }); return; }
							std::ostringstream req;
							req << method << " " << path << " HTTP/1.1\r\n";
							req << "Host: " << host << "\r\n";
							req << "Connection: close\r\n";
							req << "User-Agent: ALang/1.0\r\n";
							if (hdrObj) { for (auto& kv : *hdrObj) { req << kv.first << ": " << toString(kv.second) << "\r\n"; } }
							if (!body.empty()) { req << "Content-Length: " << body.size() << "\r\n"; }
							req << "\r\n"; if (!body.empty()) req << body;
							std::string rs = req.str(); if (write(sockfd, rs.c_str(), rs.size()) < 0) { close(sockfd); settlePromise(p, true, Value{ std::string("write failed") }); return; }
							std::string response; char buf[4096]; int n; while ((n = read(sockfd, buf, sizeof(buf))) > 0) response.append(buf, n); close(sockfd);
							auto respObj = std::make_shared<Object>();
							size_t headerEnd = response.find("\r\n\r\n"); std::string headers, bodyStr; double status = 0.0;
							if (headerEnd != std::string::npos) { headers = response.substr(0, headerEnd); bodyStr = response.substr(headerEnd + 4);
								size_t sp1 = headers.find(' '); size_t sp2 = headers.find(' ', sp1 + 1);
								if (sp1 != std::string::npos && sp2 != std::string::npos) { try { status = std::stod(headers.substr(sp1 + 1, sp2 - sp1 - 1)); } catch (...) {} }
							}
							(*respObj)["status"] = Value{ status };
							(*respObj)["headers"] = Value{ headers };
							// text(): Promise<string>
							{
								auto textFn = std::make_shared<Function>(); textFn->isBuiltin = true;
								std::string copy = bodyStr;
								textFn->builtin = [this, copy](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
									auto tp = std::make_shared<PromiseState>(); tp->loopPtr = this; settlePromise(tp, false, Value{ copy }); return Value{ tp };
								};
								(*respObj)["text"] = Value{ textFn };
							}
							// json(): Promise<any>
							{
								auto jsonFn = std::make_shared<Function>(); jsonFn->isBuiltin = true; std::string copy = bodyStr;
								jsonFn->builtin = [this, copy](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
									auto tp = std::make_shared<PromiseState>(); tp->loopPtr = this;
									postTask([this, tp, copy]{
										try {
											auto jsonPkg = ensurePackage("json");
											Value parseV = (*jsonPkg)["parse"]; if (!std::holds_alternative<std::shared_ptr<Function>>(parseV)) { settlePromise(tp, true, Value{ std::string("json.parse not found") }); return; }
											auto parseFn = std::get<std::shared_ptr<Function>>(parseV);
											Value res = parseFn->builtin({ Value{ copy } }, parseFn->closure);
											settlePromise(tp, false, res);
										} catch (const std::exception& ex) {
											settlePromise(tp, true, Value{ std::string(ex.what()) });
										}
									});
									return Value{ tp };
								};
								(*respObj)["json"] = Value{ jsonFn };
							}
							settlePromise(p, false, Value{ respObj });
						} catch (const std::exception& ex) {
							settlePromise(p, true, Value{ std::string(ex.what()) });
						}
					}).detach();
					return Value{ p };
				};
				(*netPkg)["fetch"] = Value{ fetchFn };
			}

			// std.network.http.Server
			{
				auto httpObj = std::make_shared<Object>();
				(*netPkg)["http"] = Value{ httpObj };
				auto serverClass = std::make_shared<ClassInfo>(); serverClass->name = "Server"; serverClass->isNative = true; (*httpObj)["Server"] = Value{ serverClass };
				// constructor(): no-op
				auto ctor = std::make_shared<Function>(); ctor->isBuiltin = true; ctor->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { return Value{ std::monostate{} }; }; serverClass->methods["constructor"] = ctor;
				// listen(port, callback)
				auto listenFn = std::make_shared<Function>(); listenFn->isBuiltin = true;
				listenFn->builtin = [this, serverClass](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
					if (args.size() != 2) throw std::runtime_error("listen expects (port, callback)");
					int port = static_cast<int>(getNumber(args[0], "port"));
					if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) throw std::runtime_error("callback must be function");
					auto cb = std::get<std::shared_ptr<Function>>(args[1]);
					int sfd = socket(AF_INET, SOCK_STREAM, 0); if (sfd < 0) throw std::runtime_error("socket failed");
					int opt=1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
					struct sockaddr_in addr; std::memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(port);
					if (bind(sfd,(struct sockaddr*)&addr,sizeof(addr))<0){ close(sfd); throw std::runtime_error("bind failed"); }
					if (listen(sfd, 16)<0){ close(sfd); throw std::runtime_error("listen failed"); }
					// store on instance to keep fd lifetime
					Value thisVal = closure->get("this"); auto inst = std::dynamic_pointer_cast<InstanceExt>(std::get<std::shared_ptr<Instance>>(thisVal));
					if (inst) {
						inst->nativeHandle = new int(sfd);
						inst->nativeDestructor = [](void* p){ if(!p) return; int* fd=(int*)p; if(*fd>=0) close(*fd); delete fd; };
					}
					std::thread([this, cb, sfd]{
						for(;;){
							struct sockaddr_in cli; socklen_t cl = sizeof(cli);
							int cfd = accept(sfd,(struct sockaddr*)&cli,&cl);
							if (cfd < 0) break;
							std::thread([this, cb, cfd]{
								char buf[8192]; ssize_t n = read(cfd, buf, sizeof(buf)); if (n <= 0) { close(cfd); return; }
								std::string reqStr(buf, n);
								postTask([this, cb, cfd, reqStr]{
									// Build req
									auto req = std::make_shared<Object>();
									std::string method="GET", url="/", body=""; auto headersObj = std::make_shared<Object>();
									size_t lineEnd = reqStr.find("\r\n");
									if (lineEnd != std::string::npos) {
										std::string line = reqStr.substr(0, lineEnd);
										size_t s1 = line.find(' '); size_t s2 = (s1==std::string::npos)?std::string::npos:line.find(' ', s1+1);
										if (s1 != std::string::npos) method = line.substr(0, s1);
										if (s2 != std::string::npos) url = line.substr(s1+1, s2-s1-1);
										// headers
										size_t hStart = lineEnd + 2; size_t hEnd = reqStr.find("\r\n\r\n", hStart);
										if (hEnd != std::string::npos) {
											std::string hs = reqStr.substr(hStart, hEnd - hStart);
											std::istringstream iss(hs); std::string hline;
											while (std::getline(iss, hline)) {
												if (!hline.empty() && hline.back()=='\r') hline.pop_back();
												size_t col = hline.find(':');
												if (col != std::string::npos) {
													std::string k = hline.substr(0,col); std::string v = hline.substr(col+1);
													// trim leading space in v
													size_t p=0; while(p<v.size() && (v[p]==' '||v[p]=='\t')) p++; v = v.substr(p);
													(*headersObj)[k] = Value{ v };
												}
											}
											body = reqStr.substr(hEnd + 4);
										}
									}
									(*req)["method"] = Value{ method }; (*req)["url"] = Value{ url }; (*req)["headers"] = Value{ headersObj }; (*req)["body"] = Value{ body };

									// Build res
									auto res = std::make_shared<Object>();
									auto sent = std::make_shared<bool>(false);
									// writeHead(code, headers)
									auto writeHeadFn = std::make_shared<Function>(); writeHeadFn->isBuiltin = true;
									writeHeadFn->builtin = [cfd, sent](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
										int code = 200; if (!args.empty()) code = static_cast<int>(getNumber(args[0], "code"));
												std::ostringstream oss; oss << "HTTP/1.1 " << code << " OK\r\n";
												if (args.size() >= 2 && std::holds_alternative<std::shared_ptr<Object>>(args[1])) {
													auto h = std::get<std::shared_ptr<Object>>(args[1]); for (auto& kv : *h) { oss << kv.first << ": " << toString(kv.second) << "\r\n"; }
												}
												oss << "\r\n"; std::string hs = oss.str();
												auto sendAll = [cfd](const char* buf, size_t len)->int{ size_t off=0; while(off<len){ ssize_t w=::write(cfd, buf+off, len-off); if(w<0){ if(errno==EINTR) continue; return errno; } off += (size_t)w; } return 0; };
												int serr = sendAll(hs.c_str(), hs.size());
												if (serr != 0) {
													std::cerr << "[HTTP] send error (writeHead) fd=" << cfd << ": " << std::strerror(serr) << " (" << serr << ")\n";
													auto errObj = std::make_shared<Object>(); (*errObj)["type"] = Value{ std::string("Error") }; (*errObj)["message"] = Value{ std::string(std::strerror(serr)) }; (*errObj)["errno"] = Value{ static_cast<double>(serr) };
													return Value{ errObj };
												}
												*sent = true; return Value{ std::monostate{} };
									}; (*res)["writeHead"] = Value{ writeHeadFn };
									// write(data)
											auto writeFn2 = std::make_shared<Function>(); writeFn2->isBuiltin = true; writeFn2->builtin = [cfd](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
												if (!args.empty()) {
													std::string s = toString(args[0]);
													auto sendAll = [cfd](const char* buf, size_t len)->int{ size_t off=0; while(off<len){ ssize_t w=::write(cfd, buf+off, len-off); if(w<0){ if(errno==EINTR) continue; return errno; } off += (size_t)w; } return 0; };
													int serr = sendAll(s.c_str(), s.size());
													if (serr != 0) {
														std::cerr << "[HTTP] send error (write) fd=" << cfd << ": " << std::strerror(serr) << " (" << serr << ")\n";
														auto errObj = std::make_shared<Object>(); (*errObj)["type"] = Value{ std::string("Error") }; (*errObj)["message"] = Value{ std::string(std::strerror(serr)) }; (*errObj)["errno"] = Value{ static_cast<double>(serr) };
														return Value{ errObj };
													}
												}
												return Value{ std::monostate{} };
											}; (*res)["write"] = Value{ writeFn2 };
									// end([data])
									auto endFn = std::make_shared<Function>(); endFn->isBuiltin = true;
									endFn->builtin = [cfd, sent](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
										std::string s; if (!args.empty()) s = toString(args[0]);
												auto sendAll = [cfd](const char* buf, size_t len)->int{ size_t off=0; while(off<len){ ssize_t w=::write(cfd, buf+off, len-off); if(w<0){ if(errno==EINTR) continue; return errno; } off += (size_t)w; } return 0; };
												if (!*sent) {
													std::ostringstream oss; oss << "HTTP/1.1 200 OK\r\n"; oss << "Content-Length: " << s.size() << "\r\n"; oss << "Connection: close\r\n\r\n"; std::string h = oss.str(); int serr = sendAll(h.c_str(), h.size()); if (serr != 0) {
														std::cerr << "[HTTP] send error (end writeHead) fd=" << cfd << ": " << std::strerror(serr) << " (" << serr << ")\n";
														auto errObj = std::make_shared<Object>(); (*errObj)["type"] = Value{ std::string("Error") }; (*errObj)["message"] = Value{ std::string(std::strerror(serr)) }; (*errObj)["errno"] = Value{ static_cast<double>(serr) };
														// attempt shutdown then close
														shutdown(cfd, SHUT_WR);
														close(cfd);
														return Value{ errObj };
													}
													*sent = true;
												}
												if (!s.empty()) { int serr2 = sendAll(s.c_str(), s.size()); if (serr2 != 0) {
													std::cerr << "[HTTP] send error (end body) fd=" << cfd << ": " << std::strerror(serr2) << " (" << serr2 << ")\n";
													auto errObj = std::make_shared<Object>(); (*errObj)["type"] = Value{ std::string("Error") }; (*errObj)["message"] = Value{ std::string(std::strerror(serr2)) }; (*errObj)["errno"] = Value{ static_cast<double>(serr2) };
													shutdown(cfd, SHUT_WR);
													close(cfd);
													return Value{ errObj };
												} }
												// graceful shutdown of write side before close
												shutdown(cfd, SHUT_WR);
												close(cfd);
												return Value{ std::monostate{} };
									}; (*res)["end"] = Value{ endFn };

									std::vector<Value> cargs{ Value{ req }, Value{ res } };
									try {
										if (cb->isBuiltin) cb->builtin(cargs, cb->closure);
										else { auto local = std::make_shared<Environment>(cb->closure); if(cb->params.size()>0) local->define(cb->params[0], cargs[0]); if(cb->params.size()>1) local->define(cb->params[1], cargs[1]); executeBlock(cb->body, local); }
									} catch (...) { /* ignore user errors */ }
								});
							}).detach();
						}
					}).detach();
					return Value{ std::monostate{} };
				};
				serverClass->methods["listen"] = listenFn;
			}

		// Helper for HTTP requests (Simple blocking implementation)
		auto httpRequest = [](const std::string& method, const std::string& url, const std::string& data = "") -> Value {
			// 1. Parse URL
			std::string host;
			int port = 80;
			std::string path = "/";
			
			std::string protocol = "http://";
			if (url.substr(0, 7) != protocol) throw std::runtime_error("Only http:// supported currently");
			
			size_t hostStart = 7;
			size_t pathStart = url.find('/', hostStart);
			size_t portStart = url.find(':', hostStart);
			
			if (pathStart == std::string::npos) {
				if (portStart == std::string::npos) {
					host = url.substr(hostStart);
				} else {
					host = url.substr(hostStart, portStart - hostStart);
					port = std::stoi(url.substr(portStart + 1));
				}
			} else {
				if (portStart != std::string::npos && portStart < pathStart) {
					host = url.substr(hostStart, portStart - hostStart);
					port = std::stoi(url.substr(portStart + 1, pathStart - portStart - 1));
				} else {
					host = url.substr(hostStart, pathStart - hostStart);
				}
				path = url.substr(pathStart);
			}

			// 2. Resolve Host
			struct hostent* server = gethostbyname(host.c_str());
			if (server == NULL) throw std::runtime_error("No such host: " + host);

			// 3. Create Socket
			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd < 0) throw std::runtime_error("Error opening socket");

			struct sockaddr_in serv_addr;
			std::memset(&serv_addr, 0, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET;
			std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
			serv_addr.sin_port = htons(port);

			// 4. Connect
			if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
				close(sockfd);
				throw std::runtime_error("Error connecting to " + host);
			}

			// 5. Send Request
			std::ostringstream req;
			req << method << " " << path << " HTTP/1.1\r\n";
			req << "Host: " << host << "\r\n";
			req << "Connection: close\r\n";
			req << "User-Agent: ALang/1.0\r\n";
			if (!data.empty()) {
				req << "Content-Length: " << data.length() << "\r\n";
				req << "Content-Type: application/x-www-form-urlencoded\r\n";
			}
			req << "\r\n";
			if (!data.empty()) req << data;

			std::string reqStr = req.str();
			if (write(sockfd, reqStr.c_str(), reqStr.length()) < 0) {
				close(sockfd);
				throw std::runtime_error("Error writing to socket");
			}

			// 6. Read Response
			std::string response;
			char buffer[4096];
			int n;
			while ((n = read(sockfd, buffer, sizeof(buffer))) > 0) {
				response.append(buffer, n);
			}
			close(sockfd);

			// 7. Parse Response
			auto obj = std::make_shared<Object>();
			
			size_t headerEnd = response.find("\r\n\r\n");
			if (headerEnd != std::string::npos) {
				std::string headers = response.substr(0, headerEnd);
				std::string body = response.substr(headerEnd + 4);
				(*obj)["body"] = Value{body};
				(*obj)["headers"] = Value{headers};
				
				// Parse status code (e.g., "HTTP/1.1 200 OK")
				size_t firstSpace = headers.find(' ');
				size_t secondSpace = headers.find(' ', firstSpace + 1);
				if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
					std::string statusStr = headers.substr(firstSpace + 1, secondSpace - firstSpace - 1);
					try {
						(*obj)["status"] = Value{std::stod(statusStr)};
					} catch(...) {
						(*obj)["status"] = Value{0.0};
					}
				} else {
					(*obj)["status"] = Value{0.0};
				}
			} else {
				 (*obj)["body"] = Value{response};
				 (*obj)["status"] = Value{0.0};
				 (*obj)["headers"] = Value{std::string("")};
			}

			return Value{obj};
		};

		auto getFn = std::make_shared<Function>();
		getFn->isBuiltin = true;
		getFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 1) throw std::runtime_error("http.get expects 1 argument (url)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("GET", std::get<std::string>(args[0]), "");
		};
		(*netPkg)["get"] = Value{getFn};

		auto postFn = std::make_shared<Function>();
		postFn->isBuiltin = true;
		postFn->builtin = [httpRequest](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() != 2) throw std::runtime_error("http.post expects 2 arguments (url, data)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("url must be string");
			return httpRequest("POST", std::get<std::string>(args[0]), toString(args[1]));
		};
		(*netPkg)["post"] = Value{postFn};
		});

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
			if (args.size() != 1) throw std::runtime_error("readFile expects 1 argument (path string)");
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
			if (args.size() != 2) throw std::runtime_error("writeFile expects 2 arguments (path, data)");
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
			if (args.size() != 2) throw std::runtime_error("appendFile expects 2 arguments (path, data)");
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
				// store path in this.fields["path"]
				Value thisVal = closure->get("this");
				if (!std::holds_alternative<std::shared_ptr<Instance>>(thisVal)) throw std::runtime_error("this is not instance in File.constructor");
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
			auto openM = std::make_shared<Function>(); openM->isBuiltin = true; openM->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment> closure)->Value {
				if (args.size() != 1) throw std::runtime_error("File.open expects 1 argument (mode)");
				if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("File.open mode must be string");
				std::string mode = std::get<std::string>(args[0]);
				Value thisVal = closure->get("this"); auto inst = std::get<std::shared_ptr<Instance>>(thisVal);
				std::string path = std::get<std::string>(inst->fields["path"]);
				
				auto ioPkgLocal = ensurePackage("std.io");
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
				if (args.size() != 1) throw std::runtime_error("FileStream.write expects 1 argument");
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
			auto ioPkg = ensurePackage("std.io");
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
			auto walkFn = std::make_shared<Function>(); walkFn->isBuiltin=true; walkFn->builtin=[this](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
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
						try { executeBlock(cb->body, local); } catch(const ReturnSignal& rs) { res = rs.value; }
					}
					if(std::holds_alternative<bool>(res) && std::get<bool>(res)==false) break;
				}
				return Value{std::monostate{}};
			};
			(*fsPkg)["walk"] = Value{walkFn};
		}
		importPackageSymbols("std.io");

		// ===== Date & Time (std.time) =====




		// 注意: 不将 json 自动导入到全局，使用方可通过 `import json.*;` 或 `json.<name>` 访问

		// 注意：不要把 json 自动注册到 `std.` 根下，保持顶层包 `json` 独立。
		// (之前为兼容性添加的 std.json 别名已移除，以符合不将所有包放到 std. 的理念)

		// len(x): string/array/object长度
		auto lenFn = std::make_shared<Function>();
		lenFn->isBuiltin = true;
		lenFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("len expects 1 argument");
			const Value& v = args[0];
			if (auto s = std::get_if<std::string>(&v)) return Value{ static_cast<double>(s->size()) };
			if (auto a = std::get_if<std::shared_ptr<Array>>(&v)) return Value{ static_cast<double>((*a) ? (*a)->size() : 0) };
			if (auto o = std::get_if<std::shared_ptr<Object>>(&v)) return Value{ static_cast<double>((*o) ? (*o)->size() : 0) };
			if (std::holds_alternative<std::monostate>(v)) return Value{ 0.0 };
			throw std::runtime_error("len: unsupported type: " + typeOf(v));
		};
		globals->define("len", lenFn);

		// performance.now()
		auto perfObj = std::make_shared<Object>();
		auto nowFn = std::make_shared<Function>(); nowFn->isBuiltin = true;
		nowFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
			using namespace std::chrono;
			static auto start = high_resolution_clock::now();
			auto now = high_resolution_clock::now();
			duration<double, std::milli> ms = now - start;
			return Value{ ms.count() };
		};
		(*perfObj)["now"] = Value{nowFn};
		globals->define("performance", Value{perfObj});

		// quote(str): 返回一个对象 { tokens: [...], source: string, apply: function() }
		auto quoteFn = std::make_shared<Function>();
		quoteFn->isBuiltin = true;
		quoteFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("quote expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("quote expects a string");
			std::string src = std::get<std::string>(args[0]);
			Lexer lx(src);
			auto toks = lx.scanTokens();
			auto arr = std::make_shared<Array>();
			for (auto &t : toks) {
				if (t.type == TokenType::EndOfFile) continue;
				auto obj = std::make_shared<Object>();
				std::string tname;
				switch (t.type) {
					case TokenType::LeftParen: tname = "LeftParen"; break;
					case TokenType::RightParen: tname = "RightParen"; break;
					case TokenType::LeftBrace: tname = "LeftBrace"; break;
					case TokenType::RightBrace: tname = "RightBrace"; break;
					case TokenType::LeftBracket: tname = "LeftBracket"; break;
					case TokenType::RightBracket: tname = "RightBracket"; break;
					case TokenType::Comma: tname = "Comma"; break;
					case TokenType::Semicolon: tname = "Semicolon"; break;
					case TokenType::Colon: tname = "Colon"; break;
					case TokenType::Dot: tname = "Dot"; break;
					case TokenType::Ellipsis: tname = "Ellipsis"; break;
					case TokenType::Plus: tname = "Plus"; break;
					case TokenType::Minus: tname = "Minus"; break;
					case TokenType::Star: tname = "Star"; break;
					case TokenType::Slash: tname = "Slash"; break;
					case TokenType::Percent: tname = "Percent"; break;
					case TokenType::Ampersand: tname = "Ampersand"; break;
					case TokenType::Pipe: tname = "Pipe"; break;
					case TokenType::Caret: tname = "Caret"; break;
					case TokenType::ShiftLeft: tname = "ShiftLeft"; break;
					case TokenType::ShiftRight: tname = "ShiftRight"; break;
					case TokenType::Tilde: tname = "Tilde"; break;
					case TokenType::MatchInterface: tname = "MatchInterface"; break;
					case TokenType::Bang: tname = "Bang"; break;
					case TokenType::Equal: tname = "Equal"; break;
					case TokenType::Less: tname = "Less"; break;
					case TokenType::Greater: tname = "Greater"; break;
					case TokenType::BangEqual: tname = "BangEqual"; break;
					case TokenType::EqualEqual: tname = "EqualEqual"; break;
					case TokenType::StrictEqual: tname = "StrictEqual"; break;
					case TokenType::StrictNotEqual: tname = "StrictNotEqual"; break;
					case TokenType::LessEqual: tname = "LessEqual"; break;
					case TokenType::GreaterEqual: tname = "GreaterEqual"; break;
					case TokenType::LeftArrow: tname = "LeftArrow"; break;
					case TokenType::Arrow: tname = "Arrow"; break;
					case TokenType::AndAnd: tname = "AndAnd"; break;
					case TokenType::OrOr: tname = "OrOr"; break;
					case TokenType::Identifier: tname = "Identifier"; break;
					case TokenType::String: tname = "String"; break;
					case TokenType::Number: tname = "Number"; break;
					case TokenType::Let: tname = "Let"; break;
					case TokenType::Var: tname = "Var"; break;
					case TokenType::Const: tname = "Const"; break;
					case TokenType::Function: tname = "Function"; break;
					case TokenType::Return: tname = "Return"; break;
					case TokenType::If: tname = "If"; break;
					case TokenType::Else: tname = "Else"; break;
					case TokenType::While: tname = "While"; break;
					case TokenType::For: tname = "For"; break;
					case TokenType::Break: tname = "Break"; break;
					case TokenType::Continue: tname = "Continue"; break;
					case TokenType::Class: tname = "Class"; break;
					case TokenType::Extends: tname = "Extends"; break;
					case TokenType::New: tname = "New"; break;
					case TokenType::True: tname = "True"; break;
					case TokenType::False: tname = "False"; break;
					case TokenType::Null: tname = "Null"; break;
					case TokenType::Await: tname = "Await"; break;
					case TokenType::Async: tname = "Async"; break;
					case TokenType::Go: tname = "Go"; break;
					case TokenType::Try: tname = "Try"; break;
					case TokenType::Catch: tname = "Catch"; break;
					case TokenType::Throw: tname = "Throw"; break;
					case TokenType::Interface: tname = "Interface"; break;
					case TokenType::Import: tname = "Import"; break;
					case TokenType::From: tname = "From"; break;
					default: tname = "Unknown"; break;
				}
				(*obj)["token"] = Value{ tname };
				(*obj)["lexeme"] = Value{ t.lexeme };
				(*obj)["line"] = Value{ static_cast<double>(t.line) };
				(*obj)["column"] = Value{ static_cast<double>(t.column) };
				(*obj)["length"] = Value{ static_cast<double>(t.length) };
				arr->push_back(Value{ obj });
			}
			// 构建带有 apply() 的容器对象
			auto qobj = std::make_shared<Object>();
			(*qobj)["tokens"] = Value{ arr };
			(*qobj)["source"] = Value{ src };
			// apply(): 基于当前 tokens 重新拼接代码并执行（eval），返回其值
			{
				auto self = qobj; // 捕获对象以读取被用户修改后的 tokens
				auto applyFn = std::make_shared<Function>();
				applyFn->isBuiltin = true;
				applyFn->builtin = [this, self](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
					// 读取 tokens 数组
					auto it = self->find("tokens");
					if (it == self->end() || !std::holds_alternative<std::shared_ptr<Array>>(it->second))
						throw std::runtime_error("quote.apply: missing tokens array");
					auto arrPtr = std::get<std::shared_ptr<Array>>(it->second);
					if (!arrPtr) return Value{std::monostate{}};
					// 将 tokens 重建为源码
					auto escapeString = [](const std::string& s){
						std::string out; out.reserve(s.size()+2);
						for (char c: s) {
							switch (c) {
								case '\\': out += "\\\\"; break;
								case '"': out += "\\\""; break;
								case '\n': out += "\\n"; break;
								case '\t': out += "\\t"; break;
								case '\r': out += "\\r"; break;
								case '\0': out += "\\0"; break;
								default: out.push_back(c); break;
							}
						}
						return std::string("\"") + out + std::string("\"");
					};
					std::string code;
					bool first = true;
					for (const auto& v : *arrPtr) {
						if (!std::holds_alternative<std::shared_ptr<Object>>(v)) continue;
						auto tobj = std::get<std::shared_ptr<Object>>(v);
						if (!tobj) continue;
						std::string tname, lex;
						auto itName = tobj->find("token");
						if (itName != tobj->end() && std::holds_alternative<std::string>(itName->second)) tname = std::get<std::string>(itName->second);
						auto itLex = tobj->find("lexeme");
						if (itLex != tobj->end() && std::holds_alternative<std::string>(itLex->second)) lex = std::get<std::string>(itLex->second);
						std::string piece;
						if (tname == "String") piece = escapeString(lex);
						else piece = lex;
						if (!first) code.push_back(' ');
						first = false;
						code += piece;
					}
					// 通过内置 eval 处理，复用其单/多语句与返回值规则
					return callFunction("eval", std::vector<Value>{ Value{code} });
				};
				(*qobj)["apply"] = Value{ applyFn };
			}
			return Value{ qobj };
		};
		globals->define("quote", quoteFn);

		// push(arr, ...values): 追加元素，返回新长度
		auto pushFn = std::make_shared<Function>();
		pushFn->isBuiltin = true;
		pushFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.empty()) throw std::runtime_error("push expects at least 1 argument");
			const Value& target = args[0];
			auto parr = std::get_if<std::shared_ptr<Array>>(&target);
			if (!parr || !(*parr)) throw std::runtime_error("push: first argument must be array");
			auto& vec = **parr;
			for (size_t i=1;i<args.size();++i) vec.push_back(args[i]);
			return Value{ static_cast<double>(vec.size()) };
		};
		globals->define("push", pushFn);

		// typeof(x): return a type-name string for x; useful in type expressions e.g. : typeof(b.type())
		auto typeFn = std::make_shared<Function>();
		typeFn->isBuiltin = true;
		typeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment> clos) -> Value {
			if (args.size() != 1) throw std::runtime_error("typeof expects 1 argument");
			const Value& v = args[0];
			if (auto ps = std::get_if<std::string>(&v)) return Value{ *ps };
			if (auto po = std::get_if<std::shared_ptr<Object>>(&v)) {
				if (*po) {
					auto it = (**po).find("declaredType");
					if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) return Value{ std::get<std::string>(it->second) };
					it = (**po).find("runtimeType");
					if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) return Value{ std::get<std::string>(it->second) };
				}
			}
			return Value{ typeOf(v) };
		};
		globals->define("typeof", typeFn);

		// eval(str): evaluate ALang source in a child environment and return last-expression value or null
		auto evalFn = std::make_shared<Function>();
		evalFn->isBuiltin = true;
		evalFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
			if (args.size() != 1) throw std::runtime_error("eval expects 1 argument (string)");
			if (!std::holds_alternative<std::string>(args[0])) throw std::runtime_error("eval expects a string");
			std::string code = std::get<std::string>(args[0]);
			// Try full parse first; on failure (e.g., missing semicolons), fallback to single-expression snippet
			std::vector<StmtPtr> stmts;
			{
				Lexer lx(code);
				auto toks = lx.scanTokens();
				Parser ps(toks, code);
				try {
					stmts = ps.parse();
				} catch (const std::exception&) {
					// Try adding a trailing semicolon to support code ending with a bare expression
					std::string withSemi = code;
					withSemi.push_back(';');
					try {
						Lexer lxSemi(withSemi);
						auto toksSemi = lxSemi.scanTokens();
						Parser psSemi(toksSemi, withSemi);
						stmts = psSemi.parse();
					} catch (const std::exception&) {
						// Fallback to single-expression snippet `(expr);`
						std::string snippet = "(" + code + ")";
						snippet.push_back(';');
						Lexer lx2(snippet);
						auto toks2 = lx2.scanTokens();
						Parser ps2(toks2, snippet);
						auto s2 = ps2.parse();
						if (s2.size() == 1) {
							if (auto es = std::dynamic_pointer_cast<ExprStmt>(s2[0])) {
								auto evalEnv = std::make_shared<Environment>(this->env);
								auto prev = this->env; this->env = evalEnv;
								try { Value v = evaluate(es->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
							}
						}
						throw; // not an expression either
					}
				}
			}
			// execute in a child environment so we don't pollute caller
			auto evalEnv = std::make_shared<Environment>(this->env);
			if (stmts.empty()) return Value{std::monostate{}};
			if (stmts.size() == 1) {
				if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmts[0])) {
					auto prev = this->env; this->env = evalEnv;
					try { Value v = evaluate(es->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
				} else {
					executeBlock(stmts, evalEnv);
					return Value{std::monostate{}};
				}
			}
			// multiple statements: execute all but last, then evaluate last expression if expression-stmt
			if (stmts.size() > 1) {
				std::vector<StmtPtr> prefix(stmts.begin(), stmts.end() - 1);
				executeBlock(prefix, evalEnv);
				if (auto lastEs = std::dynamic_pointer_cast<ExprStmt>(stmts.back())) {
					auto prev = this->env; this->env = evalEnv;
					try { Value v = evaluate(lastEs->expr); this->env = prev; return v; } catch(...) { this->env = prev; throw; }
				} else {
					executeBlock(std::vector<StmtPtr>{ stmts.back() }, evalEnv);
					return Value{std::monostate{}};
				}
			}
			return Value{std::monostate{}};
		};
		globals->define("eval", evalFn);

		// sleep(ms): 返回一个 Promise，在 ms 毫秒后 resolve(null)
		auto sleepFn = std::make_shared<Function>();
		sleepFn->isBuiltin = true;
		sleepFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
			if (args.size() != 1) throw std::runtime_error("sleep expects 1 argument (ms)");
			double ms = getNumber(args[0], "sleep ms");
			auto p = std::make_shared<PromiseState>();
			p->loopPtr = this; // 指向当前解释器以便派发回调
			std::thread([p, this, ms]{
				std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(ms)));
				settlePromise(p, false, Value{std::monostate{}});
			}).detach();
			return Value{p};
		};
		globals->define("sleep", sleepFn);

		// Promise 对象：resolve / reject
		auto promiseObj = std::make_shared<Object>();
		// Promise.resolve(value)
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				Value v = args.empty() ? Value{std::monostate{}} : args[0];
				settlePromise(p, false, v);
				return Value{p};
			};
			(*promiseObj)["resolve"] = fn;
		}
		// Promise.reject(reason)
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = this;
				Value v = args.empty() ? Value{std::string("Promise rejected")} : args[0];
				settlePromise(p, true, v);
				return Value{p};
			};
			(*promiseObj)["reject"] = fn;
		}
		globals->define("Promise", Value{promiseObj});

		// --- Packages ---
		// Math: pi, abs
		// ===== OS (os) =====
		// 提供异步系统调用：`os.call(program, argsArray, cwd)` 返回 Promise


		// ---- Builtin containers and helpers ----
		// Provide native host-backed classes: Map, Set, Deque, Stack
		{
			// NativeMap is defined at file-scope (shared index for O(1) deletions)

			auto mapClass = std::make_shared<ClassInfo>(); mapClass->name = "Map";

			// helpers to extract InstanceExt from closure
			auto getThisInstanceExt = [](std::shared_ptr<Environment> clos)->InstanceExt* {
				if (!clos) throw std::runtime_error("internal: instance method called without closure");
				Value tv = clos->get("this");
				if (!std::holds_alternative<std::shared_ptr<Instance>>(tv)) throw std::runtime_error("internal: invalid 'this' value");
				auto pins = std::get<std::shared_ptr<Instance>>(tv);
				if (!pins) throw std::runtime_error("internal: null 'this'");
				return static_cast<InstanceExt*>(pins.get());
			};

			// map.set(key, value)
			auto setFn = std::make_shared<Function>(); setFn->isBuiltin = true;
			setFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size() != 2) throw std::runtime_error("map.set expects 2 arguments");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto nm = static_cast<NativeMap*>(ie->nativeHandle);
				if (!nm) throw std::runtime_error("map: native handle missing");
				auto it = nm->m.find(args[0]);
				if (it == nm->m.end()) {
					nm->order.push_back(args[0]);
					nm->index[args[0]] = nm->order.size() - 1;
				}
				nm->m[args[0]] = args[1];
				return Value{std::monostate{}};
			};
			mapClass->methods["set"] = setFn;

			// map.get(key)
			auto getFn = std::make_shared<Function>(); getFn->isBuiltin = true;
			getFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size() != 1) throw std::runtime_error("map.get expects 1 argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto nm = static_cast<NativeMap*>(ie->nativeHandle);
				auto it = nm->m.find(args[0]);
				if (it == nm->m.end()) return Value{std::monostate{}};
				return it->second;
			};
			mapClass->methods["get"] = getFn;

			// map.has(key)
			auto hasFn = std::make_shared<Function>(); hasFn->isBuiltin = true;
			hasFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size() != 1) throw std::runtime_error("map.has expects 1 argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto nm = static_cast<NativeMap*>(ie->nativeHandle);
				return Value{ nm->m.find(args[0]) != nm->m.end() };
			};
			mapClass->methods["has"] = hasFn;

			// map.delete(key)
			auto delFn = std::make_shared<Function>(); delFn->isBuiltin = true;
			delFn->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size() != 1) throw std::runtime_error("map.delete expects 1 argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto nm = static_cast<NativeMap*>(ie->nativeHandle);
				auto it = nm->m.find(args[0]);
				if (it == nm->m.end()) return Value{false};
				nm->m.erase(it);
				// O(1) removal from order via swap-with-back using index map
				auto idxIt = nm->index.find(args[0]);
				if (idxIt != nm->index.end()) {
					size_t pos = idxIt->second;
					size_t last = nm->order.size() - 1;
					if (pos != last) {
						Value swapped = nm->order[last];
						nm->order[pos] = swapped;
						nm->index[swapped] = pos;
					}
					nm->order.pop_back();
					nm->index.erase(idxIt);
				}
				return Value{true};
			};
			mapClass->methods["delete"] = delFn;

			// map.size()
			auto sizeFn = std::make_shared<Function>(); sizeFn->isBuiltin = true; sizeFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); return Value{ static_cast<double>(nm->m.size()) }; };
			mapClass->methods["size"] = sizeFn;

			// map.clear()
			auto clearFn = std::make_shared<Function>(); clearFn->isBuiltin = true; clearFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); nm->m.clear(); nm->order.clear(); nm->index.clear(); return Value{std::monostate{}}; };
			mapClass->methods["clear"] = clearFn;

			// map.keys()
			auto keysFn = std::make_shared<Function>(); keysFn->isBuiltin = true; keysFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order) out->push_back(k); return Value{out}; };
			mapClass->methods["keys"] = keysFn;

			// map.values()
			auto valuesFn = std::make_shared<Function>(); valuesFn->isBuiltin = true; valuesFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order) out->push_back(nm->m[k]); return Value{out}; };
			mapClass->methods["values"] = valuesFn;

			// map.entries()
			auto entriesFn = std::make_shared<Function>(); entriesFn->isBuiltin = true; entriesFn->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nm = static_cast<NativeMap*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &k : nm->order){ auto pair = std::make_shared<Array>(); pair->push_back(k); pair->push_back(nm->m[k]); out->push_back(Value{pair}); } return Value{out}; };
			mapClass->methods["entries"] = entriesFn;

			// Register class in globals
			globals->define("Map", mapClass);

			// Constructor function: new Map() -> Instance with native handle
			auto mapCtor = std::make_shared<Function>(); mapCtor->isBuiltin = true;
			mapCtor->builtin = [mapClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
				auto inst = std::make_shared<InstanceExt>();
				inst->klass = mapClass;
				// allocate native map
				auto nm = new NativeMap();
				inst->nativeHandle = nm;
				inst->nativeDestructor = [](void* p){ delete static_cast<NativeMap*>(p); };
				return Value{inst};
			};
			globals->define("map", mapCtor);

			// ---- Set (native) ----
			auto setClass = std::make_shared<ClassInfo>(); setClass->name = "Set";
			auto setAdd = std::make_shared<Function>(); setAdd->isBuiltin = true;
			setAdd->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=1) throw std::runtime_error("set.add expects 1 argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto ns = static_cast<NativeSet*>(ie->nativeHandle);
				if (ns->s.find(args[0]) == ns->s.end()) { ns->s.insert(args[0]); ns->order.push_back(args[0]); ns->index[args[0]] = ns->order.size() - 1; }
				return Value{std::monostate{}};
			};
			setClass->methods["add"] = setAdd;
			auto setHas = std::make_shared<Function>(); setHas->isBuiltin = true; setHas->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { if (args.size()!=1) throw std::runtime_error("set.has expects 1 arg"); InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); return Value{ ns->s.find(args[0]) != ns->s.end() }; };
			setClass->methods["has"] = setHas;
			auto setDelete = std::make_shared<Function>(); setDelete->isBuiltin = true; setDelete->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=1) throw std::runtime_error("set.delete expects 1 arg");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto ns = static_cast<NativeSet*>(ie->nativeHandle);
				auto it = ns->s.find(args[0]); if (it == ns->s.end()) return Value{false}; ns->s.erase(it);
				auto idxIt = ns->index.find(args[0]);
				if (idxIt != ns->index.end()) {
					size_t pos = idxIt->second;
					size_t last = ns->order.size() - 1;
					if (pos != last) {
						Value swapped = ns->order[last];
						ns->order[pos] = swapped;
						ns->index[swapped] = pos;
					}
					ns->order.pop_back();
					ns->index.erase(idxIt);
				}
				return Value{true};
			};
			setClass->methods["delete"] = setDelete;
			auto setSize = std::make_shared<Function>(); setSize->isBuiltin = true; setSize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); return Value{ static_cast<double>(ns->s.size()) }; };
			setClass->methods["size"] = setSize;
			auto setValues = std::make_shared<Function>(); setValues->isBuiltin = true; setValues->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeSet*>(ie->nativeHandle); auto out = std::make_shared<Array>(); for (auto &v : ns->order) out->push_back(v); return Value{out}; };
			setClass->methods["values"] = setValues;
			// Set union(other): returns new Set containing all unique elements
			auto setUnion = std::make_shared<Function>(); setUnion->isBuiltin = true; setUnion->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=1) throw std::runtime_error("set.union expects 1 Set argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto ns = static_cast<NativeSet*>(ie->nativeHandle);
				if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.union expects Set instance");
				auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
				if (inst2->klass != setClass) throw std::runtime_error("set.union expects Set instance");
				auto ie2 = static_cast<InstanceExt*>(inst2.get());
				auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
				// create new Set instance
				auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
				for (auto &v : ns->order) { if (newNative->s.insert(v).second) { newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
				for (auto &v : ns2->order) { if (newNative->s.insert(v).second) { newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
				return Value{newInst};
			};
			setClass->methods["union"] = setUnion;
			// Set intersection(other): returns new Set containing elements in both sets
			auto setIntersection = std::make_shared<Function>(); setIntersection->isBuiltin = true; setIntersection->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=1) throw std::runtime_error("set.intersection expects 1 Set argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto ns = static_cast<NativeSet*>(ie->nativeHandle);
				if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.intersection expects Set instance");
				auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
				if (inst2->klass != setClass) throw std::runtime_error("set.intersection expects Set instance");
				auto ie2 = static_cast<InstanceExt*>(inst2.get());
				auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
				auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
				for (auto &v : ns->order) { if (ns2->s.find(v)!=ns2->s.end()) { newNative->s.insert(v); newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
				return Value{newInst};
			};
			setClass->methods["intersection"] = setIntersection;
			// Set difference(other): returns elements in this set not in other
			auto setDifference = std::make_shared<Function>(); setDifference->isBuiltin = true; setDifference->builtin = [getThisInstanceExt,setClass](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=1) throw std::runtime_error("set.difference expects 1 Set argument");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto ns = static_cast<NativeSet*>(ie->nativeHandle);
				if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0])) throw std::runtime_error("set.difference expects Set instance");
				auto inst2 = std::get<std::shared_ptr<Instance>>(args[0]);
				if (inst2->klass != setClass) throw std::runtime_error("set.difference expects Set instance");
				auto ie2 = static_cast<InstanceExt*>(inst2.get());
				auto ns2 = static_cast<NativeSet*>(ie2->nativeHandle);
				auto newInst = std::make_shared<InstanceExt>(); newInst->klass = setClass; auto newNative = new NativeSet(); newInst->nativeHandle = newNative; newInst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); };
				for (auto &v : ns->order) { if (ns2->s.find(v)==ns2->s.end()) { newNative->s.insert(v); newNative->order.push_back(v); newNative->index[v] = newNative->order.size()-1; } }
				return Value{newInst};
			};
			setClass->methods["difference"] = setDifference;
			globals->define("Set", setClass);
			auto setCtor = std::make_shared<Function>(); setCtor->isBuiltin = true; setCtor->builtin = [setClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = setClass; auto ns = new NativeSet(); inst->nativeHandle = ns; inst->nativeDestructor = [](void* p){ delete static_cast<NativeSet*>(p); }; return Value{inst}; };
			globals->define("set", setCtor);
			// Package namespace bindings for std.collections
			registerPackageSymbol("std.collections", "Set", Value{setClass});
			registerPackageSymbol("std.collections", "set", Value{setCtor});

			// ---- Deque (native) ----
			struct NativeDeque { std::deque<Value> d; };
			auto dequeClass = std::make_shared<ClassInfo>(); dequeClass->name = "Deque";
			auto dpush = std::make_shared<Function>(); dpush->isBuiltin = true; dpush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); for (auto &v: args) nd->d.push_back(v); return Value{ static_cast<double>(nd->d.size()) }; };
			dequeClass->methods["push"] = dpush;
			auto dpop = std::make_shared<Function>(); dpop->isBuiltin = true; dpop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; Value v = nd->d.back(); nd->d.pop_back(); return v; };
			dequeClass->methods["pop"] = dpop;
			auto dunshift = std::make_shared<Function>(); dunshift->isBuiltin = true; dunshift->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); for (auto it = args.rbegin(); it != args.rend(); ++it) nd->d.push_front(*it); return Value{ static_cast<double>(nd->d.size()) }; };
			dequeClass->methods["unshift"] = dunshift;
			auto dshift = std::make_shared<Function>(); dshift->isBuiltin = true; dshift->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; Value v = nd->d.front(); nd->d.pop_front(); return v; };
			dequeClass->methods["shift"] = dshift;
			auto dpeek = std::make_shared<Function>(); dpeek->isBuiltin = true; dpeek->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); if (nd->d.empty()) return Value{std::monostate{}}; if (args.size()==0) return nd->d.front(); return nd->d.back(); };
			dequeClass->methods["peek"] = dpeek;
			auto dsize = std::make_shared<Function>(); dsize->isBuiltin = true; dsize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); return Value{ static_cast<double>(nd->d.size()) }; };
			dequeClass->methods["size"] = dsize;
			auto dclear = std::make_shared<Function>(); dclear->isBuiltin = true; dclear->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto nd = static_cast<NativeDeque*>(ie->nativeHandle); nd->d.clear(); return Value{std::monostate{}}; };
			dequeClass->methods["clear"] = dclear;
			globals->define("Deque", dequeClass);
			auto dequeCtor = std::make_shared<Function>(); dequeCtor->isBuiltin = true; dequeCtor->builtin = [dequeClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = dequeClass; auto nd = new NativeDeque(); inst->nativeHandle = nd; inst->nativeDestructor = [](void* p){ delete static_cast<NativeDeque*>(p); }; return Value{inst}; };
			globals->define("deque", dequeCtor);

			// ---- Stack (native) ----
			struct NativeStack { std::vector<Value> v; };
			auto stackClass = std::make_shared<ClassInfo>(); stackClass->name = "Stack";
			auto spush = std::make_shared<Function>(); spush->isBuiltin = true; spush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); for (auto &x: args) ns->v.push_back(x); return Value{ static_cast<double>(ns->v.size()) }; };
			stackClass->methods["push"] = spush;
			auto spop = std::make_shared<Function>(); spop->isBuiltin = true; spop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); if (ns->v.empty()) return Value{std::monostate{}}; Value v = ns->v.back(); ns->v.pop_back(); return v; };
			stackClass->methods["pop"] = spop;
			auto speek = std::make_shared<Function>(); speek->isBuiltin = true; speek->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); if (ns->v.empty()) return Value{std::monostate{}}; return ns->v.back(); };
			stackClass->methods["peek"] = speek;
			auto ssize = std::make_shared<Function>(); ssize->isBuiltin = true; ssize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto ns = static_cast<NativeStack*>(ie->nativeHandle); return Value{ static_cast<double>(ns->v.size()) }; };
			stackClass->methods["size"] = ssize;
			globals->define("Stack", stackClass);
			auto stackCtor = std::make_shared<Function>(); stackCtor->isBuiltin = true; stackCtor->builtin = [stackClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = stackClass; auto ns = new NativeStack(); inst->nativeHandle = ns; inst->nativeDestructor = [](void* p){ delete static_cast<NativeStack*>(p); }; return Value{inst}; };
			globals->define("stack", stackCtor);
			registerPackageSymbol("std.collections", "Stack", Value{stackClass});
			registerPackageSymbol("std.collections", "stack", Value{stackCtor});

			// ---- PriorityQueue (native) ----
			struct NativePriorityQueue { struct Node { double priority; Value value; }; std::vector<Node> heap; };
			auto pqClass = std::make_shared<ClassInfo>(); pqClass->name = "PriorityQueue";
			auto pqPush = std::make_shared<Function>(); pqPush->isBuiltin = true; pqPush->builtin = [getThisInstanceExt](const std::vector<Value>& args, std::shared_ptr<Environment> clos)->Value {
				if (args.size()!=2) throw std::runtime_error("priorityQueue.push expects value, priority");
				InstanceExt* ie = getThisInstanceExt(clos);
				auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
				double pr = 0.0; if (std::holds_alternative<double>(args[1])) pr = std::get<double>(args[1]); else throw std::runtime_error("priority must be number");
				npq->heap.push_back(NativePriorityQueue::Node{pr, args[0]});
				std::push_heap(npq->heap.begin(), npq->heap.end(), [](const NativePriorityQueue::Node& a, const NativePriorityQueue::Node& b){ return a.priority < b.priority; });
				return Value{ static_cast<double>(npq->heap.size()) };
			};
			pqClass->methods["push"] = pqPush;
			auto pqPop = std::make_shared<Function>(); pqPop->isBuiltin = true; pqPop->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
				InstanceExt* ie = getThisInstanceExt(clos);
				auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
				if (npq->heap.empty()) return Value{std::monostate{}};
				std::pop_heap(npq->heap.begin(), npq->heap.end(), [](const NativePriorityQueue::Node& a, const NativePriorityQueue::Node& b){ return a.priority < b.priority; });
				auto node = npq->heap.back(); npq->heap.pop_back(); return node.value;
			};
			pqClass->methods["pop"] = pqPop;
			auto pqPeek = std::make_shared<Function>(); pqPeek->isBuiltin = true; pqPeek->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value {
				InstanceExt* ie = getThisInstanceExt(clos);
				auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle);
				if (npq->heap.empty()) return Value{std::monostate{}}; return npq->heap.front().value;
			};
			pqClass->methods["peek"] = pqPeek;
			auto pqSize = std::make_shared<Function>(); pqSize->isBuiltin = true; pqSize->builtin = [getThisInstanceExt](const std::vector<Value>&, std::shared_ptr<Environment> clos)->Value { InstanceExt* ie = getThisInstanceExt(clos); auto npq = static_cast<NativePriorityQueue*>(ie->nativeHandle); return Value{ static_cast<double>(npq->heap.size()) }; };
			pqClass->methods["size"] = pqSize;
			globals->define("PriorityQueue", pqClass);
			auto pqCtor = std::make_shared<Function>(); pqCtor->isBuiltin = true; pqCtor->builtin = [pqClass](const std::vector<Value>&, std::shared_ptr<Environment>)->Value { auto inst = std::make_shared<InstanceExt>(); inst->klass = pqClass; auto npq = new NativePriorityQueue(); inst->nativeHandle = npq; inst->nativeDestructor = [](void* p){ delete static_cast<NativePriorityQueue*>(p); }; return Value{inst}; };
			globals->define("priorityQueue", pqCtor);
			registerPackageSymbol("std.collections", "PriorityQueue", Value{pqClass});
			registerPackageSymbol("std.collections", "priorityQueue", Value{pqCtor});

			// ---- binarySearch (function) ----
			auto binarySearchFn = std::make_shared<Function>(); binarySearchFn->isBuiltin = true; binarySearchFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
				if (args.size()!=2) throw std::runtime_error("binarySearch expects (array, target)");
				if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) throw std::runtime_error("binarySearch first arg must be array");
				auto arr = std::get<std::shared_ptr<Array>>(args[0]);
				Value target = args[1];
				// Support number or string search only
				bool targetIsNumber = std::holds_alternative<double>(target);
				bool targetIsString = std::holds_alternative<std::string>(target);
				if (!targetIsNumber && !targetIsString) throw std::runtime_error("binarySearch target must be number or string");
				int left = 0; int right = static_cast<int>(arr->size()) - 1;
				while (left <= right) {
					int mid = left + (right - left)/2;
					Value v = (*arr)[mid];
					if (targetIsNumber) {
						if (!std::holds_alternative<double>(v)) throw std::runtime_error("binarySearch array must be homogeneous numbers");
						double tv = std::get<double>(target); double mv = std::get<double>(v);
						if (mv == tv) return Value{ static_cast<double>(mid) };
						if (mv < tv) left = mid + 1; else right = mid - 1;
					} else {
						if (!std::holds_alternative<std::string>(v)) throw std::runtime_error("binarySearch array must be homogeneous strings");
						const std::string& tv = std::get<std::string>(target); const std::string& mv = std::get<std::string>(v);
						if (mv == tv) return Value{ static_cast<double>(mid) };
						if (mv < tv) left = mid + 1; else right = mid - 1;
					}
				}
				return Value{ -1.0 };
			};
			registerPackageSymbol("std.collections", "binarySearch", Value{binarySearchFn});
		}

		// ---- Utility helper functions ----
		{
			// keys(obj)
			auto keysFn = std::make_shared<Function>(); keysFn->isBuiltin = true;
			keysFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("keys expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("keys expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po) arr->push_back(Value{kv.first}); return Value{arr}; };
			globals->define("keys", keysFn);

			// values(obj)
			auto valuesFn = std::make_shared<Function>(); valuesFn->isBuiltin = true;
			valuesFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("values expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("values expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po) arr->push_back(kv.second); return Value{arr}; };
			globals->define("values", valuesFn);

			// entries(obj)
			auto entriesFn = std::make_shared<Function>(); entriesFn->isBuiltin = true;
			entriesFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if (args.size()!=1) throw std::runtime_error("entries expects 1 object argument"); if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) throw std::runtime_error("entries expects an object"); auto po = std::get<std::shared_ptr<Object>>(args[0]); auto arr = std::make_shared<Array>(); for(auto &kv:*po){ auto p = std::make_shared<Array>(); p->push_back(Value{kv.first}); p->push_back(kv.second); arr->push_back(Value{p}); } return Value{arr}; };
			globals->define("entries", entriesFn);

			// clone(obj): shallow clone object or array
			auto cloneFn = std::make_shared<Function>(); cloneFn->isBuiltin = true;
			cloneFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("clone expects 1 argument"); if (std::holds_alternative<std::shared_ptr<Object>>(args[0])){ auto src = std::get<std::shared_ptr<Object>>(args[0]); auto dst = std::make_shared<Object>(); for(auto &kv:*src) (*dst)[kv.first]=kv.second; return Value{dst}; } if (std::holds_alternative<std::shared_ptr<Array>>(args[0])){ auto src = std::get<std::shared_ptr<Array>>(args[0]); auto dst = std::make_shared<Array>(*src); return Value{dst}; } throw std::runtime_error("clone expects object or array"); };
			globals->define("clone", cloneFn);

			// merge(a,b): shallow merge objects into new object
			auto mergeFn = std::make_shared<Function>(); mergeFn->isBuiltin = true;
			mergeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=2) throw std::runtime_error("merge expects 2 object arguments"); if(!std::holds_alternative<std::shared_ptr<Object>>(args[0])||!std::holds_alternative<std::shared_ptr<Object>>(args[1])) throw std::runtime_error("merge expects objects"); auto a=std::get<std::shared_ptr<Object>>(args[0]); auto b=std::get<std::shared_ptr<Object>>(args[1]); auto dst=std::make_shared<Object>(); for(auto &kv:*a) (*dst)[kv.first]=kv.second; for(auto &kv:*b) (*dst)[kv.first]=kv.second; return Value{dst}; };
			globals->define("merge", mergeFn);

			// range(n) -> array [0..n-1]
			auto rangeFn = std::make_shared<Function>(); rangeFn->isBuiltin = true;
			rangeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("range expects 1 numeric argument"); double n = 0; if (std::holds_alternative<double>(args[0])) n = std::get<double>(args[0]); else throw std::runtime_error("range expects a number"); auto arr = std::make_shared<Array>(); for(int i=0;i<static_cast<int>(n);++i) arr->push_back(Value{ static_cast<double>(i) }); return Value{arr}; };
			globals->define("range", rangeFn);

			// enumerate(iterable) -> array of [index/key, value]
			auto enumFn = std::make_shared<Function>(); enumFn->isBuiltin = true;
			enumFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>){ if(args.size()!=1) throw std::runtime_error("enumerate expects 1 iterable"); if(std::holds_alternative<std::shared_ptr<Array>>(args[0])){ auto a=std::get<std::shared_ptr<Array>>(args[0]); auto out=std::make_shared<Array>(); for(size_t i=0;i<a->size();++i){ auto pair=std::make_shared<Array>(); pair->push_back(Value{static_cast<double>(static_cast<int>(i))}); pair->push_back((*a)[i]); out->push_back(Value{pair}); } return Value{out}; } if(std::holds_alternative<std::shared_ptr<Object>>(args[0])){ auto o=std::get<std::shared_ptr<Object>>(args[0]); auto out=std::make_shared<Array>(); for(auto &kv:*o){ auto pair=std::make_shared<Array>(); pair->push_back(Value{kv.first}); pair->push_back(kv.second); out->push_back(Value{pair}); } return Value{out}; } throw std::runtime_error("enumerate expects array or object"); };
			globals->define("enumerate", enumFn);

			// keysSorted(container [, comparator]) -> array of keys sorted by default rules or comparator
			auto keysSortedFn = std::make_shared<Function>(); keysSortedFn->isBuiltin = true;
			keysSortedFn->builtin = [this](const std::vector<Value>& args, std::shared_ptr<Environment> env)->Value{
				if (args.size() < 1 || args.size() > 2) throw std::runtime_error("keysSorted expects 1 or 2 arguments");
				// extract keys depending on container shape
				auto keysArr = std::make_shared<Array>();
				if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
					auto a = std::get<std::shared_ptr<Array>>(args[0]);
					for (size_t i=0;i<a->size();++i) keysArr->push_back(Value{ static_cast<double>(static_cast<int>(i)) });
				} else if (std::holds_alternative<std::shared_ptr<Object>>(args[0])) {
					auto o = std::get<std::shared_ptr<Object>>(args[0]);
					// detect map-like backed by __data
					auto it = o->find("__data");
					if (it != o->end() && std::holds_alternative<std::shared_ptr<Array>>(it->second)) {
						auto data = std::get<std::shared_ptr<Array>>(it->second);
						for (auto &v : *data) {
							if (!std::holds_alternative<std::shared_ptr<Array>>(v)) continue;
							auto pair = std::get<std::shared_ptr<Array>>(v);
							if (pair->size()>=1) keysArr->push_back((*pair)[0]);
						}
					} else {
						for (auto &kv : *o) keysArr->push_back(Value{kv.first});
					}
				} else if (std::holds_alternative<std::shared_ptr<Instance>>(args[0])) {
					auto inst = std::get<std::shared_ptr<Instance>>(args[0]);
					if (!inst) throw std::runtime_error("keysSorted: null instance");
					if (inst->klass && inst->klass->name == "Map") {
						// InstanceExt expected
						auto ie = static_cast<InstanceExt*>(inst.get());
						auto nm = static_cast<NativeMap*>(ie->nativeHandle);
						for (auto &k : nm->order) keysArr->push_back(k);
					} else if (inst->klass && inst->klass->name == "Set") {
						auto ie = static_cast<InstanceExt*>(inst.get());
						auto ns = static_cast<NativeSet*>(ie->nativeHandle);
						for (auto &k : ns->order) keysArr->push_back(k);
					} else {
						// fallback: try to enumerate instance fields
						for (auto &kv : inst->fields) keysArr->push_back(Value{kv.first});
					}
				} else {
					throw std::runtime_error("keysSorted expects an array or object/map-like value");
				}

				// comparator handling
				std::shared_ptr<Function> cmpFn = nullptr;
				bool useComparator = false;
				if (args.size() == 2) {
					if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) throw std::runtime_error("keysSorted comparator must be a function");
					cmpFn = std::get<std::shared_ptr<Function>>(args[1]);
					useComparator = true;
				}

				// prepare index array for stable sort
				std::vector<size_t> idx(keysArr->size()); for (size_t i=0;i<idx.size();++i) idx[i]=i;

				auto typeOrder = [](const Value& v)->int{
					switch (v.index()) {
						case 1: return 0; // number
						case 2: return 1; // string
						case 3: return 2; // bool
						case 0: return 3; // null
						default: return 4; // complex types
					}
				};

				auto compareDefault = [&](const Value& A, const Value& B)->int{
					int ta = typeOrder(A); int tb = typeOrder(B);
					if (ta != tb) return (ta < tb) ? -1 : 1;
					switch (A.index()) {
						case 1: { double da = std::get<double>(A); double db = std::get<double>(B); if (da<db) return -1; if (da>db) return 1; return 0; }
						case 2: { const std::string &sa = std::get<std::string>(A); const std::string &sb = std::get<std::string>(B); if (sa<sb) return -1; if (sa>sb) return 1; return 0; }
						case 3: { bool ba = std::get<bool>(A); bool bb = std::get<bool>(B); if (ba==bb) return 0; return ba?1:-1; }
						case 0: return 0;
						default: {
							// compare by pointer address for deterministic order
							size_t pa = 0, pb = 0;
							switch (A.index()) {
								case 4: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Function>>(A).get()); break;
								case 5: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Array>>(A).get()); break;
								case 6: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Object>>(A).get()); break;
								case 7: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<ClassInfo>>(A).get()); break;
								case 8: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<Instance>>(A).get()); break;
								case 9: pa = reinterpret_cast<size_t>(std::get<std::shared_ptr<PromiseState>>(A).get()); break;
							}
							switch (B.index()) {
								case 4: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Function>>(B).get()); break;
								case 5: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Array>>(B).get()); break;
								case 6: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Object>>(B).get()); break;
								case 7: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<ClassInfo>>(B).get()); break;
								case 8: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<Instance>>(B).get()); break;
								case 9: pb = reinterpret_cast<size_t>(std::get<std::shared_ptr<PromiseState>>(B).get()); break;
							}
							if (pa < pb) return -1; if (pa > pb) return 1; return 0;
						}
					}
					return 0; // fallback
				};

				// comparator wrapper: returns negative/zero/positive
				auto cmpWrapper = [&](const Value& A, const Value& B)->int{
					if (useComparator) {
						try {
							std::vector<Value> cargs; cargs.push_back(A); cargs.push_back(B);
							Value res{std::monostate{}};
							if (cmpFn->isBuiltin) {
								res = cmpFn->builtin(cargs, cmpFn->closure);
							} else {
								// Execute interpreted comparator function
								auto local = std::make_shared<Environment>(cmpFn->closure);
								// bind parameters
								for (size_t i=0;i<cmpFn->params.size() && i<cargs.size();++i) local->define(cmpFn->params[i], cargs[i]);
								for (size_t i=cargs.size(); i<cmpFn->params.size(); ++i) local->define(cmpFn->params[i], Value{std::monostate{}});
								try {
									executeBlock(cmpFn->body, local);
								} catch (const ReturnSignal& rs) { res = rs.value; }
							}
							if (auto pd = std::get_if<double>(&res)) {
								double d = *pd; if (d < 0) return -1; if (d > 0) return 1; return 0;
							}
							if (auto ps = std::get_if<std::string>(&res)) {
								std::string s = *ps; try { double dv = std::stod(s); if (dv<0) return -1; if (dv>0) return 1; return 0; } catch(...) { return s.empty()?0: (s[0]=='-'?-1:1); }
							}
							if (auto pb = std::get_if<bool>(&res)) return *pb ? 1 : -1;
							return 0;
						} catch (const std::exception& ex) { throw; }
					}
					return compareDefault(A,B);
				};

				// stable sort indices by comparator over keysArr
				std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b){ return cmpWrapper((*keysArr)[a], (*keysArr)[b]) < 0; });

				auto out = std::make_shared<Array>();
				for (size_t i=0;i<idx.size();++i) out->push_back((*keysArr)[idx[i]]);
				return Value{out};
			};
			globals->define("keysSorted", keysSortedFn);
		}


		// Regex class
	}
};

} // namespace asul

#endif // ASUL_INTERPRETER_H
