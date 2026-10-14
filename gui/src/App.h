#pragma once
// 应用组装（规格 §3）：三栏工作台（低保真原型变体 A）。
// 顶栏 HUD · 左栏（会话/回放进度）· 中栏转录 · 右栏检查器（协议健康/评估读数/用量）
// · Composer · 状态栏；~30Hz 定时器拉取回放行、合并 delta；退出时输出评估摘要（§6.3）。

#include "ChildSource.h"
#include "CountingPanel.h"
#include "MeasureProbe.h"
#include "PaintProbe.h"
#include "PromptBar.h"
#include "ProtocolWriter.h"
#include "ReplaySource.h"
#include "Transcript.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/ScrollView.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct CliOptions {
    std::filesystem::path replayPath;   // --replay（必填）
    bool timed = false;                 // --timed（默认 --instant）
    bool rowModeList = false;           // --row-mode listlayout（默认 autosize）
    std::filesystem::path outPath;      // --out <json>
    std::string ecdiCommit;             // --ecdi-commit <hash>
    std::string autoAnswer;             // --auto-answer allow|deny（无人值守跳过等待）
    int autoCloseMs = 0;                // --auto-close <ms>（跑批/冒烟自动退出；0 = 关闭）
    bool useGl = false;                 // --backend gl（Phase 26 批二：GL 后端 A/B；默认 GDI）
    bool cullProbe = false;               // --cull-probe：包 PaintProbe 计数每帧到达后端的命令
    int scrollSweepStep = 0;              // --scroll-sweep <px>：加载完后从顶扫到底（0 = 关）
    std::filesystem::path corePath;       // --core <exe>：拉起假 core 走真子进程（P1.5）
    std::string coreArgs;                 // --core-args "<args>"：透传给假 core
    std::string autoPrompt;               // --auto-prompt <text>：启动后自动发一次 prompt（跑批）
    std::string fixtureName;              // 摘要用：replayPath 文件名
    // ★ 探针专用（2026-10-08）：高对比配色——转录画布纯白 / 文本纯黑 / 气泡无描边。
    //   动机：默认暗色主题下，像素探针要与卡片圆角、描边、滚动条抢阈值，
    //   反复产生「0 行」「跨度到 899」这类**仪器噪声**（多次误判的根因）。
    //   ★ 默认 false ⇒ 任何既有路径**零行为变化**（逐位等价）；仅探针跑批开启。
    bool probeContrast = false;           // --probe-contrast：白底黑字（仅测量用，非交付外观）
    // ★★ REQ-04 直接回读探针（2026-10-14）：把「发送/停止按钮的使能 + 标题文本」
    //   逐次记入摘要——**取代截图**，让协议 §6「按钮只由 state 驱动」变成机器可读。
    //   ★ 为何必须新开通道：`Widget::SetEnabled` **故意不内建 Invalidate**
    //     （`Widget.h:185-187`——IsEnabled() 无任何绘制消费者）⇒ 使能翻转
    //     在 invalidations/paints 里**不可见**；且 `m_invalidations` 只数应用侧
    //     `RequestRepaint()`。实测 A/B/C 对照组（2026-10-14）证明该间接通道
    //     **判不出** state 驱动（B 与 C 的 inval 均为 25，B-C=+0）。
    //   ★ 默认 false ⇒ 零行为变化（只在 RefreshHud 里多几次只读查询）。
    bool stateProbe = false;              // --state-probe：回读按钮使能/标题到摘要
    // ★★ REQ-04 **边界**探针（2026-10-14）：`--state-probe` 只证明了「使能位被置对」，
    //   而 REQ-04 的边界是「**禁用后点击不达**」——使能位正确 ≠ 命中门控生效。
    //   本开关在**同一个点**上做受控对照（同几何、只改 enabled）：
    //     · as-is           —— 落定后的真实状态（正对照：enabled 时应命中并触发点击）
    //     · off-point       —— 同状态、点移到按钮**外侧**（负对照：位置门控）
    //     · forced-enabled  —— 显式置使能
    //     · forced-disabled —— 显式置禁用（判据：`HitTest` 不再返回该按钮 ∧ 点击不达）
    //   ★ 观测通道 = `Widget::HitTest` 返回值 + `SendPrompt` 被进入的次数（不靠像素/注入）。
    //   ★ 默认 false ⇒ 不跑探针。★ 开启时的**已实测副作用**（条 57③：自己的仪器会污染实验）：
    //     合成点击走真实派发链 ⇒ `Application::OnMouseButtonDown` 会 `SetFocusedWidget`
    //     （`Button::CanFocus()==true`）⇒ 输入框失焦 ⇒ `TextBox::OnFocusLost` 停掉
    //     caret 闪烁定时器。本探针在结束时**已复原焦点**，但定时器随之重启 ⇒
    //     `paints`/`frames` 有 **±1~2 的相位扰动**（实测 cot 夹具 0/0/−2/−1，随跑批时长变）。
    //     ⇒ 结论：**探针自身读数（hitProbe）不受影响**；但**不要**拿「探针开」的
    //     paints/frames 与「探针关」的做性能对比。
    bool hitProbe = false;                // --hit-probe：命中门控回读（REQ-04 边界）
};

