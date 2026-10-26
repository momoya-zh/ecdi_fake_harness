// ============================================================
// --preview-probe：P4 Phase I（路径可点 + 只读预览）的相判据
// ------------------------------------------------------------
// 判据（写在各相内——「跑过」≠「判过」，门里三分纪律）：
//   regex-cases      PreviewPathFromText 用例组（含「夹具自身有区分力」的元断言：
//                    正例确有命中、负例确未命中）
//   open-ok          workspace 内文件 → 面板开 + 行数/首行逐字节相等
//   escape-rejected  `..` 逃逸 → 拒读（★ IO 纪律核心相）
//   missing          不存在 → notice，不静默
//   no-workspace     workspacePath 空 → 拒读（协议 §3.9：只读 core 确认路径之下）
//   too-large        >512KB → truncated 闸 + 仍有行
//   line-cap         >2000 行 → 行数闸（一次性 wrap O(n^1.5) 的保护闸）
//   close-esc        Esc 关闭（用户拍板加入的交互）
// ★ 无 shell 副作用：本探针不断言「在资源管理器中显示」（用户手测——2026-10-26）。
// ★ 临时目录：temp_directory_path 失败 ⇒ 显式 skipped 相（不是 PASS——跳过必须说出来）。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

using harness::internal::PathToUtf8;

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// >512KB（AppPreview.cpp 的 kPreviewMaxBytes 是它的匿名 ns 常量，这里按同一数重列；
// 两个数若漂移，too-large 相会立刻红——这正是想要的）。
constexpr size_t kProbeTooLargeBytes = 512 * 1024 + 1;

