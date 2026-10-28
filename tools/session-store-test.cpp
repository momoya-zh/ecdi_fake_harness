// SessionStore 单元与判据测试（2026-10-28）
//
// 验证内容：
//   1. EnsureEcdiDir 建立目录与 .gitignore ("*")
//   2. GenerateSessionId 形如 s-<utc>-<4hex>
//   3. SaveMetaAtomic 原子写 .meta.json，无临时文件残留
//   4. LoadMeta 准确还原所有元数据字段（不包含 workspacePath，不包含密钥）
//   5. JournalWriter 逐字节追加与单行 flush
//   6. ScanSessionMetas 扫描并按 (createdAt, id) 升序排序
//   7. DeleteSession 同时删除 4 份会话相关文件

#include "SessionStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_fail = 0;

void Check(const char* name, bool ok, const std::string& detail = {}) {
    std::printf("  %s %-42s %s\n", ok ? "ok  " : "FAIL", name, detail.c_str());
    if (!ok) ++g_fail;
}

bool Has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path testWs = fs::temp_directory_path() / "ecdi-session-store-test-ws";
    std::error_code ec;
    fs::remove_all(testWs, ec);

    std::string err;

    // ── 1. 目录初始化与 .gitignore ──────────────────────────────
    Check("1a EnsureEcdiDir 成功", SessionStore::EnsureEcdiDir(testWs, err), err);
    Check("1b sessions 目录已创建", fs::is_directory(SessionStore::SessionsDir(testWs)));
    Check("1c memory 目录已创建", fs::is_directory(SessionStore::MemoryDir(testWs)));
    const fs::path gi = SessionStore::GitignorePath(testWs);
    Check("1d .gitignore 文件存在", fs::is_regular_file(gi));
    Check("1e .gitignore 内容为 *", ReadFile(gi) == "*\n", ReadFile(gi));

    // ── 2. 会话 ID 格式 ─────────────────────────────────────────
    const std::string id1 = SessionStore::GenerateSessionId();
    const std::string id2 = SessionStore::GenerateSessionId();
    Check("2a id1 起始为 s-", id1.rfind("s-", 0) == 0, id1);
    Check("2b 两次生成 ID 互不相同", id1 != id2, id1 + " vs " + id2);
    Check("2c ID 长度符合规范", id1.size() >= 20, std::to_string(id1.size()));

    // ── 3. Meta 原子写与读取 ────────────────────────────────────
    SessionMeta meta1;
    meta1.id = id1;
    meta1.label = "测试会话 1";
    meta1.providerId = "openai";
    meta1.modelName = "gpt-4o";
    meta1.createdAt = "2026-10-28T03:00:00Z";
    meta1.lastUsedAt = "2026-10-28T03:05:00Z";
    meta1.lastSeq = 42;
    meta1.archived = false;
    meta1.sizeBytes = 1024;
    meta1.contextLimit = 128000;

    Check("3a SaveMetaAtomic 成功", SessionStore::SaveMetaAtomic(testWs, meta1, err), err);
    const fs::path metaFile = SessionStore::MetaPath(testWs, id1);
    const fs::path tmpFile = metaFile.wstring() + L".tmp";
    Check("3b .meta.json 存在", fs::is_regular_file(metaFile));
    Check("3c .tmp 临时文件不存在", !fs::exists(tmpFile));

    const std::string metaText = ReadFile(metaFile);
    Check("3d meta 不含 workspacePath", !Has(metaText, "workspacePath"));
    Check("3e meta 不含 key 或 credential", !Has(metaText, "credential") && !Has(metaText, "key"));

    SessionMeta loaded1;
    Check("3f LoadMeta 成功", SessionStore::LoadMeta(metaFile, loaded1, err), err);
    Check("3g id 一致", loaded1.id == id1, loaded1.id);
    Check("3h label 一致", loaded1.label == "测试会话 1", loaded1.label);
    Check("3i providerId 一致", loaded1.providerId == "openai", loaded1.providerId);
    Check("3j modelName 一致", loaded1.modelName == "gpt-4o", loaded1.modelName);
    Check("3k createdAt 一致", loaded1.createdAt == "2026-10-28T03:00:00Z", loaded1.createdAt);
    Check("3l lastSeq 一致", loaded1.lastSeq == 42, std::to_string(loaded1.lastSeq));
    Check("3m contextLimit 一致", loaded1.contextLimit == 128000, std::to_string(loaded1.contextLimit));

    // ── 4. JournalWriter 追加与 flush ───────────────────────────
    const fs::path journalFile = SessionStore::JournalPath(testWs, id1);
    {
        SessionStore::JournalWriter writer;
        Check("4a JournalWriter Open 成功", writer.Open(journalFile, err), err);
        Check("4b AppendLine 成功", writer.AppendLine("{\"t\":\"hello\",\"seq\":0}", err), err);
        Check("4c AppendLine 第二行成功", writer.AppendLine("{\"t\":\"text\",\"seq\":1,\"delta\":\"hi\"}\n", err), err);
        Check("4d BytesWritten > 0", writer.BytesWritten() > 0, std::to_string(writer.BytesWritten()));
        writer.Close();
    }
    const std::string journalText = ReadFile(journalFile);
    Check("4e journal 包含两行并补换行", Has(journalText, "{\"t\":\"hello\",\"seq\":0}\n") &&
                                       Has(journalText, "{\"t\":\"text\",\"seq\":1,\"delta\":\"hi\"}\n"));

    // ── 5. 多会话扫描与 (createdAt, id) 升序 ────────────────────
    SessionMeta meta2;
    meta2.id = id2;
    meta2.label = "测试会话 2";
    meta2.providerId = "zhipu";
    meta2.modelName = "glm-4.6";
    // 故意让 meta2 的 createdAt 早于 meta1，验证扫描按 createdAt 升序排
    meta2.createdAt = "2026-10-28T02:00:00Z";
    meta2.lastUsedAt = "2026-10-28T02:30:00Z";
    Check("5a SaveMetaAtomic session2 成功", SessionStore::SaveMetaAtomic(testWs, meta2, err), err);

    const auto scanned = SessionStore::ScanSessionMetas(testWs, err);
    Check("5b 扫描出 2 个会话", scanned.size() == 2, std::to_string(scanned.size()));
    if (scanned.size() == 2) {
        Check("5c 排序第 1 个是早创建的 meta2", scanned[0].id == id2, scanned[0].id);
        Check("5d 排序第 2 个是晚创建的 meta1", scanned[1].id == id1, scanned[1].id);
    }

    // ── 6. DeleteSession 清理四件套 ─────────────────────────────
    // 为 id1 创建 sidecar 与 context 伪文件
    const fs::path uiSidecar = SessionStore::UiStatePath(testWs, id1);
    const fs::path ctxFile = SessionStore::ContextPath(testWs, id1);
    {
        std::ofstream(uiSidecar) << "{}";
        std::ofstream(ctxFile) << "{}";
    }
    Check("6a sidecar 与 context 存在", fs::exists(uiSidecar) && fs::exists(ctxFile));

    Check("6b DeleteSession id1 成功", SessionStore::DeleteSession(testWs, id1, err), err);
    Check("6c journal 已删除", !fs::exists(journalFile));
    Check("6d meta 已删除", !fs::exists(metaFile));
    Check("6e sidecar 已删除", !fs::exists(uiSidecar));
    Check("6f context 已删除", !fs::exists(ctxFile));

    const auto afterDel = SessionStore::ScanSessionMetas(testWs, err);
    Check("6g 删除后扫描只剩 1 个会话", afterDel.size() == 1, std::to_string(afterDel.size()));

    fs::remove_all(testWs, ec);
    std::printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
