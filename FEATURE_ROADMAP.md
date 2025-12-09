# ALang 特性路线图 (Feature Roadmap)

本文档描述了 ALang 语言计划实现的新特性和增强功能，包括 FFI 增强、语法糖和高级语言特性。

---

## 1. FFI（Foreign Function Interface）增强

### 1.1 当前状态 (Current Status)

**已支持的功能：**
- ✅ 动态库加载：`dlopen()`, `dlclose()`
- ✅ 符号解析：`dlsym()`
- ✅ 基础类型调用：`call()` 支持以下返回类型
  - `void` - 无返回值函数
  - `int` - 整数返回值
  - `double` - 浮点数返回值
  - `pointer` - 指针返回值
  - `string` - 字符串返回值
- ✅ 跨平台支持：Linux (.so), macOS (.dylib), Windows (.dll)
- ✅ 引用计数管理：自动管理库加载/卸载
- ✅ 最多 6 个参数传递

**实现文件：**
- `src/AsulPackages/Std/Ffi/StdFfi.cpp`
- `src/AsulPackages/Std/Ffi/StdFfi.h`

**示例代码：**
```alang
import std.ffi.*;

// 加载 C 标准库
let libc = dlopen("/lib/x86_64-linux-gnu/libc.so.6", RTLD_LAZY);

// 获取 strlen 函数
let strlenPtr = dlsym(libc, "strlen");

// 调用 strlen
let len = call(strlenPtr, "int", "Hello, World!");
println("Length:", len);  // 输出: Length: 13

// 关闭库
dlclose(libc);
```

### 1.2 当前限制 (Current Limitations)

**类型系统限制：**
- ❌ 不支持传递 ALang 复杂对象（array, object）到 C 函数
- ❌ 不支持结构体（struct）类型的传递和返回
- ❌ 不支持函数指针回调从 C 到 ALang
- ❌ 参数数量限制在 6 个以内
- ❌ 不支持可变参数函数（varargs）

**调用约定限制：**
- ⚠️ 使用简化的调用约定，可能与某些平台不兼容
- ⚠️ 不支持 Windows 的 `__stdcall`, `__fastcall` 等约定
- ⚠️ 浮点数和整数混合参数可能在某些平台失败

**当前解决方案（Workarounds）：**
```alang
// 1. 使用基础类型转换
let arr = [1, 2, 3];
// 不能直接传递 arr，需要逐个传递元素
call(funcPtr, "void", arr[0], arr[1], arr[2]);

// 2. 传递对象字段
let obj = { x: 10, y: 20 };
call(funcPtr, "void", obj.x, obj.y);

// 3. 使用指针传递结构化数据（需要手动内存管理）
// 这需要额外的辅助函数来分配和释放内存
```

### 1.3 计划增强 (Planned Enhancements)

#### Phase 1: libffi 集成 (高优先级)

**目标：**
- 集成 libffi 库实现完整的 FFI 支持
- 支持任意数量的参数
- 支持更多调用约定
- 自动类型转换和安全检查

**实现计划：**
```alang
// 新增 API 设计
let ffi = new std.ffi.FFI();

// 定义函数签名
let signature = ffi.defineSignature({
    returnType: "int",
    argTypes: ["string", "int", "double"],
    convention: "cdecl"  // 或 "stdcall", "fastcall"
});

// 创建可调用对象
let myFunc = ffi.wrap(funcPtr, signature);

// 直接调用，类型自动转换
let result = myFunc("hello", 42, 3.14);
```

**技术实现：**
- 依赖：添加 libffi 到构建系统
- 文件修改：
  - `CMakeLists.txt`: 添加 libffi 依赖检测
  - `src/AsulPackages/Std/Ffi/StdFfi.cpp`: 实现 libffi 包装器
  - `src/AsulPackages/Std/Ffi/FfiTypes.h`: 新增类型描述系统

#### Phase 2: 复杂类型支持 (中优先级)

**目标：**
- 支持传递 ALang 数组到 C 数组
- 支持传递 ALang 对象到 C 结构体
- 支持从 C 返回结构体并转换为 ALang 对象