void HarnessApp::RunPreviewProbe() {
    m_probeState.previewProbeDone = true;
    auto& log = m_probeState.previewProbeLog;
    auto snap = [&log](const char* phase, bool matched, std::string detail) {
        PreviewProbeRec rec;
        rec.phase = phase;
        rec.matched = matched;
        rec.detail = std::move(detail);
        log.push_back(std::move(rec));
    };

    // ── regex-cases：判定函数只有一份实现，这里验它的行为契约 ──
    {
        struct Case { const char* text; const char* want; };
        static constexpr Case kCases[] = {
            {"read_file src/main.go", "src/main.go"},
            {"wrote 12 lines to notes.md", "notes.md"},
            {"output.jsonl", "output.jsonl"},
            {"no path here", ""},
            {"UPPER.JSON not matched", ""},      // 大写扩展名不在 §11 起步集
            {"https://example.com/a.md", "example.com/a.md"},  // 已知边界（见 AppPreview.cpp 头注）
        };
        std::string bad;
        for (const Case& c : kCases) {
            const std::string got = PreviewPathFromText(c.text);
            if (got != c.want) {
                if (!bad.empty()) bad += "; ";
                bad += std::string("'") + c.text + "' -> '" + got + "' (want '" + c.want + "')";
            }
        }
        // 元断言：用例组必须有区分力——正例至少命中一个、负例至少挡住一个。
        if (PreviewPathFromText("read_file src/main.go").empty() ||
            !PreviewPathFromText("no path here").empty()) {
            if (!bad.empty()) bad += "; ";
            bad += "meta: case set lost discriminative power";
        }
        snap("regex-cases", bad.empty(), bad);
    }

    // ── 临时工作区：temp_directory_path 失败 ⇒ 显式 skipped（不是绿）──
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) {
        snap("skipped-no-tempdir", false, "temp_directory_path failed: " + ec.message());
        return;
    }
    const fs::path ws = base / "ecdi-preview-probe" / "ws";
    fs::create_directories(ws, ec);
    if (ec) {
        snap("skipped-no-tempdir", false, "create_directories failed: " + ec.message());
        return;
    }
    const auto write = [&ec](const fs::path& p, const std::string& content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
        return !f.fail();
    };
    const std::string sample = "line1\nline2\nline3\n";
    if (!write(ws / "sample.md", sample)) {
        snap("skipped-no-tempdir", false, "failed to write sample.md");
        return;
    }
    write(ws / "big.md", std::string(kProbeTooLargeBytes, 'x') + "\n");
    {
        std::string manyLines;
        manyLines.reserve(2500 * 4);
        for (int i = 0; i < 2500; ++i) manyLines += "ab\n";
        write(ws / "many.md", manyLines);
    }

    // workspacePath 换成探针的临时目录（先例：workspace-probe 直写恢复，ProbeSettings.cpp:709）。
    const std::string wsRestore = m_sess ? m_sess->workspacePath : std::string();
    auto restoreWs = [&]() {
        if (m_sess) m_sess->workspacePath = wsRestore;
    };
    if (m_sess) m_sess->workspacePath = PathToUtf8(ws);

    // ── open-ok ──
    {
        const bool opened = OpenPreview("sample.md");
        std::string bad;
        if (!opened) bad = "OpenPreview returned false";
        if (m_previewError.empty()) {
            if (m_previewLines.size() != 3) {
                bad += bad.empty() ? "" : "; ";
                bad += "lines=" + std::to_string(m_previewLines.size()) + " (want 3)";
            }
            if (m_previewLines.size() == 3 && m_previewLines[0] != "line1") {
                bad += bad.empty() ? "" : "; ";
                bad += "first line mismatch";
            }
        } else {
            bad = "unexpected error: " + m_previewError;
        }
        snap("open-ok", bad.empty(), bad);
    }

    // ── escape-rejected：`..` 逃出 workspace 必须被拒（IO 纪律核心相）──
    {
        const bool opened = OpenPreview("..\\..\\evil.md");
        std::string bad;
        if (opened) bad = "OpenPreview accepted an escaping path";
        if (m_previewError.find("越出") == std::string::npos) {
            bad += bad.empty() ? "" : "; ";
            bad += "error missing escape reason: '" + m_previewError + "'";
        }
        snap("escape-rejected", bad.empty(), bad);
    }

    // ── missing ──
    {
        const bool opened = OpenPreview("nope.md");
        std::string bad;
        if (opened) bad = "OpenPreview accepted a missing file";
        if (m_previewError.find("不存在") == std::string::npos) {
            bad += bad.empty() ? "" : "; ";
            bad += "error missing missing-file reason: '" + m_previewError + "'";
        }
        snap("missing", bad.empty(), bad);
    }

    // ── no-workspace：确认过的路径不在 ⇒ 拒读 ──
    {
        if (m_sess) m_sess->workspacePath.clear();
        const bool opened = OpenPreview("sample.md");
        std::string bad;
        if (opened) bad = "OpenPreview accepted without a workspace";
        if (m_previewError.find("无工作区") == std::string::npos) {
            bad += bad.empty() ? "" : "; ";
            bad += "error missing no-workspace reason: '" + m_previewError + "'";
        }
        if (m_sess) m_sess->workspacePath = PathToUtf8(ws);   // 设回探针工作区（后续相还要用）
        snap("no-workspace", bad.empty(), bad);
    }

    // ── too-large：>512KB ⇒ truncated 闸，且不是空内容 ──
    {
        const bool opened = OpenPreview("big.md");
        std::string bad;
        if (!opened) bad = "OpenPreview returned false";
        if (m_previewError.empty()) {
            if (!m_previewTruncated) bad = "truncation flag not set for >512KB file";
            if (m_previewLines.empty()) {
                bad += bad.empty() ? "" : "; ";
                bad += "truncated file yielded no lines";
            }
        } else {
            bad = "unexpected error: " + m_previewError;
        }
        snap("too-large", bad.empty(), bad);
    }

    // ── line-cap：>2000 行 ⇒ 行数闸恰好 2000 ──
    {
        const bool opened = OpenPreview("many.md");
        std::string bad;
        if (!opened) bad = "OpenPreview returned false";
        if (m_previewError.empty()) {
            if (m_previewLines.size() != 2000) {
                bad = "lines=" + std::to_string(m_previewLines.size()) + " (want 2000)";
            }
            if (!m_previewTruncated) {
                bad += bad.empty() ? "" : "; ";
                bad += "truncation flag not set at line cap";
            }
        } else {
            bad = "unexpected error: " + m_previewError;
        }
        snap("line-cap", bad.empty(), bad);
    }

    // ── close-esc：Esc 必须关闭预览（先开一个合法文件）──
    {
        (void)OpenPreview("sample.md");
        SynthesizeKey(ECDI::KeyCode::Escape, /*shift=*/false);
        snap("close-esc", !m_previewOpen,
             m_previewOpen ? "preview still open after Esc" : "");
    }

    // ── 收尾：关面板 + 恢复 workspace（探针不留痕迹）──
    SetPreviewVisible(false);
    restoreWs();
    std::error_code rmEc;
    fs::remove_all(base / "ecdi-preview-probe", rmEc);   // rmEc 刻意忽略：清理失败不影响判据
}

// 摘要段：previewProbe: [{phase, matched, detail}]（自带转义，作用域同 answerProbe 段纪律）。
// 分隔约定同全仓：**开头无逗号、结尾 "],\n" 带逗号**（基础字段行尾自带逗号）。
void HarnessApp::AppendPreviewProbeSummary(std::string& json) {
    json += "  \"previewProbe\": [";
    auto escP = [](const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char e[8];
                        std::snprintf(e, sizeof(e), "\\u%04x", c);
                        out += e;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    };
    for (size_t i = 0; i < m_probeState.previewProbeLog.size(); ++i) {
        const PreviewProbeRec& r = m_probeState.previewProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"detail\": \"%s\"}",
                      escP(r.phase).c_str(), r.matched ? "true" : "false",
                      escP(r.detail).c_str());
        json += b;
    }
    json += "],\n";
}
