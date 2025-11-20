# ALang 简易解释器

ALang 是一个用 C++17 实现的轻量脚本语言解释器/运行时，目标用于嵌入式脚本扩展与实验性语言特性验证。核心代码位于 `ALangEngine.cpp`/`ALangEngine.h`，命令行入口为 `Main.cpp`。

**主要特性**
- 基本类型：number、string、boolean、null、array、object
- 变量与作用域：`let/var/const`（当前实现未严格区分可变性）
- 表达式与语句：算术、比较、逻辑运算；`if/else`、`while`、`for`、`foreach`、`break`/`continue`、`return`、块语句
- 运算符：增量运算符（`++`、`--`）、复合赋值（`+=`、`-=`、`*=`、`/=`、`%=`）
- 函数与闭包：`function`、匿名 lambda `[](args){}`、函数作为一等值
- 类与继承：`class`、`new`、构造器 `constructor`、多继承语法 `class A <- (B, C)`、`extends` 扩展
- 接口：`interface` 用作方法签名与多继承占位
- 异步与事件循环：`async/await`、`Promise`、`then/catch`、`go` 将任务投递到事件循环
- 元编程：`eval(string)` 与 `quote(string)`，支持 token 级别的源码修改与 `apply()`
- 模块与导入：文件导入与包导入 (`import` / `from`)，示例见 `Example/` 目录

> 📖 **详细文档**：有关 Token、类型系统及完整 API 列表，请参阅 [ALang 技术参考手册](ALang_Technical_Reference.md)。

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

**数组方法**

ALang 数组支持现代函数式编程方法，所有回调函数接收 `(element, index, array)` 三个参数：

```javascript
let numbers = [1, 2, 3, 4, 5];

// map - 转换每个元素
let doubled = numbers.map([](x) { return x * 2; });
println(doubled);  // [2, 4, 6, 8, 10]

// filter - 过滤元素
let evens = numbers.filter([](x) { return x % 2 == 0; });
println(evens);  // [2, 4]

// reduce - 归约为单个值
let sum = numbers.reduce([](acc, x) { return acc + x; }, 0);
println(sum);  // 15

// find - 查找第一个匹配元素
let firstEven = numbers.find([](x) { return x % 2 == 0; });
println(firstEven);  // 2

// some - 检查是否至少有一个元素满足条件
let hasEven = numbers.some([](x) { return x % 2 == 0; });
println(hasEven);  // true

// every - 检查是否所有元素都满足条件
let allPositive = numbers.every([](x) { return x > 0; });
println(allPositive);  // true

// includes - 检查数组是否包含某个值
let hasThree = numbers.includes(3);
println(hasThree);  // true

// 链式调用
let result = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    .filter([](x) { return x % 2 == 0; })
    .map([](x) { return x * x; })
    .reduce([](acc, x) { return acc + x; }, 0);
println(result);  // 220 (2² + 4² + 6² + 8² + 10²)
```

示例文件：`Example/array_methods_test.alang`

**字符串方法**

ALang 字符串支持常用的文本处理方法：

```javascript
// split - 分割字符串为数组
let csv = "apple,banana,orange";
let fruits = csv.split(",");
println(fruits);  // [apple, banana, orange]

let sentence = "hello world";
let words = sentence.split(" ");
println(words);  // [hello, world]

// 空分隔符分割为字符数组
let chars = "Hello".split("");
println(chars);  // [H, e, l, l, o]

// substring - 提取子字符串
let str = "Hello World";
let sub1 = str.substring(0, 5);    // "Hello"
let sub2 = str.substring(6);       // "World"
let sub3 = str.substring(6, 11);   // "World"

// replace - 替换第一个匹配项
let original = "Hello World";
let replaced = original.replace("World", "ALang");
println(replaced);  // "Hello ALang"

let code = "let x = 10;";
let updated = code.replace("10", "42");
println(updated);  // "let x = 42;"

// 实用示例：解析邮箱
let email = "user@example.com";
let parts = email.split("@");
println("用户名:", parts[0]);     // user
println("域名:", parts[1]);       // example.com

// CSV 解析
let csvLine = "Alice,25,Engineer";
let fields = csvLine.split(",");
println("姓名:", fields[0]);      // Alice
println("年龄:", fields[1]);      // 25
println("职业:", fields[2]);      // Engineer
```

