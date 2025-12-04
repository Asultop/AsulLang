#include "StdMath.h"
#include "../../../AsulInterpreter.h"
#include <cmath>
#include <random>

namespace asul {

void registerStdMathPackage(Interpreter& interp) {
	interp.registerLazyPackage("std.math", [](std::shared_ptr<Object> mathPkg) {
		(*mathPkg)["pi"] = Value{ 3.14159265358979323846 };
		{
			auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
			fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
				if (args.empty()) return Value{0.0};
				double x = getNumber(args[0], "abs x");
				return Value{ x < 0 ? -x : x };
			};
			(*mathPkg)["abs"] = fn;
		}
		// Extended math functions
		{
			auto unary = [](auto op, const char* name){
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [op,name](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 1) throw std::runtime_error(std::string(name) + " expects 1 number argument");
					double x = getNumber(args[0], name);
					return Value{ op(x) };
				};
				return fn;
			};
			(*mathPkg)["sin"] = unary(static_cast<double(*)(double)>(std::sin), "sin");
			(*mathPkg)["cos"] = unary(static_cast<double(*)(double)>(std::cos), "cos");
			(*mathPkg)["tan"] = unary(static_cast<double(*)(double)>(std::tan), "tan");
			(*mathPkg)["sqrt"] = unary(static_cast<double(*)(double)>(std::sqrt), "sqrt");
			(*mathPkg)["exp"] = unary(static_cast<double(*)(double)>(std::exp), "exp");
			(*mathPkg)["log"] = unary(static_cast<double(*)(double)>(std::log), "log");
			// pow(a,b)
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 2) throw std::runtime_error("pow expects 2 number arguments");
					double a = getNumber(args[0], "pow base");
					double b = getNumber(args[1], "pow exp");
					return Value{ std::pow(a,b) };
				};
				(*mathPkg)["pow"] = fn;
			}
			// ceil / floor / round
			(*mathPkg)["ceil"] = unary(static_cast<double(*)(double)>(std::ceil), "ceil");
			(*mathPkg)["floor"] = unary(static_cast<double(*)(double)>(std::floor), "floor");
			(*mathPkg)["round"] = unary(static_cast<double(*)(double)>(std::round), "round");
			// min/max support variable number of args
			{
				auto mkVar = [](bool isMin){
					auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
					fn->builtin = [isMin](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
						if (args.empty()) throw std::runtime_error(std::string(isMin?"min":"max") + " expects at least 1 argument");
						double best = getNumber(args[0], isMin?"min arg":"max arg");
						for (size_t i=1;i<args.size();++i){ 
							double v = getNumber(args[i], isMin?"min arg":"max arg"); 
							best = isMin? (v < best ? v : best) : (v > best ? v : best); 
						}
						return Value{ best };
					};
					return fn;
				};
				(*mathPkg)["min"] = mkVar(true);
				(*mathPkg)["max"] = mkVar(false);
			}
			// random(): 0<=x<1 ; random(max): [0,max); random(min,max): [min,max)
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					static thread_local std::mt19937 rng{ std::random_device{}() };
					if (args.empty()) {
						std::uniform_real_distribution<double> dist(0.0,1.0);
						return Value{ dist(rng) };
					} else if (args.size()==1) {
						double max = getNumber(args[0], "random max");
						std::uniform_real_distribution<double> dist(0.0,max);
						return Value{ dist(rng) };
					} else if (args.size()==2) {
						double min = getNumber(args[0], "random min");
						double max = getNumber(args[1], "random max");
						if (max < min) std::swap(max,min);
						std::uniform_real_distribution<double> dist(min,max);
						return Value{ dist(rng) };
					}
					throw std::runtime_error("random expects 0,1 or 2 numeric arguments");
				};
				(*mathPkg)["random"] = fn;
			}
			// clamp(value, min, max)
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 3) {
						throw std::runtime_error("clamp expects 3 numeric arguments (value, min, max)");
					}
					double value = getNumber(args[0], "clamp value");
					double minVal = getNumber(args[1], "clamp min");
					double maxVal = getNumber(args[2], "clamp max");
					
					if (minVal > maxVal) {
						throw std::runtime_error("clamp: min must be <= max");
					}
					
					if (value < minVal) return Value{minVal};
					if (value > maxVal) return Value{maxVal};
					return Value{value};
				};
				(*mathPkg)["clamp"] = fn;
			}
			// lerp(start, end, t) - linear interpolation
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() != 3) {
						throw std::runtime_error("lerp expects 3 numeric arguments (start, end, t)");
					}
					double start = getNumber(args[0], "lerp start");
					double end = getNumber(args[1], "lerp end");
					double t = getNumber(args[2], "lerp t");
					
					return Value{start + (end - start) * t};
				};
				(*mathPkg)["lerp"] = fn;
			}
			// approxEqual(a, b, epsilon = 1e-9) - floating point approximate equality
			{
				auto fn = std::make_shared<Function>(); fn->isBuiltin = true;
				fn->builtin = [](const std::vector<Value>& args, std::shared_ptr<Environment>) -> Value {
					if (args.size() < 2 || args.size() > 3) {
						throw std::runtime_error("approxEqual expects 2 or 3 numeric arguments (a, b, [epsilon])");
					}
					double a = getNumber(args[0], "approxEqual a");
					double b = getNumber(args[1], "approxEqual b");
					double epsilon = args.size() == 3 ? getNumber(args[2], "approxEqual epsilon") : 1e-9;
					
					if (epsilon < 0) {
						throw std::runtime_error("approxEqual: epsilon must be non-negative");
					}
					
					return Value{std::abs(a - b) <= epsilon};
				};
				(*mathPkg)["approxEqual"] = fn;
			}
		}
	});
}

} // namespace asul
