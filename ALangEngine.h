#ifndef ALANGENGINE_H
#define ALANGENGINE_H


#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class ALangEngine {
public:
    ALangEngine();
    ~ALangEngine();
    void initialize();
    // 执行一段JS源码
    void execute(const std::string& code);
    // 兼容旧接口：执行最近一次设置的源码（若有）
    void execute();
    // 加载源码（供execute()无参调用）
    void setSource(const std::string& code);
    void registerModule(const char* moduleName, std::function<void()> initFunc);
    // 错误输出颜色映射配置（键：header/code/caret/label/value）
    void setErrorColorMap(const std::unordered_map<std::string, std::string>& colorMap);

    // 宿主原生类注册（简化版）：仅支持基本类型（null/number/string/bool）参数与返回
    using NativeValue = std::variant<std::monostate,double,std::string,bool>;
    using NativeFunc = std::function<NativeValue(const std::vector<NativeValue>&, void* thisHandle)>;
    void registerClass(
        const std::string& className,
        NativeFunc constructor,
        const std::unordered_map<std::string, NativeFunc>& methods,
        const std::vector<std::string>& baseClasses = {}
    );

    // 从宿主侧调用脚本中的全局函数（仅基元参数与返回值）
    NativeValue callFunction(
        const std::string& functionName,
        const std::vector<NativeValue>& args
    );

    // 运行事件循环直到空闲（处理 then/catch、go 任务）
    void runEventLoopUntilIdle();
private:
    struct Impl;
    Impl* impl; // PImpl以隐藏实现细节，减少头文件依赖

protected:


};

#endif // ALANGENGINE_H