示例文件：`Example/string_methods_test.alang`

**foreach 枚举容器**

`foreach` 语句用于遍历可迭代对象（数组、对象、字符串），语法如下：

```javascript
// 遍历数组
foreach (item in array) {
    println(item);
}

// 遍历对象（遍历键名）
foreach (key in object) {
    println(key, ":", object[key]);
}

// 遍历字符串（遍历字符）
foreach (ch in "Hello") {
    println(ch);
}
```

支持 `break` 和 `continue` 控制流程。示例请参考 `Example/foreachExample.alang` 和 `Example/foreachAdvanced.alang`。

**增量运算符和复合赋值**

ALang 支持常见的增量和复合赋值运算符：

```javascript
// 前置递增/递减（先更新后返回）
var x = 5;
println(++x);  // 输出 6，x 变成 6
println(--x);  // 输出 5，x 变成 5

// 后置递增/递减（先返回后更新）
var y = 10;
println(y++);  // 输出 10，y 变成 11
println(y--);  // 输出 11，y 变成 10

// 复合赋值运算符
var n = 10;
n += 5;   // n = n + 5
n -= 3;   // n = n - 3
n *= 2;   // n = n * 2
n /= 4;   // n = n / 4
n %= 3;   // n = n % 3

// 在循环中使用
for (var i = 0; i < 10; i++) {
    println(i);
}

// 对数组元素和对象属性也适用
var arr = [1, 2, 3];
arr[0]++;              // arr 变成 [2, 2, 3]

var obj = {count: 0};
obj["count"] += 5;     // obj.count 变成 5
```

示例请参考 `Example/incrementExample.alang`。

**位运算符**

现已支持常见的整数位运算（内部以 64 位整数视图进行计算，基于将 `number` 转为 `long long`）：

| 运算符 | 说明 | 示例 | 结果 |
|--------|------|------|------|
| `&` | 按位与 | `13 & 6` | `4` |
| `|` | 按位或 | `13 | 6` | `15` |
| `^` | 按位异或 | `13 ^ 6` | `11` |
| `~` | 按位取反（一元） | `~13` | `-14`（实现为对 64 位补码取反后再转回 number） |
| `<<` | 左移 | `13 << 2` | `52` |
| `>>` | 右移（算术） | `13 >> 1` | `6` |

优先级（高 → 低，仅列出相关）：

1. 乘法 / 除法 / 取模 `* / %`
2. 加减 `+ -`
3. 移位 `<< >>`
4. 比较 `> >= < <=`
5. 接口匹配 `=~=` （判断左值是否实现右侧接口/类声明）
6. 等于 / 不等 `== != === !==`
7. 按位与 `&`
8. 按位异或 `^`
9. 按位或 `|`
10. 逻辑与 `&&`
11. 逻辑或 `||`

示例文件：`Example/bitwiseExample.alang`

接口匹配运算符：`a =~= SomeInterface`

含义：若左侧是类实例，检查其类及父类是否包含接口声明的全部方法；若左侧是普通对象，则检查对象是否含有这些方法名（属性存在即可）。右侧必须是接口或类描述符。示例参见 `Example/type_and_match_example.alang`。

**文件 I/O (std.io)**

`std.io` 包默认导入，新增以下文件与目录操作函数（文本按 UTF-8 处理）：

| 函数 | 说明 | 示例 | 返回 |
|------|------|------|------|
| `readFile(path)` | 读取整个文件内容 | `readFile("a.txt")` | `string` |
| `writeFile(path, data)` | 覆盖写入文本 | `writeFile("a.txt", "Hello")` | `true` |
| `appendFile(path, data)` | 末尾追加文本 | `appendFile("a.txt", " World")` | `true` |
| `exists(path)` | 判断路径是否存在 | `exists("a.txt")` | `boolean` |
| `listDir(path)` | 列出目录项（文件/子目录名数组） | `listDir(".")` | `array` |

