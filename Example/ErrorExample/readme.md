ErrorExample 目录说明

- 每个 .alang 文件演示一种典型的错误（词法、解析、运行时）并在文件开头提供中文注释说明触发条件。
- 用法：

```bash
# 构建
bash build.sh
# 运行某个示例
./alang Example/ErrorExample/undefined_variable.alang
```

示例列表：

## 基础错误
- undefined_variable.alang — 未定义变量访问（运行时）
- invalid_assignment_target.alang — 非左值赋值（解析）
- unterminated_string.alang — 未终止字符串（词法）
- call_non_function.alang — 调用非函数（运行时）

## 数组与对象错误
- index_out_of_range.alang — 数组索引越界（运行时）
- index_non_array.alang — 对非数组进行索引（运行时）
- index_assignment_non_array.alang — 对非数组/非对象进行索引赋值（运行时）
- property_access_non_object.alang — 对非对象进行属性访问（运行时）
- expect_property_name.alang — 对象字面量缺失属性名（解析）

## 展开运算符错误
- spread_element_not_array.alang — 在数组字面量中展开非数组值（运行时）
- spread_value_not_object.alang — 在对象字面量中展开非对象值（运行时）

## 导入错误
- import_not_found.alang — import 无法找到/打开文件（加载错误）
- import_private_symbol.alang — 导入私有符号（运行时）
- missing_import_math.alang — 未导入 `std.math` 时访问 `abs` 等符号（运行时/符号未定义）

## 接口与类错误
- interface_with_body.alang — 接口方法包含函数体（解析）
- missing_interface_method.alang — 类未实现接口方法（运行时）
- missing_multiple_interface.alang — 类未实现多个接口的方法（运行时）

## 异步编程错误
- await_non_promise.alang — 在非 Promise 上使用 await（运行时）

## 标准库包错误

### std.math
- math_arity_error.alang — 数学函数参数数量错误（运行时）
- clamp_invalid_range.alang — clamp 函数 min > max 错误（运行时）

### std.string
- string_repeat_negative.alang — repeat 函数重复次数为负数（运行时）
- type_mismatch_string.alang — 字符串函数类型不匹配（运行时）

### std.io
- file_not_found.alang — 文件读取失败（运行时）

### std.events
- emit_no_signal_name.alang — emit 未提供信号名称（运行时）
- receive_non_function.alang — receive 槽参数不是函数（运行时）
- connect_non_asulobject.alang — connect 连接非 AsulObject 实例（运行时）

### json
- json_parse_error.alang — JSON 解析错误（运行时）

### collections (map/set)
- map_arity_error.alang — Map 方法参数数量错误（运行时）

## 其他
- invalid_interpolation.alang — 插值表达式错误（解析）
- assign_undefined.alang — 赋值时使用未定义变量（运行时/解析）