class HarnessApp;   // 前置（HarnessApplication 转发用）

// Application 子类：只把本应用的三个定时器路由进 HarnessApp，
// 其余（光标闪烁 / 动画 tick）转发基类——与 ECDI ModelProbe 同款先例。
class HarnessApplication : public ECDI::Application {
public:
    void SetOwner(HarnessApp* owner) { m_owner = owner; }

protected:
    void OnTimer(const ECDI::TimerEvent& event) override;

private:
    HarnessApp* m_owner = nullptr;
};

class HarnessApp {
public:
    explicit HarnessApp(CliOptions options);
    ~HarnessApp();

    int Run();   // 进入消息循环；退出后写评估摘要

    // HarnessApplication 回调
    void OnReplayTick();
    void OnAutoClose();
    void OnSweepTick();
    void SendPrompt(const std::string& text);     // P1.5：发送路径（按钮与自动发共用）

    static constexpr int kReplayTimer = 100;   // ~30Hz（33ms）
    static constexpr int kCloseTimer = 101;    // --auto-close
    static constexpr int kSweepTimer = 102;   // --scroll-sweep 步进

private:
    // 顶栏 / 侧栏 / 检查器 / 状态栏里的动态文本（非拥有——Widget 树所有）
    struct Hud {
        ECDI::Label* title = nullptr;        // 顶栏：harness — fixture
        ECDI::Label* rowChip = nullptr;      // 顶栏：autosize | listlayout
        ECDI::Label* backendChip = nullptr;  // 顶栏：gdi ▾ / gl ▾
        ECDI::Label* sessChip = nullptr;     // 顶栏：会话 id chip
        ECDI::Panel* statusDot = nullptr;    // 顶栏状态圆点（ok/dim 随 state 翻色）
        ECDI::Panel* sbDot = nullptr;        // 状态栏圆点
        ECDI::Label* stateChip = nullptr;    // 顶栏：running / idle
        ECDI::Label* sessSession = nullptr;  // 左栏：活动会话 id
        ECDI::Label* sessCore = nullptr;     // 左栏：会话行 meta（core 版本）
        ECDI::Label* sessProgress = nullptr; // 左栏：回放进度 n/N
        ECDI::Label* healthSeq = nullptr;    // 右栏：seq 连续性（I4）
        ECDI::Label* healthViol = nullptr;   // 右栏：违规 · 未知
        ECDI::Label* healthKind = nullptr;   // ★ 右栏：未知 kind 计数（2026-10-13，协议空白）
        ECDI::Label* evalPaints = nullptr;   // 右栏：重绘读数
        ECDI::Label* probeCmds = nullptr;    // 右栏：命令/帧 avg
        ECDI::Label* probeExec = nullptr;    // 右栏：执行 avg us
        ECDI::Label* usageInV = nullptr;     // 右栏：输入 tokens 值
        ECDI::Label* usageOutV = nullptr;    // 右栏：输出 tokens 值
        ECDI::Label* histChip[3] = {nullptr, nullptr, nullptr};  // 权限历史 allow/deny
        ECDI::Label* histWhat[3] = {nullptr, nullptr, nullptr};
        ECDI::Label* stState = nullptr;      // 状态栏：state
        ECDI::Label* stMsgs = nullptr;       // 状态栏：N msg
        ECDI::Label* stTokens = nullptr;     // 状态栏：tokens
        ECDI::Label* stPaint = nullptr;      // 状态栏：paints · 上次帧时
    };

    void BuildUi(ECDI::Window& window);
    void BuildTopBar(ECDI::Widget& root);
    void BuildMainArea(ECDI::Widget& root);
    void BuildBottom(ECDI::Widget& root);
    void DrainInstant();
    void DrainTimed();
    void HandleLine(const std::string& line);
    void RouteLastMessage();

