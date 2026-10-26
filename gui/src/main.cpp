#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

#ifdef DrawText
#undef DrawText
#endif

#include <shellapi.h>   // CommandLineToArgvW

#include "App.h"
// ★ 2026-10-25（Stage 3 刀①）：显式包 CliOptions，不再靠 App.h 的传递包含——
//   本文件只用到这一个结构体，说明依赖就该写明白。
#include "CliOptions.h"

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
    bool showUsage = false;
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
        } else if (arg == L"--jump-probe") {
            options.jumpProbe = true;       // ★ P4 Phase A：跳到最新按钮显隐+回底判据
        } else if (arg == L"--key-probe") {
            options.keyProbe = true;        // ★ P4 Phase B：全局快捷键判据
        } else if (arg == L"--empty-probe") {
            options.emptyProbe = true;      // ★ P4 Phase C：空状态引导判据
        } else if (arg == L"--model-probe") {
            options.modelProbe = true;      // ★ P4 Phase H：模型浮窗+用量判据
        } else if (arg == L"--selectable-rows") {
            options.selectableRows = true;  // ★ P4 Phase G 尖峰：assistant 行只读 TextBox
        } else if (arg == L"--select-probe") {
            options.selectProbe = true;     // ★ P4 Phase G：选中复制判据
        } else if (arg == L"--delprov-probe") {
            options.delProvProbe = true;    // ★ P4：删除所选不崩判据
        } else if (arg == L"--grow-probe") {
            options.growProbe = true;       // ★ 2026-10-21：输入框多行自动长高
        } else if (arg == L"--async-probe") {
            options.asyncProbe = true;      // ★ REQ-03：投递 vs 轮询的机读 A/B 读数
        } else if (arg == L"--legacy-poll") {
            options.legacyPoll = true;      // ★ REQ-03 对照世界：每 tick 等 8ms（改动前形状）
        } else if (arg == L"--settings-probe") {
            options.settingsProbe = true;   // ★ P3.5：设置面板 + 凭据/配置/env 注入判据
        } else if (arg == L"--config") {
            options.configPath = next();    // ★ P3.5：配置文件路径覆盖（探针/测试隔离）
            AppConfig::SetPathForTesting(options.configPath);
        } else if (arg == L"--settings-url") {
            options.settingsUrl = Utf8FromWide(next());
        } else if (arg == L"--settings-key") {
            options.settingsKey = Utf8FromWide(next());
        } else if (arg == L"--settings-model") {
            options.settingsModel = Utf8FromWide(next());
        } else if (arg == L"--help" || arg == L"-h") {
            showUsage = true;
        }
    }
    if (argv) LocalFree(argv);

    // ★★ 2026-10-26（缺陷：settings 探针的 cleanup 相删配置文件）：
    //   `AppConfig::DefaultPath()` 是"本次进程生效路径"——传了 `--config` 就是隔离路径，
    //   没传就是用户的真实 `%APPDATA%\ecdi-fake-harness\config.json`。cleanup 无条件删它，
    //   于是**一次无隔离的跑批会删掉用户的全部提供商配置**，而 cleanup 相仍报绿
    //   （`std::filesystem::remove` 对不存在的文件不设 error）——仪器把破坏行为报成通过。
    //   修法：不靠调用者记得传隔离参数，直接拒绝启动。exit 2 与 --help 同码，跑批可判；
    //   原因写 stderr（GUI 子系统无控制台，但重定向时句柄有效）。
    if (options.settingsProbe && options.configPath.empty()) {
        static const char kMsg[] =
            "harness_gui: --settings-probe requires --config <isolated-path>\n"
            "  reason: its cleanup phase deletes the configuration file it ran against;\n"
            "  without --config that path is the real %APPDATA%\\ecdi-fake-harness\\config.json,\n"
            "  so an unisolated run would destroy the user's providers.\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), kMsg,
                  static_cast<DWORD>(sizeof(kMsg) - 1), &written, nullptr);
        return 2;
    }

    // ★★ P3.5（规格 §8.5 D7）：无参 = 正常入口——配置可用 ⇒ 直连默认提供商；
    //   否则首启引导（设置面板）。用法文档只挂在 --help 上。
    const bool haveSource = haveReplay || !options.corePath.empty();
    if (!haveSource && !showUsage) {
        options.firstRun = true;
    }
    if (showUsage) {
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
                    L"  --jump-probe                     跳到最新按钮：显隐状态机+点击回底判据\n"
                    L"  --key-probe                      全局快捷键：Ctrl+B/I/,/N + Esc 逐层判据\n"
                    L"  --empty-probe                    空状态引导：回放负对照 + firstRun 正对照\n"
                    L"  --model-probe                    模型浮窗（Ctrl+M）+ v4 分母上屏判据\n"
                    L"  --selectable-rows                转录正文行改只读 TextBox（可选中复制）\n"
                    L"  --select-probe                   选中复制判据（配 --selectable-rows 用）\n"
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
