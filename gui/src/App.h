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
#include "ECDI/Core/Color.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/RingProgressBar.h"
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
    // 浅色是默认交互主题（对应 variant-c）；--theme dark 保留深色选项。
    bool lightTheme = true;             // --theme light|dark
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
    // ★★ 协议 v2 §7 思维链折叠行为探针（2026-10-14）：默认收起 / 点击展开 / 箭头翻转
    //   ——把**渲染行为**变成机读读数（截图只能证明像素，不能证明"点了真的翻"）。
    //   默认 false ⇒ 零行为变化。
    bool reasoningProbe = false;          // --reasoning-probe：折叠行为取证
    // ★★ Step 0a 顺序回读探针（2026-10-19）：把 `Transcript::Order()` 原样写进摘要。
    //   动机：0a 的**唯一**断言是「到达顺序可恢复」。而这条断言**不能靠肉眼**——
    //   屏幕上恰好正确也可能只是 demo 夹具的巧合（b1..b8 位数相同 ⇒ 字典序 == 到达序）；
    //   cot/realistic/scale 三份夹具则是 42/218/1998 个位置不同（实测，见
    //   `.scratch/order_fixture_scan.py`）⇒ 必须让程序把顺序**打印出来**再与夹具对账。
    //   ★ 默认 false ⇒ 既有摘要逐字节不变（所有回归基线仍可比）。
    bool orderProbe = false;              // --order-probe：回读到达到顺序到摘要
    // ★★ Step 0c 重建探针（2026-10-19）：把「照模型重建视图 ⇒ 视图不变」变成机读读数。
    //   动机：0c 的断言是「会话模型足够重建出**同一份**转录视图」。这条断言**不能靠眼睛**：
    //   少一条通知、顺序错一位、展开态丢了，屏幕上看都像「差不多」。
    //   ★ 做法：建一个**必有**区分力的前置条件（把所有可折叠行展开、所有 ask 标已答），
    //     快照 → 拆掉全部行重建 → 再快照 → 逐字段比对。
    //   ★ **必须有负对照**：同一探针再按**字典序**（= `m_blocks` 遍历序）重建一次，
    //     它**必须**报出 42/218/1998 处次序错（cot/realistic/scale）。否则探针是瞎的
    //     （0a 已经吃过这个教训：demo 夹具上两种顺序恰好一致，只用 demo 验不出来）。
    //   ★ 默认 false ⇒ 零行为变化；且**永远不**在测量跑批里自动重建。
    bool rebuildProbe = false;            // --rebuild-probe：照模型重建视图并比对
    // ★★ Step 0d 会话探针（2026-10-19）：新建/切换会话的机读取证。
    //   只在回放模式有意义（live 新建会话会真拉 core 子进程，探针不制造真进程）。
    //   默认 false ⇒ 零行为变化。
    bool sessionProbe = false;            // --session-probe：新建/切换会话取证
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
        ECDI::Label* convTitle = nullptr;    // 中栏会话头：标题（原型 C .conversation-head）
        ECDI::Label* convSub = nullptr;      // 中栏会话头：元信息副标
        ECDI::Label* healthSeq = nullptr;    // 右栏：seq 连续性（I4）
        ECDI::Label* healthViol = nullptr;   // 右栏：违规 · 未知
        ECDI::Label* healthKind = nullptr;   // ★ 右栏：未知 kind 计数（2026-10-13，协议空白）
        ECDI::Label* evalPaints = nullptr;   // 右栏：重绘读数
        ECDI::Label* probeCmds = nullptr;    // 右栏：命令/帧 avg
        ECDI::Label* probeExec = nullptr;    // 右栏：执行 avg us
        ECDI::Label* usageInV = nullptr;     // 右栏：输入 tokens 值
        ECDI::Label* usageOutV = nullptr;    // 右栏：输出 tokens 值
        // ★★ 协议 v2 §3.1（2026-10-14）：上下文占用百分比（用户要求）。
        //   ★ 2026-10-16 更新：环形进度条**已可做**——ECDI Phase 33 提供了 `DrawArc`
        //     （`RenderingBackend.h:110`）与 `RingProgressBar`，`m_ctxRing` 即其消费点。
        //     （此前这里写「当前做不了」，那是 Phase 33 之前的真实状态，现已失效。）
        ECDI::Label* ctxPct = nullptr;       // 右栏：上下文占用 `42%` / `—`（环内不叠字，故仍需本标签）
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

    // ★★ 0c（2026-10-19）：**转录时间线**——「该被看见的一行」的模型侧描述。
    //   为什么必须有它：在此之前「屏幕上有什么行」这件事**只存在于控件树里**
    //   （`rows` 里全是控件指针）。于是任何"把视图重新建一遍"的诉求都无法成立：
    //     · 块按什么次序排？——只能去遍历 `Transcript::m_blocks`，
    //       而那是 **id 字典序**（0a 实测 cot/realistic/scale 分别错 42/218/1998 处）；
    //     · 通知行（协议健康、hello/bye、截断报错）**根本不在块表里** ⇒ 重建即全丢；
    //     · `ask` 行的问句只存在于那条 json 消息里 ⇒ 重建即丢失；
    //     · 展开/已答是**用户状态**，却只挂在 `RowRec` 上 ⇒ 重建即回退。
    //   本结构逐条解决这四点：一行一条，**顺序即屏幕顺序**，状态也在条目里。
    //   ★ 与 `rows` 的**不变量**：两表**等长且同序**（`rows[i].timelineIdx == i`）。
    //     这条不变量可机检，构建期由 `AddRow` 保证，重建期由 `MaterializeRows` 保证。
    struct TimelineEntry {
        enum class Kind { Block, Notice, Ask };
        Kind kind = Kind::Block;
        // Block ⇒ 块 id（delta/block_end 回查）；Ask ⇒ 协议 ask 的 id（answer 回执用）；
        // Notice ⇒ 空（通知没有标识——它本来就不是协议对象）。
        std::string id;
        std::string text;              // Notice ⇒ 通知文本；Ask ⇒ 问句（`question`）
        ECDI::Color color;             // Notice ⇒ 文本色（其余不用）
        bool expanded = false;         // Block(tool/reasoning) ⇒ 用户展开态
        bool answered = false;         // Ask ⇒ 已答（重建时**只回填状态，不重发 answer**）
    };

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
        // ★★ 协议 v2 §7（2026-10-14）：思维链卡。
        //   与 `tool` **分开记**而非复用：两者虽然共用 `CollapsiblePanel` 机制，
        //   但标题语义、配色、`block_end` 后的行为都不同（工具卡改标题为「名 · 摘要」，
        //   思维链卡只把箭头从 ▼ 换成 ▶）。混用一个 bool 会让分支里到处是
        //   「是工具卡还是思维链」的二次判断。
        bool reasoning = false;
        bool ended = false;                       // 该块是否已 `block_end`（标题两态用）
        std::string blockId;                      // 点开展开时回查块（标题重算用）
        std::string askId;                        // P1.5：协议 ask 的 id（answer 回执用）
        int bodyH = 0;                            // 展开体高度缓存（autosize 布局用）
        // ★ A-2 曾拟在此加 `bodyHDirty`（按需重测），**已复核否决**（2026-10-13 晚）：
        //   把 `chars` 分解为 `a(一次性) + b(每帧)·frames` 后实测 ⇒ b 与文本长度**无关**
        //   （body 4000 与 40 的 b 分别为 504 / 502，比 1.00×）⇒ 折叠态**没有**逐帧
        //   重测长文本；长文本的成本是**一次性**的布局建立（C29-10 缓存按文档生效）。
        //   故 `Relayout` 保持无条件 `AutoSize()`。取证：`.scratch/probe_a2_decompose.py`。
        int lastBodyW = 0;                        // 上次下发给 body 的基准尺寸——
        int lastBodyH = 0;                        // 未变则不重发（动画期间重入会钉死动画）
        // ★★ 0c：本行对应的时间线条目下标。与 `Session::timeline` **双向**对应关系的一半
        //   （另一半是 `timeline[i]` 的顺序）。它的用处是把**视图侧**的临场状态
        //   （展开/已答）写回**模型侧**——否则重建会把用户的操作回退掉。
        //   ★ 只增不改序：条目一旦建好，下标永不变（时间线只追加）。
        size_t timelineIdx = 0;
    };

    // ★★ 0b（2026-10-18）：**逐会话**状态收进一个可整体替换的对象。
    //   为什么需要这样一层：下面这些字段此前和窗口/控件指针平铺在同一个类里，等于
    //   断言「一个应用 = 一个会话」。于是"新建会话"**无处落脚**——新会话的转录、
    //   用量、行表该存到哪？没有容器，连暂存的地方都不存在。
    //   ★ 本步是**纯搬运**：仍只有一个会话，回放读数必须逐位不变才算过（0a 立的规矩）。
    //     真正的切换逻辑在 0c，UI 重建在 0d。
    //   ★ 判据不是"字段搬了家"，而是"同一时刻只有一份东西能叫**当前**会话"。
    //   ★ 不变量（0c 依赖）：`blockRows` 里是指向 `rows` 元素的裸指针，`rows` 用 deque
    //     保证 push_back 不失效 ⇒ Session 一旦建立就**不得被搬动/拷贝**。0c 用
    //     unique_ptr 持有正是为了这条；这里先按值放一个，只为把结构立起来。
    struct Session {
        // ── 消息源 / 反向通道 ──
        std::unique_ptr<MessageSource> source;    // 多态持有（Next/Send/IsLive）
        ReplaySource* replay = nullptr;            // 非拥有；仅回放模式下非空（进度 n/N 用）
        ChildSource* child = nullptr;              // 非拥有；仅 --core 模式下非空
        std::unique_ptr<ProtocolWriter> writer;    // 反向通道（仅 live 模式构造）
        size_t liveLines = 0;                      // live 模式：已消费行数（无"总量"概念）
        Transcript transcript;

        // ── 转录行表（行的视图状态也在这里：折叠/已答属于会话，不属于窗口）──
        // ★★ 0c：`timeline` 是**模型侧**（该看见什么），`rows` 是**视图侧**（控件指针）。
        //   两表等长同序，`rows[i].timelineIdx == i`。重建 = 清 `rows` 清控件树，
        //   按 `timeline` 逐条重放构造函数再回填状态。
        std::vector<TimelineEntry> timeline;
        std::deque<RowRec> rows;
        std::map<std::string, RowRec*> blockRows;  // block id → 行（delta 流式追加用）

        // ── hello / 协议协商 ──
        std::string coreState;
        std::string sessionId;                     // hello 捕获（HUD 左栏）
        std::string coreVer;
        std::string capsStr;
        int protocolVer = 0;
        // ★ 协议 §9：core 声明的版本高于本 GUI 上限 ⇒ 已拒绝连接（2026-10-14）。
        //   用途：摘要里如实记下"这次运行是被拒的"，避免读到一个空摘要却以为跑成功了。
        bool protocolIncompatible = false;

        // ── usage / 上下文容量 ──
        int usageTurn = -1;                        // usage 捕获（HUD 右栏/状态栏）
        long long usageIn = -1;
        long long usageOut = -1;
        // ★★ 协议 v2 §3.1（2026-10-14）：上下文窗口容量 = 「上下文占用百分比」的**分母**。
        //   ★ 来源**只有** `hello.context` 一处；缺字段 ⇒ 保持 0 ⇒ 百分比显示 `—`。
        //   ★ **不得**猜测容量、也**不得**按 0 当分母（分母未知 ⇒ 百分比无定义）。
        //   ★ 这正是本轮之前那两根用量条的问题：分母取 `max(input, output)`，
        //     即**拿输入跟输出比**——两个不同量纲的东西相除，读数无意义。
        long long contextLimit = 0;

        // ── 权限询问历史（右栏三行）──
        std::deque<std::pair<std::string, std::string>> askHist;   // {choice, question}

        // ── 拉取节拍（各会话各自走到哪一步，互相独立）──
        bool waitForAnswer = false;                // timed 模式：ask 未答，暂停拉取
        bool instantDone = false;
        bool replaySettled = false;                // 夹具拉取完毕（停止制造重绘）
        std::string pendingTruncated;              // ReplaySource 的 EOF 截断提示（延后展示）
        bool liveEof = false;                      // live 模式：源已 EOF（停止空转刷 HUD）

        // ── ★★ 0d：会话身份与各自的 timed 节奏 ──
        //   为什么 `timedAnchor` 必须**逐会话**：它是"这个会话从什么时候开始按 ts 播"的
        //   锚点。若留在窗口级，新建/切回的会话会拿**别人的**锚点算 elapsedMs ⇒
        //   一上来就把整条夹具判成"早已到期"并瞬间抽干（读数全错，而且错得很安静）。
        std::string label;                         // 会话显示名（侧栏 / 会话头）
        std::chrono::steady_clock::time_point timedAnchor;
        // ★ 暂停补偿：会话被切走时记下时刻，切回来把锚点整体后移同一段墙上时间。
        //   没有这一步，"切走 30 秒再切回"会让 timed 会话凭空快进 30 秒。
        bool timedPaused = false;
        std::chrono::steady_clock::time_point timedPausedAt;
    };

    // timelineIdx = 本行对应的时间线条目下标（**由调用方显式给出**——
    //   重建时若有条目建不出行，两者会错位，而下标错位会把 A 的展开态写到 B 上）
    void AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                const std::string& blockId, size_t timelineIdx);
    void CreateBlockRow(const Block& block);
    void CreateAskRow(const json::Value& msg);
    void MarkAnswered(RowRec& rec, const std::string& choice);
    void AddNoticeRow(const std::string& text, const ECDI::Color& color);

    // ── ★★ 0d：多会话生命周期 ──────────────────────────────────────────
    // `CreateSessionFromOptions()`：按当前 CLI 建一个会话并 push 进 `m_sessions`，
    //   **返回**它（live 子进程启动失败 ⇒ 返回 nullptr 且不 push）。
    //   ★ 只"创建"不"激活"——激活（含视图重建）由调用方经 `SwitchToSession` 做。
    //     若创建时就改 `m_sess`，`SwitchToSession` 会因"已在该会话"早退而不重建视图。
    Session* CreateSessionFromOptions();
    // `NewSession()`：`＋ 新建会话` 的真实行为。live 模式拉起新 core（真新会话）；
    //   回放模式建一个空会话并如实提示（没有 core 会说话，不伪造转录）。
    void NewSession();
    // `SwitchToSession(i)`：切换活动会话——拆当前视图、指向新会话、按新会话模型重建。
    //   ★ 复用 0c 的 `RebuildTranscriptView`：这正是 0c 存在的全部理由。
    void SwitchToSession(size_t index);
    // 切换/新建前，把**当前**活动会话的 timed 锚点冻结（记下暂停时刻）。
    void PauseActiveTimedClock();
    // 切到某会话后，把它的 timed 锚点按"被冻结了多久"整体后移（补偿暂停）。
    void ResumeSessionTimedClock(Session& s);

    // ── ★★ 0c：模型 → 视图（重建路径）────────────────────────────────
    // 为什么要「重建」而不是就地改：`Theme.h:19-20` 写明令牌是**引用别名**，UI 一旦建好
    // 就把当时的颜色值拷进了各自的 PanelStyle/ButtonStyle ⇒ 事后 SwitchTheme 不会重绘。
    // 故换主题/换会话的唯一可靠手段是**拆掉重来**。而拆掉重来的前提正是本组函数：
    // 模型足够把视图原样再生出来（0c 的探针就是拿它自证的）。
    //
    // `MaterializeRows()`：按 `timeline` 逐条重放行构造，并把条目上的状态回填到控件
    //   （展开态 ⇒ `CollapsiblePanel::SetExpanded`；已答 ⇒ 静默置灰**不重发 answer**）。
    //   ★ 不复用 `RouteLastMessage`——那是**增量**路由（依赖"刚才那一行消息"），
    //     重建是**全量**重放，两者不能混（混用就会重复 AddNoticeRow 之类）。
    void MaterializeRows();
    // `RebuildTranscriptView()`：拆掉 `m_content` 的全部子控件并按模型重建（Level 1）。
    //   ★ 只碰转录区，**不碰**窗口/侧栏/检查器 ⇒ `CountingPanel` 与 HUD 指针全部存活，
    //     评估读数不中断（Level 2 的整树重建才需要续接计数器，留给 0e）。
    //   ★ `orderOverride` **只供负对照**：给一组块 id 就按那组次序重建（探针用字典序
    //     喂进来，看它能不能被抓出来）。非空时**只建块行**、不建通知/问句行——
    //     那正是"只照块表重建"这个错误实现会有的样子，损失必须被看见。
    void RebuildTranscriptView(const std::vector<std::string>* orderOverride = nullptr);
    // `SyncRowViewState()`：把视图侧的临场状态写回模型（重建前必须调用）。
    //   ★ 方向是 视图→模型（用户点了展开，模型要记下）；`MaterializeRows` 是模型→视图。
    void SyncRowViewState();
    // 把一条时间线条目落成行（三种 Kind 的分派点；重建期专用）。
    //   ★ 构建期各走 `CreateBlockRow`/`CreateAskRow`/`AddNoticeRow`（它们带**副作用**：
    //     登记条目、自动应答）。重建期走本函数，**纯渲染、零副作用**——
    //     否则重建一次就等于把用户做过的决策重放一次。
    void MaterializeOne(size_t idx, const TimelineEntry& entry);
    // 可折叠行的点击回调绑定（构建期与重建期共用；含「写回条目」这一步）。
    void BindCollapsible(const Block& block, RowRec& stored);
    // 展开/收起一行的**唯一入口**（点击与探针共用）。
    //   ★ 为什么抽出来：0c 的重建探针要造"已展开"的前置状态，而它不能合成点击
    //     （要造状态的行可能在视口外、HitTest 命不中）；若探针自己再写一遍
    //     "改标志 + 翻箭头 + 重排"，那就是**第二份实现**——测的是它自己。
    void ToggleRow(RowRec& stored, bool reasoning);
    // 行构造的**纯函数**部分：只建控件、不追加时间线、不发副作用（重建可重放）。
    std::unique_ptr<ECDI::Widget> BuildBlockRow(const Block& block, RowRec& rec);
    std::unique_ptr<ECDI::Widget> BuildAskRow(const TimelineEntry& entry, RowRec& rec);
    std::unique_ptr<ECDI::Widget> BuildNoticeRow(const std::string& text,
                                                 const ECDI::Color& color, RowRec& rec);

    void Relayout();
    void FollowTailIfPinned();
    void RequestRepaint();
    void CacheEvalMetrics();
    void RefreshHud();
    void WriteSummary();

    CliOptions m_options;

    // ★★ 0d（2026-10-19）：**多会话**。0b 把逐会话状态收进 `Session`，0c 让视图能从
    //   模型重建，0d 就是把"一个应用 = 一个会话"这条断言拆掉：
    //     · `m_sessions` = 全部会话（`unique_ptr` 持有 ⇒ Session 对象地址稳定，
    //       满足 0b 记的不变量：`blockRows` 的裸指针不得因容器增长而失效）；
    //     · `m_sess` = **活动会话**的裸指针。保留这个名字是有意的——186 处调用点
    //       只从 `m_sess.` 变成 `m_sess->`，语义仍是"当前会话"，不必逐处理解。
    //   ★ 线划在这里：**N 个会话，同时只有一个活动**。活动的那个才接 tick、才拉取；
    //     其余是冻结的只读历史。不做并行多 core——那会把这一步长成进程管理器（P3 的活）。
    //   ★ 新建会话：`--core` 模式**真的拉起一个新 core 子进程**（真新会话）；
    //     回放模式是**空会话**（没有 core 会说话），如实提示而不伪造转录。
    std::vector<std::unique_ptr<Session>> m_sessions;
    Session* m_sess = nullptr;                     // 活动会话（非拥有；指向 m_sessions 里的元素）

    // 活动会话在 m_sessions 里的下标（会话头 / 侧栏显示用；-1 = 尚无会话）
    size_t m_activeIndex = static_cast<size_t>(-1);

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
    // ★★ 上下文占用环（协议 v2 §3.1 + Phase 33）：分母 = `hello.context`（**不是** max(in,out)）。
    //   ★ 2026-10-16 由「百分比条」改为「环形进度条」——ECDI Phase 33 落地了框架首个
    //     曲线原语 `DrawArc`（纯虚，`RenderingBackend.h:110`）与其首个消费者
    //     `RingProgressBar`，本项目正是账本 `REQ-08` 登记的**首个真实消费者**，
    //     挂账前提（"无消费者不动"）由此改变 ⇒ 本条兑现。
    //   ★ 保留 `ProgressBar` 头：输入/输出两行仍用细条（用户裁决：不换）。
    ECDI::RingProgressBar* m_ctxRing = nullptr;
    // ★ 环色档位记忆（-1 = 尚未设置；0 = 绿 · 1 = 琥珀 · 2 = 红）。
    //   为何要记：`RingProgressBar::SetStyle` 的头**未**声明「覆盖后 Invalidate」
    //   （对比 `Panel.h:39` 明确写了），实现又在 lib 里 ⇒ 不能假定它内建重绘。
    //   但若**每 tick** 都补一次 `RequestRepaint()`，`invalidations`/`paints`
    //   会被凭空刷高（cot instant 20s ≈ 600 ticks ⇒ 16 变 ~616），而这两个数
    //   正是本项目的**评估读数**（与 p32 基线逐位对比）⇒ 那是「自己的仪器污染
    //   实验」（条 57③）。故只在**档位翻转**时补一次重绘。
    int m_ctxRingLevel = -1;
    ECDI::ProgressBar* m_replayBar = nullptr;      // 左栏回放进度条（非拥有）
    Hud m_hud;

    // ── 窗口级运行状态（与会话无关：它们描述的是"这一次进程跑了什么"）──
    bool m_sweepStarted = false;                   // --scroll-sweep 已启动（仅一次）
    bool m_sweepDone = false;                      // --scroll-sweep 扫到底（退出）
    bool m_hudDirty = true;                        // HUD 有新数据待刷新
    int m_relayoutPulse = 0;                       // 折叠动画期间的补重排 tick 数（~33ms each）
    bool m_summaryWritten = false;
    std::string m_backendName = "gdi";             // 实际运行的后端（GL 就绪检查后定；摘要用）

    // ★★ REQ-04 探针读数（--state-probe）：每次 RefreshHud 观察到「与上次不同」的
    //   {send 使能, stop 使能, send 标题} 时记一行 ⇒ 摘要里是一份**状态翻转轨迹**。
    //   ★ 只在**变化时**记录（否则 30Hz 下会记几千行相同内容，摘要不可读）。
    //   ★ 与 core 的 `state` 并排输出 ⇒ 可逐行核对「state:X ⇒ 按钮使能应为 Y」。
    struct StateProbeRec {
        std::string coreState;   // 本次观察时的 core state（来自活动会话）
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

    // ★★ 协议 v2 §7 思维链折叠行为的机读取证（--reasoning-probe，2026-10-14）
    //   用户口径是**渲染行为**（默认收起 / 点击展开 / 箭头翻转），截图只能证明"像素长这样"，
    //   不能证明"点一下真的翻了、再点一下真的回去了"。故沿用 `--hit-probe` 的做法：
    //   合成点击走真实派发链，把**状态轨迹**写成机读读数。
    //   ★ 每条 = {阶段, 标题文本, 折叠体是否展开, 内容是否可见, 行高}。
    struct ReasoningProbeRec {
        std::string phase;        // default / after-click-1 / after-click-2
        std::string headerText;   // Button::GetText()——箭头与两态文本都在里面
        bool expanded = false;    // CollapsiblePanel::IsExpanded()
        bool contentVisible = false;  // 折叠体内容容器 IsVisible()
        int panelH = 0;           // 行高（展开应显著变大）
    };
    std::vector<ReasoningProbeRec> m_reasoningProbeLog;
    // ★ 跨 tick 推进的状态机（不能在一个 tick 内做完——见 RunReasoningProbe 注释）
    int m_reasoningProbeStep = 0;
    std::chrono::steady_clock::time_point m_reasoningProbeNextAt;
    bool m_reasoningProbeDone = false;
    // ★★ 流式态标题必须在**建行时刻**抓，不能事后补（2026-10-14 用户裁决后重新需要）：
    //   标题现在**真的会变**（`▼ 思考中` → `▼ 已完成 · N tokens`），而 `--instant`
    //   在同一 tick 抽干整个夹具 ⇒ 事后回读**永远**是「已完成」，「思考中」采不到。
    //   ★ 与首版同一个坑（那次三条读数全一样），故这次保留该捕获点。
    std::string m_reasoningTitleStreaming;   // 建行时读到的标题（应为 ▼ 思考中）
    void RunReasoningProbe();

    // ★★ Step 0c 重建探针（--rebuild-probe，2026-10-19）
    //   断言：**照模型重建视图 ⇒ 视图逐行不变**。为何必须机读：丢一条通知、错一位次序、
    //   丢一个展开态，在屏幕上都像"差不多"——肉眼不是量具。
    //   ★ 三组读数（每组一行快照 + 一次比对）：
    //     ① forward  —— 按 `timeline` 重建 ⇒ 判据 A == B（必须全等）；
    //     ② lexicographic —— **故意**按 `m_blocks`（id 字典序）重建的负对照 ⇒
    //        必须报出次序错（cot 42 / realistic 218 / scale 1998 / demo 0）。
    //        它证明本探针**看得出**顺序错，否则 ① 全等毫无意义。
    //     ③ noticeLoss —— 若模型只存块、不存通知，重建会丢几行（这一项用「模型里实际
    //        记下的通知条数」直接给出，说明第 ② 组之外**还有**一类损失，非零才证明
    //        通知进模型这件事是承重的）。
    struct RebuildProbeRec {
        std::string phase;                 // forward / lexicographic / forward-restore
        size_t rowCount = 0;               // 重建后行数
        size_t beforeCount = 0;            // 重建前行数（两者不等 ⇒ 丢了/多了行）
        size_t timelineCount = 0;          // 模型条目数
        size_t mismatches = 0;             // 与重建前快照不同的行数（含长度差）
        size_t orderMismatches = 0;        // 其中属于**次序/条目**不同的行数
        std::string firstDivergence;       // 首个分歧的可复读写形（空 = 无分歧）
        int scrollOffset = 0;              // 重建后恢复到的滚动偏移
        int maxScrollOffset = 0;
    };
    std::vector<RebuildProbeRec> m_rebuildProbeLog;
    // 重建前快照（一行一条，`\x1f` 分隔字段——比对用，不进摘要原文）
    std::vector<std::string> m_rebuildProbeBefore;
    size_t m_rebuildNoticeCount = 0;       // 模型里记下的通知行数（承重性证据）
    int m_rebuildScrollBefore = 0;         // 重建前的滚动偏移（判"有没有跳回顶部"）
    // ★ 点击一次之后标题是否被清空（0c 顺手修的那个 bug 的**专属**读数）。
    //   它必须独立于重建比对：那个 bug 让重建前后标题**同为**空串，A == B 照样成立。
    std::string m_rebuildTitleSample;      // 首个样本点击**前**的标题
    std::string m_rebuildTitleAfter;       // 同一样本点击**后**的标题
    int m_rebuildTitleEmptied = 0;         // 点击后有标题被清空的次数
    // ★ 前置条件是否真的成立（展开样本数 / 已答样本数）。若都为 0，则 forward 全等
    //   **证明不了"状态被保住"**——那种全等只说明"两边都是空的"，探针是瞎的。
    int m_rebuildExpandedSamples = 0;
    int m_rebuildAnsweredSamples = 0;
    bool m_rebuildProbeDone = false;
    int m_rebuildProbeStep = 0;
    std::chrono::steady_clock::time_point m_rebuildProbeNextAt;
    // 快照 A 的「只含块行」投影（负对照的比对面——不含通知/问句，正是"只照块表重建"
    // 这个错误实现能产出的全部内容；故两者的差**只**来自次序）
    std::vector<std::string> m_rebuildProbeBlockOnly;
    void RunRebuildProbe();
    // 把当前 `rows` 压成可比较的行快照（探针 A/B 两侧共用**同一个**量具）
    std::vector<std::string> SnapshotRows() const;
    // 两侧快照逐字段比对（`before` 为准；长度差也计入 mismatches）
    RebuildProbeRec CompareRows(const char* phase,
                                const std::vector<std::string>& before,
                                const std::vector<std::string>& after,
                                bool blockOnly) const;

    // ★★ Step 0d 会话探针（--session-probe，2026-10-19）
    //   断言：**新建会话真的多出一个、能切走、切回来内容不丢**。
    //   为何必须机读：多会话最容易"看着能用其实没动"——新会话若共享了旧会话的模型，
    //   切过去还是满屏旧转录，肉眼分不出"建了个新的"和"根本没建"。
    //   ★ 轨迹（跨 tick 状态机，与 rebuild/hit 探针同款，避免在一个 tick 里做完）：
    //     ① baseline —— 记下当前会话数与行数；
    //     ② new      —— 触发 `NewSession()`，会话数必须 +1，新会话行数必须 == 0
    //                    （**负对照点**：若新会话不是空的，说明它抄了旧会话的模型）；
    //     ③ back     —— 切回会话 0，行数必须回到 baseline（**往返无损**）。
    //   ★ 只在**回放**模式跑：live 模式新建会话会真拉一个 core 子进程，
    //     探针不该在测量里制造真进程（仪器污染实验，条 57③）。
    struct SessionProbeRec {
        std::string phase;        // baseline / new / back
        size_t sessionCount = 0;  // 当时的会话总数
        size_t activeIndex = 0;   // 当时的活动会话下标
        size_t rowCount = 0;      // 视图行数（含通知行）
        size_t blockRowCount = 0; // 其中**块行**数 —— 负对照的关键：新会话必须为 0
    };
    std::vector<SessionProbeRec> m_sessionProbeLog;
    bool m_sessionProbeDone = false;
    void RunSessionProbe();


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
    // ★ 0d：`m_timedAnchor` 已下沉到 `Session::timedAnchor`（逐会话）——窗口级不再需要它。
};
