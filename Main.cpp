#include "ALangEngine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <cmath>

int main(int argc, char* argv[]) {
	ALangEngine engine;
	engine.initialize();

	// 注册一个原生类：Math，提供 sum(a,b) 与 abs(x)
	engine.registerClass(
		"Math",
		// constructor: 忽略参数，无状态
		[](const std::vector<ALangEngine::NativeValue>& /*args*/, void* /*thisHandle*/)->ALangEngine::NativeValue {
			return ALangEngine::NativeValue{std::monostate{}}; // 构造器返回值将被忽略
		},
		// 方法表
		std::unordered_map<std::string, ALangEngine::NativeFunc>{
			{"sum", [](const std::vector<ALangEngine::NativeValue>& args, void* /*thisHandle*/){
				double a = 0, b = 0;
				if (args.size() > 0 && std::holds_alternative<double>(args[0])) a = std::get<double>(args[0]);
				if (args.size() > 1 && std::holds_alternative<double>(args[1])) b = std::get<double>(args[1]);
				return ALangEngine::NativeValue{a + b};
			}},
			{"abs", [](const std::vector<ALangEngine::NativeValue>& args, void* /*thisHandle*/){
				double x = 0; if (!args.empty() && std::holds_alternative<double>(args[0])) x = std::get<double>(args[0]);
				return ALangEngine::NativeValue{ std::fabs(x) };
			}},
		}
	);

	std::string code;
	if (argc > 1) {
		std::ifstream in(argv[1]);
		if (!in) { std::cerr << "Cannot open file: " << argv[1] << std::endl; return 1; }
		std::ostringstream ss; ss << in.rdbuf(); code = ss.str();
	} else {
		// 示例脚本（含原生类 Math 的调用）
		code = R"( 
			// 示例：求和并打印
			function add(a, b) { return a + b; }
			let x = 10; let y = 20;
			print("sum:", add(x, y));

			// 条件与循环
			let i = 0; let acc = 0;
			while (i < 5) { acc = acc + i; i = i + 1; }
			if (acc >= 10) { print("acc:", acc); } else { print("small", acc); }

			// 原生类 Math 测试
			let m = new Math();
			print("math sum 3+4:", m.sum(3,4));
			print("math abs -5:", m.abs(-5));
		)";
	}

	try {
		engine.execute(code);
		// 从 C++ 调用脚本函数：add(1, 2)
		auto r = engine.callFunction("add", { 1.0, 2.0 });
		if (std::holds_alternative<double>(r)) {
			std::cout << "host add(1,2): " << std::get<double>(r) << std::endl;
		}
	} catch (...) {
		return 1;
	}
	return 0;
}