面向对象封装：提供 `std.io.File` 与 `std.io.Dir` 类，便于链式与实例化管理。

`new std.io.File(path)` 方法：
| 方法 | 说明 |
|------|------|
| `write(data)` | 覆盖写入文件内容 |
| `append(data)` | 追加写入到文件末尾 |
| `read()` | 读取全部内容返回字符串 |
| `exists()` | 判断文件是否存在 |
| `size()` | 返回文件大小（字节），失败返回 -1 |
| `delete()` | 删除文件，返回是否成功 |
| `rename(newPath)` | 重命名文件，成功返回 true |
| `readBytes()` | 以字节数组形式读取整个文件 |
| `writeBytes(array)` | 覆盖写入字节数组到文件 |
| `appendBytes(array)` | 追加字节数组到文件末尾 |
| `open(mode)` | 返回 `FileStream` 实例，`mode` 为 `r` / `w` / `a` |

`new std.io.Dir(path)` 方法：
| 方法 | 说明 |
|------|------|
| `create()` | 递归创建目录，返回是否创建成功 |
| `list()` | 返回目录内文件/子目录名数组（非递归） |
| `exists()` | 判断目录是否存在 |
| `delete()` | 递归删除目录及其内容，返回删除的条目数或 -1 失败 |
| `rename(newPath)` | 重命名目录，成功返回 true |
| `walk()` | 递归遍历，返回相对路径数组 |
`FileStream` 流式读写：
| 方法 | 说明 |
|------|------|
| `read(n)` | 在 `r` 模式下读取最多 n 字节为数组，维护内部位置 |
| `write(data)` | 在 `w` 模式覆盖写或 `a` 模式追加写（字符串或字节数组） |
| `eof()` | 到达文件末尾或已关闭返回 true |
| `close()` | 关闭流，后续读写抛出异常 |

示例：`Example/fileIOAdvancedExample.alang`

示例：`Example/fileIOClassExample.alang`

注意：
- 失败会抛出异常（例如文件不存在、路径不是目录等）。
- 所有写入为覆盖或追加模式的二进制/文本直接输出，未自动添加换行。
- 不提供文件删除/重命名，可后续扩展。

示例：`Example/fileIOExample.alang`。


**三元运算符（条件表达式）**

ALang 支持三元运算符 `condition ? trueValue : falseValue`，用于简洁的条件表达式：

```javascript
// 基础用法
let max = (a > b) ? a : b;
let min = (a < b) ? a : b;

// 在赋值中使用
let age = 18;
let status = (age >= 18) ? "adult" : "minor";

// 嵌套三元运算符
let score = 85;
let grade = (score >= 90) ? "A" :
            (score >= 80) ? "B" :
            (score >= 70) ? "C" :
            (score >= 60) ? "D" : "F";

// 在函数参数中使用
println((x > 0) ? "positive" : "negative or zero");

// 默认值设置
let value = (input != null) ? input : "default";
```

三元运算符可以嵌套使用，但为了代码可读性，复杂的条件判断建议使用 `if-else` 或 `switch` 语句。示例请参考 `Example/ternaryExample.alang`。

**Switch/Case 语句**

ALang 支持 switch 语句进行多路分支选择，使用严格相等（`===`）进行匹配：