    // 行记录：m_rows 用 deque（push_back 不失效指针——回调捕获依赖这一点）
    struct RowRec {
        ECDI::Widget* panel = nullptr;            // 行根（Panel），非拥有
        ECDI::Label* label = nullptr;             // 主文本；tool 行 = 展开体文本，非拥有
        ECDI::Button* headerBtn = nullptr;        // tool 卡片标题按钮，非拥有
        ECDI::CollapsiblePanel* body = nullptr;   // tool 卡片展开体，非拥有
        ECDI::Button* allowBtn = nullptr;         // ask 行按钮，非拥有
        ECDI::Button* denyBtn = nullptr;
        bool tool = false;
        bool expanded = false;
        bool wrapped = false;                     // Phase 29：气泡文本是否已开断行（防 SetWordWrap 抖动）
        int lastWrapW = 0;                        // 上次下发的 wrap 宽度（宽度不变不 SetSize——保住 C29-10 布局缓存）
        bool alignRight = false;                  // user 行右对齐
        bool ask = false;
        bool answered = false;
        std::string askId;                        // P1.5：协议 ask 的 id（answer 回执用）
        int bodyH = 0;                            // 展开体高度缓存（autosize 布局用）
        // ★ A-2 曾拟在此加 `bodyHDirty`（按需重测），**已复核否决**（2026-10-13 晚）：
        //   把 `chars` 分解为 `a(一次性) + b(每帧)·frames` 后实测 ⇒ b 与文本长度**无关**
        //   （body 4000 与 40 的 b 分别为 504 / 502，比 1.00×）⇒ 折叠态**没有**逐帧
        //   重测长文本；长文本的成本是**一次性**的布局建立（C29-10 缓存按文档生效）。
        //   故 `Relayout` 保持无条件 `AutoSize()`。取证：`.scratch/probe_a2_decompose.py`。
        int lastBodyW = 0;                        // 上次下发给 body 的基准尺寸——
        int lastBodyH = 0;                        // 未变则不重发（动画期间重入会钉死动画）
    };

