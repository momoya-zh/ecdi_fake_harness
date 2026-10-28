#include "SessionStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

json::Value SessionMeta::ToJson() const {
    json::Value root;
    root.type = json::Value::Type::Object;

    auto putStr = [&root](const char* k, const std::string& sv) {
        json::Value v;
        v.type = json::Value::Type::String;
        v.text = sv;
        root.object[k] = std::move(v);
    };
    auto putNum = [&root](const char* k, double n) {
        json::Value v;
        v.type = json::Value::Type::Number;
        v.number = n;
        root.object[k] = std::move(v);
    };
    auto putBool = [&root](const char* k, bool b) {
        json::Value v;
        v.type = json::Value::Type::Bool;
        v.boolean = b;
        root.object[k] = std::move(v);
    };

    putStr("id", id);
    putStr("label", label);
    putStr("providerId", providerId);
    putStr("modelName", modelName);
    putStr("createdAt", createdAt);
    putStr("lastUsedAt", lastUsedAt);
    putNum("lastSeq", static_cast<double>(lastSeq));
    putBool("archived", archived);
    putNum("sizeBytes", static_cast<double>(sizeBytes));
    if (contextLimit > 0) {
        putNum("contextLimit", static_cast<double>(contextLimit));
    }

    return root;
}

bool SessionMeta::FromJson(const json::Value& val, SessionMeta& out, std::string& error) {
    if (!val.IsObject()) {
        error = "session meta is not a JSON object";
        return false;
    }
    out.id = val.GetString("id");
    if (out.id.empty()) {
        error = "session meta missing required 'id' field";
        return false;
    }
    out.label = val.GetString("label");
    out.providerId = val.GetString("providerId");
    out.modelName = val.GetString("modelName");
    out.createdAt = val.GetString("createdAt");
    out.lastUsedAt = val.GetString("lastUsedAt");
    out.lastSeq = static_cast<long long>(val.GetNumber("lastSeq", 0));
    out.archived = val.GetBool("archived", false);
    out.sizeBytes = static_cast<long long>(val.GetNumber("sizeBytes", 0));
    out.contextLimit = static_cast<long long>(val.GetNumber("contextLimit", 0));
    return true;
}

namespace SessionStore {

std::filesystem::path EcdiDir(const std::filesystem::path& workspacePath) {
    return workspacePath / ".ecdi";
}

std::filesystem::path SessionsDir(const std::filesystem::path& workspacePath) {
    return EcdiDir(workspacePath) / "sessions";
}

std::filesystem::path MemoryDir(const std::filesystem::path& workspacePath) {
    return EcdiDir(workspacePath) / "memory";
}

std::filesystem::path GitignorePath(const std::filesystem::path& workspacePath) {
    return EcdiDir(workspacePath) / ".gitignore";
}

std::filesystem::path JournalPath(const std::filesystem::path& workspacePath, const std::string& sessionId) {
    return SessionsDir(workspacePath) / (sessionId + ".jsonl");
}

std::filesystem::path MetaPath(const std::filesystem::path& workspacePath, const std::string& sessionId) {
    return SessionsDir(workspacePath) / (sessionId + ".meta.json");
}

std::filesystem::path UiStatePath(const std::filesystem::path& workspacePath, const std::string& sessionId) {
    return SessionsDir(workspacePath) / (sessionId + ".jsonl.ui.json");
}

std::filesystem::path ContextPath(const std::filesystem::path& workspacePath, const std::string& sessionId) {
    return SessionsDir(workspacePath) / (sessionId + ".context.jsonl");
}

bool EnsureEcdiDir(const std::filesystem::path& workspacePath, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path sDir = SessionsDir(workspacePath);
    fs::create_directories(sDir, ec);
    if (ec) {
        error = "failed to create sessions dir: " + ec.message();
        return false;
    }

    const fs::path mDir = MemoryDir(workspacePath);
    fs::create_directories(mDir, ec);
    if (ec) {
        error = "failed to create memory dir: " + ec.message();
        return false;
    }

    const fs::path giPath = GitignorePath(workspacePath);
    if (!fs::exists(giPath, ec)) {
        std::ofstream out(giPath, std::ios::binary | std::ios::trunc);
        if (out) {
            out << "*\n";
        }
    }
    return true;
}

std::string CurrentUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    gmtime_s(&tmBuf, &t);
#else
    gmtime_r(&t, &tmBuf);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
    return buf;
}

std::string GenerateSessionId() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    gmtime_s(&tmBuf, &t);
#else
    gmtime_r(&t, &tmBuf);
#endif
    char timeStr[32]{};
    // 紧凑 UTC 格式：YYYYMMDD-HHMMSS
    std::strftime(timeStr, sizeof(timeStr), "%Y%m%d-%H%M%S", &tmBuf);

    static std::atomic<uint32_t> s_counter{0};
    const uint32_t c = ++s_counter;
    const uint32_t randVal = static_cast<uint32_t>(
        (std::chrono::steady_clock::now().time_since_epoch().count() ^ (c * 2654435761u)) & 0xFFFF);
    char hexBuf[8]{};
    std::snprintf(hexBuf, sizeof(hexBuf), "%04x", randVal);

    return "s-" + std::string(timeStr) + "-" + std::string(hexBuf);
}

