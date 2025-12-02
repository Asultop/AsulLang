# ALang 技术参考手册

本文档详细整理了 ALang 语言的 Token、类型系统、内置方法及 API 接口等技术细节。

## 1. 词法与语法基础 (Tokens & Syntax)

### 1.1 关键字 (Keywords)
ALang 保留了以下关键字，用于控制流、声明和其他语言特性：

| 类别 | 关键字 |
| :--- | :--- |
| **声明** | `let`, `var`, `const`, `function` (`fn`), `class`, `interface` |
| **控制流** | `if`, `else`, `while`, `for`, `foreach`, `in`, `break`, `continue`, `switch`, `case`, `default`, `return` |
| **面向对象** | `new`, `extends`, `this` (隐含), `super` (隐含) |
| **异步/并发** | `async`, `await`, `go` |
| **异常处理** | `try`, `catch`, `throw` |
| **模块化** | `import`, `from` |
| **字面量** | `true`, `false`, `null` |

### 1.2 运算符 (Operators)

| 类型 | 运算符 | 说明 |
| :--- | :--- | :--- |
| **算术** | `+`, `-`, `*`, `/`, `%` | 基本算术运算 |
| **赋值** | `=`, `+=`, `-=`, `*=`, `/=`, `%=` | 赋值与复合赋值 |
| **自增/减** | `++`, `--` | 前缀/后缀自增减 |
| **比较** | `==`, `!=`, `===`, `!==`, `<`, `>`, `<=`, `>=` | 相等性与关系比较 |
| **接口匹配** | `=~=` | 检查左值是否实现右侧接口 / 类描述符 |
| **逻辑** | `&&`, `||`, `!` | 逻辑与、或、非 |
| **位运算** | `~`, `&`, `|`, `^`, `<<`, `>>` | 按位非（仅一元 ~）、与、或、异或、左移、右移（均对 number 转 64 位整数后操作） |
| **其他** | `.`, `...`, `->`, `?`, `:`, `(`, `)`, `[`, `]`, `{`, `}` | 成员访问、展开、箭头函数、三元运算等 |

### 1.3 注释 (Comments)
- **单行注释**: 使用 `//` 或 `#` (Python风格)。
- **多行/块注释**: 使用 `/* ... */`。
- **文档字符串**: 支持 `""" ... """` 或 `''' ... '''` (类似 Python 的多行字符串作为注释)。

### 1.4 字符串插值 (String Interpolation)
支持在字符串中使用 `${expression}` 进行插值。
- 语法: `"Value: ${x + 1}"`
- 机制: 解析器会自动将插值字符串转换为字符串连接表达式。

---

## 2. 类型系统 (Type System)

ALang 是动态类型语言，但在内部通过 `ValueTag` 维护类型。

### 2.1 基础类型 (Primitives)
- **null**: 空值 (`std::monostate`)。
- **number**: 双精度浮点数 (`double`)。
- **string**: 字符串 (`std::string`)。
- **boolean**: 布尔值 (`bool`)。

### 2.2 复杂类型 (Complex Types)
- **function**: 函数对象 (`std::shared_ptr<Function>`)。
- **array**: 动态数组 (`std::shared_ptr<Array>`)，底层为 `std::vector<Value>`。
- **object**: 键值对集合 (`std::shared_ptr<Object>`)，底层为 `std::unordered_map<std::string, Value>`。
- **class**: 类定义 (`std::shared_ptr<ClassInfo>`)。
- **instance**: 类实例 (`std::shared_ptr<Instance>`)。
- **promise**: 异步承诺 (`std::shared_ptr<PromiseState>`)。

### 2.3 类型检查 API
- **`typeof(x)`**: 返回变量 `x` 的类型名称字符串（如 `"string"`, `"number"`, `"array"` 等）。
- **`len(x)`**: 获取字符串、数组或对象的长度。

---

## 3. 内置函数 (Built-in Functions)