```javascript
// 基础用法
let day = 3;
switch (day) {
    case 1:
        println("星期一");
        break;
    case 2:
        println("星期二");
        break;
    case 3:
        println("星期三");
        break;
    default:
        println("其他");
        break;
}

// 字符串匹配
let fruit = "apple";
switch (fruit) {
    case "apple":
        println("苹果: 5元/斤");
        break;
    case "banana":
        println("香蕉: 3元/斤");
        break;
    default:
        println("水果不在列表中");
        break;
}

// Fall-through（不使用 break 时继续执行下一个 case）
let month = 2;
switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        println("31天");
        break;
    case 4:
    case 6:
    case 9:
    case 11:
        println("30天");
        break;
    case 2:
        println("28天");
        break;
}

// 在函数中使用
function calculate(op, a, b) {
    switch (op) {
        case "+":
            return a + b;
        case "-":
            return a - b;
        case "*":
            return a * b;
        case "/":
            return a / b;
        default:
            return null;
    }
}

// Switch 嵌套
switch (category) {
    case "electronics":
        println("电子产品:");
        switch (item) {
            case "laptop":
                println("笔记本电脑");
                break;
            case "phone":
                println("手机");
                break;
        }
        break;
    case "books":
        println("图书类");
        break;
}
```

注意事项：
- 使用 `break` 终止当前 case，否则会 fall-through 到下一个 case
- 匹配使用严格相等（`===`），不进行类型转换
- `default` 分支是可选的，匹配所有未被 case 捕获的值
- 如需在 case 中声明变量，请使用代码块 `{ let x = ...; }`

示例请参考 `Example/switchExample.alang` 和 `Example/switchAdvanced.alang`。

**Rest Parameters（剩余参数）**

ALang 支持 rest parameters（剩余参数），允许函数接收可变数量的参数，所有剩余参数会被收集到一个数组中：

```javascript
// 基础用法：求和函数
function sum(...numbers) {
    let total = 0;
    foreach (num in numbers) {
        total += num;
    }
    return total;
}

println(sum(1, 2, 3));           // 输出: 6
println(sum(10, 20, 30, 40));    // 输出: 100
println(sum());                   // 输出: 0（空数组）

// 普通参数 + Rest 参数
function greet(greeting, ...names) {
    foreach (name in names) {
        println(greeting, name);
    }
}

greet("Hello", "Alice", "Bob", "Charlie");
// 输出:
// Hello Alice
// Hello Bob
// Hello Charlie

// 在 Lambda 表达式中使用
let multiply = [](factor, ...numbers) {
    let result = [];
    foreach (num in numbers) {
        push(result, num * factor);
    }
    return result;
};

println(multiply(2, 1, 2, 3, 4));  // [2, 4, 6, 8]

// 查找最大值
function max(...numbers) {
    if (numbers.len() == 0) return null;
    let maxVal = numbers[0];
    foreach (num in numbers) {
        if (num > maxVal) maxVal = num;
    }
    return maxVal;
}

println(max(5, 2, 9, 1, 7));  // 输出: 9

// 字符串拼接
function concat(separator, ...strings) {
    if (strings.len() == 0) return "";
    let result = strings[0];
    for (let i = 1; i < strings.len(); i++) {
        result = result + separator + strings[i];
    }
    return result;
}

println(concat("-", "a", "b", "c"));  // 输出: a-b-c
```

重要规则：
- Rest 参数必须是参数列表中的最后一个参数
- 一个函数只能有一个 rest 参数
- Rest 参数总是一个数组，即使没有传递任何剩余参数（此时为空数组）
- 调用时必须至少提供普通参数的数量

示例请参考 `Example/restParamsExample.alang` 和 `Example/restParamsAdvanced.alang`。

**Default Parameters（默认参数）**

ALang 支持默认参数，允许为函数参数指定默认值。当调用函数时未提供某个参数，将使用其默认值：

