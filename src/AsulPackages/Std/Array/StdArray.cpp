#include "StdArray.h"
#include "../../../AsulInterpreter.h"
#include <algorithm>
#include <functional>

namespace asul {

void registerStdArrayPackage(Interpreter& interp) {
	// Get interpreter pointer for lambdas that need it
	Interpreter* interpPtr = &interp;
	
	interp.registerLazyPackage("std.array", [interpPtr](std::shared_ptr<Object> arrayPkg) {
		
		// flat(arr, depth=1)
		auto flatFn = std::make_shared<Function>();
		flatFn->isBuiltin = true;
		flatFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
				throw std::runtime_error("flat 第一个参数必须是数组");
			}
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			int depth = 1;
			if (args.size() >= 2) {
				if (!std::holds_alternative<double>(args[1])) {
					throw std::runtime_error("flat 深度参数必须是数字");
				}
				depth = static_cast<int>(std::get<double>(args[1]));
			}
			
			std::function<void(const std::shared_ptr<Array>&, std::shared_ptr<Array>&, int)> flattenHelper;
			flattenHelper = [&flattenHelper](const std::shared_ptr<Array>& src, std::shared_ptr<Array>& dst, int d) {
				for (const auto& elem : *src) {
					if (d > 0 && std::holds_alternative<std::shared_ptr<Array>>(elem)) {
						flattenHelper(std::get<std::shared_ptr<Array>>(elem), dst, d - 1);
					} else {
						dst->push_back(elem);
					}
				}
			};
			
			auto out = std::make_shared<Array>();
			flattenHelper(arr, out, depth);
			return Value{out};
		};
		(*arrayPkg)["flat"] = Value{flatFn};
		
		// flatMap(arr, fn)
		auto flatMapFn = std::make_shared<Function>();
		flatMapFn->isBuiltin = true;
		flatMapFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> env) -> Value {
			if (args.size() < 2) {
				throw std::runtime_error("flatMap 需要数组和函数两个参数");
			}
			if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
				throw std::runtime_error("flatMap 第一个参数必须是数组");
			}
			if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) {
				throw std::runtime_error("flatMap 第二个参数必须是函数");
			}
			
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			auto cb = std::get<std::shared_ptr<Function>>(args[1]);
			auto out = std::make_shared<Array>();
			
			for (size_t i = 0; i < arr->size(); ++i) {
				Value elem = (*arr)[i];
				Value res{std::monostate{}};
				
				if (cb->isBuiltin) {
					std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{arr} };
					res = cb->builtin(carg, cb->closure);
				} else {
					auto local = std::make_shared<Environment>(cb->closure);
					if (cb->params.size() > 0) local->define(cb->params[0], elem);
					if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
					if (cb->params.size() > 2) local->define(cb->params[2], Value{arr});
					try { interpPtr->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { res = rs.value; }
				}
				
				if (std::holds_alternative<std::shared_ptr<Array>>(res)) {
					auto resArr = std::get<std::shared_ptr<Array>>(res);
					for (const auto& item : *resArr) {
						out->push_back(item);
					}
				} else {
					out->push_back(res);
				}
			}
			return Value{out};
		};
		(*arrayPkg)["flatMap"] = Value{flatMapFn};
		
		// unique(arr)
		auto uniqueFn = std::make_shared<Function>();
		uniqueFn->isBuiltin = true;
		uniqueFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
				throw std::runtime_error("unique 需要数组参数");
			}
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			auto out = std::make_shared<Array>();
			
			for (const auto& elem : *arr) {
				bool found = false;
				for (const auto& outElem : *out) {
					if (valueEqual(elem, outElem)) {
						found = true;
						break;
					}
				}
				if (!found) {
					out->push_back(elem);
				}
			}
			return Value{out};
		};
		(*arrayPkg)["unique"] = Value{uniqueFn};
		
		// chunk(arr, size)
		auto chunkFn = std::make_shared<Function>();
		chunkFn->isBuiltin = true;
		chunkFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) {
				throw std::runtime_error("chunk 需要数组和大小两个参数");
			}
			if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
				throw std::runtime_error("chunk 第一个参数必须是数组");
			}
			if (!std::holds_alternative<double>(args[1])) {
				throw std::runtime_error("chunk 大小参数必须是数字");
			}
			
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			double sizeD = std::get<double>(args[1]);
			if (sizeD <= 0) throw std::runtime_error("chunk 大小必须为正数");
			size_t chunkSize = static_cast<size_t>(sizeD);
			
			auto out = std::make_shared<Array>();
			for (size_t i = 0; i < arr->size(); i += chunkSize) {
				auto chunk = std::make_shared<Array>();
				for (size_t j = 0; j < chunkSize && i + j < arr->size(); ++j) {
					chunk->push_back((*arr)[i + j]);
				}
				out->push_back(Value{chunk});
			}
			return Value{out};
		};
		(*arrayPkg)["chunk"] = Value{chunkFn};
		
		// groupBy(arr, fn)
		auto groupByFn = std::make_shared<Function>();
		groupByFn->isBuiltin = true;
		groupByFn->builtin = [interpPtr](const std::vector<Value>& args, std::shared_ptr<Environment> env) -> Value {
			if (args.size() < 2) {
				throw std::runtime_error("groupBy 需要数组和函数两个参数");
			}
			if (!std::holds_alternative<std::shared_ptr<Array>>(args[0])) {
				throw std::runtime_error("groupBy 第一个参数必须是数组");
			}
			if (!std::holds_alternative<std::shared_ptr<Function>>(args[1])) {
				throw std::runtime_error("groupBy 第二个参数必须是函数");
			}
			
			auto arr = std::get<std::shared_ptr<Array>>(args[0]);
			auto cb = std::get<std::shared_ptr<Function>>(args[1]);
			auto out = std::make_shared<Object>();
			
			for (size_t i = 0; i < arr->size(); ++i) {
				Value elem = (*arr)[i];
				Value key{std::monostate{}};
				
				if (cb->isBuiltin) {
					std::vector<Value> carg{ elem, Value{ static_cast<double>(static_cast<int>(i)) }, Value{arr} };
					key = cb->builtin(carg, cb->closure);
				} else {
					auto local = std::make_shared<Environment>(cb->closure);
					if (cb->params.size() > 0) local->define(cb->params[0], elem);
					if (cb->params.size() > 1) local->define(cb->params[1], Value{ static_cast<double>(static_cast<int>(i)) });
					if (cb->params.size() > 2) local->define(cb->params[2], Value{arr});
					try { interpPtr->executeBlock(cb->body, local); } catch (const ReturnSignal& rs) { key = rs.value; }
				}
				
				std::string keyStr = toString(key);
				if (out->find(keyStr) == out->end()) {
					(*out)[keyStr] = Value{std::make_shared<Array>()};
				}
				std::get<std::shared_ptr<Array>>((*out)[keyStr])->push_back(elem);
			}
			return Value{out};
		};
		(*arrayPkg)["groupBy"] = Value{groupByFn};
		
		// zip(arr1, arr2, ...)
		auto zipFn = std::make_shared<Function>();
		zipFn->isBuiltin = true;
		zipFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.empty()) {
				throw std::runtime_error("zip 至少需要一个数组参数");
			}
			
			std::vector<std::shared_ptr<Array>> arrays;
			for (const auto& arg : args) {
				if (!std::holds_alternative<std::shared_ptr<Array>>(arg)) {
					throw std::runtime_error("zip 所有参数必须是数组");
				}
				arrays.push_back(std::get<std::shared_ptr<Array>>(arg));
			}
			
			size_t minLen = arrays[0]->size();
			for (const auto& arr : arrays) {
				if (arr->size() < minLen) minLen = arr->size();
			}
			
			auto out = std::make_shared<Array>();
			for (size_t i = 0; i < minLen; ++i) {
				auto tuple = std::make_shared<Array>();
				for (const auto& arr : arrays) {
					tuple->push_back((*arr)[i]);
				}
				out->push_back(Value{tuple});
			}
			return Value{out};
		};
		(*arrayPkg)["zip"] = Value{zipFn};
		
		// diff(arr1, arr2)
		auto diffFn = std::make_shared<Function>();
		diffFn->isBuiltin = true;
		diffFn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
			if (args.size() < 2) {
				throw std::runtime_error("diff 需要两个数组参数");
			}
			if (!std::holds_alternative<std::shared_ptr<Array>>(args[0]) ||
			    !std::holds_alternative<std::shared_ptr<Array>>(args[1])) {
				throw std::runtime_error("diff 所有参数必须是数组");
			}
			
			auto arr1 = std::get<std::shared_ptr<Array>>(args[0]);
			auto arr2 = std::get<std::shared_ptr<Array>>(args[1]);
			auto out = std::make_shared<Array>();
			
			for (const auto& elem : *arr1) {
				bool found = false;
				for (const auto& otherElem : *arr2) {
					if (valueEqual(elem, otherElem)) {
						found = true;
						break;
					}
				}
				if (!found) {
					out->push_back(elem);
				}
			}
			return Value{out};
		};
		(*arrayPkg)["diff"] = Value{diffFn};
	});
}

} // namespace asul