    void AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                const std::string& blockId);
    void CreateBlockRow(const Block& block);
    void CreateAskRow(const json::Value& msg);
    void MarkAnswered(RowRec& rec, const std::string& choice);
    void AddNoticeRow(const std::string& text, const ECDI::Color& color);
    void Relayout();
    void FollowTailIfPinned();
    void RequestRepaint();
    void CacheEvalMetrics();
    void RefreshHud();
    void WriteSummary();

    CliOptions m_options;
    // P1.5：源接口化——回放（ReplaySource）与子进程（ChildSource）二选一。
    std::unique_ptr<MessageSource> m_source;      // 多态持有（Next/Send/IsLive）
    ReplaySource* m_replay = nullptr;             // 非拥有；仅回放模式下非空（进度 n/N 用）
    ChildSource* m_child = nullptr;               // 非拥有；仅 --core 模式下非空
    std::unique_ptr<ProtocolWriter> m_writer;     // 反向通道（仅 live 模式构造）
    size_t m_liveLines = 0;                       // live 模式：已消费行数（无"总量"概念）
    Transcript m_transcript;

    HarnessApplication* m_application = nullptr;   // Run() 栈上对象，非拥有
    ECDI::Window* m_window = nullptr;              // Application 所有，非拥有
    CountingPanel* m_counting = nullptr;           // Widget 树所有，非拥有
    ECDI::PaintProbe* m_probe = nullptr;           // --cull-probe 时非拥有（Run 栈上 services 所有）
    ECDI::MeasureProbe* m_measureProbe = nullptr;  // 同上（测量链探针）
    ECDI::ScrollView* m_scroll = nullptr;          // 非拥有
    ECDI::Widget* m_content = nullptr;             // ScrollContent，非拥有
    PromptBar* m_promptBar = nullptr;              // 非拥有
    ECDI::ProgressBar* m_usageBarIn = nullptr;     // 右栏用量条（非拥有）
    ECDI::ProgressBar* m_usageBarOut = nullptr;
    ECDI::ProgressBar* m_replayBar = nullptr;      // 左栏回放进度条（非拥有）
    Hud m_hud;

    std::deque<std::pair<std::string, std::string>> m_askHist;   // 权限历史 {choice, question}

    std::deque<RowRec> m_rows;
    std::map<std::string, RowRec*> m_blockRows;    // block id → 行（delta 流式追加用）
    std::string m_coreState;
    std::string m_sessionId;                       // hello 捕获（HUD 左栏）
    std::string m_coreVer;
    std::string m_capsStr;
    int m_protocolVer = 0;
    int m_usageTurn = -1;                          // usage 捕获（HUD 右栏/状态栏）
    long long m_usageIn = -1;
    long long m_usageOut = -1;
    bool m_waitForAnswer = false;                  // timed 模式：ask 未答，暂停拉取
    bool m_instantDone = false;
    bool m_replaySettled = false;                  // 夹具拉取完毕（停止制造重绘）
    bool m_sweepStarted = false;                   // --scroll-sweep 已启动（仅一次）
    bool m_sweepDone = false;                      // --scroll-sweep 扫到底（退出）
    bool m_hudDirty = true;                        // HUD 有新数据待刷新
    int m_relayoutPulse = 0;                       // 折叠动画期间的补重排 tick 数（~33ms each）
    bool m_summaryWritten = false;
    std::string m_pendingTruncated;                // ReplaySource 的 EOF 截断提示（延后展示）
    bool m_liveEof = false;                        // live 模式：源已 EOF（停止空转刷 HUD）
    std::string m_backendName = "gdi";             // 实际运行的后端（GL 就绪检查后定；摘要用）

    // ★★ REQ-04 探针读数（--state-probe）：每次 RefreshHud 观察到「与上次不同」的
    //   {send 使能, stop 使能, send 标题} 时记一行 ⇒ 摘要里是一份**状态翻转轨迹**。
    //   ★ 只在**变化时**记录（否则 30Hz 下会记几千行相同内容，摘要不可读）。
    //   ★ 与 core 的 `state` 并排输出 ⇒ 可逐行核对「state:X ⇒ 按钮使能应为 Y」。
    struct StateProbeRec {
        std::string coreState;   // 本次观察时的 core state（来自 m_coreState）
        bool sendEnabled = true;
        bool stopEnabled = false;
        std::string sendTitle;   // Button::GetText()——标题文本直接回读，不靠像素
    };
    std::vector<StateProbeRec> m_stateProbeLog;
    bool m_stateProbeLastValid = false;            // 是否已有「上次」可比
    bool m_stateProbeLastSend = true;
    bool m_stateProbeLastStop = false;

    // ★★ REQ-04 边界探针（--hit-probe，2026-10-14）
    //   `--state-probe` 回答的是「使能位对不对」；本组回答的是「**禁用后点击到不到**」。
    //   两者是 REQ-04 的**两个不同断言**——前者为真**推不出**后者（命中门控是另一段代码，
    //   `Widget.cpp:188/195`），故必须单独取证。
    //   ★ 每条 = {场景, 坐标, 使能, 命中者是否==发送按钮, 命中者类型, 点击是否达}。
    struct HitProbeRec {
        std::string scene;           // as-is / off-point / forced-enabled / forced-disabled
        int px = 0;                  // 本场景的客户区坐标（可复核）
        int py = 0;
        bool sendEnabled = false;    // 本场景下发送按钮的真实 IsEnabled()
        bool hitIsSend = false;      // HitTest 返回的是否**正是**发送按钮
        bool hitIsNull = false;      // HitTest 是否返回 nullptr
        bool hitIsRoot = false;      // 命中者是否**就是 RootWidget**（禁用后被跳过的典型落点）
        bool hitConsumes = false;    // 命中者是否 `ConsumesMouseInput()`（D9：可交互判据）
        std::string hitClass;        // 命中者类型名（"(null)" / Widget / Button / …）
        bool clickReached = false;   // 合成 Down+Up 后 SendPrompt 是否被进入
    };
    std::vector<HitProbeRec> m_hitProbeLog;
    int m_hitProbeSendCalls = 0;      // SendPrompt 进入次数（探针期间唯一观测通道）
    bool m_hitProbeActive = false;    // 探针执行中：SendPrompt 只计数、零副作用
    bool m_hitProbeDone = false;      // 已跑过（夹具 settle 后只跑一次）

    void RunHitProbe();                       // 跑四组受控对照并写入 m_hitProbeLog
    void SynthesizeClickAt(int x, int y);     // 合成 Down+Up 走**真实**派发链
    static std::string WidgetKindName(const ECDI::Widget* w);


    // 评估读数（每 tick 缓存一份——窗口销毁后 CountingPanel 不可再读）
    size_t m_evalPaints = 0;
    size_t m_evalFrames = 0;
    uint64_t m_evalMaxUs = 0;
    uint64_t m_evalSumUs = 0;
    uint64_t m_evalLastUs = 0;
    size_t m_invalidations = 0;                    // 应用侧 Invalidate() 调用数（§6.2）
    // PaintProbe 读数（--cull-probe 时有效）
    size_t m_evalProbeFrames = 0;
    size_t m_evalProbeCommands = 0;
    size_t m_evalProbeMax = 0;
    size_t m_evalProbeAvg = 0;
    uint64_t m_evalMaxExecUs = 0;                  // 后端执行端帧时（BeginFrame..EndFrame）
    uint64_t m_evalAvgExecUs = 0;
    size_t m_evalMeasureCalls = 0;                 // MeasureProbe 读数（文本测量成本）
    uint64_t m_evalMeasureTotalUs = 0;
    uint64_t m_evalMeasureMaxUs = 0;
    std::chrono::steady_clock::time_point m_timedAnchor;
};