ALang 提供了一组全局可用的内置函数：

| 函数名 | 参数 | 描述 |
| :--- | :--- | :--- |
| `print` | `...args` | 打印参数到标准输出（无换行）。 |
| `println` | `...args` | 打印参数到标准输出并换行。 |
| `readFile` | `path` | 读取文件全部内容为字符串。 |
| `writeFile` | `path, data` | 覆盖写入文本/数据到文件。返回 `true`。 |
| `appendFile` | `path, data` | 末尾追加文本/数据到文件。返回 `true`。 |
| `exists` | `path` | 判断路径是否存在，返回布尔。 |
| `listDir` | `path` | 返回目录下 (非递归) 文件/子目录名数组。 |
| `std.io.fileSystem.File` | `class` | 内置文件类：支持 `write/append/read/exists/size` 等方法。 |
| `std.io.fileSystem.Dir` | `class` | 内置目录类：支持 `create/list/exists` 等方法。 |
| `std.io.FileStream` | `class` | 流式读写：`read(n)`, `write(data)`, `eof()`, `close()`。由 `File.open(mode)` 创建。 |

### 4.x 文件系统扩展 (std.io.fileSystem)

`std.io.fileSystem` 提供了更丰富的文件系统操作接口。

#### 核心类
- **`File`**: 文件操作类。
- **`Dir`**: 目录操作类。

#### 静态函数
| 函数 | 参数 | 描述 |
| :-- | :-- | :-- |
| `mkdir(path)` | 路径 | 创建目录（递归）。 |
| `rmdir(path)` | 路径 | 删除目录（递归）。 |
| `stat(path)` | 路径 | 获取文件/目录元数据。返回对象 `{ size, mtime, isFile, isDir, permissions }`。 |
| `copy(src, dest)` | 源, 目标 | 复制文件或目录（递归）。 |
| `move(src, dest)` | 源, 目标 | 移动/重命名文件或目录。 |
| `chmod(path, mode)` | 路径, 模式 | 修改权限（模式为数字）。 |
| `walk(path, callback)` | 路径, 回调 | 递归遍历目录。回调 `fn(path, isDir)`。若回调返回 `false` 则停止遍历。 |

#### File / Dir / FileStream 方法速览

| 类 | 方法 | 描述 |
| :-- | :-- | :-- |
| `File` | `write(data)` | 覆盖写入文本或字节（若为数组数字视为字节）。 |
| `File` | `append(data)` | 追加文本。 |
| `File` | `read()` | 读取全文本。 |
| `File` | `writeBytes(bytes[])` | 覆盖写入字节数组。 |
| `File` | `appendBytes(bytes[])` | 追加写入字节。 |
| `File` | `readBytes()` | 返回全部字节数组。 |
| `File` | `exists()` | 路径是否存在。 |
| `File` | `size()` | 字节大小。 |
| `File` | `delete()` | 删除文件。 |
| `File` | `rename(newPath)` | 重命名/移动。 |
| `File` | `open(mode)` | 返回 `FileStream` (`r`/`w`/`a`)。 |
| `FileStream` | `read(n)` | 读取 `n` 字符（文本模式）。 |
| `FileStream` | `write(data)` | 写入文本或字节追加/覆盖取决于模式。 |
| `FileStream` | `eof()` | 是否到达末尾。 |
| `FileStream` | `close()` | 关闭流。 |
| `Dir` | `create()` | 创建目录（含递归）。 |
| `Dir` | `list()` | 列出当前层级子项。 |
| `Dir` | `exists()` | 是否存在。 |
| `Dir` | `delete()` | 递归删除目录内容与自身。 |
| `Dir` | `rename(newPath)` | 重命名/移动目录。 |
| `Dir` | `walk()` | 递归返回所有相对路径。 |

示例参考：`fileIOExample.alang`, `fileIOClassExample.alang`, `fileIOAdvancedExample.alang`。

