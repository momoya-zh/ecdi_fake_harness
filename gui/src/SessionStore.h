#pragma once
// 会话存储层（规格 docs/session-persistence-decisions.md）。
//
// 负责工作区下 `<工作区>/.ecdi/` 目录的初始化、会话元数据（.meta.json）原子写、
// 原始转录日志（.jsonl）逐字节追加与刷盘、以及扫描与清理。
//
// ★ 结构性无密钥：SessionMeta 绝不留 API Key / 凭据字段。
// ★ 归属由位置决定：SessionMeta 不存 workspacePath（搬家时历史跟随工作区走）。

#include "Json.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct SessionMeta {
    std::string id;               // 会话唯一标识：s-<utc>-<4hex>
    std::string label;            // 显示名（首条 prompt 前 N 字，或工作区叶子+序号）
    std::string providerId;       // 提供商识别名（二元组之一）
    std::string modelName;        // 模型名称（二元组之二）
    std::string createdAt;        // 创建时间（ISO 8601 UTC，如 2026-10-28T03:20:00Z）
    std::string lastUsedAt;       // 最后活跃时间
    long long lastSeq = 0;        // 最后见到的协议序号
    bool archived = false;        // 是否归档
    long long sizeBytes = 0;      // journal 文件体积
    long long contextLimit = 0;   // 可选：上下文容量（来自 hello.context）

    // 序列化与反序列化
    json::Value ToJson() const;
    static bool FromJson(const json::Value& val, SessionMeta& out, std::string& error);
};

namespace SessionStore {

// ── 路径计算辅助 ──────────────────────────────────────────
std::filesystem::path EcdiDir(const std::filesystem::path& workspacePath);
std::filesystem::path SessionsDir(const std::filesystem::path& workspacePath);
std::filesystem::path MemoryDir(const std::filesystem::path& workspacePath);
std::filesystem::path GitignorePath(const std::filesystem::path& workspacePath);
std::filesystem::path JournalPath(const std::filesystem::path& workspacePath, const std::string& sessionId);
std::filesystem::path MetaPath(const std::filesystem::path& workspacePath, const std::string& sessionId);
std::filesystem::path UiStatePath(const std::filesystem::path& workspacePath, const std::string& sessionId);
std::filesystem::path ContextPath(const std::filesystem::path& workspacePath, const std::string& sessionId);

// ── 目录初始化与身份生成 ──────────────────────────────────
// 保证 <ws>/.ecdi/、sessions/、memory/ 存在，且如果不存在 .gitignore 则自动写入 "*"
bool EnsureEcdiDir(const std::filesystem::path& workspacePath, std::string& error);

// 生成形如 s-<utc>-<4hex> 的会话 ID（如 s-20261028-032000-a1b2）
std::string GenerateSessionId();

// 获取当前 ISO 8601 UTC 时间字符串（如 2026-10-28T03:20:00Z）
std::string CurrentUtcTimestamp();

// ── 元数据原子落盘与读取 ──────────────────────────────────
// 走 ".tmp + rename" 原子写，失败报出 error，绝不静默丢失
bool SaveMetaAtomic(const std::filesystem::path& workspacePath, const SessionMeta& meta, std::string& error);
bool LoadMeta(const std::filesystem::path& metaPath, SessionMeta& meta, std::string& error);

// ── 会话清理 ──────────────────────────────────────────────
// 同时删除 .jsonl / .meta.json / .jsonl.ui.json / .context.json，缺一不可
bool DeleteSession(const std::filesystem::path& workspacePath, const std::string& sessionId, std::string& error);

// ── 扫描工作区下所有有效会话元数据 ────────────────────────
// 扫描 <ws>/.ecdi/sessions/*.meta.json，按 (createdAt, id) 升序排序
std::vector<SessionMeta> ScanSessionMetas(const std::filesystem::path& workspacePath, std::string& error);

// ── Journal 日志追加器 ────────────────────────────────────
// 仅用于 live 读点：逐字节 append + 每行 flush
class JournalWriter {
public:
    JournalWriter() = default;
    ~JournalWriter();

    JournalWriter(const JournalWriter&) = delete;
    JournalWriter& operator=(const JournalWriter&) = delete;

    bool Open(const std::filesystem::path& journalPath, std::string& error);
    bool AppendLine(const std::string& rawLine, std::string& error);
    void Flush();
    void Close();

    bool IsOpen() const;
    uint64_t BytesWritten() const;
    const std::filesystem::path& Path() const;

private:
    std::filesystem::path m_path;
    std::ofstream m_file;
    uint64_t m_bytesWritten = 0;
};

}  // namespace SessionStore