**API 设计：**
```alang
// 定义结构体映射
let PointStruct = ffi.defineStruct("Point", {
    fields: [
        { name: "x", type: "double", offset: 0 },
        { name: "y", type: "double", offset: 8 }
    ],
    size: 16,
    align: 8
});

// 创建结构体实例
let point = { x: 10.0, y: 20.0 };
let pointPtr = ffi.allocStruct(PointStruct, point);

// 调用接受结构体的函数
call(drawPoint, "void", pointPtr);

// 释放内存
ffi.free(pointPtr);

// 自动管理版本
ffi.callWithStruct(drawPoint, PointStruct, point);
```

#### Phase 3: 回调支持 (中优先级)

**目标：**
- 允许 C 代码调用 ALang 函数
- 实现函数指针的双向转换

**API 设计：**
```alang
// 创建可被 C 调用的函数指针
function myCallback(value) {
    println("Callback called with:", value);
    return value * 2;
}

let callbackPtr = ffi.createCallback(myCallback, {
    returnType: "int",
    argTypes: ["int"]
});

// 传递回调给 C 函数
call(registerCallback, "void", callbackPtr);

// 清理回调（重要！）
ffi.destroyCallback(callbackPtr);
```

#### Phase 4: 高级功能 (低优先级)

**目标：**
- 支持可变参数函数
- 支持函数重载检测
- 自动生成 C 头文件绑定

**功能清单：**
- 可变参数支持：`printf`, `scanf` 等
- 头文件解析器：自动从 `.h` 生成绑定
- 类型安全检查：编译时类型验证
- 内存安全：自动引用计数和垃圾回收

### 1.4 相关文档

**现有文档：**
- [HTTP_FIXES_DOCUMENTATION.md](HTTP_FIXES_DOCUMENTATION.md) - 第 4 节 FFI 类型支持
- [HTTP_COMPLETE_DESIGN_PLAN.md](HTTP_COMPLETE_DESIGN_PLAN.md) - HTTP 和网络功能设计
- [README.md](README.md) - FFI 快速开始指南

**示例文件：**
- `Example/ffi_test.alang` - FFI 基础功能测试
- `Example/ffi_test_windows.alang` - Windows 特定测试

---

## 2. 解构赋值 (Destructuring Assignment)

### 2.1 功能描述

解构赋值允许从数组或对象中提取值并赋给多个变量，简化代码并提高可读性。

### 2.2 数组解构

**基础语法：**
```alang
// 基本数组解构
let arr = [1, 2, 3];
let [a, b, c] = arr;
println(a, b, c);  // 输出: 1 2 3

// 跳过元素
let [first, , third] = arr;
println(first, third);  // 输出: 1 3

// 剩余元素
let [head, ...tail] = [1, 2, 3, 4, 5];
println(head);  // 输出: 1
println(tail);  // 输出: [2, 3, 4, 5]

// 默认值
let [x = 0, y = 0] = [10];
println(x, y);  // 输出: 10 0

// 嵌套解构
let nested = [1, [2, 3], 4];
let [a, [b, c], d] = nested;
println(a, b, c, d);  // 输出: 1 2 3 4
```

**在函数参数中使用：**
```alang
function processCoordinates([x, y, z = 0]) {
    println(`Position: ${x}, ${y}, ${z}`);
}

processCoordinates([10, 20]);  // Position: 10, 20, 0
processCoordinates([10, 20, 30]);  // Position: 10, 20, 30
```

### 2.3 对象解构

**基础语法：**
```alang
// 基本对象解构
let obj = { name: "Alice", age: 30, city: "Beijing" };
let { name, age } = obj;
println(name, age);  // 输出: Alice 30

// 重命名变量
let { name: userName, age: userAge } = obj;
println(userName, userAge);  // 输出: Alice 30

// 默认值
let { name, country = "China" } = obj;
println(name, country);  // 输出: Alice China

// 剩余属性
let { name, ...rest } = obj;
println(name);  // 输出: Alice
println(rest);  // 输出: { age: 30, city: "Beijing" }

// 嵌套解构
let person = {
    name: "Bob",
    address: {
        city: "Shanghai",
        zip: "200000"
    }
};
let { name, address: { city, zip } } = person;
println(name, city, zip);  // 输出: Bob Shanghai 200000
```