#### 日期与时间 (std.time)

提供轻量日期/时间支持（UTC）：

| 函数 | 描述 |
| :-- | :-- |
| `nowEpochMillis()` | 当前 UTC 时间戳（毫秒）。 |
| `nowEpochSeconds()` | 当前 UTC 时间戳（秒）。 |
| `nowISO()` | 当前 UTC ISO8601 字符串，格式 `YYYY-MM-DDTHH:mm:SS.mmmZ`。 |
| `dateFromEpoch(ms)` | 由毫秒时间戳构造 `Date` 实例。 |

`Date` 类（UTC）字段与方法：

| 方法 | 说明 |
| :-- | :-- |
| `toISO()` | 返回构造时的 ISO 字符串。 |
| `getYear()` | 年 (YYYY)。 |
| `getMonth()` | 月 (1-12)。 |
| `getDay()` | 日 (1-31)。 |
| `getHour()` | 时 (0-23)。 |
| `getMinute()` | 分 (0-59)。 |
| `getSecond()` | 秒 (0-59)。 |
| `getMillisecond()` | 毫秒 (0-999)。 |
| `getEpochMillis()` | 原始毫秒时间戳。 |

构造用法：`new std.time.Date(epochMillis)` 或 `dateFromEpoch(ms)`。

示例：参见 `dateTimeExample.alang`。

**注意**：`std.time` 并不自动导入到全局（当前默认仅 `std.io` 自动导入）。要使用 `std.time` 中的符号，请在脚本中显式导入：

```alang
import std.time.*; // 或者使用 std.time.nowISO() 等完全限定名
```

## JSON 支持 (包: `json`)

ALang 提供内建的 `json` 包用于在 ALang 值与 JSON 文本之间互相转换。包名为 `json`，可以使用简洁导入语法：

```alang
import json;
```

主要 API：

- `json.stringify(value)`：把 ALang 值序列化为 JSON 字符串。支持 `object`、`array`、`string`、`number`、`boolean` 与 `null`。对象的键将以字符串形式输出；函数与未序列化的宿主类型会被忽略或引发错误（视具体值而定）。
- `json.parse(text)`：把合法的 JSON 字符串解析为 ALang 值（`object`、`array`、`string`、`number`、`boolean`、`null`）。解析错误会抛出异常并包含位置提示。

示例：

```alang
import json;

let obj = { name: "Alice", age: 30, tags: ["dev", "rust"], active: true, meta: null };
let s = json.stringify(obj);
print(s);

let parsed = json.parse(s);
print(parsed.name);
```

示例脚本：`Example/jsonExample.alang`

| `len` | `x` | 返回字符串字符数、数组元素数或对象键值对数。 |
| `quote` | `str` | 将代码字符串解析为 Token 列表对象，包含 `apply()` 方法可重新执行。 |
| `push` | `arr, ...values` | 向数组末尾追加元素，返回新长度。 |
| `typeof` | `x` | 返回值的类型名称。 |
| `eval` | `str` | 在子环境中执行 ALang 代码字符串，返回最后一个表达式的值。 |
| `sleep` | `ms` | 异步休眠指定毫秒数，返回 Promise。 |
| `keys` | `obj` | 返回对象所有键组成的数组。 |
| `values` | `obj` | 返回对象所有值组成的数组。 |
| `entries` | `obj` | 返回对象 `[key, value]` 对组成的数组。 |
| `clone` | `obj` | 浅拷贝对象或数组。 |
| `merge` | `a, b` | 浅合并两个对象，返回新对象。 |
| `range` | `n` | 生成 `[0, n-1]` 的数字数组。 |
| `enumerate` | `iterable` | 返回 `[index, value]` (数组) 或 `[key, value]` (对象) 的数组。 |
| `keysSorted` | `container, [cmp]` | 返回排序后的键数组。 |

---

