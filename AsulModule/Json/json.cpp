#include "json.h"

// 注意：这个文件的实现会被ALangEngine.cpp调用，所以需要确保类型匹配

// 这里我们将json package的实现放在一个独立的命名空间中
// 但是由于无法直接访问ALangEngine.cpp中的匿名命名空间类型，
// 我们将在ALangEngine.cpp中保留json package的注册代码，
// 并将具体的实现逻辑迁移到这里

namespace AsulModule {
    namespace Json {
        // 这里暂时留空，我们将在ALangEngine.cpp中直接实现json package的初始化
        // 因为需要访问ALangEngine.cpp中的内部类型
        void initialize(std::shared_ptr<Object> jsonPkg) {
            // 这个函数的实现将在ALangEngine.cpp中完成
        }
    }
}