**在函数参数中使用：**
```alang
function greet({ name, age, greeting = "Hello" }) {
    println(`${greeting}, ${name}! You are ${age} years old.`);
}

greet({ name: "Alice", age: 30 });
// 输出: Hello, Alice! You are 30 years old.
```

### 2.4 实现计划

**语法分析：**
- 扩展 Parser 识别解构模式
- 新增 AST 节点：`DestructuringPattern`, `ArrayPattern`, `ObjectPattern`

**语义分析：**
- 验证解构模式的有效性
- 处理默认值和剩余元素

**代码生成：**
- 将解构转换为多个赋值语句
- 优化连续解构操作

**文件修改：**
- `src/AsulParser.cpp`: 解构语法解析
- `src/AsulAst.h`: 新增解构相关 AST 节点
- `src/AsulInterpreter.cpp`: 解构求值逻辑

---

## 3. 可选链 (Optional Chaining)

### 3.1 功能描述

可选链操作符 `?.` 允许安全地访问可能为 `null` 或 `undefined` 的对象属性，避免抛出错误。

### 3.2 语法示例

**基础用法：**
```alang
let user = { name: "Alice", address: { city: "Beijing" } };

// 安全访问存在的属性
println(user?.name);  // 输出: Alice
println(user?.address?.city);  // 输出: Beijing

// 访问不存在的属性返回 null
println(user?.phone);  // 输出: null
println(user?.address?.country);  // 输出: null

// 与 null/undefined 对象
let emptyUser = null;
println(emptyUser?.name);  // 输出: null，不会抛出错误

// 传统写法对比
// 以前需要：
if (user && user.address && user.address.city) {
    println(user.address.city);
}
// 现在只需：
println(user?.address?.city);
```

**方法调用：**
```alang
let obj = {
    method: function() {
        return "Hello";
    }
};

println(obj.method?.());  // 输出: Hello

let nullObj = null;
println(nullObj?.method?.());  // 输出: null，不会抛出错误
```

**数组访问：**
```alang
let arr = [1, 2, 3];
println(arr?.[0]);  // 输出: 1

let nullArr = null;
println(nullArr?.[0]);  // 输出: null

// 动态索引
let index = 5;
println(arr?.[index]);  // 输出: null（索引越界）
```

**组合使用：**
```alang
let data = {
    users: [
        { name: "Alice", contacts: { email: "alice@example.com" } },
        { name: "Bob" }
    ]
};

// 安全访问深层嵌套结构
println(data?.users?.[0]?.contacts?.email);  // alice@example.com
println(data?.users?.[1]?.contacts?.email);  // null
println(data?.users?.[2]?.name);  // null
```

### 3.3 与空值合并运算符配合

```alang
// 可选链 + 空值合并提供默认值
let user = { name: "Alice" };
let city = user?.address?.city ?? "Unknown";
println(city);  // 输出: Unknown

// 相当于
let city2 = (user?.address?.city !== null && user?.address?.city !== undefined) 
    ? user.address.city 
    : "Unknown";
```

### 3.4 实现计划

**词法分析：**
- 添加 `?.` 作为新 token 类型

**语法分析：**
- 修改属性访问、方法调用、数组索引解析逻辑
- 支持链式可选访问

**语义分析：**
- 在运行时检查左值是否为 null/undefined
- 短路求值：一旦遇到 null，整个链返回 null

**文件修改：**
- `src/AsulLexer.cpp`: 添加 `?.` token
- `src/AsulLexer.h`: 新增 `TokenType::OptionalChain`
- `src/AsulParser.cpp`: 解析可选链语法
- `src/AsulAst.h`: 新增 `OptionalChainExpr` 节点
- `src/AsulInterpreter.cpp`: 实现短路求值逻辑

