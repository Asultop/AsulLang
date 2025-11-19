# ALang 简易解释器

ALang 是一个用 C++17 实现的轻量脚本语言解释器/运行时，目标用于嵌入式脚本扩展与实验性语言特性验证。核心代码位于 `ALangEngine.cpp`/`ALangEngine.h`，命令行入口为 `Main.cpp`。

**主要特性**
- 基本类型：number、string、boolean、null、array、object
- 变量与作用域：`let/var/const`（当前实现未严格区分可变性）
- 表达式与语句：算术、比较、逻辑运算；`if/else`、`while`、`for`、`break`/`continue`、`return`、块语句
- 函数与闭包：`function`、匿名 lambda `[](args){}`、函数作为一等值
- 类与继承：`class`、`new`、构造器 `constructor`、多继承语法 `class A <- (B, C)`、`extends` 扩展
- 接口：`interface` 用作方法签名与多继承占位
- 异步与事件循环：`async/await`、`Promise`、`then/catch`、`go` 将任务投递到事件循环
- 元编程：`eval(string)` 与 `quote(string)`，支持 token 级别的源码修改与 `apply()`
- 模块与导入：文件导入与包导入 (`import` / `from`)，示例见 `Example/` 目录

**仓库结构（主要）**
- `Main.cpp`：CLI 入口，初始化引擎并执行脚本
- `ALangEngine.h` / `ALangEngine.cpp`：解释器实现、运行时与标准内置
- `Example/`：语言特性示例脚本
- `AsulFormatString/`：用于格式化输出的辅助库

**快速开始**

构建（在仓库根目录）：

```bash
bash build.sh
```

说明：`build.sh` 会用编译器（如 g++）编译 `Main.cpp` 与 `ALangEngine.cpp`。也可以手动使用：

```bash
g++ -std=c++17 -O2 Main.cpp ALangEngine.cpp -o alang
```

运行：

```bash
# 运行单个示例文件
./alang Example/example.alang

# 运行其它示例
./alang Example/lambdaExample.alang
./alang Example/evalExample.alang
```

如果脚本使用了异步（`then/catch` / `go`），宿主（CLI）通常在脚本执行后调用 `runEventLoopUntilIdle()` 来处理事件循环任务（CLI 已在 `Main.cpp` 中演示此调用）。

**常用内置函数**
- `print(...args)`、`println(...args)`：输出（后者换行）
- `len(x)`：字符串/数组/对象的长度
- `push(arr, ...values)`：向数组追加元素
- `sleep(ms)`：返回在 `ms` 毫秒后 resolve 的 Promise

**模块与文件导入**
- 支持相对/绝对路径导入：`import "path/to/module"`（后缀 `.alang` 可省略）
- 相对路径基于主脚本文件所在目录（可通过宿主调用 `setImportBaseDir()` 设置）
- 多次导入去重以避免重复执行

**宿主 (C++) 集成要点**
ALang 提供简单的宿主注册接口（在 `ALangEngine.h` 中声明）：

- `using NativeValue = std::variant<std::monostate,double,std::string,bool>`：宿主与脚本之间的值桥接仅支持基元类型
- `using NativeFunc = std::function<NativeValue(const std::vector<NativeValue>&, void* thisHandle)>`
- `registerClass(const std::string& className, NativeFunc constructor, const std::unordered_map<std::string, NativeFunc>& methods, const std::vector<std::string>& baseClasses = {})`：注册原生类
- `callFunction(const std::string& functionName, const std::vector<NativeValue>& args)`：从 C++ 调用脚本全局函数
- `setGlobal(const std::string& name, const NativeValue& value)` / `registerFunction(...)`：设置全局变量和注册宿主函数
- `runEventLoopUntilIdle()`：驱动事件循环以处理 `then`/`go` 等异步任务

示例（摘自 `Main.cpp`）：

```cpp
engine.registerClass(
	"Math",
	/* constructor */ [](const std::vector<ALangEngine::NativeValue>&, void*){ return ALangEngine::NativeValue{std::monostate{}}; },
	/* methods */ std::unordered_map<std::string, ALangEngine::NativeFunc>{
		{"sum", [](const std::vector<ALangEngine::NativeValue>& args, void*){ /* ... */ }},
		{"abs", [](const std::vector<ALangEngine::NativeValue>& args, void*){ /* ... */ }}
	}
);
```

**示例脚本**
请查看 `Example/` 目录（含大量示例：`lambdaExample.alang`、`quoteExample.alang`、`evalExample.alang`、`interfaceExample.alang` 等）。

**开发与调试**
- 代码主要位于 `ALangEngine.cpp`，包含词法、解析、执行与运行时实现。阅读该文件可以了解语言实现细节与扩展点。