## 4. 内置类与对象 (Built-in Classes & Objects)

### 3.x 位运算详解 (Bitwise Operations)

ALang 的位运算针对 `number` 值：在执行时把双精度浮点转为 `long long`（截断 toward zero），然后进行 64 位补码运算，再把结果转换回 `number`。

支持的运算符：

| 运算符 | 说明 | 示例 | 结果 |
| :--- | :--- | :--- | :--- |
| `~x` | 按位取反 | `~13` | `-14` |
| `a =~= I` | 接口匹配 | `obj =~= SomeInterface` | `true/false` |
| `a & b` | 按位与 | `13 & 6` | `4` |
| `a | b` | 按位或 | `13 | 6` | `15` |
| `a ^ b` | 按位异或 | `13 ^ 6` | `11` |
| `a << n` | 左移 | `13 << 2` | `52` |
| `a >> n` | 右移（算术） | `13 >> 1` | `6` |

当前优先级（高 → 低，节选）：
1. 一元：`! ~ -`  
2. 乘除模：`* / %`  
3. 加减：`+ -`  
4. 移位：`<< >>`  
5. 比较：`< <= > >=`  
6. 接口匹配：`=~=`  
7. 相等：`== != === !==`  
8. 按位与：`&`  
9. 按位异或：`^`  
10. 按位或：`|`  
11. 逻辑与：`&&`  
12. 逻辑或：`||`

注意：由于目前实现顺序，位运算与接口匹配和相等之间仍保持较低优先级；编写如 `a | b == 15` 时请使用括号 `(a | b) == 15` 以确保意图明确。后续可根据需要调整为更常见的 JS/C 风格优先级。

示例：参见 `Example/bitwiseExample.alang`。

### 4.1 Promise
用于处理异步操作。
- **`Promise.resolve(value)`**: 返回一个已解决的 Promise。
- **`Promise.reject(reason)`**: 返回一个已拒绝的 Promise。

### 4.2 std.math

数学工具库包，提供基础数学常量与函数：

#### 常量
- **`std.math.pi`**: 圆周率 π ≈ 3.14159。

#### 基础函数
- **`abs(x)`**: 返回绝对值。

#### 三角函数（弧度制）
- **`sin(x)`**, **`cos(x)`**, **`tan(x)`**: 正弦、余弦、正切。

#### 指数与对数
- **`exp(x)`**: e^x。
- **`log(x)`**: 自然对数 ln(x)。
- **`pow(base, exp)`**: 幂运算 base^exp。
- **`sqrt(x)`**: 平方根 √x。

#### 取整
- **`ceil(x)`**: 向上取整。
- **`floor(x)`**: 向下取整。
- **`round(x)`**: 四舍五入到最近整数。

#### 最值与随机
- **`min(...nums)`**: 返回最小值（可变参数）。
- **`max(...nums)`**: 返回最大值（可变参数）。
- **`random()`**: 返回 [0, 1) 内随机数。
- **`random(max)`**: 返回 [0, max) 内随机数。
- **`random(min, max)`**: 返回 [min, max) 内随机数。

使用方式：`import std.math.*` 或 `std.math.sin()` 等完全限定名。

示例：参见 `Example/mathExample.alang`。

### 4.3 数据结构类
ALang 内置了基于宿主 C++ 实现的高效数据结构。

#### Map
有序键值对集合。
- `new Map()` / `map()`: 构造函数。
- `set(key, value)`: 设置键值。
- `get(key)`: 获取值。
- `has(key)`: 检查键是否存在。
- `delete(key)`: 删除键。
- `size()`: 返回大小。
- `clear()`: 清空。
- `keys()`, `values()`, `entries()`: 迭代器方法。

#### Set
唯一值集合。
- `new Set()` / `set()`: 构造函数。
- `add(value)`: 添加值。
- `has(value)`: 检查值是否存在。
- `delete(value)`: 删除值。
- `size()`: 返回大小。
- `values()`: 返回所有值。

