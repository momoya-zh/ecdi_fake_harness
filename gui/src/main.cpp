#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

#ifdef DrawText
#undef DrawText
#endif

#include <shellapi.h>   // CommandLineToArgvW

#include "App.h"

#include <filesystem>
#include <string>

namespace {

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

}   // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine,
                    int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    int argc = 0;
    LPWSTR* const argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    CliOptions options;
    bool haveReplay = false;
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring { return (i + 1 < argc) ? argv[++i] : L""; };
        if (arg == L"--replay") {
            options.replayPath = next();
            haveReplay = true;
        } else if (arg == L"--timed") {
            options.timed = true;
        } else if (arg == L"--instant") {
            options.timed = false;
        } else if (arg == L"--row-mode") {
            options.rowModeList = (next() == L"listlayout");
        } else if (arg == L"--out") {
            options.outPath = next();
        } else if (arg == L"--ecdi-commit") {
            options.ecdiCommit = Utf8FromWide(next());
        } else if (arg == L"--auto-answer") {
            options.autoAnswer = Utf8FromWide(next());
        } else if (arg == L"--auto-close") {
            options.autoCloseMs = _wtoi(next().c_str());
        } else if (arg == L"--gl") {
            options.useGl = true;   // 整词开关（与 ModelProbe 同款）
        } else if (arg == L"--theme") {
            options.lightTheme = (next() == L"light");   // 2026-10-18 v7：浅色令牌
        } else if (arg == L"--backend") {
            options.useGl = (next() == L"gl");
        } else if (arg == L"--cull-probe") {
            options.cullProbe = true;   // Phase 27：包 PaintProbe 统计每帧命令数
        } else if (arg == L"--scroll-sweep") {
            options.scrollSweepStep = _wtoi(next().c_str());
        } else if (arg == L"--core") {
            options.corePath = next();       // P1.5：拉起子进程走真管道
        } else if (arg == L"--core-args") {
            options.coreArgs = Utf8FromWide(next());
        } else if (arg == L"--auto-prompt") {
            options.autoPrompt = Utf8FromWide(next());
        } else if (arg == L"--probe-contrast") {
            options.probeContrast = true;   // ★ 探针专用：白底黑字（仅测量，非交付外观）
        } else if (arg == L"--state-probe") {
            options.stateProbe = true;      // ★ REQ-04：回读发送/停止按钮使能+标题到摘要
        } else if (arg == L"--hit-probe") {
            options.hitProbe = true;        // ★ REQ-04 边界：命中门控受控对照（无注入）
        } else if (arg == L"--reasoning-probe") {
            options.reasoningProbe = true;  // ★ 协议 v2 §7：思维链折叠行为取证
        } else if (arg == L"--order-probe") {
            options.orderProbe = true;      // ★ Step 0a：回读块到达顺序（字典序陷阱取证）
        } else if (arg == L"--rebuild-probe") {
            options.rebuildProbe = true;    // ★ Step 0c：照模型重建视图并逐行比对
        } else if (arg == L"--session-probe") {
            options.sessionProbe = true;    // ★ Step 0d：新建/切换会话取证
        } else if (arg == L"--sidebar-probe") {
            options.sidebarProbe = true;    // ★ Step 1：侧栏列表 A==B + 删除取证
        } else if (arg == L"--answer-probe") {
            options.answerProbe = true;     // ★ Step 3：ask 的答案内容进模型取证
        } else if (arg == L"--theme-probe") {
            options.themeProbe = true;      // ★ Step 4：整树重建 + 运行期主题切换取证
        } else if (arg == L"--history-probe") {
            options.historyProbe = true;    // ★ Step 5：打开历史会话（回放 + 跳到末尾 + sidecar）
        } else if (arg == L"--workspace-probe") {
            options.workspaceProbe = true;  // v3：workspace ack 与 optimistic prompt 判据
        } else if (arg == L"--menu-probe") {
            options.menuProbe = true;       // ★ 2026-10-21：中栏头导出/菜单判据
        } else if (arg == L"--resize-probe") {
            options.resizeProbe = true;     // ★ 2026-10-21：窗口拉伸的布局判据
        } else if (arg == L"--enter-probe") {
            options.enterProbe = true;      // ★ 2026-10-21：Enter 发送 / Shift+Enter 换行
        } else if (arg == L"--hud-probe") {
            options.hudProbe = true;        // ★ 2026-10-21：HUD 指针"读了但没建"判据
        } else if (arg == L"--grow-probe") {
            options.growProbe = true;       // ★ 2026-10-21：输入框多行自动长高
        } else if (arg == L"--async-probe") {
            options.asyncProbe = true;      // ★ REQ-03：投递 vs 轮询的机读 A/B 读数
        } else if (arg == L"--legacy-poll") {
            options.legacyPoll = true;      // ★ REQ-03 对照世界：每 tick 等 8ms（改动前形状）
        }
    }
    if (argv) LocalFree(argv);

    const bool haveSource = haveReplay || !options.corePath.empty();
    if (!haveSource) {
        MessageBoxW(nullptr,
                    L"用法: harness_gui (--replay <file.jsonl> | --core <exe>)\n"
                    L"  --instant|--timed                回放模式（--replay 用）\n"
                    L"  --core <exe>                     拉起假 core 走真子进程（P1.5）\n"
                    L"  --core-args \"<args>\"             透传给假 core\n"
                    L"  --row-mode autosize|listlayout   行布局 A/B 对照（默认 autosize）\n"
                    L"  --out <eval.json>                退出时输出评估摘要\n"
                    L"  --ecdi-commit <hash>             记录被评估的 ECDI 提交\n"
                    L"  --auto-answer allow|deny         无人值守自动回答 ask\n"
                    L"  --backend gdi|gl                 渲染后端 A/B（默认 gdi）\n"
                    L"  --theme dark|light               配色令牌（默认 light）\n"
                    L"  --auto-close <ms>                定时自动退出（跑批/冒烟）\n"
                    L"  --cull-probe                     探针套件：命令分类/执行帧时/测量成本\n"
                    L"  --state-probe                    回读发送/停止按钮使能+标题（REQ-04 机读证据）\n"
                    L"  --hit-probe                      REQ-04 边界：命中门控受控对照（无注入）\n"
                    L"  --reasoning-probe                协议 v2 §7：思维链折叠行为取证\n"
                    L"  --order-probe                    Step 0a：回读块到达顺序（字典序陷阱取证）\n"
                    L"  --rebuild-probe                  Step 0c：照模型重建视图并逐行比对（含负对照）\n"
                    L"  --sidebar-probe                  Step 1：侧栏会话列表 A==B + 删除判据\n"
                    L"  --answer-probe                   Step 3：ask 的答案内容进模型/上屏判据\n"
                    L"  --theme-probe                    Step 4：整树重建 + 运行期主题切换判据\n"
                    L"  --history-probe                  Step 5：打开历史会话（列表/跳到末尾/sidecar）判据\n"
                    L"  --workspace-probe                协议 v3：workspace 与 prompt client_id 判据\n"
                    L"  --menu-probe                     中栏头 `↗` 导出 / `···` 菜单判据\n"
                    L"  --resize-probe                   窗口拉伸：行宽/标题栏/无横滚条判据\n"
                    L"  --enter-probe                    Enter 发送 / Shift+Enter 换行判据\n"
                    L"  --hud-probe                      HUD 指针“读了但没建”判据\n"
                    L"  --grow-probe                     输入框多行自动长高判据\n"
                    L"  --async-probe                    REQ-03：PostToUi 投递 vs 8ms 轮询的读数\n"
                    L"  --legacy-poll                    REQ-03 对照世界：恢复每 tick 等 8ms 的旧路径\n"
                    L"  --scroll-sweep <px>              回放加载完后从顶扫到底，扫完退出",
                    L"harness_gui", MB_ICONINFORMATION);
        return 2;
    }
    if (haveReplay) {
        options.fixtureName = options.replayPath.filename().generic_string();
    }

    HarnessApp app(std::move(options));
    return app.Run();
}
