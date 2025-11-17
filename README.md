# ALang 简易 JS 解释器

一个用 C++17 实现的极简 JavaScript 风格解释器，支持：

- 变量声明：`let/var/const`（目前不区分可变性）
- 基本类型：number、string、boolean、null
- 表达式：`+ - * / %`，比较与相等（严格按类型），逻辑 `&& || !`
- 语句：表达式语句、`if/else`、`while`、`return`、块 `{}`
- 函数：`function name(a,b){...}`，带作用域闭包
- 内置：`print(...)`

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

## 限制

- 无对象/数组/原型
- 无 `for`/`break`/`continue`，仅 `while`
- `==` 实现为严格相等（按类型）
- 没有异常机制，仅用于函数 `return` 的内部信号

## 结构

- `ALangEngine.h/.cpp`：解释器实现（词法、语法、AST、解释执行）
- `Main.cpp`：命令行入口

欢迎根据需要继续扩展内置函数或语法。