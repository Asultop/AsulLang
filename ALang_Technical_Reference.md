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
| **逻辑** | `&&`, `||`, `!` | 逻辑与、或、非 |
| **位运算** | `~` | 按位取反 (目前仅见 Tilde Token) |
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

### 4.1 Promise
用于处理异步操作。
- **`Promise.resolve(value)`**: 返回一个已解决的 Promise。
- **`Promise.reject(reason)`**: 返回一个已拒绝的 Promise。

### 4.2 Math
数学工具库对象。
- **`Math.pi`**: 圆周率常量。
- **`Math.abs(x)`**: 返回绝对值。

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
