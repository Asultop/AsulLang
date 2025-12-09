<p align="center"><img src="./picture/ALang.png" width="128" align="center"></p>
<h1 align="center"> Asul Language </h1>

<div align="center">
  <a href="https://deepwiki.com/Asultop/AsulLang" target="_blank">
    <img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki" />
  </a>
  <img src="https://img.shields.io/badge/language-C%2B%2B17-blue.svg" alt="C++17" />
  <img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License" />
  <img src="https://img.shields.io/badge/platform-cross--platform-orange.svg" alt="Cross Platform" />
  
  <p align="center">轻量、高效的嵌入式脚本语言解释器，专注于扩展能力与特性验证</p>
</div>

---

## 📋 目录
- [项目简介](#-项目简介)
- [核心特性](#-核心特性)
- [仓库结构](#-仓库结构)
- [快速开始](#-快速开始)
  - [构建步骤](#构建步骤)
  - [运行方式](#运行方式)
  - [平台注意事项](#平台注意事项)
- [VSCode 语法高亮插件](#-vscode-语法高亮插件)
- [语言特性详解](#-语言特性详解)
  - [基础语法](#基础语法)
  - [数据类型与内置函数](#数据类型与内置函数)
  - [数组方法](#数组方法)
  - [字符串方法](#字符串方法)
  - [流程控制](#流程控制)
  - [函数特性](#函数特性)
  - [面向对象](#面向对象)
  - [异步编程](#异步编程)
  - [元编程](#元编程)
  - [模块与导入](#模块与导入)
  - [文件I/O](#文件-io-stdio)
- [示例运行](#-示例运行)
- [宿主集成（C++）](#-宿主集成c)
- [开发与调试](#-开发与调试)
- [限制说明](#%EF%B8%8F-限制说明)
- [架构设计](#-架构设计)
- [许可协议](#-许可协议)

---

## 📖 项目简介
ALang 是一款基于 C++17 开发的轻量脚本语言解释器/运行时，专为嵌入式场景的脚本扩展与实验性语言特性验证设计。核心代码高度模块化，易于集成到各类 C++ 项目中，同时提供完善的语法特性与标准库支持。

核心入口文件：
- 引擎核心：`ALangEngine.cpp` / `ALangEngine.h`
- 命令行入口：`Main.cpp`

> 📚 详细文档：有关 Token、类型系统及完整 API 列表，请参阅 [ALang 技术参考手册](ALang_Technical_Reference.md)。

---

## ✨ 核心特性
- 📦 **基础类型完备**：number、string、boolean、null、array、object
- 🔧 **变量与作用域**：`let/var/const` 声明（支持块级作用域）
- 🧮 **表达式与语句**：算术/比较/逻辑运算；`if/else`、`while`、`for`、`foreach` 等
- ⚙️ **增强运算符**：增量（`++`/`--`）、复合赋值（`+=`/`-=` 等）、位运算
- 🔗 **函数与闭包**：命名函数、匿名 lambda、函数一等公民特性
- 🏷️ **面向对象**：类（`class`）、多继承（`<- (B, C)`）、`extends` 扩展、构造器
- 📋 **接口契约**：`interface` 方法签名定义与动态匹配（`=~=` 运算符）
- ⚡ **异步编程**：`async/await`、`Promise`、`then/catch`、事件循环
- 🔍 **元编程能力**：`eval(string)` 动态执行、`quote(string)` Token 级源码操作
- 📥 **模块系统**：文件导入与包导入（`import`/`from`），支持去重机制
- 📚 **标准库丰富**：字符串、数学、文件 I/O、网络、JSON/XML/YAML 解析等

---

## 📂 仓库结构
```
ALang/
├── Main.cpp                  # 命令行入口（CLI）
├── ALangEngine.h/cpp         # 引擎外观层（Facade）- 协调核心模块
├── AsulFormatString/         # 格式化输出辅助库
├── Example/                  # 语言特性示例脚本（含所有功能演示）
│   ├── array_methods_test.alang
│   ├── fileIOExample.alang
│   ├── lambdaExample.alang
│   └── ...（更多示例）
├── vscode-extension/         # VSCode 语法高亮扩展
│   ├── package.json          # 扩展清单文件
│   ├── syntaxes/             # TextMate 语法定义
│   │   └── alang.tmLanguage.json
│   ├── language-configuration.json  # 语言配置
│   ├── examples/             # 语法高亮示例
│   ├── images/               # 扩展图标
│   └── *.md                  # 扩展文档
├── src/                      # 模块化核心组件
│   ├── AsulLexer.h/cpp       # 词法分析器（TokenType、Token、Lexer）
│   ├── AsulParser.h/cpp      # 递归下降语法分析器
│   ├── AsulAst.h             # AST 节点定义（表达式/语句）
│   ├── AsulRuntime.h/cpp     # 运行时系统（Value、Environment、ClassInfo）
│   ├── AsulInterpreter.h/cpp # 解释器核心（执行引擎、事件循环）
│   ├── AsulAsync.h           # 异步操作接口（解耦异步逻辑）
│   ├── AsulPackages.h        # 标准库包注册入口
│   └── AsulPackages/         # 外部标准库实现
│       ├── Std/              # std.* 核心包
│       │   ├── Path/         # 路径处理（std.path）
│       │   ├── String/       # 字符串工具（std.string）
│       │   ├── Math/         # 数学函数（std.math）
│       │   ├── Time/         # 时间处理（std.time）
│       │   ├── Os/           # 系统调用（std.os）
│       │   ├── Regex/        # 正则表达式（std.regex）
│       │   ├── Encoding/     # 编码转换（std.encoding）
│       │   └── Network/      # 网络操作（std.network）
│       ├── Json/             # JSON 解析/序列化
│       ├── Xml/              # XML 解析/序列化
│       ├── Yaml/             # YAML 解析/序列化
│       └── Os/               # 系统扩展包
├── CMakeLists.txt            # 跨平台构建配置
├── LICENSE                   # 许可协议文件
└── README.md                 # 项目说明文档（本文档）
```

---

## 🚀 快速开始

### 构建步骤
推荐使用 CMake 进行跨平台构建，支持 Windows/macOS/Linux。

1. **创建构建目录并配置**
```bash
mkdir -p build_cmake
cd build_cmake
cmake ..
```

> 📝 可选配置：指定构建类型或安装路径
```bash
#  Release 模式 + 自定义安装前缀（如 /usr/local）
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
```

2. **并行构建**
```bash
# macOS/Linux：使用全部 CPU 核心加速构建
cmake --build . -- -j $(nproc)  # Linux
cmake --build . -- -j $(sysctl -n hw.ncpu)  # macOS

# Windows（PowerShell）
cmake --build . --config Release -- -m
```

3. **可选安装（系统级可用）**
```bash
cmake --install .
```

### 运行方式
```bash
# 1. 启动交互式 REPL（支持行编辑与历史记录）
./alang

# 2. 执行指定脚本文件
./alang -f Example/array_methods_test.alang

# 3. 查看帮助/版本信息
./alang --help
./alang --version
```

### 平台注意事项
#### Windows
```powershell
# 使用 CMake GUI 或命令行配置
mkdir build
cd build
cmake ..
cmake --build . --config Release

# 运行
.\Release\alang.exe
```
- **编译器**: 需要 Visual Studio 2017 或更高版本（支持 C++17）
- **网络功能**: 自动链接 Winsock2 (ws2_32.lib)
- **FFI**: 使用 LoadLibrary/GetProcAddress，支持 .dll 动态库
- **OpenSSL**: 可选，用于加密功能（crypto 包）

#### macOS
```bash
# 推荐安装依赖（提升 REPL 体验与构建速度）
brew install readline ccache
```
- `readline`：提供 REPL 行编辑、历史记录功能（未安装则回退简单输入）
- `ccache`：加速增量构建（可选）

#### Linux
```bash
# 确保安装必要的开发工具
sudo apt-get update
sudo apt-get install build-essential cmake libssl-dev libreadline-dev

# 编译和运行
mkdir build && cd build
cmake ..
make -j$(nproc)
./alang
```
- **依赖**: GCC 7+ 或 Clang 5+（支持 C++17）
- **网络功能**: 使用标准 POSIX socket API
- **FFI**: 使用 dlopen/dlsym，支持 .so 动态库

#### 跨平台特性支持
| 特性 | Linux | macOS | Windows |
|------|-------|-------|---------|
| 核心语言 | ✅ | ✅ | ✅ |
| 异步/Promise | ✅ | ✅ | ✅ |
| 文件 I/O | ✅ | ✅ | ✅ |
| 网络 Socket | ✅ | ✅ | ✅ (Winsock2) |
| FFI (动态库) | ✅ (.so) | ✅ (.dylib) | ✅ (.dll) |
| OpenSSL 加密 | ✅ | ✅ | ✅ |
| Readline REPL | ✅ | ✅ | ⚠️ (fallback) |

#### 异步脚本注意事项
若脚本使用 `then/catch`、`go` 等异步特性，宿主程序需在脚本执行后调用 `runEventLoopUntilIdle()` 处理事件循环任务（CLI 已在 `Main.cpp` 中内置该逻辑）。

---

## 🎨 VSCode 语法高亮插件

ALang 提供官方 Visual Studio Code 语法高亮扩展，为 `.alang` 文件提供完整的语法着色和编辑器支持。

### 特性

- **完整语法高亮**：支持所有 ALang 语言特性
  - 关键字（let、var、const、function、class、async、await 等）
  - 控制流语句（if、while、for、foreach、switch 等）
  - 特殊运算符（`=~=`、`?.`、`??`、`<-`、`=>` 等）
  - 字符串插值和模板字面量
  - 多种注释风格（`//`、`/* */`、`#`、`"""`、`'''`）

- **编辑器功能**：
  - 括号匹配和自动闭合
  - 注释切换（Ctrl+/）
  - 代码折叠支持
  - 智能缩进

### 安装

**方法 1：从源码安装（开发）**

```bash
# 复制扩展到 VSCode 扩展目录
# Windows:
xcopy /E /I /Y vscode-extension "%USERPROFILE%\.vscode\extensions\alang-language-support-0.1.0"

# macOS/Linux:
mkdir -p ~/.vscode/extensions/alang-language-support-0.1.0
cp -r vscode-extension/* ~/.vscode/extensions/alang-language-support-0.1.0/

# 重新加载 VSCode 窗口
```

**方法 2：打包并安装**

```bash
cd vscode-extension
npm install -g vsce
vsce package
code --install-extension alang-language-support-0.1.0.vsix
```

### 使用

安装后，VSCode 会自动为 `.alang` 文件应用语法高亮。打开任何 ALang 脚本文件即可享受完整的编辑器支持。

### 文档

- [安装指南](vscode-extension/INSTALL.md)
- [语法参考](vscode-extension/SYNTAX-REFERENCE.md)
- [开发者指南](vscode-extension/DEVELOPER.md)
- [示例文件](vscode-extension/examples/)

---

## 🔍 语言特性详解

### 基础语法
#### 变量声明
```javascript
let a = 42;          // 块级作用域变量
var b = "hello";     // 函数级作用域变量
const c = true;      // 常量（不可重新赋值）
```

#### 数组与对象
```javascript
// 数组
let arr = [1, 2, 3];
arr.push(4);         // 追加元素
println(arr.len());  // 输出：4

// 对象
let obj = { name: "ALang", version: 1.0 };
obj["author"] = "Dev";  // 动态添加属性
println(obj.name);       // 输出：ALang
```

#### 计算属性与字符串插值
```javascript
// 计算属性名
let key = "dynamicKey";
let obj = { [key]: 123, [1 + 2]: "456" };
println(obj.dynamicKey);  // 123
println(obj[3]);          // 456

// 字符串插值
let x = 10, y = 20;
println(`x + y = ${x + y}, obj.key = ${obj.dynamicKey}`);
// 输出：x + y = 30, obj.key = 123
```

### 数据类型与内置函数
| 类型       | 说明                  | 内置函数示例                  |
|------------|-----------------------|-----------------------------|
| number     | 数值（64位浮点数）    | `len(x)`、`sleep(ms)`       |
| string     | 字符串（UTF-8 编码）  | `print(...)`、`println(...)`|
| boolean    | 布尔值（true/false）  | -                           |
| null       | 空值                  | -                           |
| array      | 动态数组              | `push(arr, ...vals)`        |
| object     | 键值对集合            | -                           |

核心内置函数：
- `print(...args)`：无分隔符、无换行输出
- `println(...args)`：无分隔符、带换行输出
- `len(x)`：返回字符串/数组/对象的长度
- `push(arr, ...vals)`：向数组追加元素，返回新长度
- `sleep(ms)`：返回指定毫秒后 resolve 的 Promise

### 数组方法
支持函数式编程风格的数组方法，回调函数接收 `(element, index, array)` 参数：
```javascript
let numbers = [1, 2, 3, 4, 5];

// 映射转换
let doubled = numbers.map([](x) { return x * 2; });  // [2,4,6,8,10]

// 过滤元素
let evens = numbers.filter([](x) { return x % 2 === 0; });  // [2,4]

// 归约计算
let sum = numbers.reduce([](acc, x) { return acc + x; }, 0);  // 15

// 链式调用
let result = numbers.filter([](x) { return x > 2; })
                    .map([](x) { return x * x; })
                    .reduce([](acc, x) { return acc + x; }, 0);  // 3²+4²+5²=50
```
示例文件：`Example/array_methods_test.alang`

### 字符串方法
```javascript
let str = "Hello ALang";

// 分割字符串
let parts = str.split(" ");  // ["Hello", "ALang"]

// 提取子串
let sub = str.substring(6);  // "ALang"

// 替换内容
let replaced = str.replace("ALang", "Script");  // "Hello Script"

// 字符遍历
foreach (ch in str) {
    println(ch);
}
```
示例文件：`Example/string_methods_test.alang`

### 流程控制
#### foreach 枚举
```javascript
// 遍历数组
foreach (item in [1, 2, 3]) {
    println(item);
}

// 遍历对象（键名）
foreach (key in {a:1, b:2}) {
    println(`${key}: ${obj[key]}`);
}

// 遍历字符串（字符）
foreach (ch in "Hello") {
    println(ch);
}
```

#### Switch/Case 语句
```javascript
let fruit = "apple";
switch (fruit) {
    case "apple":
        println("苹果：5元/斤");
        break;
    case "banana":
        println("香蕉：3元/斤");
        break;
    default:
        println("未知水果");
}
```
支持 `break`/`continue`、fall-through 特性、嵌套 switch。

#### 三元运算符
```javascript
let age = 18;
let status = age >= 18 ? "成年" : "未成年";
println(status);  // 输出：成年
```

### 函数特性
#### 剩余参数（Rest Parameters）
```javascript
function sum(...numbers) {
    let total = 0;
    foreach (num in numbers) {
        total += num;
    }
    return total;
}

println(sum(1, 2, 3));  // 6
println(sum(10, 20, 30, 40));  // 100
```

#### 默认参数（Default Parameters）
```javascript
function greet(name, greeting = "Hello") {
    println(`${greeting}, ${name}!`);
}

greet("Alice");  // Hello, Alice!
greet("Bob", "Hi");  // Hi, Bob!
```

#### 方法重写与重载
```javascript
// 方法重写
class Animal {
    function speak() {
        println("动物发出声音");
    }
}

class Dog <- (Animal) {
    function speak() {  // 重写父类方法
        println("汪！汪！");
    }
}

// 函数重载模拟（通过默认参数）
function calculate(a, b = 0, op = "+") {
    switch (op) {
        case "+": return a + b;
        case "-": return a - b;
        default: return null;
    }
}
```

### 面向对象
#### 类与继承
```javascript
// 基类
class Base {
    function constructor(value) {
        this.value = value;
    }

    function getValue() {
        return this.value;
    }
}

// 多继承
class Mixin {
    function increment() {
        this.value += 1;
    }
}

class Derived <- (Base, Mixin) {
    function doubleValue() {
        return this.value * 2;
    }
}

// 实例化与使用
let obj = new Derived(10);
obj.increment();
println(obj.getValue());  // 11
println(obj.doubleValue());  // 22
```

#### 接口与类型匹配
```javascript
// 声明接口
interface Printable {
    function print();
}

// 实现接口
class Document <- (Printable) {
    function print() {
        println("打印文档内容");
    }
}

// 类型匹配（=~= 运算符）
let doc = new Document();
if (doc =~= Printable) {
    doc.print();  // 输出：打印文档内容
}
```

### 异步编程
```javascript
// Promise 链式调用
Promise.resolve(1)
    .then([](v) { return v + 10; })
    .then([](v) { println("结果：", v); })
    .catch([](e) { println("错误：", e); });

// async/await 语法
async function task() {
    println("开始任务");
    await sleep(1000);  // 等待 1 秒
    println("任务完成");
    return 42;
}

// 异步投递任务（go 关键字）
go task().then([](res) { println("异步结果：", res); });

// 事件循环（宿主需调用 runEventLoopUntilIdle()）
```

### 元编程
#### eval 动态执行
```javascript
let x = 10;
let result = eval("x * 2 + 5");
println(result);  // 25
```

#### quote Token 操作
```javascript
// 解析源码为 Token 数组
let quoted = quote("let a = 1 + 2;");

// 修改 Token（示例：将 1 改为 10）
foreach (token in quoted.tokens) {
    if (token.token === "Number" && token.lexeme === "1") {
        token.lexeme = "10";
    }
}

// 执行修改后的代码
quoted.apply();
println(a);  // 12
```

### 模块与导入
#### 包导入
```javascript
// 导入指定符号
import std.math.(pi, abs);

// from 语法
from std.string import split;

// 通配导入
import std.io.*;

// 多包混合导入
import (std.math.pi, "utils.alang");
```

#### 文件导入
```javascript
// 导入单个文件（后缀可省略）
import "path/to/module";

// 导入多个文件
import ("modA", "modB");

// 使用导入的符号
println(utils.add(1, 2));
```

### 文件 I/O (std.io)
#### 函数式 API
| 函数名          | 说明                  | 示例                          | 返回值   |
|-----------------|-----------------------|-------------------------------|----------|
| `readFile(path)`| 读取文件全部内容      | `readFile("test.txt")`        | string   |
| `writeFile(path, data)` | 覆盖写入文件 | `writeFile("test.txt", "Hi")` | boolean  |
| `appendFile(path, data)` | 追加写入 | `appendFile("test.txt", "Hello")` | boolean |
| `exists(path)`  | 判断路径是否存在      | `exists("test.txt")`          | boolean  |
| `listDir(path)` | 列出目录内容          | `listDir(".")`                | array    |

#### 面向对象 API
```javascript
// 文件操作
let file = new std.io.File("test.txt");
file.write("Hello ALang");  // 覆盖写入
file.append("\nAppend content");  // 追加写入
println(file.read());  // 读取全部内容
file.delete();  // 删除文件

// 目录操作
let dir = new std.io.Dir("temp");
dir.create();  // 创建目录
let files = dir.list();  // 列出目录内容
dir.delete();  // 删除目录（递归）
```

---

## 📝 示例运行
```bash
# 启动 REPL
./alang

# 执行基础示例
./alang -f Example/example.alang

# 执行数组方法示例
./alang -f Example/array_methods_test.alang

# 执行异步示例（自动处理事件循环）
./alang -f Example/asyncExample.alang

# 执行文件 I/O 示例
./alang -f Example/fileIOExample.alang
```

所有示例文件均位于 `Example/` 目录，涵盖语言全部特性，可直接运行验证。

---

## 🔗 宿主集成（C++）
ALang 提供简洁的 C++ 集成接口，支持注册原生函数、类及调用脚本函数。

### 核心接口定义
```cpp
// 宿主与脚本的值桥接类型
using NativeValue = std::variant<std::monostate, double, std::string, bool>;

// 原生函数类型
using NativeFunc = std::function<NativeValue(
    const std::vector<NativeValue>& args, 
    void* thisHandle
)>;

// 注册原生类
void registerClass(
    const std::string& className,
    NativeFunc constructor,
    const std::unordered_map<std::string, NativeFunc>& methods,
    const std::vector<std::string>& baseClasses = {}
);

// 调用脚本全局函数
NativeValue callFunction(
    const std::string& functionName,
    const std::vector<NativeValue>& args
);

// 驱动事件循环
void runEventLoopUntilIdle();
```

### 集成示例
```cpp
#include "ALangEngine.h"

int main() {
    ALangEngine engine;
    engine.initialize();

    // 注册原生类 Math
    engine.registerClass(
        "Math",
        // 构造器（无参数）
        [](const std::vector<ALangEngine::NativeValue>&, void*) {
            return ALangEngine::NativeValue{std::monostate{}};
        },
        // 类方法
        {
            {"sum", [](const std::vector<ALangEngine::NativeValue>& args, void*) {
                double a = 0, b = 0;
                if (args.size() > 0 && std::holds_alternative<double>(args[0])) {
                    a = std::get<double>(args[0]);
                }
                if (args.size() > 1 && std::holds_alternative<double>(args[1])) {
                    b = std::get<double>(args[1]);
                }
                return ALangEngine::NativeValue{a + b};
            }},
            {"abs", [](const std::vector<ALangEngine::NativeValue>& args, void*) {
                double x = 0;
                if (!args.empty() && std::holds_alternative<double>(args[0])) {
                    x = std::get<double>(args[0]);
                }
                return ALangEngine::NativeValue{std::fabs(x)};
            }}
        }
    );

    // 执行脚本
    engine.execute(R"(
        let math = new Math();
        println("3 + 4 =", math.sum(3, 4));
        println("abs(-5) =", math.abs(-5));
    )");

    // 处理异步任务（若脚本使用异步特性）
    engine.runEventLoopUntilIdle();

    return 0;
}
```

---

## 🔧 开发与调试
### 核心模块职责
| 模块名称               | 核心职责                          |
|------------------------|-----------------------------------|
| `AsulLexer`            | 词法分析：将源码转换为 Token 流   |
| `AsulParser`           | 语法分析：将 Token 流转换为 AST   |
| `AsulAst`              | AST 节点定义：表达式、语句等结构   |
| `AsulRuntime`          | 运行时环境：值系统、作用域、类信息 |
| `AsulInterpreter`      | 解释执行：遍历 AST 并执行逻辑     |
| `AsulAsync`            | 异步接口：事件循环、Promise 管理  |
| `AsulPackages`         | 标准库：提供各类内置功能扩展      |

### 新增标准库包流程
1. 在 `src/AsulPackages/` 下创建包目录（如 `MyPackage/`）
2. 实现包的头文件与源文件（如 `MyPackage.h/cpp`）
3. 在 `AsulPackages.h` 中注册包：
   ```cpp
   void registerMyPackage(AsulInterpreter& interpreter);
   ```
4. 在 `ALangEngine.cpp` 的 `initialize` 方法中调用注册函数

---

## ⚠️ 限制说明
- 暂不支持原型链与动态派发（方法调用基于静态查找）
- 无 `for-of`/`for-in` 语法，仅支持 C 风格 `for` 与 `foreach`
- 异常处理暂不支持 `finally` 块
- `go` 关键字中的异常会被静默吞掉（需自行扩展日志记录）
- 宿主与脚本间仅支持基元类型（number/string/boolean/null）传递

---

## 📊 架构设计
### 核心依赖关系
```
AsulLexer → AsulAst → AsulParser → AsulInterpreter
                     ↑
AsulRuntime → AsulAsync → AsulInterpreter
                     ↑
        AsulPackages/* → AsulInterpreter
```

### 执行流程
1. 脚本源码 → `AsulLexer` → Token 流
2. Token 流 → `AsulParser` → AST
3. AST → `AsulInterpreter` → 执行（依赖 `AsulRuntime` 管理状态）
4. 异步任务 → `AsulAsync` → 事件循环调度

---

## 📜 许可协议
本项目的许可协议以仓库根目录下的 `LICENSE` 文件为准。

---

<div align="center">
  <p>© 2025 ALang 开发团队</p>
</div>
