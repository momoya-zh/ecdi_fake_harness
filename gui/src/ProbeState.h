#pragma once
// 探针状态（`--xxx-probe` 的读数与跨 tick 状态机）。
//
// ★ 2026-10-25（Stage 4 仪器收口）：从 `HarnessApp` 收进这里，成员**只去掉 `m_` 前缀**，
//   类型 / 初始化器 / 行内注释 / 相对顺序一字未改（改名脚本逐文件用「逆映射后与改前全等」验证）。
//
// ★ 为什么值得单独放一个头：
//   ① `HarnessApp` 的私有成员里，探针状态与产品状态原本是**交错**的（例如 rebuild 探针的
//      七个快照字段夹在 `m_historyDirty` 和 `m_relayoutPulse` 之间），读产品逻辑时全是噪声；
//   ② 现在全仓 `grep -n "m_probeState\."` 就是**仪器状态接触面**的完整清单，
//      `grep -v` 之后剩下的才是产品状态——这条边界以前只能靠人记；
//   ③ 探针的「为什么必须有 / 判据是什么」注释仍留在 `App.h` 的 `Run*Probe()` 声明上方，
//      它们讲的是**行为**不是数据，跟函数放一起才对。
//
// ★ 范围判据（三条，逐条 grep 核实）：
//   A. 名字含 `Probe` 的成员（`m_measureProbe` 除外——那是 ECDI 的测量对象，产品侧 HUD 读数链）；
//   B. `m_ap*` 家族（REQ-03 异步投递读数，名字没带 Probe）；
//   C. 名字不带 Probe 但**读写点全在探针门控内**的草稿：`m_reasoningTitleStreaming`、
//      rebuild 的七个快照字段、theme 的三个 before 快照、`m_hudAudit` / `m_hudTouched`。
//
// ★ 刻意**不搬**（读写点在产品路径上，属产品状态，别顺手挪进来）：
//   `m_measureProbe`（`--cull-probe` 下由 ECDI 创建）、`m_uiRebuilds`（`RebuildUi` 无条件自增，
//   探针只是读它）、`m_jumpLastOffsetY`、`m_tooltipAnchor`、`m_tooltipPending`、
//   `m_evalPaints` / `m_evalFrames` / `m_evalMaxUs` / `m_evalSumUs` / `m_evalLastUs` /
//   `m_invalidations` 等**产品**读数（它们进摘要与探针无关）。