---

## 4. 生成器与 yield (Generators and yield)

### 4.1 功能描述

生成器函数使用 `function*` 声明，通过 `yield` 关键字暂停执行并返回值，支持惰性求值和无限序列。

### 4.2 语法示例

**基础生成器：**
```alang
// 定义生成器函数
function* numberGenerator() {
    yield 1;
    yield 2;
    yield 3;
}

// 使用生成器
let gen = numberGenerator();
println(gen.next().value);  // 输出: 1
println(gen.next().value);  // 输出: 2
println(gen.next().value);  // 输出: 3
println(gen.next().done);   // 输出: true

// 使用 for-of 遍历（需要实现 for-of）
for (let num of numberGenerator()) {
    println(num);  // 输出: 1, 2, 3
}
```

**带参数的生成器：**
```alang
function* range(start, end, step = 1) {
    for (let i = start; i < end; i += step) {
        yield i;
    }
}

for (let i of range(0, 10, 2)) {
    println(i);  // 输出: 0, 2, 4, 6, 8
}
```

**无限生成器：**
```alang
function* fibonacci() {
    let a = 0, b = 1;
    while (true) {
        yield a;
        [a, b] = [b, a + b];  // 需要解构赋值
    }
}

// 获取前 10 个斐波那契数
let fib = fibonacci();
for (let i = 0; i < 10; i++) {
    println(fib.next().value);
}
```

**yield* 委托：**
```alang
function* inner() {
    yield 1;
    yield 2;
}

function* outer() {
    yield 0;
    yield* inner();  // 委托给另一个生成器
    yield 3;
}

for (let num of outer()) {
    println(num);  // 输出: 0, 1, 2, 3
}
```

**双向通信：**
```alang
function* echo() {
    while (true) {
        let value = yield;
        println("Received:", value);
        yield value * 2;
    }
}

let gen = echo();
gen.next();  // 启动生成器
gen.next(5);  // 发送 5，输出: Received: 5
println(gen.next().value);  // 输出: 10
```

### 4.3 应用场景

**惰性求值：**
```alang
function* lazyMap(iterable, fn) {
    for (let item of iterable) {
        yield fn(item);
    }
}

function* lazyFilter(iterable, predicate) {
    for (let item of iterable) {
        if (predicate(item)) {
            yield item;
        }
    }
}

// 组合使用
let numbers = range(0, 1000000);
let evens = lazyFilter(numbers, [](x) { return x % 2 === 0; });
let squares = lazyMap(evens, [](x) { return x * x; });

// 只计算前 10 个，不会处理全部 100 万个数字
let result = [];
let gen = squares;
for (let i = 0; i < 10; i++) {
    result.push(gen.next().value);
}
```

**异步生成器：**
```alang
async function* fetchPages(urls) {
    for (let url of urls) {
        let response = await fetch(url);
        yield response.json();
    }
}

// 使用
for await (let page of fetchPages(["url1", "url2", "url3"])) {
    println(page);
}
```

### 4.4 实现计划

**词法分析：**
- 添加 `yield` 关键字
- 识别 `function*` 语法

**语法分析：**
- 解析生成器函数声明
- 解析 `yield` 表达式
- 解析 `yield*` 委托

**运行时支持：**
- 实现 Generator 对象
- 实现协程状态机
- 支持 `next()`, `return()`, `throw()` 方法

**文件修改：**
- `src/AsulLexer.h`: 添加 `TokenType::Yield`, `TokenType::Star`
- `src/AsulParser.cpp`: 解析生成器语法
- `src/AsulAst.h`: 新增 `GeneratorFunctionStmt`, `YieldExpr`
- `src/AsulRuntime.h`: 新增 `Generator` 类
- `src/AsulInterpreter.cpp`: 实现生成器执行逻辑

---

## 5. 装饰器 (Decorators)

### 5.1 功能描述

装饰器是一种元编程特性，允许在不修改原函数或类的情况下，动态地添加功能或修改行为。