#### Deque (双端队列)
- `new Deque()` / `deque()`: 构造函数。
- `push(v)`: 尾部添加。
- `pop()`: 尾部移除。
- `unshift(v)`: 头部添加。
- `shift()`: 头部移除。
- `peek()`: 查看两端元素（无参看头，有参看尾）。
- `size()`: 大小。
- `clear()`: 清空。

#### Stack (栈)
- `new Stack()` / `stack()`: 构造函数。
- `push(v)`: 入栈。
- `pop()`: 出栈。
- `peek()`: 查看栈顶。
- `size()`: 大小。

### 4.4 字符串方法 (String Methods)

ALang 字符串对象提供以下合成方法：

| 方法 | 参数 | 描述 |
| :-- | :-- | :-- |
| `len()` | 无 | 返回字符串长度。 |
| `split(delim)` | 分隔符 | 按分隔符拆分字符串，返回数组；若分隔符为空，按字符分割。 |
| `substring(start, [end])` | 起点、可选终点 | 返回子串；索引越界返回空串。 |
| `replace(search, repl)` | 查找串、替换串 | 替换首次出现的查找串，返回新字符串。 |
| `trim()` | 无 | 移除首尾空白，返回新字符串。 |
| `toLowerCase()` | 无 | 转小写。 |
| `toUpperCase()` | 无 | 转大写。 |
| `startsWith(prefix)` | 前缀串 | 返回布尔，检查是否以给定前缀开头。 |
| `endsWith(suffix)` | 后缀串 | 返回布尔，检查是否以给定后缀结尾。 |
| `includes(sub)` | 子串 | 返回布尔，检查是否包含子串。 |
| `indexOf(search, [start])` | 查找串、可选起点 | 返回首次出现的索引，不存在返回 -1；若提供起点则从该位置开始。 |

示例：
```alang
let s = "  Hello World  ";
println(s.trim());              // "Hello World"
println(s.toLowerCase());       // "  hello world  "
println(s.indexOf("World"));     // 8
println(s.includes("lo"));       // true
println(s.split(" "));          // ["", "", "Hello", "World", "", ""]
```

示例：参见 `Example/interpolationExample.alang`。

### 4.5 数组方法 (Array Methods)

ALang 数组对象提供以下合成方法：

#### 查询与判断
| 方法 | 参数 | 描述 |
| :-- | :-- | :-- |
| `len()` | 无 | 返回数组长度。 |
| `includes(value)` | 值 | 返回布尔，检查数组是否包含该值。 |
| `indexOf(value, [start])` | 值、可选起点 | 返回首次出现的索引，不存在返回 -1。 |
| `find(predicate)` | 谓词函数 | 返回首个满足条件的元素，不存在返回 `null`。 |
| `some(predicate)` | 谓词函数 | 返回布尔，是否存在满足条件的元素。 |
| `every(predicate)` | 谓词函数 | 返回布尔，是否所有元素都满足条件。 |

#### 转换
| 方法 | 参数 | 描述 |
| :-- | :-- | :-- |
| `map(callback)` | 映射函数 | 映射每个元素，返回新数组；回调参数为 `(elem, index, array)`。 |
| `filter(predicate)` | 谓词函数 | 筛选满足条件的元素，返回新数组。 |
| `reduce(callback, [init])` | 归约函数、可选初值 | 累积计算，返回单一值；回调参数为 `(acc, elem, index, array)`。 |
| `slice(start, [end])` | 起点、可选终点 | 返回数组片段的浅拷贝。 |
| `join(delim)` | 分隔符 | 将数组元素用分隔符连接成字符串。 |

