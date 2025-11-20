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
| `std.io.File` | `class` | 内置文件类：支持 `write/append/read/exists/size` 方法。 |
| `std.io.Dir` | `class` | 内置目录类：支持 `create/list/exists` 方法。 |
| `std.io.FileStream` | `class` | 流式读写：`read(n)`, `write(data)`, `eof()`, `close()`。由 `File.open(mode)` 创建。 |
### 4.x 文件与目录内置类扩展

`File` 额外方法：`delete`, `rename`, `readBytes`, `writeBytes`, `appendBytes`, `open`。`open(mode)` 返回 `FileStream`，`mode` 可为：
- `r`: 只读，支持 `read(n)`
- `w`: 覆盖写，支持 `write(data)`
- `a`: 追加写，支持 `write(data)`

`FileStream` 维护位置 `pos` 与 `closed` 标记；`eof()` 基于文件大小与当前位置判断。

`Dir` 额外方法：`delete`(递归删除)，`rename`，`walk`(递归列出相对路径)。

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
数学工具库包。
- **`std.math.pi`**: 圆周率常量。
- **`std.math.abs(x)`**: 返回绝对值。
- **`std.math.Math`**: 内置 `Math` 类，可通过 `import std.math.*` 或 `std.math.Math` 构造使用。

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

---

## 5. 模块系统 (Module System)

ALang 支持多种导入语法来组织代码。

### 导入语法
1. **从包导入指定成员**:
   ```alang
   from Package import name;
   from Package import (name1, name2);
   ```
2. **导入包成员 (Dot 语法)**:
   ```alang
   import Package.member;
   import Package.(member1, member2);
   ```
3. **导入整个包**:
   ```alang
   import Package.*;
   ```
4. **文件导入**:
   ```alang
   import "path/to/file.alang";
   import ("file1.alang", "file2.alang");
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

*文档生成日期: 2025-11-20*