**许可**
项目以仓库内的 `LICENSE` 文件为准。

---

如果你希望我将 README 翻译为英文、补充 API 细节或生成更结构化的文档（例如 `docs/`），我可以继续完善。
# 运行更多示例
.
\alang.exe .\lambdaExample.alang
.
.
\alang.exe .\Example\computedProps.alang
.
\alang.exe .\Example\interfaceExample.alang
\alang.exe .\Example\builtins_test.alang
```

如果脚本中使用了 then/catch 或 go 等异步特性，运行后需要排空事件循环（对 CLI 版 Main.cpp，可在执行脚本后调用一次 `runEventLoopUntilIdle()`）。

## 语言子集示例

```js
.
\alang.exe .\Example\example.alang
println("sum:", add(x, y));

.
\alang.exe .\lambdaExample.alang
.
\alang.exe .\Example\builtins_test.alang
.
\alang.exe .\Example\try_catchExample.alang

## 数组/对象

```js
let arr = [1, 2, 3];
arr[1] = 42;
println(arr);           // => [1, 42, 3]

let obj = { a: 1, b: "hi" };
println(obj.a);         // => 1
obj["c"] = 7;
println(obj);           // => {c: 7, b: hi, a: 1} （键顺序未定义）
```

## eval 与 quote（代码文本操作）

ALang 提供两个用于在运行时处理源代码文本的内置：`eval(string)` 与 `quote(string)`，常用于元编程或简单宏式变换。

- `eval(string)`：在一个子环境中解析并执行传入的代码片段（不会向外部环境泄漏变量）。
	- 若传入是单个表达式，返回该表达式的值。
	- 若传入是多语句且最后一条是表达式，返回最后表达式的值；若最后一条不是表达式（例如以分号结尾的语句），返回 `null`。
	- 解析策略有容错：尝试完整解析；若失败会尝试追加分号或作为单表达式片段解析。

- `quote(string)`：将源代码字符串词法化，返回一个对象 `{ tokens, source, apply }`：
	- `tokens`：数组，每个 token 为对象，字段包括 `token`（Token 名称，如 `Number`/`Identifier`/`Plus`/`Semicolon`/`String`/`Let`/`False` 等）、`lexeme`（原始文本）、`line`、`column`、`length`。
	- 你可以在 `tokens` 上就地修改 `token`/`lexeme` 或替换整个数组，然后调用 `apply()` 将重建的源码执行（在子环境中），`apply()` 的返回语义同 `eval`（最后语句值或 `null`）。

示例要点：
- 若希望 `apply()` 返回一个值，请确保重建的代码的最后一条语句是一个裸表达式（例如 `expr`），而非 `println(...)`（`println` 返回 `null`）。
- `quote` 适合做 Token 级的修改（如替换运算符、替换标识符、插入/删除 token 片段、拼接多段代码），但请谨慎处理字符串字面量中的引号转义（示例中通过直接构造 `String` token 避免嵌套引号问题）。

运行示例：
```bash
# 构建（在 repo 根目录）
bash build.sh

# 运行基础 eval 示例
./alang Example/evalExample.alang

# 运行简单 quote 示例
./alang Example/quoteExample.alang

# 运行进阶 quote 示例（复杂 token 级改写、拼接、模板生成）
./alang Example/quote_complex.alang
```

更多示例请参阅 `Example/` 目录下的 `quoteExample.alang`, `quote_complex.alang`, `evalExample.alang`。

## 计算属性名

对象字面量支持计算属性名（方括号内为任意表达式，值将转为字符串用作键）：

```js
let key = "dyn";
let o = { [key]: 1, ["x"]: 2, y: 3 };
println(o["dyn"]); // 1
println(o.x);       // 2
println(o.y);       // 3
```

## 字符串插值

使用 `${...}` 在字符串字面量中内联表达式，按字符串拼接语义求值：

```js
let a = 42; let b = { x: 7 };
println("a:${a}, b.x:${b.x}, sum:${a + b.x}");
```

注意：若插值表达式以对象字面量开头，可能与外层字符串的 `{`/`}` 冲突，建议先赋值到变量再插值：

```js
let obj = { k: a, y: 2 };
println("obj.k:${obj.k}");
```

## 模块与导入（import/from）

内置包通过 `import`/`from` 导入到当前作用域。当前提供 `Math` 包：`pi`、`abs(x)`。

支持形式：

```js
// 1) 指定包内符号列表
import Math.(pi, abs);

// 2) 列出包名+符号的列表（可跨包）
import (Math.pi, Math.abs);

// 3) from 形式
from Math import abs;
from Math import (pi, abs);

// 4) 通配导入
import Math.*;

println("pi:", pi, ", abs(-3):", abs(-3));
```

更多见 `Example/importExample.alang`。

