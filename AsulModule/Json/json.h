#pragma once

// 这个头文件只用于声明json package的初始化函数
// 注意：这个函数只能在ALangEngine.cpp内部调用，因为它依赖于ALangEngine.cpp中的内部类型

// 前向声明ALangEngine.cpp中定义的类型
struct Object;

namespace AsulModule {
    namespace Json {
        // 初始化json package
        void initialize(std::shared_ptr<Object> jsonPkg);
    }
}
