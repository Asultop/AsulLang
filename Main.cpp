#include "ALangEngine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <cmath>
#include <filesystem>

int main(int argc, char* argv[]) {
	ALangEngine engine;
	engine.initialize();

	// 配置错误配色：标题红色，代码行浅灰，插入符红色
	engine.setErrorColorMap({
		{"header", "RED"},
		{"code", "DARK_GRAY"},
		{"caret", "RED"},
		{"token", "RED"},
		{"lineLabel", "YELLOW"},
		{"lineValue", "CYAN"}
	});
	// 其上是默认值 (如果不进行额外配置的话)

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
				if (args.size() > 0) if (auto p = std::get_if<double>(&args[0])) a = *p;
				if (args.size() > 1) if (auto p = std::get_if<double>(&args[1])) b = *p;
				return ALangEngine::NativeValue{a + b};
			}},
			{"abs", [](const std::vector<ALangEngine::NativeValue>& args, void* /*thisHandle*/){
				double x = 0; if (!args.empty()) if (auto p = std::get_if<double>(&args[0])) x = *p;
				return ALangEngine::NativeValue{ std::fabs(x) };
			}},
		}
	);

	std::string code;
	if (argc > 1) {
		// 先读取脚本内容（使用传入路径，不依赖当前工作目录变更）
		std::ifstream in(argv[1]);
		if (!in) { std::cerr << "Cannot open file: " << argv[1] << std::endl; return 1; }
		std::ostringstream ss; ss << in.rdbuf(); code = ss.str();
		// 设置 import 相对路径基准与进程工作目录为脚本所在目录，便于脚本内部的相对路径访问（文件 I/O 与后续 import）
		try {
			std::filesystem::path inPath(argv[1]);
			std::filesystem::path base = inPath.has_parent_path() ? inPath.parent_path() : std::filesystem::current_path();
			// Make base absolute before changing CWD or setting importBaseDir
			base = std::filesystem::absolute(base);
			engine.setImportBaseDir(base.string());
			std::filesystem::current_path(base);
		} catch (...) { /* ignore base dir errors */ }
	} else {
		// 示例脚本（含原生类 Math 的调用；await/sleep；async/go/then 示例）
		code = R"( 
			// 示例：求和并打印
			function add(a, b) { return a + b; }
			let x = 10; let y = 20;
			print("sum:", add(x, y));
			import std.math.*;

			// 条件与循环
			let i = 0; let acc = 0;
			while (i < 5) { acc = acc + i; i = i + 1; }
			if (acc >= 10) { print("acc:", acc); } else { print("small", acc); }

			// 原生类 Math 测试
			let m = new Math();
			print("math sum 3+4:", m.sum(3,4));
			print("math abs -5:", m.abs(-5));

			// await/sleep 测试（阻塞等待 300ms）
			print("before sleep");
			await sleep(300);
			print("after sleep");

			// async 函数与 then/catch
			async function task(v) {
				print("task start", v);
				await sleep(200);
				print("task done", v);
				return v * 2;
			}
			function onTask(r) { print("task result", r); }
			let p = task(21);
			p.then(onTask);

			// go 语句：将调用放入事件循环执行
			go task(5);
		)";
	}

	try {
		// 设置脚本位置
		engine.execute(code);
		// 从 C++ 调用脚本函数：add(1, 2)
		// auto r = engine.callFunction("add", { 1.0, 2.0 });
		// if (std::holds_alternative<double>(r)) {
		// 	std::cout << "host add(1,2): " << std::get<double>(r) << std::endl;
		// }
		// 驱动事件循环，处理 then/catch 与 go 任务
		engine.runEventLoopUntilIdle();
	} catch (...) {
		return 1;
	}
	return 0;
}