bool SaveMetaAtomic(const std::filesystem::path& workspacePath, const SessionMeta& meta, std::string& error) {
    namespace fs = std::filesystem;
    if (meta.id.empty()) {
        error = "cannot save meta with empty session id";
        return false;
    }
    if (!EnsureEcdiDir(workspacePath, error)) {
        return false;
    }

    const fs::path dest = MetaPath(workspacePath, meta.id);
    const fs::path tmp = dest.wstring() + L".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot open tmp meta file for write: " + WideToUtf8(tmp.wstring());
            return false;
        }
        out << meta.ToJson().Pretty(2) << "\n";
        out.flush();
        if (!out) {
            error = "flush failed writing tmp meta file: " + WideToUtf8(tmp.wstring());
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD errCode = GetLastError();
        error = "MoveFileExW failed (" + std::to_string(errCode) + ") replacing " + WideToUtf8(dest.wstring());
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }
#else
    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec) {
        error = "fs::rename failed (" + ec.message() + ") replacing " + dest.string();
        fs::remove(tmp, ec);
        return false;
    }
#endif
    return true;
}

bool LoadMeta(const std::filesystem::path& metaPath, SessionMeta& meta, std::string& error) {
    std::ifstream in(metaPath, std::ios::binary);
    if (!in) {
        error = "cannot open meta file: " + WideToUtf8(metaPath.wstring());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    json::Value root;
    std::string parseErr;
    if (!json::Parse(text, root, parseErr)) {
        error = "failed to parse meta JSON: " + parseErr;
        return false;
    }
    return SessionMeta::FromJson(root, meta, error);
}

bool DeleteSession(const std::filesystem::path& workspacePath, const std::string& sessionId, std::string& error) {
    if (sessionId.empty()) return true;
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::remove(JournalPath(workspacePath, sessionId), ec);
    fs::remove(MetaPath(workspacePath, sessionId), ec);
    fs::remove(UiStatePath(workspacePath, sessionId), ec);
    fs::remove(ContextPath(workspacePath, sessionId), ec);
    fs::remove(SessionsDir(workspacePath) / (sessionId + ".context.json"), ec);
    return true;
}

std::vector<SessionMeta> ScanSessionMetas(const std::filesystem::path& workspacePath, std::string& error) {
    namespace fs = std::filesystem;
    std::vector<SessionMeta> out;
    const fs::path sDir = SessionsDir(workspacePath);

    std::error_code ec;
    if (!fs::is_directory(sDir, ec) || ec) {
        return out;
    }

    for (fs::directory_iterator it(sDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        const std::wstring fn = p.filename().wstring();
        if (fn.size() < 10 || fn.substr(fn.size() - 10) != L".meta.json") {
            continue;
        }

        SessionMeta meta;
        std::string err;
        if (LoadMeta(p, meta, err)) {
            out.push_back(std::move(meta));
        }
    }

    // ★ 按 (createdAt, id) 升序钉死排序，与启动恢复及测试断言对齐
    std::sort(out.begin(), out.end(), [](const SessionMeta& a, const SessionMeta& b) {
        if (a.createdAt != b.createdAt) return a.createdAt < b.createdAt;
        return a.id < b.id;
    });

    return out;
}

// ── JournalWriter ──────────────────────────────────────────

JournalWriter::~JournalWriter() {
    Close();
}

bool JournalWriter::Open(const std::filesystem::path& journalPath, std::string& error) {
    namespace fs = std::filesystem;
    Close();
    m_path = journalPath;

    std::error_code ec;
    fs::create_directories(journalPath.parent_path(), ec);
    if (ec) {
        error = "cannot create directory for journal: " + ec.message();
        return false;
    }

    m_file.open(journalPath, std::ios::binary | std::ios::app);
    if (!m_file) {
        error = "cannot open journal file for append: " + WideToUtf8(journalPath.wstring());
        return false;
    }

    const auto sz = fs::file_size(journalPath, ec);
    m_bytesWritten = ec ? 0 : sz;
    return true;
}

bool JournalWriter::AppendLine(const std::string& rawLine, std::string& error) {
    if (!m_file.is_open()) {
        error = "journal writer is not open";
        return false;
    }
    m_file.write(rawLine.data(), rawLine.size());
    m_bytesWritten += rawLine.size();

    if (rawLine.empty() || rawLine.back() != '\n') {
        m_file.put('\n');
        m_bytesWritten += 1;
    }
    m_file.flush();
    if (!m_file) {
        error = "journal flush failed on write";
        return false;
    }
    return true;
}

void JournalWriter::Flush() {
    if (m_file.is_open()) {
        m_file.flush();
    }
}

void JournalWriter::Close() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

bool JournalWriter::IsOpen() const {
    return m_file.is_open();
}

uint64_t JournalWriter::BytesWritten() const {
    return m_bytesWritten;
}

const std::filesystem::path& JournalWriter::Path() const {
    return m_path;
}

}  // namespace SessionStore
