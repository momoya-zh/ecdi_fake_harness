// ============================================================
// 转录导出（菜单 / Ctrl+E → 存为 .md）
// ------------------------------------------------------------
// 为什么单独一个文件：导出链是**唯一会弹 Windows 壳对话框**（GetSaveFileNameW）
//   并写用户文件系统的产品路径。它和渲染/tick 无任何交集，却是每次改 UI 时
//   要滚过去的 150 行——独立出来，改导出不必看转录。
// ★ 纪律：导出文本的**行序**是对外契约（用户拿到的 .md 就是他看到的顺序）。
//   `BuildTranscriptExportText` 只读 `m_rows`，不许在这里做二次排序/过滤。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1c）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>   // GetSaveFileNameW（存为对话框）
#ifdef DrawText
#undef DrawText
#endif

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

// 跨 TU 共享符号（定义见 AppInternal.h / PathUtil.cpp）
using harness::internal::PathToUtf8;
using harness::internal::Utf8FromWidePath;
using harness::internal::WideFromUtf8Path;

// 把活动会话的转录渲染成文本。**只读**：不碰模型、不碰协议、不改 seq。
// ★ 导出源是 `timeline`（视图看到的那一份），**不是** `Transcript::m_blocks`。
//   两者不等价：`m_blocks` 只有协议块，而 `timeline` 还含通知行（`[error]` /
//   `[协议不兼容]` / I4 缺口告警）与问句行。导出后者才叫「把屏幕存下来」；
//   导出前者会把「这次运行可不可信」的全部读数默默丢掉。
std::string HarnessApp::BuildTranscriptExportText() const {
    if (!m_sess) return {};
    const std::vector<TimelineEntry>& entries = m_sess->timeline;

    std::string out;
    out.reserve(4096);
    out += "# harness 转录\n";
    out += "# 会话：" + m_sess->label + "\n";
    out += "# 来源：" + m_sess->originKind +
           (m_sess->originPath.empty() ? std::string() : (" " + PathToUtf8(m_sess->originPath))) +
           "\n";
    out += "# 契约：protocol " + std::to_string(m_sess->protocolVer) + "\n";
    out += "# 违规：" + std::to_string(m_sess->transcript.Violations()) +
           "，seq 缺口：" + std::to_string(m_sess->transcript.SeqGaps()) + "\n";
    out += "\n";
    for (const TimelineEntry& e : entries) {
        switch (e.kind) {
        case TimelineEntry::Kind::Block: {
            const Block* block = m_sess->transcript.FindBlock(e.id);
            if (!block) {
                out += "[块 已丢失] " + e.id + "\n\n";
                break;
            }
            out += "[" + block->kind + "]";
            if (!block->name.empty()) out += " " + block->name;
            out += "\n";
            if (!block->text.empty()) out += block->text + "\n";
            if (block->kind == "tool_call" && !block->argsPretty.empty()) {
                out += block->argsPretty + "\n";
            }
            if (!block->summary.empty() && block->summary != block->text) {
                out += "摘要：" + block->summary + "\n";
            }
            out += "\n";
            break;
        }
        case TimelineEntry::Kind::Ask:
            out += "[ask] " + e.text + "\n";
            if (!e.answer.empty()) out += "  答：" + e.answer + "\n";
            out += "\n";
            break;
        case TimelineEntry::Kind::Notice:
            out += e.text + "\n";
            break;
        }
    }
    return out;
}

bool HarnessApp::WriteTranscriptExport(const std::filesystem::path& path, std::string* error) {
    if (!m_sess) return false;
    const std::string out = BuildTranscriptExportText();
    // ★ 二进制模式 + 手动 `\n`：`std::ofstream` 在 Windows 文本模式下会把 `\n`
    //   翻译成 `\r\n`，那会让导出文件与仓库里 `*.jsonl` 的无 BOM+LF 纪律不一致
    //   （虽然导出物不入库，但「同一产品的文件行尾应当一致」）。
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "无法写入文件";
        return false;
    }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    const bool wrote = static_cast<bool>(f);
    f.close();
    if (!wrote && error) *error = "写入中断";
    return wrote;
}