#include "ProbeTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ProbeState {
    std::filesystem::path menuProbeExportPath;

    std::vector<StateProbeRec> stateProbeLog;
    bool stateProbeLastValid = false;            // 是否已有「上次」可比
    bool stateProbeLastSend = true;
    bool stateProbeLastStop = false;

    std::vector<HitProbeRec> hitProbeLog;
    int hitProbeSendCalls = 0;      // SendPrompt 进入次数（探针期间唯一观测通道）
    bool hitProbeActive = false;    // 探针执行中：SendPrompt 只计数、零副作用
    bool hitProbeDone = false;      // 已跑过（夹具 settle 后只跑一次）

    std::vector<ReasoningProbeRec> reasoningProbeLog;

    // ★★ 协议 v5（2026-11-04）：子代理缩进/生命周期（--agent-probe）。同步探针，一相。
    std::vector<AgentProbeRec> agentProbeLog;
    bool agentProbeDone = false;

    // ★★ 协议 v5（2026-11-05）：ask 提案卡（--proposal-probe）。同步探针，一相。
    std::vector<ProposalProbeRec> proposalProbeLog;
    bool proposalProbeDone = false;

    int reasoningProbeStep = 0;
    std::chrono::steady_clock::time_point reasoningProbeNextAt;
    bool reasoningProbeDone = false;

    std::string reasoningTitleStreaming;   // 建行时读到的标题（应为 ▼ 思考中）

    std::vector<RebuildProbeRec> rebuildProbeLog;

    std::vector<std::string> rebuildProbeBefore;
    size_t rebuildNoticeCount = 0;       // 模型里记下的通知行数（承重性证据）
    int rebuildScrollBefore = 0;         // 重建前的滚动偏移（判"有没有跳回顶部"）

    std::string rebuildTitleSample;      // 首个样本点击**前**的标题
    std::string rebuildTitleAfter;       // 同一样本点击**后**的标题
    int rebuildTitleEmptied = 0;         // 点击后有标题被清空的次数

    int rebuildExpandedSamples = 0;
    int rebuildAnsweredSamples = 0;
    bool rebuildProbeDone = false;
    int rebuildProbeStep = 0;
    std::chrono::steady_clock::time_point rebuildProbeNextAt;

    std::vector<std::string> rebuildProbeBlockOnly;

    std::vector<SessionProbeRec> sessionProbeLog;
    bool sessionProbeDone = false;

    std::vector<SidebarProbeRec> sidebarProbeLog;
    bool sidebarProbeDone = false;

    std::vector<AnswerProbeRec> answerProbeLog;
    bool answerProbeDone = false;

    std::vector<ThemeProbeRec> themeProbeLog;
    bool themeProbeDone = false;

    // ★★ 2026-10-30（浮层迁移）：浮层拓扑取证。同步探针（跑完即 Exit），
    //   故只有「做完了没有」一个标志，没有跨 tick 的步号/时刻。
    std::vector<OverlayProbeRec> overlayProbeLog;
    bool overlayProbeDone = false;

    int themeProbeStep = 0;
    std::chrono::steady_clock::time_point themeProbeNextAt;
    UiSnapshot themeUiBefore;        // baseline 的四路快照（所有 rebuild 相的比对面）
    size_t themePaintsBefore = 0;    // 同一 tick 内 `RebuildUi` 前后的 paints（必须相等）
    size_t themeFramesBefore = 0;

    std::vector<HistoryProbeRec> historyProbeLog;
    bool historyProbeDone = false;

    int historyProbeStep = 0;
    std::chrono::steady_clock::time_point historyProbeNextAt;

    std::filesystem::path historyProbeFile;

    ReplayDigest historyProbeDigest;

    std::vector<WorkspaceProbeRec> workspaceProbeLog;

    std::vector<PreviewProbeRec> previewProbeLog;
    bool previewProbeDone = false;

    std::vector<PersistProbeRec> persistProbeLog;
    bool persistProbeDone = false;

    std::string workspaceProbeBubbleText;
    bool workspaceProbeBubbleShapeOk = false;
    int workspaceProbeStep = 0;
    bool workspaceProbeDone = false;
    std::string workspaceProbeOriginalPath;
    std::string workspaceProbeTargetPath;
    std::string workspaceProbeFailPath;
    std::string workspaceProbeWorkspaceId;
    std::string workspaceProbeAcceptedId;
    std::string workspaceProbeRejectedId;
    std::chrono::steady_clock::time_point workspaceProbeDeadline{};

    // ★★ 2026-11-02：工作区分组标题行「＋」的**归属判据**（ws-plus 相，挂在
    //   `--workspace-probe` 末尾）。
    //   为什么必须有它：此前这条链**零机器判据**——侧栏探针 ⑫ 刻意不走 live 的「＋」
    //   （回放无 core，`NewTemporarySession` 首行就 return），于是"点 B 组的＋却挂到
    //   A 组"只能靠人眼看出来。实测缺陷（`workspaceSyncStarted` 漏置位 ⇒ hello 后的
    //   初始同步把外键覆盖回 GUI cwd）就是这么漏过去的。
    //   ★ 判据用 **sessionId 作身份锚**、不用下标：`SwitchToSession` 会摘掉旧的临时
    //     会话，下表会漂（同族缺陷见 `NewTemporarySession` 的注释）。
    std::string wsPlusProbeTargetId;       // 被点的工作区 id（必须 ≠ 建会话前所在的）
    std::string wsPlusProbeTargetPath;     // 该工作区 core 回显过的路径
    std::string wsPlusProbeNewSessionId;   // 新建会话的 sessionId（身份锚）
    size_t wsPlusProbeCountBefore = 0;     // 建会话前的会话数（验"确实多了一个"）
    int wsPlusProbeStableTicks = 0;        // hello 已处理后的稳定窗口计数
    bool wsPlusProbeDone = false;
    bool wsPlusProbeOk = false;
    std::string wsPlusProbeDetail;         // 翻红时的定性原因（数值同时进摘要）

    std::vector<MenuProbeRec> menuProbeLog;
    bool menuProbeDone = false;

    // ★★ 2026-11-03：侧栏**行宽**判据（挂在 `--sidebar-probe` 末尾，独立摘要键
    //   `sidebarGeom`，**不塞进** `sidebarProbe` 数组——那 23 相的形状是既有基线，
    //   往每条记录里加字段会让历史读数失去可比性，同 `wsPlusProbe` 的理由）。
    //   为什么必须补：行宽从 `ScrollView` 的实际宽度算出，而它只有 `Arrange()` 之后
    //   才成立。旧实现把建列表放在 Arrange 之前 ⇒ 会话行永久 160、历史行却是栏宽，
    //   同一栏里两块列表差 131px。**这条缺陷此前 23 相全绿**——探针只比模型、行数、
    //   选中态，从不读几何。
    //   ★ 判据是**两段式**（详见 `RunSidebarProbe` ⑬）：首帧路径（整树重建）+
    //     换自变量（侧栏变宽）后的剂量响应。只测前者会被"恰好算对一次"骗过。
    int sidebarGeomSideW = 0;        // 侧栏宽（首帧路径重建后）
    int sidebarGeomListW = 0;        // 会话列表控件宽
    int sidebarGeomRowW = 0;         // 会话行宽
    int sidebarGeomHistListW = 0;    // 历史列表控件宽（行宽 = f(它)，与列表宽差一档即建行时序缺陷）
    int sidebarGeomHistRowW = 0;     // 历史行宽
    int sidebarGeomExpectW = 0;      // 期望行宽（**从侧栏宽推**，不从列表推）
    int sidebarGeomMaxW = 0;         // 上界：超过它就会恒出横向滚动条
    int sidebarGeomWideSideW = 0;    // 换自变量后的侧栏宽
    int sidebarGeomWideRowW = 0;     // 换自变量后的会话行宽
    int sidebarGeomWideExpectW = 0;  // 换自变量后的期望值
    bool sidebarGeomDone = false;
    bool sidebarGeomOk = false;
    std::string sidebarGeomDetail;   // 翻红时的定性原因（数值同时进摘要）

    std::filesystem::path menuProbeTempFile;

    std::vector<ResizeProbeRec> resizeProbeLog;
    bool resizeProbeDone = false;

    bool hudAudit = false;
    std::vector<std::string> hudTouched;      // 读过的字段名（去重不必要，逐次记）

    std::vector<HudProbeRec> hudProbeLog;
    bool hudProbeDone = false;

    std::vector<SettingsProbeRec> settingsProbeLog;
    bool settingsProbeDone = false;
    std::string settingsProbeKey;   // save 相输入的 key（判据 5 用：不得出现在配置文件）
    int settingsProbeStage = 0;     // 跨 tick 步进（save 点击后要等 hello 下一 tick 才到）
    std::chrono::steady_clock::time_point settingsProbeNextAt;
    // ★ §9.5 改名相：第一击「只问不做」⇒ 配置文件必须**逐字节**等于点击前。
    //   快照存在这里而不是就地重读：重读拿到的是"当前文件"，而判据要的是"点击前的文件"。
    std::string settingsProbeConfigSnapshot;
    // ★ 2026-10-27：switch-bad 相的那条请求**真的发出去了**吗？
    //   回放源没有写通道（`Send` 恒 false）⇒ 本相在该跑法下不适用。若不加这个
    //   标志，"时间线上没有失败通知"会把「仪器没接上」误报成「功能坏了」
    //   ——正是本文件反复警告的那种假红。跨 tick 传，所以必须放在状态里。
    bool settingsProbeBadSwitchSent = false;

    std::vector<EnterProbeRec> enterProbeLog;
    bool enterProbeDone = false;

    std::vector<GrowProbeRec> growProbeLog;
    bool growProbeDone = false;

    std::vector<ValignProbeRec> valignProbeLog;
    bool valignProbeDone = false;

    std::vector<JumpProbeRec> jumpProbeLog;
    bool jumpProbeDone = false;

    std::vector<KeyProbeRec> keyProbeLog;
    bool keyProbeDone = false;

    std::vector<EmptyProbeRec> emptyProbeLog;
    bool emptyProbeDone = false;

    std::string emptyProbeClickTarget;

    std::vector<ModelProbeRec> modelProbeLog;
    bool modelProbeDone = false;

    std::vector<SelectProbeRec> selectProbeLog;
    bool selectProbeDone = false;

    std::vector<DelProvProbeRec> delProvProbeLog;
    bool delProvProbeDone = false;

    size_t apTicks = 0;              // 经过 live 分支的 tick 次数
    size_t apBlockedTicks = 0;       // 其中真的阻塞过（等了 >0 µs）的 tick 数
    uint64_t apBlockedUs = 0;        // 累计阻塞 µs
    uint64_t apMaxBlockedUs = 0;     // 单次最长阻塞 µs
    size_t apLinesWake = 0;          // 由**唤醒回调**处理的行数
    size_t apLinesTick = 0;          // 由 tick 兜底处理的行数
    size_t apMaxLinesPerCallback = 0;  // 单次回调最多处理了多少行（预算是否生效）
    // ★ 2026-10-30：预算的**停止原因**计数。预算是「行数与时间谁先到」双界：
    //   · `apCapStops`   = 撞到行数上限（`kMaxLinesPerCallback`）
    //   · `apBudgetStops`= 撞到时间预算（`kBudgetUs`）
    //   · 两者都为 0     = 每次都把队列取空了（burst 不够大，判据无区分力）
    //   ★ 为什么要分：`maxLinesPerCallback == 256` 只在前者成立时才为真，而单行成本
    //     决定了哪个界先到 —— 那是**机器速度**，不是被测性质。分开记之后，
    //     「预算真的顶到了」可以写成「撞到任一双界」，快机慢机都成立。
    size_t apCapStops = 0;
    size_t apBudgetStops = 0;
    size_t apCallbacks = 0;          // 唤醒回调执行次数
    std::vector<uint64_t> apLatencyUs;  // 入队 → HandleLine 的墙钟差（开戳时才有）
    bool apEofSeen = false;

    size_t evalProbeFrames = 0;
    size_t evalProbeCommands = 0;
    size_t evalProbeMax = 0;
    size_t evalProbeAvg = 0;
    std::vector<TableProbeRec> tableProbeLog;
    bool tableProbeDone = false;
    std::vector<CopyProbeRec> copyProbeLog;
    bool copyProbeDone = false;
};
