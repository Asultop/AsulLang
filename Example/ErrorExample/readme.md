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
- undefined_variable.alang — 未定义变量访问（运行时）
- invalid_assignment_target.alang — 非左值赋值（解析）
- unterminated_string.alang — 未终止字符串（词法）
- expect_property_name.alang — 对象字面量缺失属性名（解析）
- index_out_of_range.alang — 数组索引越界（运行时）
- type_error_add_string_number.alang — 字符串与数字的加法（示例类型混合）
- call_non_function.alang — 调用非函数（运行时）
- index_non_array.alang — 对非数组进行索引（运行时）
- invalid_interpolation.alang — 插值表达式错误（解析）
- assign_undefined.alang — 赋值时使用未定义变量（运行时/解析）
 - spread_element_not_array.alang — 在数组字面量中展开非数组值（运行时）
 - spread_value_not_object.alang — 在对象字面量中展开非对象值（运行时）
 - index_assignment_non_array.alang — 对非数组/非对象进行索引赋值（运行时）
 - property_access_non_object.alang — 对非对象进行属性访问（运行时）
 - await_non_promise.alang — 在非 Promise 上使用 await（运行时）
- import_not_found.alang — import 无法找到/打开文件（加载错误）
- missing_import_math.alang — 未导入 `std.math` 时访问 `abs` 等符号（运行时/符号未定义）