## 文件导入

支持将其他脚本文件导入到当前作用域：

```js
// 单个文件
import "path/to/module";      // 后缀 .alang 可省略

// 多个文件（可混合）
import ("modA", "modB");
import (Math.pi, Math.abs, "utils");

// 使用导入的符号
greetA("world");
println(A);
```

行为说明：

- 相对路径基准：主脚本文件所在目录（由宿主在运行入口设置）。
- 绝对路径：直接使用。
- 后缀补全：若导入路径无扩展名，将先尝试原路径，若不存在再尝试追加 `.alang`。
- 多次导入去重：同一绝对路径只会导入执行一次（防止循环与重复）。
- 符号合入：导入文件在能访问全局内置的隔离环境中执行，执行完成后其顶层定义将“通配式”合入当前作用域（类似 `import Package.*`）。

示例见 `Example/fileImportExample.alang`（依赖 `Example/modA.alang` 与 `Example/modB.alang`）。

## for/break/continue

```js
let s = 0;
for (let k = 0; k < 10; k = k + 1) {
	if (k == 3) continue;
	if (k == 8) break;
	s = s + k;
}
println(s); // => 25
```

## 内置函数

- `print(...args)`: 扁平输出（无分隔、无换行）。
- `println(...args)`: 扁平输出并换行。
- `len(x)`: 返回长度。
	- string: 字符数；array: 元素个数；object: 键的数量；null: 0；其他类型报错。
- `push(arr, ...values)`: 将一个或多个值追加到数组末尾，返回新长度。
- `sleep(ms)`: 返回一个在 `ms` 毫秒后 resolve 的 Promise。

同时支持方法风格：

```js
"hello".len();      // 5
[1,2,3].len();       // 3
{a:1, b:2}.len();    // 2
let a = [1]; a.push(2,3); // a => [1,2,3]
```

## 异步、Promise 与事件循环

- 事件循环：内部维护任务队列；内置 `postTask` 调度 then/catch 与 `go` 任务。
- `await`：阻塞当前脚本直到 Promise settle。
- `async function f(...) { ... }`：调用返回 Promise，函数体在事件循环任务中执行。
- `go expr;`：将表达式（通常是调用）异步投递到事件循环执行，丢弃异常。
- Promise：支持 `then` / `catch`，返回新 Promise 的链式语义；支持 `Promise.resolve(value)` 与 `Promise.reject(reason)`。
- 匿名函数（lambda）：`[](args){ ... }` 可直接作为 then/catch 回调。

示例：

```js
// 链式 then/catch 与 lambda
Promise.resolve(1)
	.then([](v){ return v + 10; })
	.then([](v){ println("v2", v); return v; })
	.catch([](e){ println("err", e); return 0; });

// async + await + sleep
async function task(n) {
	println("before sleep", n);
	await sleep(100);
	println("after sleep", n);
	return n * 2;
}

task(5)
	.then([](r){ println("task result:", r); return r; })
	.catch([](e){ println("task error:", e); return 0; });

// go 异步触发
function tick(){ println("tick"); }
go tick();
```

主程序（宿主）在执行脚本后应调用一次 `runEventLoopUntilIdle()` 来排空事件循环（CLI 示例工程已暴露该接口）。

## 异常（throw / try...catch）

- 抛出：`throw <value>;` 支持任意值（string/number/object 等）。
- 捕获：`try { ... } catch(e) { ... }`，`e` 绑定为抛出的值。
- await：等待被拒绝的 Promise 会抛出语言级异常，可被外层 `try...catch` 捕获。
- async：函数体内未捕获的异常会使返回的 Promise 进入 reject，拒绝值为异常值。
- then/catch：回调抛出的异常将使链上下一个 Promise 变为 reject；回调返回 Promise 将被扁平化。
- go：`go expr;` 中抛出的异常会被吞掉，不会影响主流程（可在日志中自行扩展记录）。

示例：

```js
// 1) 直接 throw + try/catch
try {
	throw "bad";
	println("unreached");
} catch(e) {
	println("caught:", e);
}

// 2) await 拒绝被捕获
async function task() {
	try {
		await Promise.reject("oops");
		println("unreached in task");
	} catch(e) {
		println("task caught:", e);
	}
	return 42;
}
task().then([](v){ println("task ret:", v); });

// 3) then 回调抛出异常由链式 catch 捕获
Promise.resolve(1)
	.then([](v){ println("v=", v); throw "fail at then"; })
	.catch([](e){ println("chain caught:", e); return 0; })
	.then([](v){ println("after catch:", v); });

// 4) go 中的异常被吞掉
function boom(){ throw "boom"; }
go boom();
```

## 类、继承与扩展