#### 修改
| 方法 | 参数 | 描述 |
| :-- | :-- | :-- |
| `push(...values)` | 待添加值 | 在末尾添加元素，返回新长度。 |
| `pop()` | 无 | 移除末尾元素并返回，空数组返回 `null`。 |
| `shift()` | 无 | 移除首个元素并返回，空数组返回 `null`。 |
| `unshift(...values)` | 待添加值 | 在首部添加元素，返回新长度。 |
| `reverse()` | 无 | 原地反转数组，返回数组自身。 |
| `sort([comparator])` | 可选比较函数 | 原地排序；若无比较函数，数字升序，其他字典序；若提供比较函数，返回值 truthy 表示左 < 右。 |
| `splice(start, [deleteCount], ...items)` | 起点、删除数、插入项 | 原地修改数组，返回删除的元素组成的数组。 |

**回调函数参数说明**：
- `map`, `filter`, `find`, `some`, `every`: 回调接收 `(element, index, array)`。
- `reduce`: 回调接收 `(accumulator, element, index, array)`。

示例：
```alang
let arr = [1, 2, 3, 4, 5];
println(arr.map([](x) { return x * 2; }));        // [2, 4, 6, 8, 10]
println(arr.filter([](x) { return x % 2 == 0; })); // [2, 4]
println(arr.reduce([](sum, x) { return sum + x; }, 0)); // 15
println(arr.reverse());                            // [5, 4, 3, 2, 1]
println(arr.join("-"));                             // "5-4-3-2-1"
```

示例：参见 `Example/array_methods_test.alang`。

### 4.6 操作系统交互 (os)

`os` 包提供与操作系统交互的功能。

| 函数 | 参数 | 描述 |
| :-- | :-- | :-- |
| `call(program, [args], [cwd])` | 程序名, 参数数组, 工作目录 | 异步执行外部命令，返回 Promise。 |
| `getEnv(name)` | 变量名 | 获取环境变量值，不存在返回 `null`。 |
| `setEnv(name, value)` | 变量名, 值 | 设置环境变量。 |
| `exit(code)` | 退出码 | 终止进程。 |
| `platform()` | 无 | 返回操作系统名称 (`linux`, `windows`, `darwin`, `unknown`)。 |
| `arch()` | 无 | 返回 CPU 架构 (`x64`, `x86`)。 |

---

## 5. 模块系统 (Module System)

ALang 提供了灵活的模块化机制，支持包（Package）和文件模块（File Module）。

### 5.1 导出 (Export)
在模块文件中，可以通过以下方式导出成员供外部使用：

1. **显式导出 (`export`)**:
   使用 `export` 关键字修饰声明。
   ```alang
   export var Version = "1.0";
   export function add(a, b) { return a + b; }
   export class User { ... }
   ```

2. **隐式导出 (Implicit Export)**:
   首字母大写的变量、函数或类会自动导出，无需 `export` 关键字。
   ```alang
   var PI = 3.14;       // 导出 (大写开头)
   function Log() { ... } // 导出 (大写开头)
   class Config { ... }   // 导出 (大写开头)
   ```

3. **私有成员**:
   未标记 `export` 且首字母小写的成员仅在模块内部可见。
   ```alang
   var internal = "secret"; // 私有
   function helper() { ... } // 私有
   ```

### 5.2 导入 (Import)

#### 文件导入
1. **导入并合并**:
   将目标模块导出的所有符号直接合并到当前作用域。
   ```alang
   import "path/to/module.alang";
   println(Version); // 直接访问
   ```

2. **导入为别名 (Namespace Import)**:
   将目标模块导出的符号封装到一个对象中。
   ```alang
   import "path/to/module.alang" as mod;
   println(mod.Version); // 通过别名访问
   ```

#### 包导入
1. **从包导入指定成员**:
   ```alang
   from std.math import abs;
   from std.math import (pi, sin);
   ```

2. **导入包成员 (Dot 语法)**:
   ```alang
   import std.math.pi;
   import std.math.(abs, max);
   ```

3. **导入整个包**:
   ```alang
   import std.math.*;
   ```