### 5.2 语法示例

**函数装饰器：**
```alang
// 定义装饰器
function log(target, name, descriptor) {
    let originalMethod = descriptor.value;
    descriptor.value = function(...args) {
        println(`Calling ${name} with args:`, args);
        let result = originalMethod.apply(this, args);
        println(`${name} returned:`, result);
        return result;
    };
    return descriptor;
}

function timer(target, name, descriptor) {
    let originalMethod = descriptor.value;
    descriptor.value = function(...args) {
        let start = Date.now();
        let result = originalMethod.apply(this, args);
        let end = Date.now();
        println(`${name} took ${end - start}ms`);
        return result;
    };
    return descriptor;
}

// 使用装饰器
class Calculator {
    @log
    @timer
    function add(a, b) {
        return a + b;
    }
    
    @log
    function multiply(a, b) {
        return a * b;
    }
}

let calc = new Calculator();
calc.add(2, 3);
// 输出:
// Calling add with args: [2, 3]
// add took 0ms
// add returned: 5
```

**类装饰器：**
```alang
// 类装饰器
function singleton(target) {
    let instance = null;
    
    return class {
        function constructor(...args) {
            if (instance === null) {
                instance = new target(...args);
            }
            return instance;
        }
    };
}

@singleton
class Database {
    function constructor(connectionString) {
        this.connection = connectionString;
        println("Database created");
    }
}

let db1 = new Database("localhost:5432");
let db2 = new Database("localhost:5432");
println(db1 === db2);  // 输出: true，单例模式
```

**属性装饰器：**
```alang
function readonly(target, name, descriptor) {
    descriptor.writable = false;
    return descriptor;
}

function validate(validator) {
    return function(target, name, descriptor) {
        let originalSet = descriptor.set;
        descriptor.set = function(value) {
            if (!validator(value)) {
                throw new Error(`Invalid value for ${name}: ${value}`);
            }
            originalSet.call(this, value);
        };
        return descriptor;
    };
}

class Person {
    @readonly
    let id = generateId();
    
    @validate([](v) { return typeof v === "string" && v.length > 0; })
    let name;
    
    @validate([](v) { return typeof v === "number" && v >= 0; })
    let age;
}

let person = new Person();
person.name = "Alice";  // OK
person.age = 30;  // OK
person.age = -5;  // Error: Invalid value for age: -5
person.id = "new-id";  // Error: Cannot modify readonly property
```

**装饰器工厂：**
```alang
// 装饰器工厂 - 返回装饰器的函数
function memoize(maxSize = 100) {
    return function(target, name, descriptor) {
        let originalMethod = descriptor.value;
        let cache = new Map();
        let keys = [];
        
        descriptor.value = function(...args) {
            let key = JSON.stringify(args);
            
            if (cache.has(key)) {
                return cache.get(key);
            }
            
            let result = originalMethod.apply(this, args);
            
            cache.set(key, result);
            keys.push(key);
            
            // 限制缓存大小
            if (keys.length > maxSize) {
                let oldKey = keys.shift();
                cache.delete(oldKey);
            }
            
            return result;
        };
        
        return descriptor;
    };
}

class MathUtils {
    @memoize(50)
    function fibonacci(n) {
        if (n <= 1) return n;
        return this.fibonacci(n - 1) + this.fibonacci(n - 2);
    }
}

let utils = new MathUtils();
println(utils.fibonacci(40));  // 第一次计算
println(utils.fibonacci(40));  // 从缓存返回
```

**多个装饰器组合：**
```alang
@component
@injectable
@singleton
class UserService {
    @log
    @cache
    @async
    function getUser(id) {
        // 装饰器执行顺序：从下到上
        // 1. async - 转为异步函数
        // 2. cache - 添加缓存
        // 3. log - 添加日志
        return fetch(`/api/users/${id}`);
    }
}
```

### 5.3 装饰器用例

**依赖注入：**
```alang
@injectable
class HttpService {
    function get(url) { /* ... */ }
}

@injectable
class UserRepository {
    @inject(HttpService)
    function constructor(http) {
        this.http = http;
    }
    
    function findById(id) {
        return this.http.get(`/users/${id}`);
    }
}
```

