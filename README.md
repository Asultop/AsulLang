# ALang 简易 JS 解释器

一个用 C++17 实现的极简 JavaScript 风格解释器，支持：

- 变量声明：`let/var/const`（目前不区分可变性）
- 基本类型：number、string、boolean、null、array、object
- 表达式：`+ - * / %`，比较与相等（严格按类型），逻辑 `&& || !`
- 访问与赋值：标识符、属性访问 `obj.a`、下标访问 `a[i]`，可作为赋值左值
- 语句：表达式语句、`if/else`、`while`、`for`、`break`、`continue`、`return`、块 `{}`
- 函数：`function name(a,b){...}`，带作用域闭包
- 内置：`print(...)`、`len(x)`、`push(arr, ...values)`
- 方法风格内置：字符串/数组/对象支持 `len()`，数组支持 `push(...)`，例如：`"abc".len()`, `[1,2].len()`, `{a:1}.len()`, `[1].push(2,3)`
- 类系统：`class` 定义、`new` 实例化、构造器 `constructor`、多继承 `class A <- (B, C)`、扩展已有类 `extends Name { ... }`

## 快速开始

### 编译

使用 g++（或 MSVC）编译，需要 C++17：

```pwsh
# g++ （如使用 MSYS2/MinGW 或 WSL）
g++ -std=c++17 -O2 Main.cpp ALangEngine.cpp -o alang

# MSVC（开发者命令行）
cl /std:c++17 /O2 Main.cpp ALangEngine.cpp
```

### 运行

```pwsh
# 运行内置示例
./alang

# 运行指定脚本文件
./alang .\example.alang
```

## 语言子集示例

```js
function add(a, b) { return a + b; }
let x = 10; let y = 20;
print("sum:", add(x, y));

let i = 0; let acc = 0;
while (i < 5) { acc = acc + i; i = i + 1; }
if (acc >= 10) { print("acc:", acc); } else { print("small", acc); }
```

## 数组/对象

```js
let arr = [1, 2, 3];
arr[1] = 42;
print(arr);           // => [1, 42, 3]

let obj = { a: 1, b: "hi" };
print(obj.a);         // => 1
obj["c"] = 7;
print(obj);           // => {c: 7, b: hi, a: 1} （键顺序未定义）
```

## for/break/continue

```js
let s = 0;
for (let k = 0; k < 10; k = k + 1) {
	if (k == 3) continue;
	if (k == 8) break;
	s = s + k;
}
print(s); // => 25
```

## 内置函数

- `print(...args)`: 输出参数并换行。
- `len(x)`: 返回长度。
	- string: 字符数；array: 元素个数；object: 键的数量；null: 0；其他类型报错。
- `push(arr, ...values)`: 将一个或多个值追加到数组末尾，返回新长度。

同时支持方法风格：

```js
"hello".len();      // 5
[1,2,3].len();       // 3
{a:1, b:2}.len();    // 2
let a = [1]; a.push(2,3); // a => [1,2,3]
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
print(d.get());     // 11
print(d.twice());   // 22
```

方法解析顺序：实例字段 → 本类方法 → 依声明顺序线性查找父类方法。

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
print("math sum 3+4:", m.sum(3,4));
print("math abs -5:", m.abs(-5));
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

- 无原型/方法调度（`arr.push` 不支持，用 `push(arr, v)`）
- 无对象/数组字面量中的计算属性名与展开
- 无 `for-of`/`for-in`，仅 C 风格 `for`
- `==` 实现为严格相等（按类型）
- 没有异常机制，仅用于函数 `return`、`break`、`continue` 的内部信号

## 结构

- `ALangEngine.h/.cpp`：解释器实现（词法、语法、AST、解释执行）
- `Main.cpp`：命令行入口

欢迎根据需要继续扩展内置函数或语法。