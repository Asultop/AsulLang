#include "ALangEngine.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char* argv[]) {
	ALangEngine engine;
	engine.initialize();

	std::string code;
	if (argc > 1) {
		std::ifstream in(argv[1]);
		if (!in) { std::cerr << "Cannot open file: " << argv[1] << std::endl; return 1; }
		std::ostringstream ss; ss << in.rdbuf(); code = ss.str();
	} else {
		// 示例脚本
		code = R"( 
			// 示例：求和并打印
			function add(a, b) { return a + b; }
			let x = 10; let y = 20;
			print("sum:", add(x, y));

			// 条件与循环
			let i = 0; let acc = 0;
			while (i < 5) { acc = acc + i; i = i + 1; }
			if (acc >= 10) { print("acc:", acc); } else { print("small", acc); }
		)";
	}

	try {
		engine.execute(code);
	} catch (...) {
		return 1;
	}
	return 0;
}