```javascript
// 基础用法：带默认值的问候函数
function greet(name, greeting = "Hello") {
    println(greeting, name);
}

greet("Alice");              // 输出: Hello Alice
greet("Bob", "Hi");          // 输出: Hi Bob

// 多个默认参数
function createUser(name, age = 18, role = "user") {
    return {
        name: name,
        age: age,
        role: role
    };
}

let user1 = createUser("Alice");               // {name: "Alice", age: 18, role: "user"}
let user2 = createUser("Bob", 25);            // {name: "Bob", age: 25, role: "user"}
let user3 = createUser("Charlie", 30, "admin"); // {name: "Charlie", age: 30, role: "admin"}

// 在 Lambda 表达式中使用
let multiply = [](a, b = 1) {
    return a * b;
};

println(multiply(5));      // 输出: 5
println(multiply(5, 3));   // 输出: 15

// 默认参数可以是表达式（在调用时计算）
let counter = 0;
function increment(step = 1) {
    counter = counter + step;
    return counter;
}

println(increment());     // 1
println(increment(5));    // 6
println(increment());     // 7

// 默认参数与 Rest 参数结合
function createList(title = "My List", ...items) {
    println("---", title, "---");
    foreach (item in items) {
        println("  -", item);
    }
}

createList("Shopping List", "Milk", "Bread", "Eggs");
// 输出:
// --- Shopping List ---
//   - Milk
//   - Bread
//   - Eggs

createList();  // 使用默认 title，items 为空数组
// 输出:
// --- My List ---
```

重要规则：
- 默认参数必须在所有必需参数之后
- 默认参数必须在 rest 参数之前
- 参数顺序：必需参数 → 默认参数 → rest 参数
- 默认值表达式在函数调用时计算，不是在定义时
- 可以使用任何表达式作为默认值（常量、变量、函数调用等）

示例请参考 `Example/defaultParamsExample.alang`。

**Method Override（方法重写）和 Function Overloading（函数重载）**

ALang 支持面向对象编程中的方法重写，以及通过默认参数模拟函数重载：

**方法重写（Method Override）**

子类可以重写（覆盖）父类的方法，调用子类实例时将执行子类版本的方法：

```javascript
// 方法重写示例
class Animal {
    function speak() {
        println("Animal makes a sound");
    }
}

class Dog <- (Animal) {
    // 重写父类的 speak 方法
    function speak() {
        println("Dog barks: Woof!");
    }
}

class Cat <- (Animal) {
    // 重写父类的 speak 方法
    function speak() {
        println("Cat meows: Meow!");
    }
}

let animal = new Animal();
animal.speak();  // 输出: Animal makes a sound

let dog = new Dog();
dog.speak();     // 输出: Dog barks: Woof!

let cat = new Cat();
cat.speak();     // 输出: Cat meows: Meow!
```

重要特性：
- 子类方法自动覆盖父类同名方法
- 不需要特殊的 `override` 关键字
- 支持多继承场景下的方法覆盖（后继承的类优先）

**函数重载模拟（通过默认参数）**

ALang 作为动态类型语言，不支持传统的基于类型的函数重载，但可以通过**默认参数**和**Rest参数**模拟重载效果：

```javascript
// 使用默认参数模拟重载
function greet(name = null, greeting = "Hello") {
    if (name == null) {
        println("Hello!");
    } else {
        println(greeting, name + "!");
    }
}

greet();                    // 输出: Hello!
greet("Alice");            // 输出: Hello Alice!
greet("Bob", "Hi");        // 输出: Hi Bob!

// 使用 Rest 参数模拟可变参数重载
function sum(...numbers) {
    if (numbers.len() == 0) {
        return 0;
    }
    let total = 0;
    foreach (num in numbers) {
        total += num;
    }
    return total;
}

println(sum());           // 输出: 0
println(sum(5));          // 输出: 5
println(sum(1, 2, 3));    // 输出: 6

// 结合默认参数和 Rest 参数
function createMessage(prefix = "[INFO]", ...parts) {
    let msg = prefix;
    foreach (part in parts) {
        msg = msg + " " + part;
    }
    return msg;
}

println(createMessage());                    // [INFO]
println(createMessage("[ERROR]", "Failed")); // [ERROR] Failed
println(createMessage("[DEBUG]", "x:", 10)); // [DEBUG] x: 10
```

通过这种方式，可以实现类似函数重载的灵活性，而无需为每个参数组合定义单独的函数。

示例请参考 `Example/overrideTest.alang` 和 `Example/overloadTest.alang`。

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