**路由定义：**
```alang
class UserController {
    @route("GET", "/users")
    @auth("admin")
    function listUsers(req, res) {
        // ...
    }
    
    @route("POST", "/users")
    @validate(UserSchema)
    function createUser(req, res) {
        // ...
    }
}
```

### 5.4 实现计划

**词法分析：**
- 添加 `@` 符号识别

**语法分析：**
- 解析装饰器表达式
- 支持装饰器链
- 支持装饰器参数

**语义分析：**
- 装饰器应用顺序
- 类型检查和验证

**运行时支持：**
- 实现 descriptor 对象
- 实现装饰器应用机制
- 支持元数据反射

**文件修改：**
- `src/AsulLexer.cpp`: 识别 `@` 符号
- `src/AsulParser.cpp`: 解析装饰器语法
- `src/AsulAst.h`: 新增 `DecoratorExpr`
- `src/AsulInterpreter.cpp`: 实现装饰器应用逻辑

---

## 6. 模式匹配 (Pattern Matching)

### 6.1 功能描述

模式匹配提供了比 `switch` 更强大的分支控制，支持结构化匹配、类型匹配和条件守卫。

### 6.2 语法示例

**基础模式匹配：**
```alang
function describe(value) {
    match (value) {
        case 0 => "zero",
        case 1 => "one",
        case 2 => "two",
        case _ => "other"  // 默认分支
    }
}

println(describe(1));  // 输出: one
println(describe(99));  // 输出: other
```

**类型匹配：**
```alang
function processValue(value) {
    match (typeof value) {
        case "number" => println("It's a number:", value),
        case "string" => println("It's a string:", value),
        case "array" => println("It's an array with", value.length, "elements"),
        case "object" => println("It's an object"),
        case _ => println("Unknown type")
    }
}
```

**解构匹配：**
```alang
function processPoint(point) {
    match (point) {
        case { x: 0, y: 0 } => println("Origin"),
        case { x: 0, y } => println("On Y axis at", y),
        case { x, y: 0 } => println("On X axis at", x),
        case { x, y } => println("Point at", x, ",", y),
        case _ => println("Not a valid point")
    }
}

processPoint({ x: 0, y: 0 });  // Origin
processPoint({ x: 5, y: 0 });  // On X axis at 5
processPoint({ x: 3, y: 4 });  // Point at 3, 4
```

**数组模式：**
```alang
function processList(list) {
    match (list) {
        case [] => println("Empty list"),
        case [x] => println("Single element:", x),
        case [x, y] => println("Two elements:", x, y),
        case [head, ...tail] => {
            println("Head:", head);
            println("Tail:", tail);
        },
        case _ => println("Unknown pattern")
    }
}

processList([]);  // Empty list
processList([1]);  // Single element: 1
processList([1, 2, 3, 4]);  // Head: 1, Tail: [2, 3, 4]
```

**条件守卫（Guard）：**
```alang
function categorizeNumber(n) {
    match (n) {
        case x if x < 0 => "negative",
        case x if x === 0 => "zero",
        case x if x > 0 && x <= 10 => "small positive",
        case x if x > 10 => "large positive",
        case _ => "unknown"
    }
}

println(categorizeNumber(-5));  // negative
println(categorizeNumber(5));   // small positive
println(categorizeNumber(20));  // large positive
```

**嵌套模式：**
```alang
function processData(data) {
    match (data) {
        case { type: "user", user: { name, age } } if age >= 18 => {
            println(`Adult user: ${name}, age ${age}`);
        },
        case { type: "user", user: { name, age } } => {
            println(`Minor user: ${name}, age ${age}`);
        },
        case { type: "admin", level } if level > 5 => {
            println("High-level admin");
        },
        case { type: "admin" } => {
            println("Regular admin");
        },
        case _ => println("Unknown data type")
    }
}
```

