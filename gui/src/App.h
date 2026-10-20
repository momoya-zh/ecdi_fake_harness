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
    // ★★ Step 1 侧栏探针（2026-10-19）：侧栏会话列表的 A==B 判据。
    //   ★ 为什么必须单独一条：0c 的 A==B（rebuildProbe）**只覆盖转录区**——
    //     `SnapshotRows` 遍历的是 `m_sess->rows`，侧栏控件树根本不在它的视野里。
    //     于是"侧栏点错了 / 列表少画一行 / 选中态没跟上"这类缺陷，现有仪器**一个都看不见**。
    //   ★ 四条判据（跑批脚本执行）：
    //     · rebuild.matched == true（拆掉列表重建，逐行逐字段一致）
    //     · negative.matched == false（**负对照**：按逆序重建必须报出次序错——
    //       否则这条判据是瞎的，与 0a 的字典序陷阱同型）
    //     · new.rowCount == baseline.rowCount + 1 ∧ new.sessionCount == baseline.sessionCount + 1
    //     · deleted.rowCount == baseline.rowCount ∧ deleted.transcriptRows == baseline.transcriptRows
    //       （**负对照**：删掉一个会话后活动会话的转录行数必须**一位都不变**——
    //        否则"删了"和"删错了"分不清）
    //   ★ 默认 false ⇒ 零行为变化。
    bool sidebarProbe = false;            // --sidebar-probe：侧栏列表 A==B 取证
    // ★★ Step 3 权限答案探针（2026-10-19）：ask 的**答案内容**进模型的取证。
    //   ★ 为什么必须单独一条：`answered`（bool）已经能被 0c 的 rebuildProbe 覆盖，
    //     但它只说"答过了"。本轮补的是 `answer`（"allow"/"deny"）——**旧读数对它
    //     完全无感**：模型里没有这一位时，重建出来的权限门同样是"两个钮置灰"，
    //     A==B 照样全等。同 0c 的 buildTitle 缺陷：状态缺失不改变"改变前后一致"。
    //   ★ 判据（run 脚本执行）：
    //     · answered.answer == "allow"/"deny"（点哪个就是哪个）
    //     · forward.matched == true（重建后行快照全等，含新增的 answer 段）
    //     · **负对照**：把模型里的 answer 改写成另一个值再重建 ⇒
    //       matched 必须变 false，且 firstDivergence 指向 `answer` 字段
    //       （否则比对函数没读这一段，上面那条全等不作数）
    //     · 未答的 ask：answer 为空 ∧ 两个钮可用（负对照：补 answer 不得顺手置灰）
    //   ★ 默认 false ⇒ 零行为变化。
    bool answerProbe = false;             // --answer-probe：ask 答案内容进模型取证
    // ★★ Step 4 / 0e（2026-10-19）：**整树重建 + 运行期主题切换**的取证。
    //   ★ 为什么这是四步里唯一动仪器的：`Theme.h:19` 的令牌是**引用别名**
    //     （`inline const Color& kSidebarBg = g_theme.sidebarBg;`），建 UI 时颜色值
    //     已被拷进每个控件的 PanelStyle/ButtonStyle ⇒ 建完再切主题不会重绘。
    //     唯一可行的实现就是"整棵树拆了按新主题重新长出来"。
    //   ★ 而树的根子节点之一就是 `CountingPanel`（paints/frameCount 的**唯一**来源）
    //     ⇒ 一次重建会让评估读数从 61 掉回 3。不续接计数器，这一步就是拿仪器换功能。
    //   ★ 判据（run 脚本执行）：
    //     · rebuild-flip / rebuild-back 的 `paints` **恰好等于** baseline 的 `paints`
    //       （不是"更大"：重建期间不重绘，唯一正确的读数就是"一位没变"。
    //        若为 0，说明计数器被清零了；若更大，说明有东西在测量里偷偷重绘了）
    //     · rebuild-*.matched == true（侧栏可见性 / 会话行 / 转录行 / 输入框文本全等）
    //     · uiRebuilds 每次 +1（重建真的发生了，而不是走了个空分支）
    //     · toggle 相：点顶栏「主题」按钮 ⇒ 主题真的翻了、且 uiRebuilds +1
    //       （走真实派发链，不是直接调 RebuildUi）
    //     · **负对照** negative：把 `m_sidebarVisible` 单独改掉再重建 ⇒
    //       matched 必须变 false 且 firstDivergence 指向 `sidebarVisible`
    //       （否则比对函数没读这一位，前两条 matched 不作数）
    //     · restored.matched == true（改回去必须重新相等 —— 单变量纪律）
    //   ★ 默认 false ⇒ 零行为变化；摘要里连 `theme`/`uiRebuilds` 两个键都不出现。
    bool themeProbe = false;              // --theme-probe：整树重建 + 主题切换取证
    // ★★ Step 5（2026-10-19）：**打开历史会话**的取证。
    //   ★ 这一步与前四步的关系：前四步都在"活动会话"内部折腾（列表/收起/答案/重建），
    //     而这一步第一次让**磁盘上的文件**变成会话 ⇒ 引入了三种新的错误形态：
    //     ① 打开一个文件却读出了别的内容（路径错位 / 串了会话）
    //     ② 打开了但**没跳到末尾**（用户点开历史，看到的是一段慢慢播的回放）
    //     ③ 历史里的授权决定被抹平成"未回答"（Step 3 修好的病换个位置复发）
    //   ★ 判据（run 脚本执行）：
    //     · `list`：侧栏历史行数 == 磁盘上 `*.jsonl` 的个数（自己数一遍，不只信 UI 计数）
    //     · `open`：点一行 ⇒ 活动会话的 `originKind == "replay-history"`、
    //       `label` == 该文件名、`blocks > 0`（不是空会话）
    //     · `content`：**同一条 A==B**——历史打开的 blocks/deltas/messages 必须与
    //       `--replay` 直接加载同一文件**逐项相等**（这是"作者仍是 core"的机读证据：
    //       两条独立路径喂同一个文件，模型读数必须一致）
    //     · `at-end`：打开后 `replay->Empty()` 为真（真的跳到末尾了，不是刚起步）
    //     · `sidecar-missing`：状态文件不存在 ⇒ 所有 ask 的 answer 为空、两钮可用
    //       （负对照：若这里读出答案，说明读的是别人的状态或被写死成"已答"）
    //     · `sidecar-roundtrip`：写一份状态文件 ⇒ 重新打开 ⇒ 该 ask 的 answer 恢复、
    //       两钮置灰（正对照：证明 sidecar 真被读了，而不只是"没报错"）。
    //       ★ 探针收尾会把这份状态文件删掉，夹具目录保持零改动。
    //   ★ 默认 false ⇒ 零行为变化；摘要里连 `history` 键都不出现。
    bool historyProbe = false;
    bool workspaceProbe = false;       // --workspace-probe：v3 workspace/optimistic prompt 判据
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
        // ★★ Step 1（2026-10-19）：侧栏会话列表的三个句柄。
        //   ★ 为什么必须进 Hud：`RefreshHud` 的入口判据是 `if (!m_hud.title) return;`
        //     ——窗口级控件句柄只有挂在这里，才在"UI 还没建完"时被正确跳过。
        //   ★ 列表内容视图被整段重建时，这三个指针**不需要**重新指向——
        //     `sessList`（ScrollView）与 `sessCount`（Label）在重建中**存活**，
        //     只有列表**内容**（ScrollContent 的子控件）被拆掉重来。
        ECDI::ScrollView* sessList = nullptr;   // 左栏：会话列表滚动容器
        ECDI::Label* sessCount = nullptr;       // 左栏：会话计数 `2 个会话`
        // ★★ Step 2（2026-10-19）：侧栏本体与顶栏 `≡` 切换钮。
        //   ★ 侧栏句柄必须在这里而不是只在 `BuildMainArea` 的局部变量里：
        //     `SetSidebarVisible` 要在任意时刻把 `SetVisible` 打到侧栏上，
        //     而那次调用可能来自顶栏按钮回调（局部变量早没了）。
        ECDI::Panel* sidebar = nullptr;         // 左栏容器（收起 = SetVisible(false)）
        ECDI::Button* sidebarToggle = nullptr;  // 顶栏 `≡`：侧栏显隐（Step 2 前是死的 Label）
        // ★★ Step 4（2026-10-19）：主题切换钮。**必须是 Button**——这是本步唯一
        //   让用户能触发"整树重建"的入口，而重建的唯一正确触发方式就是一次真实点击
        //   （走 `Button::RaiseClick` → 回调 → `RebuildUi`）。
        //   ★ 句柄进 Hud 的理由同 `sidebarToggle`：重建后要能被探针摸到并合成点击。
        ECDI::Button* themeToggle = nullptr;    // 顶栏：深色/浅色切换
        ECDI::Panel* inspector = nullptr;       // 右栏检查器容器（可切换显隐）
        // ★★ 2026-10-20：中栏头 `≡` = 检查器显隐钮。此前它是**无判据**的装饰回调：
        //   一旦 `parent->Arrange()` 漏掉，屏幕会把检查器留在原位而探针全绿。
        //   句柄进 Hud 才能被合成点击（同 `sidebarToggle`/`themeToggle`）。
        ECDI::Button* inspectorToggle = nullptr;
        ECDI::Label* workspacePath = nullptr;   // 左栏：仅显示 core 确认的路径
        ECDI::Label* workspaceStatus = nullptr;// 左栏：workspace 请求状态
        ECDI::Button* workspacePick = nullptr;  // 左栏：目录选择器
        // ★★ Step 5（2026-10-19）：侧栏「历史会话」区。
        //   ★ 这里是**磁盘上的文件**变成会话的入口：点一行 = 新建一个 replay 会话
        //     并直接跳到末尾。历史行与 `sessList` 里的会话行是两种东西：
        //     前者是"可打开的文件"，后者是"已打开的会话"，前者点完会多出一行后者。
        //   ★ `historyList` 是 ScrollView 而不是直接往 side 里塞：历史文件数无上限，
        //     而侧栏是定宽定高的列（同 `sessList` 的理由）。
        ECDI::ScrollView* historyList = nullptr;  // 左栏：历史会话列表滚动容器
        ECDI::Label* historyCount = nullptr;      // 左栏：历史计数 `3 个历史文件`
        // ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容。
        //   数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
        //   回放会话没有工作区 ⇒ 此段显示「无工作区」。
        ECDI::ScrollView* projectList = nullptr;  // 左栏：项目目录滚动容器
        ECDI::Label* projectCount = nullptr;      // 左栏：项目计数
        // ⚠️ `sessSession` / `sessCore` **在 Step 1 之后不再被侧栏使用**：
        //   它们原本服务于"全局只有一行会话条目"，而现在每一行都有自己的 id/meta。
        //   保留字段（不在本步删除）的理由：第一，删字段要连带改 RefreshHud 与声明处，
        //   属**独立的清理**，混进这一步会把"侧栏列表"的 diff 撑大；
        //   第二，将来"活动会话的详细信息"若要在别处显示，这两个槽位是现成的。
        //   现状 = **声明仍在、恒不赋值、RefreshHud 里两处 `if` 永远不成立**（死代码但无害）。
        //   ⇒ 待办：下一步清理时连同这两条注释一起删。
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
        // ★★ Step 3（2026-10-19）：ask 的**答案内容**（"allow" / "deny"）。
        //   ★ 为什么 `answered` 不够：它只说"答过了"，不说"答的是什么"。于是任何重建
        //     （切会话 / 换主题 / 展开别的卡）都会把权限门画成"两个钮都置灰、看不出
        //     当时允许还是拒绝"——**历史会话里的授权决定会凭空变成"未知"**。
        //     这正是"打开历史会话"（下一步）的前置：不补这一位，重建出来的历史是残的。
        //   ★ 为何是本地字段、不进协议：协议里 `answer {id, choice}` 是**发出去**的
        //     回执，GUI 是它的**作者**；这里记的是作者自己的决定，属 GUI 侧状态。
        //     补协议字段会让"GUI 不产生转录块"（PROTOCOL.md:244）这条边界含糊化。
        //   ★ 空串 = 未答（与 `answered` 同义），故 `answered` 保留而不被这个字段取代：
        //     两者一起才构成合法状态（answered=true 而 answer 为空 = 模型不一致，
        //     由 `MarkAnswered` 的单一写入点保证不会出现）。
        std::string answer;
    };

    // 行记录：m_rows 用 deque（push_back 不失效指针——回调捕获依赖这一点）
    struct RowRec {
        ECDI::Widget* panel = nullptr;            // 行根（Panel），非拥有
        ECDI::Label* label = nullptr;             // 主文本；tool 行 = 展开体文本，非拥有
        ECDI::Button* headerBtn = nullptr;        // tool 卡片标题按钮，非拥有
        ECDI::CollapsiblePanel* body = nullptr;   // tool 卡片展开体，非拥有
        ECDI::Button* allowBtn = nullptr;         // ask 行按钮，非拥有
        ECDI::Button* denyBtn = nullptr;
        // ★★ Step 3：已答的**结论**（"allow" / "deny"），视图侧的回填。
        //   ★ 为何在视图侧也留一份：`SnapshotRows` 的纪律是"只读 RowRec 上已有的东西"。
        //     若两侧都回查模型，比对就恒等（同义反复）；存在这里，重建时若模型里的
        //     answer 没被搬上控件，B 侧就是空串 ⇒ 差异可见。这正是本步的判据来源。
        std::string answer;
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

    struct PendingPrompt {
        std::string clientId;
        std::string text;
        std::string status = "sending";  // sending / failed；确认后转入规范时间线并移除
        std::string failure;
    };

    struct PendingPromptView {
        std::string clientId;
        RowRec row;                         // 只属于临时视图，不进入 Session::rows/timeline
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

    // ═══════════════════════════════════════════════════════════════════════
    // ★★ 2026-10-20：**工作区是实体，不是会话上的一根字符串**。
    //
    //   改动前的模型：`Session::workspacePath` 一个字符串，每次成功 ack 就地覆盖。
    //   它能回答「**现在**在哪个目录」，但回答不了「这个会话**属于**哪个工作区」——
    //   因为覆盖之后，它过去待过哪儿一点痕迹都没留下。而「按工作区分会话」这个
    //   需求恰恰问的是归属，不是当前值。
    //
    //   ⇒ 工作是独立一等公民：有稳定 ID、有创建序、有自己的一条路径。
    //     会话只持有 `workspaceId` 这个**外键**。三条随之而来的性质：
    //     ① 归属与「当前路径」解耦：会话在 A 里，A 之后改了路径，归属不变；
    //     ② 会话显式切换工作区 = 改外键（真的搬家），而不是悄悄改写 A 自己；
    //     ③ 两个会话若在同一个目录，**共享**同一个工作区实体（按路径去重），
    //        而不是各自持有一份同值的字符串副本。
    //
    //   ★ 排序口径（用户 2026-10-20 裁决）：**创建序固定**，后建的显示在上。
    //     不因切换会话/新建会话而重排。故 `m_workspaces` 只追加（按创建序存），
    //     显示顺序在渲染时取逆序——**索引即创建序**，这是判据能比对的前提。
    struct Workspace {
        std::string id;           // 稳定 ID：`w1` / `w2` …（不是下标，删不掉、不随重排变）
        std::string path;         // core 回显并确认过的实际路径（未确认时为空）
        std::string requestedPath;// 最近一次请求的路径（含未确认/失败的那次，诊断用）
    };

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

        // ── v3 workspace / optimistic prompt state (transient prompts never enter timeline) ──
        bool workspaceCap = false;
        bool workspaceSyncStarted = false;
        bool workspaceSyncComplete = false;
        bool disconnected = false;
        bool autoPromptSent = false;
        size_t nextClientId = 1;
        size_t nextWorkspaceRequestId = 1;
        std::string workspacePath;          // 最后一次成功 ack 的 core 实际路径
        // ★★ 2026-10-20：归属外键。空 = 这个会话不属于任何工作区
        //   （replay / replay-history / new 三类没有 core，也就永远不会有工作区）。
        //   ★ 为什么与 `workspacePath` 并存：后者是「当前已确认的路径」（会被覆盖），
        //     前者是「属于哪一个实体」。两者在 live 会话上通常一致，但语义不同：
        //     `workspaceId` 为空而 `workspacePath` 非空是不可能的（路径只能由某个
        //     工作区确认得来），反之 `workspaceId` 非空而 `workspacePath` 为空则是
        //     “工作区存在但尚未确认”。
        std::string workspaceId;
        // ★ 继承口径（用户 2026-10-20 裁决）：新建 live 会话**继承活动会话的工作区**，
        //   而不是从 GUI 进程 cwd 重新开始。理由是 cwd 是进程属性、不是用户的意图；
        //   用户在 A 里选了 D:/Project 之后点「＋新建」，他的心智模型是“在同一处再开一个”，
        //   而不是“回到我刚启动图形界面的那个目录”。
        //   ⇒ 这两个字段只在“拿到 hello、还没发 workspace 请求”之间被读一次：
        //     `workspaceInheritedPath` = 要继承的路径（空 = 不继承，回落到 cwd）。
        std::string workspaceInheritedPath;
        // ★★ 2026-10-20：临时会话标志（用户裁决 m09443）。
        //   从工作区标题行的「＋」按钮拉起的会话是**临时**的：它还没有执行过任何
        //   任务（没发过 prompt）。一旦用户切走（`SwitchToSession` 到另一个会话），
        //   临时会话就被删除——因为它的存在理由（"在这个工作区开一个空会话"）
        //   已经被放弃。一旦发了 prompt（`PostPrompt` 成功），它转为正式会话。
        //   ★ 为什么不用「有没有消息」判断：`PostPrompt` 是唯一的"开始对话"入口，
        //     用它当转正条件最精确；而"有没有消息"会被 notice 行污染（新建会话时
        //     可能先收到 hello 通知）。
        bool temporary = false;
        std::string workspaceStatus = "等待 core 确认";
        std::string pendingWorkspaceId;
        bool pendingWorkspaceInitialSync = false;
        std::vector<PendingPrompt> pendingPrompts;

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
        // ★★ Step 5（2026-10-19）：**这个会话是从哪来的**。
        //   ★ 为什么必须记：在此之前"来源"是**隐式**的——`m_options.corePath` 空不空
        //     就决定了整个进程是 live 还是 replay，而所有会话共享同一份 options。
        //     一旦"打开历史会话"出现，同一个进程里可以同时有
        //     live / 启动时的 replay / 从磁盘点开的历史 replay 三种会话，
        //     而它们的 `source` 长得一样（都是 Source 指针）⇒ 无法区分。
        //   ★ `originKind` 取值：`"live"` / `"replay"`（启动时 `--replay`）/
        //     `"replay-history"`（Step 5 点开的历史）/ `"new"`（回放模式下新建的空会话）。
        //     空串 = 未分类（理论上不该出现；探针会把它当失败报出来）。
        //   ★ `originPath` 是**磁盘路径**，只有 replay 类有值：它同时是
        //     sidecar 状态文件的定位依据（见 `UiStatePath()`）。
        //   ★ 为什么不用 `replay != nullptr` 判来源：空的新建会话
        //     `replay == nullptr` 且 `source == nullptr`，但历史会话两者都非空——
        //     这确实分得开，可分不出"启动时 --replay"与"点开的历史"，
        //     而这两者的 sidecar 行为不同（前者是用户显式指定的夹具，后者才需要读状态）。
        std::string originKind;
        std::filesystem::path originPath;
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
    std::string PostPrompt(const std::string& text, bool allowBusyForProbe = false);
    bool RequestWorkspace(const std::string& path, bool initialSync = false);
    void ChooseWorkspaceFolder();
    void MaybeStartWorkspaceSync();

    // ── ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容 ─────────────
    // 数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
    // 回放会话没有工作区 ⇒ 此段显示「无工作区」。
    // ★ 只列一级（不递归）：侧栏宽度 172px，递归树会立刻溢出；且用户要的是
    //   「这个工作区里有什么」，不是文件浏览器。
    void RebuildProjectList();

    // ── ★★ 2026-10-20：临时会话（用户裁决 m09443）──────────────────────
    // 从工作区标题行的「＋」按钮拉起的会话是临时的：还没执行过任何任务。
    // 一旦切走就被删除；一旦发了 prompt 就转为正式。
    //   ★ `workspaceId` 参数：新会话直接归属该工作区（外键在创建时就设好，
    //     不等 ack——因为路径已经确认过了，工作区实体已经存在）。
    void NewTemporarySession(const std::string& workspaceId);
    // 切换会话时调用：如果当前活动会话是临时的且没发过 prompt，删除它。
    //   ★ 必须在 `SwitchToSession` 改 `m_activeIndex` **之前**调用（否则
    //     “当前活动会话”已经变成新会话了）。
    void MaybeDeleteTemporarySession();

    // ── ★★ 2026-10-20：工作区注册表（模型侧）────────────────────────────
    // `WorkspaceById()`：空 id 或查不到 ⇒ nullptr。
    //   ★ 查不到不抛也不修：调用方必须能区分“不属于任何工作区”与“属于一个
    //     已经不存在的 id”这两种情况；后者目前不可能（不提供删工作区），
    //     但一旦把 nullptr 当成“就在第一个工作区”，缺陷会安静地变成“归错桶”。
    Workspace* WorkspaceById(const std::string& id);
    const Workspace* WorkspaceById(const std::string& id) const;
    // `WorkspaceByPath()`：按路径查（用于去重）。匹配是**字符串完全相等**，
    //   因为入参已经是 core 回显的规范化路径（§3.9：绝对、`/` 分隔、无尾随 `/`）。
    //   不做 `equivalent()` 判等：那会把 `D:/a` 与 `d:/A` 当成同一处，而 core
    //   说的路径是它自己的规范化结果——GUI 没有资格替它改写大小写。
    Workspace* WorkspaceByPath(const std::string& path);
    // `AcquireWorkspace()`：按路径拿一个工作区，没有就**新建**（追加到表尾
    //   ⇒ 创建序即下标序）。返回非拥有指针，永不为空（path 为空时返回 nullptr）。
    //   ★ 这是唯一的创建入口：只有“core 真的确认了某条路径”时才调用它。
    Workspace* AcquireWorkspace(const std::string& path);
    // 把会话挂到指定工作区（外键赋值；不改路径、不发协议）。
    void AssignSessionWorkspace(Session& session, Workspace* workspace);
    // 新会话应当继承的路径（活动会话所属工作区的**已确认**路径；无可继承 ⇒ 空）。
    //   ★ 只读活动会话的 `workspaceId`，不读 `workspacePath`：后者可能属于
    //     一个尚未确认的请求（“等待 core 确认…”），继承一个未确认路径等于
    //     把不确定性复制给新会话。
    std::string InheritedWorkspacePath() const;
    void MaybeSendAutoPrompt();
    bool HasUnconfirmedPrompt() const;
    void FailPrompt(const std::string& clientId, const std::string& reason);
    void FailPendingOperations(const std::string& reason);
    void MaterializePendingPrompts();
    // ★★ Step 3：把"已答"连同**答的是什么**一起画出来。
    //   ★ 为什么必须抽成函数：首答（`MarkAnswered`）与重建（`MaterializeOne`）必须
    //     走**同一条**视觉路径。若重建分支只写 `SetEnabled(false)`，同一条目在
    //     "刚答完"与"重建后"就长得不一样，而 A==B 只比文字与 expand/answered 位，
    //     看不见配色差异 ⇒ 又是一次"比对全等但屏幕不一样"（同 0c 的 buildTitle）。
    //   ★ 做法：**选中的那个钮保持常态色，另一个灰化**（ECDI 不做 disabled 绘制，
    //     灰化是调用方责任 —— 先例 `PromptBar::ApplySendStyle`）。于是"当时允许还是
    //     拒绝"变成**像素**，不用新增控件、不动布局，且能被探针经 `GetTextColor()`
    //     读回（ECDI 无 style getter，文字色是唯一可读回的标记 —— 同侧栏选中态）。
    //   ★ 幂等：重建期对已答条目再跑一次无副作用。
    void ApplyAnsweredLook(RowRec& rec, const std::string& choice);

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

    // ── ★★ Step 1（2026-10-19）：侧栏会话列表 —— 第二条「模型 → 视图」通道 ────
    // 为什么侧栏也要能被重建：0c 只让**转录区**可重建，理由写在 `RebuildTranscriptView`
    //   的注释里（令牌是别名，事后换不了色）。而侧栏的会话条目是**从 m_sessions 派生的**
    //   ——会话增删/切换后它必然要跟着变。若它只能"就地改一改"，就会出现"列表说 2 个
    //   会话、点下去却切到第 1 个"这种**视图与模型分叉**，而现有仪器（`SnapshotRows`
    //   只走 `m_sess->rows`）完全看不见。故这里把同一条纪律搬到侧栏：
    //   列表是**纯函数** `m_sessions → 控件树`，任何变化都靠重建收敛。
    //
    // `BuildSessionRow()`：建**一行**会话条目（纯函数，不改任何状态）。
    //   ★ 行根是 `Button`（不是 Panel）：`Panel::ContainsPoint` 恒 false
    //     （`Panel.cpp:34-40`），而"可点"要求节点自身命中。用 Button 还有一个
    //     非预期的收益——ECDI Phase 35 起 Button 在 Down/Up 两个阶段都 `SetHandled()`
    //     （`Button.cpp:126-181`），所以行内那个"×"子按钮点下去**不会**再触发行本身的
    //     切换回调（父按钮收不到 Up）。这条依赖是**框架契约**，不是巧合：
    //     `EventConsumptionTests.cpp` 里有嵌套点击的回归用例。
    //   ★ `index` 只是**建行时**的下标，回调里**不捕获它**——删除会让下标整体前移，
    //     捕获下标就会"点了第 2 行却切到第 3 个会话"。回调捕获的是 `Session*`（稳定，
    //     `unique_ptr` 持有），点击时再线性查一次它的当前下标（见 `SwitchToSessionByPtr`）。
    std::unique_ptr<ECDI::Widget> BuildSessionRow(size_t index, bool active);
    // `BuildSessionGroupRow()`：建**一个工作区分组标题行**（纯函数，不改状态）。
    //   ★ 行根是 `Panel` 而不是 `Button`——这正是“标题行点不动”的**结构性**保证：
    //     `Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），故它从不参与命中。
    //     若改用 `Button` + `SetEnabled(false)`，“不可点”就会变成一个可以被后人
    //     取消的开关，而 Disabled 同时会 gate 焦点收集，语义比这里需要的更多。
    //   ★ 它**不承载**任何会话模型：标题行不是会话，探针按行的 `kind` 区分两类
    //     （见 `SnapshotSessionList` 的第 5 段），否则“分组标题”会被当成“会话行”
    //     参与 selected 判定，读数全是假的。
    std::unique_ptr<ECDI::Widget> BuildSessionGroupRow(const std::string& title,
                                                       const std::string& workspaceId = std::string());
    // `GroupTitlesForDisplay()`：算出分组标题的**显示序**（返回 `m_workspaces` 的下标）。
    //   ★ 用户裁决（m08553）：工作区列表序固定为**创建序**，且后创建的排在上面
    //     （A 先建 ⇒ A 在 B 下面），不因切换会话或新建会话而改变。
    //     `m_workspaces` 只追加 ⇒ 下标即创建序 ⇒ 显示时取逆序。这里不排序、不按
    //     “最近使用”重排：任何动态排序都会让同一个工作区在两次刷新之间换位置。
    std::vector<size_t> GroupTitlesForDisplay() const;
    // `RebuildSessionList()`：拆掉列表内容视图的全部子控件，按 `m_sessions` 重建。
    //   ★ 只碰列表那一个 Panel，**不碰**侧栏其余部分、更不碰转录区/检查器
    //     ⇒ CountingPanel 与 HUD 指针全部存活，评估读数不中断。
    //   ★ `orderOverride` **只供负对照**：给一组下标就按那组次序建（探针喂逆序进来，
    //     看它能不能被抓出来）。**只改次序这一个自变量**——选中态仍按真实活动会话
    //     判定。若这里顺手把选中态也造没了，负对照报出的差异就混着两种原因，
    //     分不清探针抓到的是次序错还是选中错（单变量纪律；REQ-01/03/04 曾因
    //     违反它被误判后反转为 harness 侧）。
    void RebuildSessionList(const std::vector<size_t>* orderOverride = nullptr);
    // `SnapshotSessionList()` / `CompareSessionList()`：侧栏的 A==B 判据。
    //   ★ 与转录区的 `SnapshotRows` 同款形状（字段用 '\x1f' 分隔、逐字段报第一处差异），
    //     但**独立一套**：两者比对的对象不同、失败语义也不同（转录错 = 重建丢状态；
    //     侧栏错 = 视图与模型的**对应关系**错了）。混用一套会把两类缺陷压成一个数字。
    std::vector<std::string> SnapshotSessionList() const;
    // ★ 比对结果单独成结构（而不是像转录区那样直接写日志）：
    //   负对照这条**要求"不匹配"**，若比对函数自己往日志里塞 matched，调用点就分不清
    //   "这次不匹配是失败还是预期"。返回结构 + 由调用点决定怎么记账，判据才可读。
    struct SidebarCompare {
        bool matched = true;
        std::string firstDivergence;   // 第一处差异（匹配时为空）
    };
    SidebarCompare CompareSessionList(const std::vector<std::string>& before,
                                      const std::vector<std::string>& after) const;
    // `DeleteSessionAt()`：删一个会话（**唯一入口**，点击与探针共用）。
    //   ★ 三条不可换序的规矩：
    //     ① **先**把要删的会话从"活动指针"上摘下来（若删的就是活动的），再动容器；
    //        否则 `m_sess` 会在 `erase` 之后短暂悬垂。
    //     ② 删活动会话 ⇒ 先切到邻居（`SwitchToSession` 内部会重建转录区），
    //        切换**完成之后**才允许容器 `erase`——旧 Session 在切换期间必须活着。
    //     ③ 删完若 `m_activeIndex` 变了，必须修正（erase 会把后面的元素整体前移）。
    //   ★ **禁止删最后一个会话**：会话数是 1 时直接返回 false。理由不是"实现麻烦"——
    //     而是空会话列表会让 `m_sess == nullptr`，而全仓 200+ 处调用点都假定它非空
    //     （`OnReplayTick` 首行就解引用它）。要支持"零会话"是一个**独立的**架构决定
    //     （所有 `m_sess->` 都要加判空），不该被"加个删除按钮"顺手带进来。
    //   ★ 回放模式下被删会话的 `ReplaySource` 随 `unique_ptr` 析构；live 模式下
    //     `ChildSource::~ChildSource()` 会 `Stop()` 收尸（`ChildSource.cpp:26`）。
    //     **不在这里**手工 Stop——那会变成第二份收尸逻辑。
    bool DeleteSessionAt(size_t index);
    // `SetSidebarVisible()`：侧栏显隐（**唯一入口**，顶栏按钮与探针共用）。
    //   ★ 三条纪律：
    //     ① 瞬时切换：只用 `SetVisible(false)` + 父容器 `Arrange()`，
    //        **不要**套 `CollapsiblePanel` 的 200ms 动画——中间帧会让
    //        paints/frameCount 多出与被测功能无关的抖动（仪器污染实验，条 57③）。
    //        `Widget::SetVisible` 自身只在**值变化**时 Invalidate（Widget.cpp:427-446，
    //        同值守卫是防 TextBox 每帧自激励重绘的正确性要求），故一次切换 = 一次重绘。
    //     ② 必须重排**父容器**：布局跳过隐藏子（C-VIS-1）并把它们停泊到负区，
    //        但"跳过"只在 Arrange 时发生 ⇒ 不 Arrange 的话中栏不会吃到那 200px。
    //     ③ 必须接着 `Relayout()`：中栏变宽 ⇒ 转录行宽变 ⇒ 自动换行要重算。
    //        否则文字按旧宽度折行、留在更宽的气泡里（看着像内容变短了）。
    void SetSidebarVisible(bool visible);
    // `SwitchToSessionByPtr()`：按**会话身份**切换（侧栏点击用）。
    //   ★ 为什么不能直接传下标进来：行是建列表那一刻生成的，下标会因删除而失效。
    //     指针是稳定的（`unique_ptr` 持有、从不搬动），点击时现查下标才是对的。
    bool SwitchToSessionByPtr(const Session* target);

    // ── ★★ Step 5（2026-10-19）：打开历史会话 ────────────────────────────
    // 历史 = 磁盘上的 `protocol/fixtures/**/*.jsonl`。点开 = **新建一个 replay 会话**
    //   并立刻抽干到末尾（用户点开历史想看的是"那次对话长什么样"，不是从头再播一遍）。
    // ★ 为什么是"回放 + 跳到末尾"，而不是"一次读到底、GUI 自己造 Block 塞 timeline"：
    //   后者会让 GUI 变成**第二个转录作者**，与 `PROTOCOL.md:244`「GUI 不产生转录块，
    //   唯一作者是 core」直接冲突；而且会顺手吞掉坏文件的读数（`seqGaps`/`unknownKinds`/
    //   `protocolIncompatible` 全都在**消费路径**上产生，绕开消费就没有读数）。
    //   跳到末尾只是**速度**参数，协议契约一个字没动。
    // ★ 为什么要点：`protocol/fixtures/` 下每个文件都有 sha256 基线 ⇒ 历史区**只列不删**。
    //   一旦 UI 能删它们，所有回归基线就都开口子了。
    std::filesystem::path HistoryRoot() const;
    // 递归扫 `HistoryRoot()` 下的 `*.jsonl`（跳过 `__pycache__`），按相对路径排序。
    std::vector<std::filesystem::path> FindHistoryFiles() const;
    // 建一行历史条目（纯函数）。行根同样是 Button（Panel 点不到）。
    //   ★ 回调**只置脏**（`m_historyOpenPending` + `m_historyDirty`），不在这里打开：
    //     ① 打开会 `RemoveChild` 掉侧栏会话列表与转录区，而本按钮正在被派发链持有；
    //     ② 抽干 2000 行夹具是**重活**，不能让消息循环在鼠标抬起里卡住。
    std::unique_ptr<ECDI::Widget> BuildHistoryRow(const std::filesystem::path& file);
    void RebuildHistoryList();
    // 打开一个历史文件：建会话 → 切过去 → 抽干 → 读 sidecar → 重建视图。
    //   ★ 失败必须如实说（回放源加载失败就 AddNoticeRow），不建半好会话。
    bool OpenHistorySession(const std::filesystem::path& file);
    // sidecar：`<fixture>.jsonl.ui.json`，与夹具同目录。
    //   ★ 为什么不写进 jsonl 本身：那会改掉夹具字节，sha256 基线与 `validate.py` 全部失效；
    //     协议也没有"GUI 往会话文件里写"的方向。
    //   ★ 为什么不用 `evals/` 里那种 `<x>.jsonl.json`：那是**评估摘要**（旧产物），
    //     与本状态文件语义完全不同，同名会让两者互相覆盖。
    std::filesystem::path UiStatePath(const Session& s) const;
    // 读/写 GUI 侧状态（answers / expanded）。**协议一行不动**：这些是视图决定，
    //   不是转录内容。缺失 ⇒ 返回 false，调用方必须如实提示"此会话没有决策记录"。
    bool LoadUiState(Session& s);
    void SaveUiState(const Session& s);
    // 把 sidecar 写进模型之后，还要把同一批状态写进**当前视图行**——
    //   `RebuildTranscriptView` 的第一步是 `SyncRowViewState`（视图→模型），
    //   若视图行还全是 false，它会立刻把刚读进来的展开态/已答态覆盖掉。
    void ApplyUiStateToView(Session& s);

    void Relayout();
    void FollowTailIfPinned();
    void RequestRepaint();
    void CacheEvalMetrics();
    void RefreshHud();
    void WriteSummary();

    // ★★ Step 4 / 0e（2026-10-19）：**整树重建**——运行期切主题的唯一可行路径。
    //   ★ 为什么必须整树：`Theme.h:19-20` 的令牌是引用别名，颜色值在 `BuildUi`
    //     时就拷进了每个控件的 `PanelStyle`/`ButtonStyle`（ECDI 的 StyleField 是
    //     值语义、没有"重新 ApplyTheme"的公开入口）⇒ 改 `g_theme` 不会让任何
    //     已存在的控件换色。**唯一能换色的办法是让树按新令牌重新长一遍。**
    //   ★ 六件事一件都不能少（顺序即下面函数体的顺序）：
    //     ① `SyncRowViewState()` —— 视图侧的展开/已答先写回模型，否则重建即回退
    //        （0c 踩过的坑；`RebuildTranscriptView` 内部也调，但这里动的是整棵树，
    //         连 `m_scroll` 都要换，必须在外层再保一次）；
    //     ② 快照评估计数器（`CountingPanel::PaintCount/FrameDurationsUs`）——
    //        重建会析构旧面板、新面板从 0 数。不续接 ⇒ 一次主题切换让 paints 归零，
    //        "仪器被实验打坏"；
    //     ③ 记住输入框文本与焦点对象（重建后按文本恢复；焦点只能重新指向新控件）；
    //     ④ 拆 root 的全部子节点（`RemoveChild` 会经 `Window::ForgetSubtree` 清掉
    //        焦点/捕获/hover —— 这正是必须重设焦点的原因）；
    //     ⑤ `SwitchTheme` + `BuildUi`（`BuildUi` 内部会重设 layout、重建 CaptionBar
    //        与三段、填首帧会话列表、`Arrange`）；
    //     ⑥ 把计数器写回新面板、恢复输入框文本、把焦点交给新输入框，
    //        `m_uiRebuilds` 自增，`m_hudDirty = true`，`RequestRepaint()`。
    //   ★ **不做**的例外：不保留旧 `CountingPanel`（那会让判据退化成"除了那棵树都行"）。
    void RebuildUi();
    // Step 4 探针入口（`--theme-probe`）。
    void RunThemeProbe();
    void RunWorkspaceProbe();

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

    // ★★ 2026-10-20：工作区注册表。`m_workspaces` 只追加 ⇒ **下标即创建序**
    //   （用户裁决：显示顺序固定为创建序，后建在上；不因切会话/新建而变）。
    //   ★ 为什么不放 `Session` 里：同一目录下的两个会话应当**共享**一个工作区，
    //     否则侧栏会画出两个同路径的标题行。去重口径 = 路径完全相等（规范化后）。
    //   ★ 与 `m_sessions` 同一条不变量：`unique_ptr` 持有 ⇒ 地址稳定；
    //     `Session::workspaceId` 存的是**字符串 ID**而不是裸指针，因为工作区
    //     永远不会被删（不提供删除 UI），但也正因为不会搬动，字符串 ID 就够用且
    //     不怕悬垂——这是比裸指针更弱的耦合，换来的是“模型可以单独 dump 出来比对”。
    std::vector<std::unique_ptr<Workspace>> m_workspaces;
    size_t m_nextWorkspaceId = 1;                  // `w1` / `w2` …（单调递增，不复用）

    // 活动会话在 m_sessions 里的下标（会话头 / 侧栏显示用；-1 = 尚无会话）
    size_t m_activeIndex = static_cast<size_t>(-1);

    HarnessApplication* m_application = nullptr;   // Run() 栈上对象，非拥有
    ECDI::Window* m_window = nullptr;              // Application 所有，非拥有
    CountingPanel* m_counting = nullptr;           // Widget 树所有，非拥有
    ECDI::PaintProbe* m_probe = nullptr;           // --cull-probe 时非拥有（Run 栈上 services 所有）
    ECDI::MeasureProbe* m_measureProbe = nullptr;  // 同上（测量链探针）
    ECDI::ScrollView* m_scroll = nullptr;          // 非拥有
    ECDI::Widget* m_content = nullptr;             // ScrollContent，非拥有
    std::vector<PendingPromptView> m_pendingPromptViews; // 当前活动会话的 UI-only 气泡视图
    bool m_inspectorVisible = true;
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
    // ★★ Step 1（2026-10-19）：侧栏列表待重建。
    //   为什么需要这个**延迟标志**（而不是点完立刻重建）：删除/切换的回调是在
    //   `Application` 的**事件派发栈**上执行的，而重建会 `RemoveChild` 拆掉整棵列表
    //   —— 其中就包含**正在执行回调的那个 Button**（点「×」时它是行的子节点，
    //   点行本身时它是行根）。回调返回后 `RaiseClick()` 还要读 `this`（`m_onClick` 刚
    //   执行完，栈帧仍持有成员）⇒ 在回调里同步拆树 = UAF。
    //   框架的 `RequestRemove()` 只保护你调它的那一个节点，护不住"我顺手重建了整个列表"。
    //   ⇒ 规矩：回调**只改模型 + 置脏**，重建留到 `OnReplayTick`（timer 回调不在派发栈上）。
    //   ★ 这条规矩在结构上不给"同步重建"留位置，比"记得用 RequestRemove"更抗腐化。
    bool m_sidebarDirty = false;
    // ★ Step 2：侧栏显隐的**模型侧**状态。
    //   ★ 为什么必须是一个成员而不是"读控件"：`RebuildSessionList`/重建类操作会
    //     重排控件树，若显隐只存在于控件上（`IsVisible()`），任何一次重建都可能
    //     把用户的收起状态丢掉——这正是 0c 踩过的坑（视图状态只存在控件里，
    //     重建即丢失）。模型侧留一份，重建后按它恢复。
    bool m_sidebarVisible = true;
    // ★★ Step 4（2026-10-19）：整树重建待执行。
    //   与 `m_sidebarDirty` 完全同族、同一个理由：顶栏「主题」钮的回调跑在
    //   `Button::RaiseClick` 的栈上，而重建要 `root.RemoveChild` 掉**这个按钮自己**
    //   ⇒ 回调里同步重建 = 析构正在执行的对象。故回调只翻 `g_themeLight` + 置这个标志，
    //   真正的重建留到 `OnReplayTick` 首段（timer 回调不在派发栈上）。
    //   ★ 放在 `m_sidebarDirty` 处理**之后**、`if (!m_sess->source) return;` **之前**：
    //     主题切换不该被"活动会话没有 source"挡住（空会话也要能换主题）。
    bool m_uiRebuildPending = false;
    // ★★ Step 5（2026-10-19）：历史区待重建 / 待打开。
    //   与上面两个标志同族、同一理由：历史行的点击回调跑在派发栈上，而"打开"要
    //   新建会话 + 切会话（`SwitchToSession` → `RebuildTranscriptView` 会拆掉转录区
    //   全部行，侧栏列表也要重画）⇒ 回调里同步做就是析构正在执行的对象。
    //   ★ 与 `m_sidebarDirty` 分成两个标志，而不是一个：重建列表是**纯渲染**，
    //     打开文件是**真副作用**（读盘 + 抽干上千行）。混成一个标志就没法表达
    //     "只刷新列表、别打开任何东西"这个必要状态（探针的 list 相正是它）。
    bool m_historyDirty = false;
    std::filesystem::path m_historyOpenPending;    // 非空 ⇒ 下一 tick 打开它
    bool m_historyScanned = false;                 // 文件清单缓存过一次（避免每 tick 扫盘）
    std::vector<std::filesystem::path> m_historyFiles;
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

    // ★★ Step 1（2026-10-19）：侧栏列表的 A==B 探针。
    //   形状照抄 `RunSessionProbe`（同步跑完即 Exit，不跨 tick）——因为侧栏重建
    //   不涉及动画、不涉及折叠，没有"要等 400ms settle"的东西。
    //   ★ 唯一需要等待的是 `NewSession()`：回放模式下它会 `AddNoticeRow` + `Relayout`，
    //     但这些全是同步的，故整条探针可以一轮跑完。
    struct SidebarProbeRec {
        std::string phase;          // baseline / rebuild / rebuild-again / negative /
                                    // new / deleted
        size_t rowCount = 0;        // 列表里的行数（控件侧）
        size_t sessionCount = 0;    // 模型侧会话数
        size_t activeIndex = 0;     // 模型侧活动下标
        size_t selectedRows = 0;    // 行里选中态为真的个数（invariant：0 或 1）
        size_t selectedIndex = 0;   // 选中的那一行的下标（无选中 = (size_t)-1）
        size_t transcriptRows = 0;  // 活动会话的转录行数（删除判据的负对照）
        // ★ Step 2：收起/展开臂的两个读数。
        //   midWidth 是**中栏**宽度：侧栏 200px 固定，收起后中栏必须宽出正好 200。
        //   它是"收起真的发生了"在布局层的唯一机读证据——只看 IsVisible() 会被
        //   "隐藏了但没重新 Arrange、中栏没吃到那 200px"骗过（那就是 0c 的
        //   "重建了但没重排"同族缺陷）。
        int midWidth = 0;           // 中栏（CountingPanel）宽度
        bool sidebarVisible = true; // 侧栏可见性
        bool matched = true;        // 与对照快照逐行一致（仅 rebuild 类 phase 有意义）
        std::string firstDivergence; // 第一处差异的行摘要（匹配时为空）
    };
    std::vector<SidebarProbeRec> m_sidebarProbeLog;
    bool m_sidebarProbeDone = false;
    void RunSidebarProbe();

    // ★★ Step 3（2026-10-19）：ask 答案内容（`answer`）进模型的取证。
    //   形状同 `RunSessionProbe`：同步跑完即 Exit，不需要跨 tick（无动画参与）。
    struct AnswerProbeRec {
        std::string phase;          // baseline / answered / reclick / forward / negative / restored
        size_t askRows = 0;         // 屏幕上的权限门行数
        size_t answeredRows = 0;    // 其中已答的（RowRec::answered）
        size_t modelAnswers = 0;    // 模型里 `answer` 非空的条目数
        std::string answerModel;    // 模型侧此刻的答案（"allow"/"deny"/空）
        // ★ 控件侧独立读数：**从两个钮的文字色反推**答案（不查模型）。
        //   这是本探针非循环的关键——模型说 allow、屏幕也说 allow，才叫"答的结论
        //   真的到了用户眼前"。ECDI 无 style getter，`GetTextColor()` 是唯一出口。
        std::string answerOnScreen; // allow / deny / unknown / (空 = 两钮都在常态)
        bool buttonsDisabled = false;  // 已答行的两个钮是否都置灰（不可再点）
        bool matched = true;           // 行快照 A==B（仅 forward/negative/restored 有意义）
        std::string firstDivergence;
    };
    std::vector<AnswerProbeRec> m_answerProbeLog;
    bool m_answerProbeDone = false;
    void RunAnswerProbe();

    // ★★ Step 4 / 0e（2026-10-19）：整树重建 + 运行期主题切换的取证（--theme-probe）。
    //   ★ 本探针与前面五个**形状不同**：那些探针测的是"局部重建"，
    //     而这里要测的是"整棵树被拆掉再长出来之后，**所有**用户可见状态还在不在"。
    //     故它的比对面不是行快照，而是**四路并联**：
    //       ① 侧栏列表（`SnapshotSessionList` —— 行数/标签/选中态/几何）；
    //       ② 转录区（`SnapshotRows` —— 行数/展开态/已答/答案/标题/文本）；
    //       ③ 输入框文本（`m_promptBar->input()->GetText()`）；
    //       ④ 状态标记（活动会话下标 / 会话数 / 侧栏可见性 / 主题是否浅色）。
    //     ★ 少任何一路，"重建后状态没丢"这句话就只覆盖了一部分树。
    //   ★ **评估读数不进比对面，它是独立断言**：`paints` 必须**恰好等于**重建前的值。
    //     相等才是对的——重建期间不该有任何重绘（拆树不画、建树不画、只有末尾
    //     `RequestRepaint` 那一帧才画）。若变小 ⇒ 计数器被清零（`SeedCounters` 没接）；
    //     若变大 ⇒ 有东西在测量窗口里偷偷重绘了（比如折叠动画、或 HUD 每 tick 刷）。
    struct ThemeProbeRec {
        std::string phase;       // baseline / rebuild / rebuild-again / toggle /
                                 // toggle-back / negative / restored
        bool light = false;      // 当前主题是否浅色（重建必须真的换色才算"换过"）
        size_t uiRebuilds = 0;   // 累计整树重建次数（每次 +1，是"重建真的发生了"的读数）
        // 评估计数器（重建前后必须**逐位相等**）
        size_t paints = 0;
        size_t frames = 0;
        size_t frameUsTotal = 0; // frameDurationsUs 的长度——续接口是否真的把历史搬回来了
        // 四路比对的合并结果（仅 rebuild 类 phase 有意义）
        bool matched = true;
        std::string firstDivergence;
        // 分路读数，方便失败时定位是哪一路断的（不为 0 表示该路比对失败）
        int sidebarRows = 0;
        int transcriptRows = 0;
        std::string inputText;
        bool sidebarVisible = true;
        size_t activeIndex = 0;
        size_t sessionCount = 0;
    };
    std::vector<ThemeProbeRec> m_themeProbeLog;
    bool m_themeProbeDone = false;

    // ★ 整树重建的比对面：一次 snapshot 抓齐四路。结构独立于 `ThemeProbeRec`，
    //   因为 baseline 与每次重建后都要抓，且**同一个量具**两侧共用。
    struct UiSnapshot {
        std::vector<std::string> sidebar;   // SnapshotSessionList()
        std::vector<std::string> transcript;// SnapshotRows()
        std::string inputText;              // 输入框文本
        bool sidebarVisible = true;
        bool light = false;
        size_t activeIndex = 0;
        size_t sessionCount = 0;
        size_t paints = 0;
        size_t frames = 0;
        size_t frameUsTotal = 0;
    };
    UiSnapshot SnapshotUi() const;
    // 返回四路比对的合并结果；`firstDivergence` 带**路名前缀**
    //（`sidebar:` / `transcript:` / `input:` / `state:`），失败时一眼看出断在哪。
    SidebarCompare CompareUi(const UiSnapshot& before, const UiSnapshot& after) const;

    // ★ 跨 tick 状态机（与 `m_rebuildProbeStep` 同款）：`toggle` 相必须走真实点击，
    //   而点击回调只置 `m_uiRebuildPending`，真正的重建发生在**下一个 tick 的开头**
    //   ⇒ 探针不能在一步里做完，必须等重建真的落地再读数。
    int m_themeProbeStep = 0;
    std::chrono::steady_clock::time_point m_themeProbeNextAt;
    UiSnapshot m_themeUiBefore;        // baseline 的四路快照（所有 rebuild 相的比对面）
    size_t m_themePaintsBefore = 0;    // 同一 tick 内 `RebuildUi` 前后的 paints（必须相等）
    size_t m_themeFramesBefore = 0;

    // ★ 整树重建的累计计数。**只在 `--theme-probe` 打开时进摘要**（默认 false ⇒
    //   摘要逐字节不变，这是本仓铁律）。计数放在成员而不是 `ThemeProbeRec` 里，
    //   因为 `rebuild` 相要读"重建前后两次"的值。
    size_t m_uiRebuilds = 0;

    // ★★ Step 5（2026-10-19）：打开历史会话的取证（--history-probe）。
    //   形状同 `RunThemeProbe`（跨 tick）：`open` 相要点真实的历史行按钮，
    //   而打开被延迟到下一个 tick（派发栈 UAF），故不能在一步里做完。
    struct HistoryProbeRec {
        std::string phase;          // list / open / content / at-end /
                                    // sidecar-missing / sidecar-write / sidecar-roundtrip
        size_t filesOnDisk = 0;     // 磁盘上真实文件数（探针自己扫一遍，不只信 UI）
        size_t rowsOnScreen = 0;    // 历史区行数（控件侧）
        std::string openedLabel;    // 打开后的 `Session::label`
        std::string originKind;     // 打开后的 `Session::originKind`
        size_t blocks = 0;          // 打开后活动会话的块数
        size_t deltas = 0;
        size_t messages = 0;
        bool atEnd = false;         // `replay->Empty()`（真的跳到末尾了）
        size_t askRows = 0;         // 屏幕上的权限门行数
        size_t answeredRows = 0;    // 其中已答的
        size_t modelAnswers = 0;    // 模型里 answer 非空的条目数
        // ── sidecar 三态（缺失 / 写入 / 读回）──
        bool sidecarExisted = false;   // 探针开始前磁盘上是否已有该 sidecar
        bool sidecarWritten = false;   // 写侧真的落盘了吗
        bool sidecarRemoved = false;   // 收尾是否把探针造的文件删掉（不留脏工作区）
        bool matched = true;
        std::string firstDivergence;
    };
    std::vector<HistoryProbeRec> m_historyProbeLog;
    bool m_historyProbeDone = false;
    void RunHistoryProbe();
    // 探针专用：直接回放某个文件到末尾并返回读数（**不**动活动会话）。
    //   ★ 用临时 `ReplaySource` + `Transcript`，与 GUI 的消费路径同源——
    //     这是 `content` 相"两条独立路径喂同一文件、读数必须一致"的一半。
    //   ★ 另一半是打开后的活动会话读数：两者都用 `Transcript::OnLine` 消费，
    //     若"打开"这条路径偷偷漏行/重放行，此处立刻不等。
    struct ReplayDigest {
        bool loaded = false;
        size_t blocks = 0;
        size_t deltas = 0;
        size_t messages = 0;
    };
    ReplayDigest DigestReplayFile(const std::filesystem::path& file) const;
    // 跨 tick 状态机的步号与下次允许时刻（同 `RunThemeProbe` 的形状）。
    int m_historyProbeStep = 0;
    std::chrono::steady_clock::time_point m_historyProbeNextAt;
    // 探针点的那一行（记住**文件路径**而不是控件指针：打开后整棵侧栏会被重建）。
    std::filesystem::path m_historyProbeFile;
    // 探针自己算出的真值（`content` 相的双路比对用）：
    ReplayDigest m_historyProbeDigest;

    struct WorkspaceProbeRec {
        std::string phase;
        std::string clientId;
        std::string path;
        std::string pendingId;
        size_t canonicalMatches = 0;
        bool sendEnabled = false;
        bool matched = true;
        bool expectedMismatch = false;
        // ★ §7 判据的读数：控件上的实际文案，以及「去掉后缀 == 正文且无换行」是否成立。
        std::string bubbleText;
        bool bubbleShapeOk = false;
    };
    std::vector<WorkspaceProbeRec> m_workspaceProbeLog;
    // ★ 当前帧的 §7 读数（snap 时拷入 rec；只用于探针）
    std::string m_workspaceProbeBubbleText;
    bool m_workspaceProbeBubbleShapeOk = false;
    int m_workspaceProbeStep = 0;
    bool m_workspaceProbeDone = false;
    std::string m_workspaceProbeOriginalPath;
    std::string m_workspaceProbeTargetPath;
    std::string m_workspaceProbeFailPath;
    std::string m_workspaceProbeWorkspaceId;
    std::string m_workspaceProbeAcceptedId;
    std::string m_workspaceProbeRejectedId;
    std::chrono::steady_clock::time_point m_workspaceProbeDeadline{};

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
