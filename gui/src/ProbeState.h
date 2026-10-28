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

    std::vector<MenuProbeRec> menuProbeLog;
    bool menuProbeDone = false;

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
    size_t apCallbacks = 0;          // 唤醒回调执行次数
    std::vector<uint64_t> apLatencyUs;  // 入队 → HandleLine 的墙钟差（开戳时才有）
    bool apEofSeen = false;

    size_t evalProbeFrames = 0;
    size_t evalProbeCommands = 0;
    size_t evalProbeMax = 0;
    size_t evalProbeAvg = 0;
};