---

## 6. 宿主交互 API (C++ Host API)

对于嵌入 ALang 的 C++ 宿主程序，`ALangEngine` 类提供了以下核心接口：

- **`execute(code)`**: 执行代码字符串。
- **`registerFunction(name, func)`**: 注册 C++ 函数供 ALang 调用。
- **`registerClass(...)`**: 注册 C++ 类。
- **`setGlobal(name, value)`**: 设置全局变量。
- **`callFunction(name, args)`**: 从 C++ 调用 ALang 函数。
- **`HostValue`**: 用于 C++ 与 ALang 之间安全交换数据的桥接类型。

---

## 7. 源码架构 (Source Architecture)

ALang 采用模块化架构，核心组件位于 `src/` 目录：

### 7.1 核心模块

| 文件 | 描述 |
| :-- | :-- |
| `AsulLexer.h/cpp` | 词法分析器：`TokenType` 枚举、`Token` 结构、`Lexer` 类 |
| `AsulParser.h/cpp` | 语法分析器：递归下降解析器 |
| `AsulAst.h` | AST 节点定义：表达式和语句抽象语法树 |
| `AsulRuntime.h/cpp` | 运行时值系统：`Value` 变体、`Environment`、`Function`、`ClassInfo`、`Instance` |
| `AsulInterpreter.h/cpp` | 解释器核心：执行引擎、事件循环、Promise 处理 |
| `AsulAsync.h` | 异步接口：解耦异步操作，允许包执行异步任务而不直接依赖解释器 |
| `AsulPackages.h` | 包注册入口：统一管理内置包的安装 |

### 7.2 标准库包

所有内置包位于 `src/AsulPackages/` 目录，按命名空间组织：

**Std 包 (`std.*`)**
| 包 | 路径 | 描述 |
| :-- | :-- | :-- |
| `std.path` | `Std/Path/` | 路径操作 |
| `std.string` | `Std/String/` | 字符串处理 |
| `std.math` | `Std/Math/` | 数学函数 |
| `std.time` | `Std/Time/` | 日期时间 |
| `std.os` | `Std/Os/` | 系统信息 |
| `std.regex` | `Std/Regex/` | 正则表达式 |
| `std.encoding` | `Std/Encoding/` | 编码转换 |
| `std.network` | `Std/Network/` | 网络通信（使用 `AsulAsync` 接口） |

**顶级包**
| 包 | 路径 | 描述 |
| :-- | :-- | :-- |
| `json` | `Json/` | JSON 序列化/反序列化 |
| `xml` | `Xml/` | XML 解析 |
| `yaml` | `Yaml/` | YAML 解析 |
| `os` | `Os/` | 操作系统交互 |

### 7.3 依赖关系

```
AsulLexer.h    →   AsulRuntime.h   →   AsulAst.h   →   AsulParser.h
     ↓                   ↓                               ↓
AsulLexer.cpp      AsulRuntime.cpp                  AsulParser.cpp
                                                         ↓
                   AsulAsync.h   →   AsulInterpreter.h/cpp
                        ↓
              AsulPackages/* (外部包)
```

### 7.4 扩展指南

添加新包的步骤：
1. 在 `src/AsulPackages/` 下创建包目录（如 `MyPackage/`）
2. 创建头文件和实现文件（`MyPackage.h`、`MyPackage.cpp`）
3. 实现 `installMyPackage(asul::Interpreter& interp)` 函数
4. 在 `AsulPackages.h` 中添加包的 include 和安装调用
5. 在 `CMakeLists.txt` 中添加新的源文件

对于需要异步操作的包，可以通过 `AsulAsync` 接口访问：
- `createPromise()` - 创建 Promise
- `resolve(promise, value)` - 解决 Promise
- `reject(promise, error)` - 拒绝 Promise
- `postTask(task)` - 投递任务到事件循环

---

*文档更新日期: 2025-12-02*