- 定义：
	- 空类：`class Name;`
	- 单继承：`class Name <- Base { ... }`
	- 多继承：`class Name <- (Base1, Base2) { ... }`
- 方法：类体内按 `function name(args) { ... }` 书写；构造器名固定为 `constructor`
- 实例化：`let o = new Name(args...)`
- 扩展已有类：`extends Name { function method() { ... } }`（可新增或覆写方法）

示例：

```js
class Base {
	function constructor(v) { this.v = v; }
	function get() { return this.v; }
}

class Derived <- Base {
	function inc() { this.v = this.v + 1; }
}

extends Derived {
	function twice() { return this.v * 2; }
}

let d = new Derived(10);
d.inc();
println(d.get());     // 11
println(d.twice());   // 22
```

方法解析顺序：实例字段 → 本类方法 → 依声明顺序线性查找父类方法。

## 接口（interface）与动态派发

- 声明：
	- `interface Name;`
	- `interface Name { function ping(a); function pong(); }`（仅签名，分号结尾）
- 使用：可被类以多继承方式继承，如 `class D <- (Base, Name) { ... }`；接口本身不提供实现，但参与类型结构与方法查找。

示例：

```js
interface Inter { function ping(a); };

class Base { function ping(a) { println("Base.ping", a); } }

class D <- (Base, Inter) { function pong() { println("D.pong"); } }

let d = new D();
d.ping(7); // 动态派发到 Base.ping
d.pong();
```

## 宿主原生类注册（C++）

可通过 `ALangEngine::registerClass` 将宿主原生类暴露给脚本使用：

- 类型：
	- `using NativeValue = std::variant<std::monostate,double,std::string,bool>;`
	- `using NativeFunc = std::function<NativeValue(const std::vector<NativeValue>&, void* thisHandle)>;`
- 接口：
	- `void registerClass(const std::string& className, NativeFunc constructor, const std::unordered_map<std::string, NativeFunc>& methods, const std::vector<std::string>& baseClasses = {});`
- 说明与限制：
	- 仅支持基元参数与返回；复杂类型在桥接时被视作 `null`
	- `thisHandle` 是脚本实例的内部指针（opaque），可用来标识实例；当前不提供直接读写实例字段的宿主 API
	- 构造器返回值会被忽略

示例（摘自 `Main.cpp`）：

```cpp
engine.registerClass(
	"Math",
	// 构造器：忽略参数
	[](const std::vector<ALangEngine::NativeValue>&, void*){ return ALangEngine::NativeValue{std::monostate{}}; },
	std::unordered_map<std::string, ALangEngine::NativeFunc>{
		{"sum", [](const std::vector<ALangEngine::NativeValue>& args, void*){
			 double a = 0, b = 0;
			 if (args.size()>0 && std::holds_alternative<double>(args[0])) a = std::get<double>(args[0]);
			 if (args.size()>1 && std::holds_alternative<double>(args[1])) b = std::get<double>(args[1]);
			 return ALangEngine::NativeValue{a + b};
		}},
		{"abs", [](const std::vector<ALangEngine::NativeValue>& args, void*){
			 double x = 0; if (!args.empty() && std::holds_alternative<double>(args[0])) x = std::get<double>(args[0]);
			 return ALangEngine::NativeValue{ std::fabs(x) };
		}}
	}
);
```

脚本侧：

```js
let m = new Math();
println("math sum 3+4:", m.sum(3,4));
println("math abs -5:", m.abs(-5));
```

## 从 C++ 调用脚本函数

提供 `ALangEngine::callFunction` 以调用全局函数（仅基元参数/返回）：

```cpp
ALangEngine engine;
engine.initialize();
engine.execute("function add(a,b){ return a+b; }");

auto ret = engine.callFunction("add", { 1.0, 2.0 });
if (std::holds_alternative<double>(ret)) {
	std::cout << std::get<double>(ret) << std::endl; // 3
}
```

## 限制

- 已支持常用方法风格（如 `arr.push(...)`、`"s".len()`），但无原型链与动态派发
- 支持对象/数组字面量中的计算属性名与展开（`[expr]` 与 `...expr`）
- 无 `for-of`/`for-in`，仅 C 风格 `for`
- `==` 实现为类似 ECMAScript 的抽象相等（带类型强制），`===` 为严格相等（按类型与引用）
- 支持语言级异常（throw / try...catch），但暂不支持 finally；`go` 中异常被吞掉；宿主未提供统一日志钩子

## 结构

- `ALangEngine.h/.cpp`：解释器实现（词法、语法、AST、解释执行、事件循环、Promise）
- `Main.cpp`：命令行入口（可执行加载脚本并在末尾调用 `runEventLoopUntilIdle()`）

更多示例：见 `Example/` 与 `lambdaExample.alang`。

欢迎根据需要继续扩展内置函数或语法。