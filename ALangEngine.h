#ifndef ALANGENGINE_H
#define ALANGENGINE_H


#include <functional>
#include <string>

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
private:
    struct Impl;
    Impl* impl; // PImpl以隐藏实现细节，减少头文件依赖

protected:


};

#endif // ALANGENGINE_H