bool HarnessApp::ExportTranscript() {
    if (!m_sess) return false;

    if (m_sess->timeline.empty()) {
        // 空会话不弹对话框：弹了也只能存个空文件，不如在转录里说一声。
        AddNoticeRow("[导出] 当前会话没有内容", kDarkMuted);
        Relayout();
        RequestRepaint();
        return false;
    }

    // 默认文件名：会话 label + 时间戳。用**本地时间到分钟**——两次导出不互相覆盖，
    // 同时文件名仍可读（用 epoch 秒就只剩一串数字了）。
    std::string baseName = m_sess->label.empty() ? std::string("transcript") : m_sess->label;
    // 文件名里不能有路径分隔符/冒号（label 可能是 fixture 路径派生出来的）。
    for (char& c : baseName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string stamp;
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char buf[32]{};
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M", &tmBuf);
        stamp = buf;
    }
    const std::string suggested = baseName + "-" + stamp + ".txt";

    // ★★ 2026-10-21：探针用**定死的目标路径**绕开保存对话框。
    //   理由见 `BuildTranscriptExportText` 头注：`GetSaveFileNameW` 是模态循环，
    //   合成点击走上去会把整个探针挂死。这不是"为了测试改产品逻辑"——
    //   探针走的是**同一个写盘函数**，绕过去的只有"路径从哪儿来"。
    //   ★ 只在 `--menu-probe` 且已经指定路径时生效（默认空 ⇒ 零行为变化）。
    std::filesystem::path target;
    if (!m_menuProbeExportPath.empty()) {
        target = m_menuProbeExportPath;
    } else {
        // 保存对话框。`OFN_OVERWRITEPROMPT` 让覆盖变成用户的选择而不是我们默默做决定。
        wchar_t fileBuf[1024]{};
        {
            const std::wstring wSuggested = WideFromUtf8Path(suggested);
            const size_t n = std::min(wSuggested.size(), static_cast<size_t>(1023));
            std::copy(wSuggested.begin(), wSuggested.begin() + static_cast<std::ptrdiff_t>(n),
                      fileBuf);
        }
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        // ★ `hwndOwner = nullptr`：ECDI 的 `PlatformWindow` **不暴露**原生窗口句柄
        //   （`grep Handle|NativeHandle Platform/PlatformWindow.h` 零命中），所以无法把
        //   对话框挂到主窗口。后果是对话框不模态于主窗口（用户可以在它开着时操作主窗口）
        //   —— 可接受：保存对话框自带自己的模态循环，不会因此错乱。
        //   ★ 不为此去 reflecion/GetActiveWindow()：那是靠推断拿句柄，拿错了会让
        //     对话框挂到别的程序的窗口上。诚实的 `nullptr` 比猜一个句柄安全。
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"\u6587\u672c\u6587\u4ef6 (*.txt)\0*.txt\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.lpstrTitle = L"\u5bfc\u51fa\u8f6c\u5f55";
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) {
            // 用户取消。★ 不写通知行：取消不是事件，往转录里塞「已取消」会把
            //   用户的视图弄脏（而且下次导出会多一行）。
            return false;
        }
        target = Utf8FromWidePath(fileBuf);
    }

    std::string error;
    if (!WriteTranscriptExport(target, &error)) {
        AddNoticeRow("[导出失败] " + error, kErrorDark);
        Relayout();
        RequestRepaint();
        return false;
    }

    // 成功也要**说一声**：保存对话框关掉后屏幕上看不出任何变化，
    //   用户只能去文件管理器里找。通知行是唯一能确认「真的导出了」的反馈。
    AddNoticeRow("[已导出] " + PathToUtf8(target), kDarkMuted);
    Relayout();
    RequestRepaint();
    return true;
}
