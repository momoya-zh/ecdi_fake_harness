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

// 一个模型 = 名字 + 两个容量参数。
// ★ 为什么 `context`/`maxOutput` 属于模型而不属于提供商：同一端点上
//   glm-4.6 与 glm-4-flash 的上下文窗口本来就不一样。v3 把它们挂在提供商上，
//   等于"同一把 key 的两个模型必须各配一份窗口"——那不是配置，那是抄写。
struct ModelConfig {
    std::string name;               // 模型名（请求体 `model` 字段的值）
    int context = 0;                // 上下文容量（token）；0 = core 不发 hello.context
    int maxOutput = 0;              // 请求体 max_tokens；0 = 不发（端点默认）
};

struct ProviderConfig {
    // ★★ §9.5（2026-10-27）：身份由用户**声明**，不再从地址里猜。
    //   id  = 识别名（机器键，**唯一**）：凭据目标名 / 环境变量片段 / 协议
    //         `model.provider` / `defaultProviderId` 全绑在它上面。字符集
    //         `[A-Za-z0-9._-]`、非空、≤64；唯一性按 **sanitize 后大写** 判
    //         （`abc` 与 `ABC` 在环境变量里同名，见 AppSettings 的校验）。
    //   name = 显示名（**可重复**，仅用于界面）。空 ⇒ 界面回落显示 id
    //         （不引入"无名"状态，规格 9.5.2）。
    //   ★ 为什么必须分开：派生主键无法表达"同一端点的两个模型 / 两把 key"，
    //     v2 时代实测同 id 配置 8 份塌成 1 份、删除按 id 过滤后空向量越界。
    std::string id;
    std::string name;
    std::string baseUrl;            // OpenAI 兼容端点根（不含 /chat/completions）
    std::string credentialTarget;   // Credential Manager 目标名（引用，不是值）

    // ★★ v4（2026-10-27）：提供商 = 端点 + 凭据，模型是它的**子项**（1—N）。
    //   拆层的动机是结构性的，不是 UI 便利：凭据目标名按 id 派生 ⇒ 一 provider
    //   一把 key。模型若还钉在 provider 上，同一端点加第二个模型就得复制整条
    //   provider 并**重新录入密钥**（v3 实况），而那把 key 本来是同一把。
    std::vector<ModelConfig> models;
    // 当前激活的模型名（**存名字不存下标**：删模型时下标会漂移，名字不会；
    // 配置 diff 也可读）。空或找不到 ⇒ 回落 `models[0]`。
    std::string activeModel;

    // 激活模型的三元组。空表 ⇒ nullptr / "" / 0。
    // ★ 这三个访问器是「v3 的 pc.model/context/maxOutput」的等价物：迁移后
    //   models 恰好一项，取值与老字段逐字节相同，所以既有读点改访问器不改行为。
    const ModelConfig* Active() const;
    ModelConfig* MutableActive();          // 空表 ⇒ push 一个空项（调用方负责填）
    std::string ActiveModelName() const;
    int ActiveContext() const;
    int ActiveMaxOutput() const;
};

struct AppConfig {
    // ★ 配置格式版本 = **单一真相**。`version` 成员的初值、`Load` 的接受上限、
    //   探针的「保存后升级到当前版本」判据都取它。
    //   演进：1 单提供商 → 2 多提供商 → 3 加 `name` → 4 provider 1—N model → 5 knownWorkspaces。
    //   ★ 为什么提成常量：v4 那次升级时，`--settings-probe` 的 rename-ok 相里
    //     硬编码的 `"version":3` 让整个回归红了——版本号散落在两处就必然漏一处。
    static constexpr int kCurrentVersion = 5;
    // ★ v2（P3.6）：多提供商。providers = 全部已配置的提供商（顺序 = 编辑序，
    //   不重排）；defaultProviderId = 无参启动连接的那一家。v1 单提供商配置在
    //   Load 时迁移（providers[0] = 原 provider，default = 它的 id）。
    // ★ v3（2026-10-27，§9.5）：ProviderConfig 增 `name`（显示名）。v1/v2 载入时
    //   `name` 缺失 ⇒ 回落 `id`——老配置观感不变、凭据不断、defaultProviderId
    //   语义不动，下次 Save 自动升级为 v3。
    // ★★ v4（2026-10-27）：`model`/`context`/`maxOutput` 收进 `models[]` +
    //   `activeModel`。v1/v2/v3 载入时顶层三元组折叠成 `models[0]`，
    //   `activeModel` = 那个名字 ⇒ **id / credentialTarget / defaultProviderId
    //   一个都不动**（老配置的 key 不断、默认提供商不变），下次 Save 升级为 v4。
    //   Save **双写**顶层 `model/context/maxOutput`（= 激活值）：给"只认顶层字段"
    //   的旧读取路径留一个降级口。两者都从 `Active()` 生成，不会各说各话。
    // ★★ v5（2026-10-28）：`knownWorkspaces`（已知工作区路径列表，几 KB，仍在 APPDATA）。
    //   工作区注册表本身是纯内存的；启动时按已知工作区扫各自的 .ecdi/ 目录恢复历史。
    //   老版本载入时字段缺失 ⇒ 默认为空，保存时自动升级为 v5。
    int version = kCurrentVersion;
    std::vector<ProviderConfig> providers;
    std::string defaultProviderId;
    std::string corePath;           // 可选：ecdi-core.exe 路径覆盖；空 = 自动解析
    std::vector<std::string> knownWorkspaces; // 已知工作区路径列表

    // 便捷访问：默认提供商（defaultProviderId 找不到 ⇒ providers[0]；空表 ⇒ nullptr）。
    const ProviderConfig* DefaultProvider() const;
    // 按 id 查找；找不到 ⇒ nullptr。
    const ProviderConfig* Find(const std::string& id) const;
    // upsert：按 id 替换或追加（设置面板「保存」的唯一写入口）。
    void Upsert(const ProviderConfig& pc);

    // 工作区已知列表操作（去重追加 / 移除）
    void AddKnownWorkspace(const std::string& path);
    void RemoveKnownWorkspace(const std::string& path);

    // 文件不存在 → false + error="no config file"（首启分支据此判定）；
    // 存在但解析失败 → false + 具体 error（带病运行被禁止）。
    bool Load(std::string& error);
    bool Save(std::string& error) const;

    static std::filesystem::path DefaultDir();
    static std::filesystem::path DefaultPath();
    // 探针/测试：替换本次进程的配置路径（不落任何静态状态——只影响本进程）。
    static void SetPathForTesting(const std::filesystem::path& p);

    // ★★ §9.5 起**降级**：这不再是 id 的唯一真相，只是「新增提供商时识别名留空」
    //   的兜底初值。身份由用户在设置面板声明（`ProviderConfig::id`），因为从 host
    //   派生表达不了"同一端点的两个模型 / 两把 key"。仍在用的唯一调用点是
    //   `OnSettingsSave` 的空 id 分支 + 探针（探针故意留空以覆盖这条兜底路径）。
    //   ⇒ 看到别处还在拿它算 id，那就是没迁移干净的旧代码。
    static std::string ProviderIdFromBaseUrl(const std::string& baseUrl);
    static std::string CredentialTargetFor(const std::string& providerId);
};
