#include "StdBuiltin.h"
#include "../../../AsulInterpreter.h"
#include <chrono>

namespace asul {

void registerStdBuiltinPackage(Interpreter& interp) {
	auto globals = interp.globalsEnv();
	// Get interpreter pointer for lambdas that need it
	Interpreter* interpPtr = &interp;
	
	// len(x): string/array/object length
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

	// push(arr, ...values): append elements, return new length
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

	// typeof(x): return a type-name string for x
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

	// performance.now()
	auto perfObj = std::make_shared<Object>();
	auto nowFn = std::make_shared<Function>(); 
	nowFn->isBuiltin = true;
	nowFn->builtin = [](const std::vector<Value>&, std::shared_ptr<Environment>)->Value {
		using namespace std::chrono;
		static auto start = high_resolution_clock::now();
		auto now = high_resolution_clock::now();
		duration<double, std::milli> ms = now - start;
		return Value{ ms.count() };
	};
	(*perfObj)["now"] = Value{nowFn};
	globals->define("performance", Value{perfObj});

			auto quoteFn = std::make_shared<Function>();
			quoteFn->isBuiltin = true;
			quoteFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
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
					applyFn->builtin = [interpPtr, self](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
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
						return interpPtr->callFunction("eval", std::vector<Value>{ Value{code} });
					};
					(*qobj)["apply"] = Value{ applyFn };
				}
				return Value{ qobj };
			};
			globals->define("quote", quoteFn);


			// eval(str): evaluate ALang source in a child environment and return last-expression value or null
			auto evalFn = std::make_shared<Function>();
			evalFn->isBuiltin = true;
			evalFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>)->Value {
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
									auto evalEnv = std::make_shared<Environment>(interpPtr->currentEnv());
									auto prev = interpPtr->currentEnv(); interpPtr->setCurrentEnv(evalEnv);
									try { Value v = interpPtr->evaluate(es->expr); interpPtr->setCurrentEnv(prev); return v; } catch(...) { interpPtr->setCurrentEnv(prev); throw; }
								}
							}
							throw; // not an expression either
						}
					}
				}
				// execute in a child environment so we don't pollute caller
				auto evalEnv = std::make_shared<Environment>(interpPtr->currentEnv());
				if (stmts.empty()) return Value{std::monostate{}};
				if (stmts.size() == 1) {
					if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmts[0])) {
						auto prev = interpPtr->currentEnv(); interpPtr->setCurrentEnv(evalEnv);
						try { Value v = interpPtr->evaluate(es->expr); interpPtr->setCurrentEnv(prev); return v; } catch(...) { interpPtr->setCurrentEnv(prev); throw; }
					} else {
						interpPtr->executeBlock(stmts, evalEnv);
						return Value{std::monostate{}};
					}
				}
				// multiple statements: execute all but last, then evaluate last expression if expression-stmt
				if (stmts.size() > 1) {
					std::vector<StmtPtr> prefix(stmts.begin(), stmts.end() - 1);
					interpPtr->executeBlock(prefix, evalEnv);
					if (auto lastEs = std::dynamic_pointer_cast<ExprStmt>(stmts.back())) {
						auto prev = interpPtr->currentEnv(); interpPtr->setCurrentEnv(evalEnv);
						try { Value v = interpPtr->evaluate(lastEs->expr); interpPtr->setCurrentEnv(prev); return v; } catch(...) { interpPtr->setCurrentEnv(prev); throw; }
					} else {
						interpPtr->executeBlock(std::vector<StmtPtr>{ stmts.back() }, evalEnv);
						return Value{std::monostate{}};
					}
				}
				return Value{std::monostate{}};
			};
			globals->define("eval", evalFn);

			// sleep(ms): 返回一个 Promise，在 ms 毫秒后 resolve(null)
			auto sleepFn = std::make_shared<Function>();
			sleepFn->isBuiltin = true;
			sleepFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value{
				if (args.size() != 1) throw std::runtime_error("sleep expects 1 argument (ms)");
				double ms = getNumber(args[0], "sleep ms");
				auto p = std::make_shared<PromiseState>();
				p->loopPtr = interpPtr; // 指向当前解释器以便派发回调
				std::thread([p, interpPtr, ms]{
					std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(ms)));
					interpPtr->settlePromise(p, false, Value{std::monostate{}});
				}).detach();
				return Value{p};
			};
			globals->define("sleep", sleepFn);

			// Promise 对象：resolve / reject
			auto promiseObj = std::make_shared<Object>();
			// Promise.resolve(value)
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					auto p = std::make_shared<PromiseState>();
					p->loopPtr = interpPtr;
					Value v = args.empty() ? Value{std::monostate{}} : args[0];
					interpPtr->settlePromise(p, false, v);
					return Value{p};
				};
				(*promiseObj)["resolve"] = fn;
			}
			// Promise.reject(reason)
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					auto p = std::make_shared<PromiseState>();
					p->loopPtr = interpPtr;
					Value v = args.empty() ? Value{std::string("Promise rejected")} : args[0];
					interpPtr->settlePromise(p, true, v);
					return Value{p};
				};
				(*promiseObj)["reject"] = fn;
			}
			// Promise.all(array) - resolves when all promises resolve, rejects if any reject
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
						throw std::runtime_error("Promise.all expects an array of promises");
					}
					auto arr = std::get<std::shared_ptr<Array>>(args[0]);
					auto resultPromise = std::make_shared<PromiseState>();
					resultPromise->loopPtr = interpPtr;
					
					if (arr->empty()) {
						// Empty array resolves immediately with empty array
						interpPtr->settlePromise(resultPromise, false, Value{std::make_shared<Array>()});
						return Value{resultPromise};
					}
					
					// Shared state for tracking completion
					struct AllState {
						std::shared_ptr<Array> results;
						size_t remaining;
						bool rejected = false;
						std::shared_ptr<PromiseState> resultPromise;
						Interpreter* interpPtr;
					};
					auto state = std::make_shared<AllState>();
					state->results = std::make_shared<Array>(arr->size());
					state->remaining = arr->size();
					state->resultPromise = resultPromise;
					state->interpPtr = interpPtr;
					
					for (size_t i = 0; i < arr->size(); ++i) {
						const auto& elem = (*arr)[i];
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(elem)) {
							auto p = std::get<std::shared_ptr<PromiseState>>(elem);
							size_t index = i;
							
							// Add then callback
							auto thenCb = std::make_shared<Function>();
							thenCb->isBuiltin = true;
							thenCb->builtin = [state, index](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->rejected) {
									(*state->results)[index] = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->remaining--;
									if (state->remaining == 0) {
										state->interpPtr->settlePromise(state->resultPromise, false, Value{state->results});
									}
								}
								return Value{std::monostate{}};
							};
							p->thenCallbacks.push_back({thenCb, nullptr});
							
							// Add catch callback
							auto catchCb = std::make_shared<Function>();
							catchCb->isBuiltin = true;
							catchCb->builtin = [state](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->rejected) {
									state->rejected = true;
									Value reason = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->interpPtr->settlePromise(state->resultPromise, true, reason);
								}
								return Value{std::monostate{}};
							};
							p->catchCallbacks.push_back({catchCb, nullptr});
							
							// If promise is already settled, dispatch callbacks
							if (p->settled) {
								interpPtr->dispatchPromiseCallbacks(p);
							}
						} else {
							// Non-promise value, treat as resolved
							(*state->results)[i] = elem;
							state->remaining--;
							if (state->remaining == 0) {
								interpPtr->settlePromise(state->resultPromise, false, Value{state->results});
							}
						}
					}
					
					return Value{resultPromise};
				};
				(*promiseObj)["all"] = fn;
			}
			// Promise.race(array) - resolves/rejects with the first settled promise
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
						throw std::runtime_error("Promise.race expects an array of promises");
					}
					auto arr = std::get<std::shared_ptr<Array>>(args[0]);
					auto resultPromise = std::make_shared<PromiseState>();
					resultPromise->loopPtr = interpPtr;
					
					if (arr->empty()) {
						// Empty array, promise stays pending forever (as per spec)
						return Value{resultPromise};
					}
					
					// Shared state for tracking settlement
					struct RaceState {
						bool settled = false;
						std::shared_ptr<PromiseState> resultPromise;
						Interpreter* interpPtr;
					};
					auto state = std::make_shared<RaceState>();
					state->resultPromise = resultPromise;
					state->interpPtr = interpPtr;
					
					for (size_t i = 0; i < arr->size(); ++i) {
						const auto& elem = (*arr)[i];
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(elem)) {
							auto p = std::get<std::shared_ptr<PromiseState>>(elem);
							
							// Add then callback
							auto thenCb = std::make_shared<Function>();
							thenCb->isBuiltin = true;
							thenCb->builtin = [state](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->settled) {
									state->settled = true;
									Value value = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->interpPtr->settlePromise(state->resultPromise, false, value);
								}
								return Value{std::monostate{}};
							};
							p->thenCallbacks.push_back({thenCb, nullptr});
							
							// Add catch callback
							auto catchCb = std::make_shared<Function>();
							catchCb->isBuiltin = true;
							catchCb->builtin = [state](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->settled) {
									state->settled = true;
									Value reason = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->interpPtr->settlePromise(state->resultPromise, true, reason);
								}
								return Value{std::monostate{}};
							};
							p->catchCallbacks.push_back({catchCb, nullptr});
							
							// If promise is already settled, dispatch callbacks
							if (p->settled) {
								interpPtr->dispatchPromiseCallbacks(p);
							}
						} else {
							// Non-promise value, settle immediately
							if (!state->settled) {
								state->settled = true;
								interpPtr->settlePromise(state->resultPromise, false, elem);
							}
							break;
						}
					}
					
					return Value{resultPromise};
				};
				(*promiseObj)["race"] = fn;
			}
			// Promise.any(array) - resolves with the first resolved promise, rejects if all reject
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
						throw std::runtime_error("Promise.any expects an array of promises");
					}
					auto arr = std::get<std::shared_ptr<Array>>(args[0]);
					auto resultPromise = std::make_shared<PromiseState>();
					resultPromise->loopPtr = interpPtr;
					
					if (arr->empty()) {
						// Empty array rejects with AggregateError
						interpPtr->settlePromise(resultPromise, true, Value{std::string("AggregateError: No promises to resolve")});
						return Value{resultPromise};
					}
					
					// Shared state for tracking completion
					struct AnyState {
						std::shared_ptr<Array> errors;
						size_t remaining;
						bool resolved = false;
						std::shared_ptr<PromiseState> resultPromise;
						Interpreter* interpPtr;
					};
					auto state = std::make_shared<AnyState>();
					state->errors = std::make_shared<Array>(arr->size());
					state->remaining = arr->size();
					state->resultPromise = resultPromise;
					state->interpPtr = interpPtr;
					
					for (size_t i = 0; i < arr->size(); ++i) {
						const auto& elem = (*arr)[i];
						if (std::holds_alternative<std::shared_ptr<PromiseState>>(elem)) {
							auto p = std::get<std::shared_ptr<PromiseState>>(elem);
							size_t index = i;
							
							// Add then callback
							auto thenCb = std::make_shared<Function>();
							thenCb->isBuiltin = true;
							thenCb->builtin = [state](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->resolved) {
									state->resolved = true;
									Value value = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->interpPtr->settlePromise(state->resultPromise, false, value);
								}
								return Value{std::monostate{}};
							};
							p->thenCallbacks.push_back({thenCb, nullptr});
							
							// Add catch callback
							auto catchCb = std::make_shared<Function>();
							catchCb->isBuiltin = true;
							catchCb->builtin = [state, index](const std::vector<Value>& cbArgs, std::shared_ptr<Environment>) -> Value {
								if (!state->resolved) {
									(*state->errors)[index] = cbArgs.empty() ? Value{std::monostate{}} : cbArgs[0];
									state->remaining--;
									if (state->remaining == 0) {
										// All promises rejected
										state->interpPtr->settlePromise(state->resultPromise, true, 
											Value{std::string("AggregateError: All promises were rejected")});
									}
								}
								return Value{std::monostate{}};
							};
							p->catchCallbacks.push_back({catchCb, nullptr});
							
							// If promise is already settled, dispatch callbacks
							if (p->settled) {
								interpPtr->dispatchPromiseCallbacks(p);
							}
						} else {
							// Non-promise value, resolve immediately
							if (!state->resolved) {
								state->resolved = true;
								interpPtr->settlePromise(state->resultPromise, false, elem);
							}
							break;
						}
					}
					
					return Value{resultPromise};
				};
				(*promiseObj)["any"] = fn;
			}
			globals->define("Promise", Value{promiseObj});
	
	// ========== Type System Enhancements ==========
	
	// isType(value, type) - Runtime type guard that checks if a value matches a type string
	auto isTypeFn = std::make_shared<Function>();
	isTypeFn->isBuiltin = true;
	isTypeFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 2) throw std::runtime_error("isType expects 2 arguments (value, type)");
		if (!std::holds_alternative<std::string>(args[1])) {
			throw std::runtime_error("isType: second argument must be a type string");
		}
		
		const Value& val = args[0];
		std::string expectedType = std::get<std::string>(args[1]);
		std::string actualType = typeOf(val);
		
		// Check for custom type on objects
		if (auto po = std::get_if<std::shared_ptr<Object>>(&val)) {
			if (*po) {
				auto it = (**po).find("declaredType");
				if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) {
					actualType = std::get<std::string>(it->second);
				} else {
					it = (**po).find("runtimeType");
					if (it != (**po).end() && std::holds_alternative<std::string>(it->second)) {
						actualType = std::get<std::string>(it->second);
					}
				}
			}
		}
		
		return Value{actualType == expectedType};
	};
	globals->define("isType", isTypeFn);
	
	// isArray(value) - Check if value is an array
	auto isArrayFn = std::make_shared<Function>();
	isArrayFn->isBuiltin = true;
	isArrayFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isArray expects 1 argument");
		return Value{std::holds_alternative<std::shared_ptr<Array>>(args[0])};
	};
	globals->define("isArray", isArrayFn);
	
	// isObject(value) - Check if value is an object (not array, function, or promise)
	auto isObjectFn = std::make_shared<Function>();
	isObjectFn->isBuiltin = true;
	isObjectFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isObject expects 1 argument");
		if (!std::holds_alternative<std::shared_ptr<Object>>(args[0])) return Value{false};
		// Make sure it's not a special object type
		auto obj = std::get<std::shared_ptr<Object>>(args[0]);
		if (!obj) return Value{false};
		// Check if it has special type markers
		auto it = obj->find("runtimeType");
		if (it != obj->end() && std::holds_alternative<std::string>(it->second)) {
			std::string rt = std::get<std::string>(it->second);
			if (rt == "Function" || rt == "Promise") return Value{false};
		}
		return Value{true};
	};
	globals->define("isObject", isObjectFn);
	
	// isFunction(value) - Check if value is a function
	auto isFunctionFn = std::make_shared<Function>();
	isFunctionFn->isBuiltin = true;
	isFunctionFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isFunction expects 1 argument");
		return Value{std::holds_alternative<std::shared_ptr<Function>>(args[0])};
	};
	globals->define("isFunction", isFunctionFn);
	
	// isPromise(value) - Check if value is a Promise
	auto isPromiseFn = std::make_shared<Function>();
	isPromiseFn->isBuiltin = true;
	isPromiseFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isPromise expects 1 argument");
		return Value{std::holds_alternative<std::shared_ptr<PromiseState>>(args[0])};
	};
	globals->define("isPromise", isPromiseFn);
	
	// isNumber(value) - Check if value is a number
	auto isNumberFn = std::make_shared<Function>();
	isNumberFn->isBuiltin = true;
	isNumberFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isNumber expects 1 argument");
		return Value{std::holds_alternative<double>(args[0])};
	};
	globals->define("isNumber", isNumberFn);
	
	// isString(value) - Check if value is a string
	auto isStringFn = std::make_shared<Function>();
	isStringFn->isBuiltin = true;
	isStringFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isString expects 1 argument");
		return Value{std::holds_alternative<std::string>(args[0])};
	};
	globals->define("isString", isStringFn);
	
	// isBoolean(value) - Check if value is a boolean
	auto isBooleanFn = std::make_shared<Function>();
	isBooleanFn->isBuiltin = true;
	isBooleanFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isBoolean expects 1 argument");
		return Value{std::holds_alternative<bool>(args[0])};
	};
	globals->define("isBoolean", isBooleanFn);
	
	// isNull(value) - Check if value is null/undefined
	auto isNullFn = std::make_shared<Function>();
	isNullFn->isBuiltin = true;
	isNullFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("isNull expects 1 argument");
		return Value{std::holds_alternative<std::monostate>(args[0])};
	};
	globals->define("isNull", isNullFn);
	
	// ========== Iterator Protocol ==========
	
	// hasIterator(obj) - Check if object has an iterator method
	auto hasIteratorFn = std::make_shared<Function>();
	hasIteratorFn->isBuiltin = true;
	hasIteratorFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("hasIterator expects 1 argument");
		
		// Arrays, strings, and objects are all iterable by default
		if (std::holds_alternative<std::shared_ptr<Array>>(args[0])) return Value{true};
		if (std::holds_alternative<std::string>(args[0])) return Value{true};
		
		// For objects, check if they have an __iterator__ method
		if (auto po = std::get_if<std::shared_ptr<Object>>(&args[0])) {
			if (*po) {
				auto it = (**po).find("__iterator__");
				if (it != (**po).end() && std::holds_alternative<std::shared_ptr<Function>>(it->second)) {
					return Value{true};
				}
			}
			// Objects are iterable by default (iterate over keys)
			return Value{true};
		}
		
		return Value{false};
	};
	globals->define("hasIterator", hasIteratorFn);
	
	// getIterator(obj) - Get an iterator for an object
	// Returns an object with {next: function() -> {value, done}}
	auto getIteratorFn = std::make_shared<Function>();
	getIteratorFn->isBuiltin = true;
	getIteratorFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
		if (args.size() != 1) throw std::runtime_error("getIterator expects 1 argument");
		
		const Value& target = args[0];
		
		// Check if object has custom __iterator__ method
		if (auto po = std::get_if<std::shared_ptr<Object>>(&target)) {
			if (*po) {
				auto it = (**po).find("__iterator__");
				if (it != (**po).end() && std::holds_alternative<std::shared_ptr<Function>>(it->second)) {
					// Call the custom iterator method
					auto iterFn = std::get<std::shared_ptr<Function>>(it->second);
					std::vector<Value> callArgs;
					
					if (iterFn->isBuiltin) {
						return iterFn->builtin(callArgs, iterFn->closure);
					} else {
						// Manually execute the function
						auto local = std::make_shared<Environment>(iterFn->closure);
						try {
							interpPtr->executeBlock(iterFn->body, local);
						} catch (const ReturnSignal& rs) {
							return rs.value;
						}
						return Value{std::monostate{}};
					}
				}
			}
		}
		
		// Default iterators for built-in types
		if (auto parr = std::get_if<std::shared_ptr<Array>>(&target)) {
			// Array iterator
			struct ArrayIterState {
				std::shared_ptr<Array> arr;
				size_t index = 0;
			};
			auto state = std::make_shared<ArrayIterState>();
			state->arr = *parr;
			
			auto iterObj = std::make_shared<Object>();
			auto nextFn = std::make_shared<Function>();
			nextFn->isBuiltin = true;
			nextFn->builtin = [state](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
				auto result = std::make_shared<Object>();
				if (state->index < state->arr->size()) {
					(*result)["value"] = (*state->arr)[state->index];
					(*result)["done"] = Value{false};
					state->index++;
				} else {
					(*result)["value"] = Value{std::monostate{}};
					(*result)["done"] = Value{true};
				}
				return Value{result};
			};
			(*iterObj)["next"] = Value{nextFn};
			return Value{iterObj};
		}
		
		if (auto pstr = std::get_if<std::string>(&target)) {
			// String iterator
			struct StringIterState {
				std::string str;
				size_t index = 0;
			};
			auto state = std::make_shared<StringIterState>();
			state->str = *pstr;
			
			auto iterObj = std::make_shared<Object>();
			auto nextFn = std::make_shared<Function>();
			nextFn->isBuiltin = true;
			nextFn->builtin = [state](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
				auto result = std::make_shared<Object>();
				if (state->index < state->str.size()) {
					(*result)["value"] = Value{std::string(1, state->str[state->index])};
					(*result)["done"] = Value{false};
					state->index++;
				} else {
					(*result)["value"] = Value{std::monostate{}};
					(*result)["done"] = Value{true};
				}
				return Value{result};
			};
			(*iterObj)["next"] = Value{nextFn};
			return Value{iterObj};
		}
		
		if (auto pobj = std::get_if<std::shared_ptr<Object>>(&target)) {
			// Object iterator (over keys)
			struct ObjectIterState {
				std::vector<std::string> keys;
				size_t index = 0;
			};
			auto state = std::make_shared<ObjectIterState>();
			if (*pobj) {
				for (const auto& [key, val] : **pobj) {
					state->keys.push_back(key);
				}
			}
			
			auto iterObj = std::make_shared<Object>();
			auto nextFn = std::make_shared<Function>();
			nextFn->isBuiltin = true;
			nextFn->builtin = [state](const std::vector<Value>&, std::shared_ptr<Environment>) -> Value {
				auto result = std::make_shared<Object>();
				if (state->index < state->keys.size()) {
					(*result)["value"] = Value{state->keys[state->index]};
					(*result)["done"] = Value{false};
					state->index++;
				} else {
					(*result)["value"] = Value{std::monostate{}};
					(*result)["done"] = Value{true};
				}
				return Value{result};
			};
			(*iterObj)["next"] = Value{nextFn};
			return Value{iterObj};
		}
		
		throw std::runtime_error("getIterator: value is not iterable");
	};
	globals->define("getIterator", getIteratorFn);

}

} // namespace asul
