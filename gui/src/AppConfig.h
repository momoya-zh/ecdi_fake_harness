#pragma once
// 应用配置文件（P3.5，规格 §8.5 D6）。
//
// ★ 结构性防泄漏：本结构体**没有**密钥字段——API key 只进 Windows
//   Credential Manager（见 Credentials.h），这里只存它的**目标名引用**。
//   「配置文件永不携带密钥值」不靠纪律，靠类型里根本放不下。
//
// 位置：%APPDATA%\ecdi-fake-harness\config.json（用户目录，不进仓库、
// 不进安装目录）；探针/测试可用 SetPath 指到别处（隔离用户真实配置）。

#include <filesystem>
#include <string>
#include <vector>

struct ProviderConfig {
    std::string id;                 // 提供商 id（由 BaseURL 的 host 派生；model 消息按它寻址）
    std::string baseUrl;            // OpenAI 兼容端点根（不含 /chat/completions）
    std::string model;              // 模型名
    int context = 0;                // 上下文容量（token）；0 = core 不发 hello.context
    int maxOutput = 0;              // 请求体 max_tokens；0 = 不发（端点默认）
    std::string credentialTarget;   // Credential Manager 目标名（引用，不是值）
};

struct AppConfig {
    // ★ v2（P3.6）：多提供商。providers = 全部已配置的提供商（顺序 = 编辑序，
    //   不重排）；defaultProviderId = 无参启动连接的那一家。v1 单提供商配置在
    //   Load 时迁移（providers[0] = 原 provider，default = 它的 id）。
    int version = 2;
    std::vector<ProviderConfig> providers;
    std::string defaultProviderId;
    std::string corePath;           // 可选：ecdi-core.exe 路径覆盖；空 = 自动解析

    // 便捷访问：默认提供商（defaultProviderId 找不到 ⇒ providers[0]；空表 ⇒ nullptr）。
    const ProviderConfig* DefaultProvider() const;
    // 按 id 查找；找不到 ⇒ nullptr。
    const ProviderConfig* Find(const std::string& id) const;
    // upsert：按 id 替换或追加（设置面板「保存」的唯一写入口）。
    void Upsert(const ProviderConfig& pc);

    // 文件不存在 → false + error="no config file"（首启分支据此判定）；
    // 存在但解析失败 → false + 具体 error（带病运行被禁止）。
    bool Load(std::string& error);
    bool Save(std::string& error) const;

    static std::filesystem::path DefaultDir();
    static std::filesystem::path DefaultPath();
    // 探针/测试：替换本次进程的配置路径（不落任何静态状态——只影响本进程）。
    static void SetPathForTesting(const std::filesystem::path& p);

    // 提供商 id 与凭据目标名由 baseUrl 派生的唯一真相（设置界面与加载共用）。
    static std::string ProviderIdFromBaseUrl(const std::string& baseUrl);
    static std::string CredentialTargetFor(const std::string& providerId);
};
