// ============================================================
// 文件导入实现与探针（富内容 §C）
// ------------------------------------------------------------
// 为什么单独一个文件：文件导入与 AppExport（导出转录）呈镜像对称：
//   - 外部文件复制进工作区受控目录 `<工作区>/.ecdi/imports/`；
//   - 协议与 core 零改动，转录与 context 记录干净的相对路径；
//   - 探针通过注入 `importProbeFilePath` 绕开 Win32 原生模态对话框
//     （GetOpenFileNameW），保证自动化测试不被挂死。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "SessionStore.h"
#include "ThemeUsing.h"
#include "UiKit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>   // GetOpenFileNameW

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using harness::internal::WideFromUtf8Path;
using harness::internal::Utf8FromWidePath;
using harness::internal::PathToUtf8;
using harness::internal::JobHumanBytes;

void HarnessApp::HandleImportFile() {
    // 1. 守卫：未绑定工作区禁止导入
    if (!m_sess || m_sess->workspacePath.empty()) {
        AddNoticeRow("[导入失败] 当前会话未绑定有效工作区", kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }

    // 2. 获取源文件路径（探针注入或弹出原生打开对话框）
    fs::path srcPath;
    if (!m_probeState.importProbeFilePath.empty()) {
        srcPath = m_probeState.importProbeFilePath;
    } else {
        wchar_t fileBuf[1024]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.lpstrTitle = L"导入文件到工作区";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) {
            // 用户取消：静默返回，不污染转录视图
            return;
        }
        srcPath = Utf8FromWidePath(fileBuf);
    }

    // 3. 校验源文件存在性与体积上限
    std::error_code ec;
    if (!fs::exists(srcPath, ec) || !fs::is_regular_file(srcPath, ec)) {
        AddNoticeRow("[导入失败] 指定的文件不存在或不是常规文件", kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }

    const auto sz = fs::file_size(srcPath, ec);
    constexpr uintmax_t kMaxImportBytes = 10 * 1024 * 1024; // 10MB
    if (sz > kMaxImportBytes) {
        AddNoticeRow("[导入失败] 文件体积超过 10MB 上限: " + JobHumanBytes(static_cast<long long>(sz)), kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }

    // 4. 准备目标落点：<工作区>/.ecdi/imports/
    std::string errStr;
    if (!SessionStore::EnsureEcdiDir(m_sess->workspacePath, errStr)) {
        AddNoticeRow("[导入失败] 初始化 .ecdi 目录失败: " + errStr, kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }
    const fs::path importsDir = SessionStore::EcdiDir(m_sess->workspacePath) / "imports";
    fs::create_directories(importsDir, ec);

    // 5. 生成目标文件名（加时间戳防覆盖）并复制
    const std::string stem = srcPath.stem().string();
    const std::string ext = srcPath.extension().string();
    std::string stamp;
    {
        std::time_t now = std::time(nullptr);
        std::tm tmBuf{};
#if defined(_WIN32)
        localtime_s(&tmBuf, &now);
#else
        localtime_r(&now, &tmBuf);
#endif
        char buf[32]{};
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmBuf);
        stamp = buf;
    }
    const std::string destName = stem + "-" + stamp + ext;
    const fs::path destPath = importsDir / destName;

    fs::copy_file(srcPath, destPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        AddNoticeRow("[导入失败] 复制到工作区失败: " + ec.message(), kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }

    // 6. 装配相对路径（规范化使用正斜杠）并呈现胶囊
    const std::string relPath = ".ecdi/imports/" + destName;
    const std::string displayName = srcPath.filename().string();

    if (m_promptBar) {
        m_promptBar->SetAttachment(displayName, relPath, static_cast<long long>(sz));
    }
    AddNoticeRow("[已暂存附件] " + displayName + " (" + JobHumanBytes(static_cast<long long>(sz)) + ")", kDarkMuted);
    Relayout();
    RequestRepaint();
}

// ── 文件导入探针（--import-probe）────────────────────────────
void HarnessApp::RunImportProbe() {
    if (m_probeState.importProbeDone) return;
    m_probeState.importProbeDone = true;

    auto add = [this](const std::string& phase, bool matched, const std::string& note) {
        m_probeState.importProbeLog.push_back(MdProbeRec{phase, matched, note});
    };

    // 准备临时测试目录与工作区
    std::error_code ec;
    const fs::path tempDir = fs::temp_directory_path(ec) / "ecdi_import_probe";
    fs::create_directories(tempDir, ec);

    const fs::path sampleFile = tempDir / "sample_test_data.txt";
    {
        std::ofstream ofs(sampleFile, std::ios::binary);
        ofs << "hello ecdi import probe content\nsecond line\n";
    }

    const fs::path oversizeFile = tempDir / "oversize_test_data.bin";
    {
        std::ofstream ofs(oversizeFile, std::ios::binary);
        // 写入 10MB + 1 字节
        const std::string chunk(1024 * 1024, 'A');
        for (int i = 0; i < 10; ++i) {
            ofs.write(chunk.data(), chunk.size());
        }
        ofs.put('B');
    }

    // 备份原有会话与 PromptBar 状态
    const std::string origWs = m_sess ? m_sess->workspacePath : std::string();
    const int origBarH = m_promptBar ? m_promptBar->GetHeight() : 0;

    // 1. import-stage：正常暂存测试
    {
        if (m_sess) m_sess->workspacePath = tempDir.string();
        m_probeState.importProbeFilePath = sampleFile;

        HandleImportFile();

        bool stagedOk = false;
        std::string relPath;
        if (m_promptBar && m_promptBar->HasAttachment()) {
            const auto& att = m_promptBar->GetAttachment();
            if (att.has_value()) {
                stagedOk = (att->displayName == "sample_test_data.txt" && att->fileSizeBytes > 0);
                relPath = att->relativePath;
            }
        }
        const bool chipVisible = m_promptBar && m_promptBar->chipPanel() && m_promptBar->chipPanel()->IsVisible();
        const bool grew = m_promptBar && (m_promptBar->GetHeight() > origBarH);

        // 验证物理文件确实拷贝至 <ws>/.ecdi/imports/
        bool fileExists = false;
        if (!relPath.empty()) {
            const fs::path targetPhysics = tempDir / relPath;
            fileExists = fs::exists(targetPhysics, ec);
        }

        const bool ok = stagedOk && chipVisible && grew && fileExists;
        add("import-stage", ok, "stagedOk=" + std::to_string(stagedOk) + " grew=" + std::to_string(grew) +
            " fileExists=" + std::to_string(fileExists));
    }

    // 2. import-remove：点击 ✕ 移除暂存
    {
        if (m_promptBar) {
            m_promptBar->ClearAttachment();
        }
        const bool hasAtt = m_promptBar && m_promptBar->HasAttachment();
        const bool chipHidden = m_promptBar && m_promptBar->chipPanel() && !m_promptBar->chipPanel()->IsVisible();
        const bool restoredH = m_promptBar && (m_promptBar->GetHeight() == origBarH);

        const bool ok = !hasAtt && chipHidden && restoredH;
        add("import-remove", ok, "hasAtt=" + std::to_string(hasAtt) + " restoredH=" + std::to_string(restoredH));
    }

    // 3. import-submit：带文字提交自动附加相对路径，并自动清空附件胶囊
    {
        // 重新暂存
        m_probeState.importProbeFilePath = sampleFile;
        HandleImportFile();

        std::string sentPrompt;
        if (m_promptBar) {
            m_promptBar->SetOnSend([&sentPrompt](const std::string& p) {
                sentPrompt = p;
            });
            if (m_promptBar->input()) {
                m_promptBar->input()->SetText("请分析这个文件");
            }
            m_promptBar->Submit();
        }

        const bool promptHasText = sentPrompt.find("请分析这个文件") != std::string::npos;
        const bool promptHasAtt = sentPrompt.find("[附件: .ecdi/imports/sample_test_data-") != std::string::npos;
        const bool cleared = m_promptBar && !m_promptBar->HasAttachment();
        const bool chipHidden = m_promptBar && m_promptBar->chipPanel() && !m_promptBar->chipPanel()->IsVisible();

        const bool ok = promptHasText && promptHasAtt && cleared && chipHidden;
        add("import-submit", ok, "prompt='" + sentPrompt + "' cleared=" + std::to_string(cleared));
    }

    // 4. import-oversize：超过 10MB 拒绝导入，不改变胶囊
    {
        m_probeState.importProbeFilePath = oversizeFile;
        HandleImportFile();

        const bool hasAtt = m_promptBar && m_promptBar->HasAttachment();
        const bool ok = !hasAtt;
        add("import-oversize", ok, "hasAtt=" + std::to_string(hasAtt));
    }

    // 5. import-no-workspace：无工作区拒绝导入
    {
        if (m_sess) m_sess->workspacePath = "";
        m_probeState.importProbeFilePath = sampleFile;
        HandleImportFile();

        const bool hasAtt = m_promptBar && m_promptBar->HasAttachment();
        const bool ok = !hasAtt;
        add("import-no-workspace", ok, "hasAtt=" + std::to_string(hasAtt));
    }

    // 恢复状态并清理临时目录
    if (m_sess) m_sess->workspacePath = origWs;
    m_probeState.importProbeFilePath.clear();
    fs::remove_all(tempDir, ec);

    if (m_application) m_application->Exit();
}

void HarnessApp::AppendImportProbeSummary(std::string& json) {
    auto esc = [](const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += static_cast<char>(c);
        }
        return out;
    };
    json += "  \"importProbe\": [";
    for (size_t i = 0; i < m_probeState.importProbeLog.size(); ++i) {
        const MdProbeRec& r = m_probeState.importProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.matched ? "true" : "false", esc(r.note).c_str());
        json += b;
    }
    json += "],\n";
}