内置包通过 `import`/`from` 导入到当前作用域。当前提供 `std.math` 包：`pi`、`abs(x)`。

支持形式：

```js
// 1) 指定包内符号列表
import std.math.(pi, abs);

// 2) 列出包名+符号的列表（可跨包）
import (std.math.pi, std.math.abs);

// 3) from 形式
from std.math import abs;
from std.math import (pi, abs);

// 4) 通配导入
import std.math.*;

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
import (std.math.pi, std.math.abs, "utils");

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

接口用于定义类必须实现的方法契约。ALang 支持接口声明、多接口继承，并在类定义时验证接口实现的完整性。

**声明语法：**
- 空接口：`interface Name;`
- 带方法签名的接口：`interface Name { function method1(); function method2(param); }`

**重要规则：**

1. **接口只能声明方法签名，不能包含函数体**
   ```javascript
   // ✅ 正确：只有签名，用分号结束
   interface Drawable {
       function draw();
       function getColor();
   }
   
   // ❌ 错误：接口方法不能有函数体
   interface BadInterface {
       function doSomething() {  // 错误！
           print("Not allowed");
       }
   }
   ```
   
   如果在接口中定义函数体，会报错：
   ```
   Interface methods cannot have function bodies. Use ';' instead of '{...}'
   Method 'doSomething' in interface 'BadInterface' should be declared as: function doSomething(...);
   ```

2. **实现接口的类必须实现所有方法**
   ```javascript
   interface Printable {
       function print();
       function getName();
   }
   
   // ✅ 正确：实现了所有方法
   class Document <- (Printable) {
       function print() {
           println("Printing document");
       }
       
       function getName() {
           return "Document";
       }
   }
   
   // ❌ 错误：缺少 getName() 方法
   class BadDocument <- (Printable) {
       function print() {
           println("Printing");
       }
       // 缺少 getName() 实现
   }
   ```
   
   如果缺少方法实现，会报错：
   ```
   Class 'BadDocument' must implement interface method 'getName' from 'Printable'
   ```

3. **支持多接口继承**
   ```javascript
   interface Readable {
       function read();
   }
   
   interface Writable {
       function write();
   }
   
   class File <- (Readable, Writable) {
       function read() {
           return "File content";
       }
       
       function write() {
           println("Writing to file");
       }
   }
   ```

4. **接口可以与类混合继承**
   ```javascript
   class Base {
       function baseMethod() {
           println("Base method");
       }
   }
   
   interface Flyable {
       function fly();
   }
   
   // 同时继承类和接口
   class Bird <- (Base, Flyable) {
       function fly() {
           println("Bird is flying");
       }
   }
   ```

**使用场景：**
- 定义类的行为契约
- 实现多态性
- 确保类实现必需的方法
- 代码规范和类型安全

**验证时机：**
接口实现的验证在类定义时进行（编译时），而非运行时。这确保了在使用类之前所有必需的方法都已实现。

示例请参考 `Example/interfaceExample.alang` 和 `Example/interfaceValidationTest.alang`。

错误示例请参考 `Example/ErrorExample/`（如 `interface_*.alang` 或新添加的 `missing_import_math.alang`）。

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
import std.math.*;
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
- ~~支持对象/数组字面量中的计算属性名与展开（`[expr]` 与 `...expr`）~~
- 无 `for-of`/`for-in`，仅 C 风格 `for`
- `==` 实现为类似 ECMAScript 的抽象相等（带类型强制），`===` 为严格相等（按类型与引用）
- 支持语言级异常（throw / try...catch），但暂不支持 finally；`go` 中异常被吞掉；宿主未提供统一日志钩子

## 结构

- `ALangEngine.h/.cpp`：解释器实现（词法、语法、AST、解释执行、事件循环、Promise）
- `Main.cpp`：命令行入口（可执行加载脚本并在末尾调用 `runEventLoopUntilIdle()`）

更多示例：见 `Example/` 与 `lambdaExample.alang`。

欢迎根据需要继续扩展内置函数或语法。