**OR 模式：**
```alang
function isWeekend(day) {
    match (day) {
        case "Saturday" | "Sunday" => true,
        case _ => false
    }
}

println(isWeekend("Saturday"));  // true
println(isWeekend("Monday"));    // false
```

**范围匹配：**
```alang
function gradeToLetter(score) {
    match (score) {
        case x if x >= 90 => "A",
        case x if x >= 80 => "B",
        case x if x >= 70 => "C",
        case x if x >= 60 => "D",
        case _ => "F"
    }
}
```

**表达式形式：**
```alang
// match 可以作为表达式使用
let result = match (value) {
    case 0 => "zero",
    case 1 => "one",
    case _ => "other"
};

// 在函数返回中使用
function fibonacci(n) {
    return match (n) {
        case 0 => 0,
        case 1 => 1,
        case _ => fibonacci(n - 1) + fibonacci(n - 2)
    };
}
```

### 6.3 与 switch 的对比

| 特性 | switch | match |
|------|--------|-------|
| 基础值匹配 | ✅ | ✅ |
| 解构匹配 | ❌ | ✅ |
| 类型匹配 | ❌ | ✅ |
| 条件守卫 | ❌ | ✅ |
| OR 模式 | ❌ | ✅ |
| 表达式形式 | ❌ | ✅ |
| Fall-through | ✅ | ❌ |
| 需要 break | ✅ | ❌ |

### 6.4 实现计划

**词法分析：**
- 添加 `match` 关键字
- 保留 `=>` 作为匹配箭头

**语法分析：**
- 解析 match 表达式
- 解析模式（字面量、变量、解构）
- 解析守卫条件

**语义分析：**
- 模式穷尽性检查
- 不可达分支检测
- 类型推断

**运行时支持：**
- 实现模式匹配算法
- 支持高效的分支选择

**文件修改：**
- `src/AsulLexer.h`: 添加 `TokenType::Match`
- `src/AsulParser.cpp`: 解析 match 语法
- `src/AsulAst.h`: 新增 `MatchExpr`, `Pattern` 相关节点
- `src/AsulInterpreter.cpp`: 实现模式匹配逻辑

---

## 7. 实现优先级与时间表

### Phase 1 (高优先级 - 3 个月)
1. **解构赋值** - 基础功能，影响多个特性
2. **可选链** - 高频使用，提升代码安全性
3. **FFI libffi 集成** - 解决当前主要限制

### Phase 2 (中优先级 - 6 个月)
4. **模式匹配** - 增强表达能力
5. **FFI 复杂类型支持** - 完善 FFI 功能
6. **生成器/yield** - 支持惰性求值

### Phase 3 (低优先级 - 12 个月)
7. **装饰器** - 元编程支持
8. **FFI 回调支持** - 双向互操作
9. **异步生成器** - 高级异步特性

---

## 8. 相关资源

### 文档
- [README.md](README.md) - 项目总览和快速开始
- [ALang_Technical_Reference.md](ALang_Technical_Reference.md) - 技术参考手册
- [Plan.md](Plan.md) - 改进建议和架构分析

### 示例代码
- `Example/` - 所有语言特性示例
- `Example/ffi_test.alang` - FFI 当前功能演示

### 实现参考
- ECMAScript 规范 - 解构、可选链、生成器
- Python PEP - 模式匹配（PEP 634-636）
- TypeScript - 装饰器设计
- Rust - 模式匹配和类型系统
- libffi 文档 - FFI 实现细节

---

## 9. 贡献指南

如果您想参与这些特性的实现：

1. **选择特性**：从上述列表中选择感兴趣的特性
2. **阅读文档**：熟悉相关规范和实现参考
3. **设计提案**：在 GitHub Issues 中提交设计提案
4. **实现原型**：创建功能分支并实现原型
5. **编写测试**：确保完整的测试覆盖
6. **提交 PR**：提交 Pull Request 并参与代码审查

### 联系方式
- GitHub Issues: 提交 bug 报告和功能请求
- Pull Requests: 贡献代码和文档改进

---

*最后更新：2024年12月*
