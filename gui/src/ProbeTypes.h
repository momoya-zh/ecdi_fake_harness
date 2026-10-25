#pragma once
// 探针读数结构体（--xxx-probe 写进摘要的那些记录）。
//
// ★ 2026-10-25（Stage 3 刀②）：从 `HarnessApp` 的**类作用域**提到命名空间作用域。
//   动机：它们只是纯数据（`bool` / `int` / `size_t` / `std::string` / `std::vector`），
//   却因为嵌在 1670 行的类里，任何只想读「摘要长什么样」的人都要先吞下整个类。
//   搬出来之后 `Probe*.cpp` 可以只包这个头。
//
// ★ 搬运纪律：每个 struct 的每一行都逐行核对过（只允许少 4 空格缩进），
//   字段名 / 字段顺序 / 行内注释 / 默认值一律未动——摘要的键序依赖字段声明序。
//
// ★ 上方的「为什么必须有这个探针」注释块**留在 App.h**：它们的主语是紧跟着的
//   `m_xxxProbeLog` 成员，不是类型本身。

#include <cstddef>
#include <string>
#include <vector>

struct StateProbeRec {
    std::string coreState;   // 本次观察时的 core state（来自活动会话）
    bool sendEnabled = true;
    bool stopEnabled = false;
    std::string sendTitle;   // Button::GetText()——标题文本直接回读，不靠像素
};

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

struct ReasoningProbeRec {
    std::string phase;        // default / after-click-1 / after-click-2
    std::string headerText;   // Button::GetText()——箭头与两态文本都在里面
    bool expanded = false;    // CollapsiblePanel::IsExpanded()
    bool contentVisible = false;  // 折叠体内容容器 IsVisible()
    int panelH = 0;           // 行高（展开应显著变大）
};

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

struct SessionProbeRec {
    std::string phase;        // baseline / new / back
    size_t sessionCount = 0;  // 当时的会话总数
    size_t activeIndex = 0;   // 当时的活动会话下标
    size_t rowCount = 0;      // 视图行数（含通知行）
    size_t blockRowCount = 0; // 其中**块行**数 —— 负对照的关键：新会话必须为 0
};

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

struct ReplayDigest {
    bool loaded = false;
    size_t blocks = 0;
    size_t deltas = 0;
    size_t messages = 0;
};

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

struct MenuProbeRec {
    std::string phase;         // baseline / menu-open / menu-toggle-back /
                               // menu-clear / menu-clear-negative /
                               // export-text / export-file / export-negative
    bool modelOpen = false;    // `m_moreMenuOpen`（模型侧）
    bool widgetVisible = false;// `Hud::moreMenu->IsVisible()`（控件侧）
    int headBottom = 0;        // 会话头底边（绝对坐标）
    int scrollTop = 0;         // 转录区顶边（绝对坐标）——判"有没有占槽"就看它
    int gap = 0;               // scrollTop - headBottom（0=菜单关 / 34=菜单开）
    int midWidth = 0;          // 中栏宽（负对照：开关菜单不该改宽度）
    size_t transcriptRows = 0; // `m_sess->rows.size()`
    size_t timelineLen = 0;    // `m_sess->timeline.size()`
    size_t exportLines = 0;    // 导出文本里以 `[` 开头的行数
    size_t modelBlockLines = 0;// `Transcript::Blocks()`——负对照的比对面
    size_t noticeRows = 0;     // 模型里 Notice 条目数（导出必须含有它们）
    bool exportHasNotice = false; // 导出正文里真的出现了某条通知的原文
    bool exportWrote = false;  // 文件真的落盘了吗
    size_t exportBytes = 0;
    bool exportLfOnly = false;// 行尾是 LF 而非 CRLF
    bool matched = true;
    std::string firstDivergence;
};

struct ResizeProbeRec {
    std::string phase;        // baseline / grow / shrink / roundtrip / negative
    int rootW = 0;            // `root` 的宽度（模型侧）
    int rootH = 0;
    int scrollW = 0;          // 转录视口宽
    int scrollH = 0;          // 转录视口高（被 composer 挤压的读数就是它）
    int captionW = 0;         // 标题栏宽（必须 == rootW）
    int midW = 0;             // 中栏宽
    int maxOffsetX = 0;       // > 0 ⇒ 出现横向滚动条（行比视口宽）
    int rowWMin = 0;          // 行宽的**最小值/最大值**：所有行必须一起变宽
    int rowWMax = 0;
    size_t rows = 0;
    bool matched = true;
    std::string firstDivergence;
};

struct HudProbeRec {
    std::string phase;              // hud-built / hud-built-negative
    size_t touched = 0;             // RefreshHud 读过多少个 Hud 字段
    size_t unbuilt = 0;             // 其中多少"从未被赋值"
    std::string unbuiltNames;       // 逗号分隔的字段名（空 = 全覆盖）
    bool matched = true;
    std::string firstDivergence;
};

struct SettingsProbeRec {
    std::string phase;
    bool matched = true;
    std::string firstDivergence;
};

struct EnterProbeRec {
    std::string phase;        // enter-sends / shift-enter-newline /
                              // enter-empty / enter-whitespace / enter-unfocused
    size_t promptCount = 0;   // `Session::timeline` 里 user 块数量
    std::string inputText;    // 合成按键之后输入框里的文本（"清空了吗"就是读它）
    size_t noticeRows = 0;    // Notice 条目数（发送会追加 receipt 行）
    bool sent = false;        // 这一相判断"发出去了"
    bool matched = true;
    std::string firstDivergence;
};

struct GrowProbeRec {
    std::string phase;     // baseline / type-newline / grow / max-clamp /
                           // resize-adapt / resize-return / send-reset / shrink / exact-fit
    int rootH = 0;         // 当前 root 客户区高度
    int maxInputH = 0;     // 产品按当前窗口计算的自适应上限
    int inputH = 0;        // 输入框高（产品路径写进去的那个值）
    int barH = 0;          // `PromptBar` 自身高
    int lines = 0;         // 文本里的行数（`\n` 数 + 1）
    int hintY = 0;         // 提示行 Y——判"有没有被输入框压住"
    int buttonY = 0;       // 发送按钮 Y（同上）
    bool matched = true;
    std::string firstDivergence;
};

struct JumpProbeRec {
    std::string phase;        // initial-consistency / after-click-tail /
                              // after-wheel-up / after-click-again / skipped
    bool visible = false;     // 按钮此刻 IsVisible()
    int offset = 0;
    int maxOffset = 0;
    bool matched = true;
    std::string note;         // 判据失败原因（ASCII——省 JSON 转义）
};

struct KeyProbeRec {
    std::string phase;          // ctrl-b-focused / ctrl-b-unfocused / ctrl-comma /
                                // esc-settings / esc-priority / ctrl-n / passthrough
    bool sidebarVisible = false;   // m_sidebarVisible（模型位）
    bool sidebarShown = false;     // m_hud.sidebar->IsVisible()（控件位）
    bool settingsVisible = false;
    bool settingsShown = false;
    bool menuOpen = false;         // m_moreMenuOpen（模型位）
    bool menuShown = false;        // m_hud.moreMenu->IsVisible()
    size_t sessionCount = 0;
    std::string inputText;         // passthrough 相的输入框回读
    bool matched = true;
    std::string note;
};

struct EmptyProbeRec {
    std::string phase;
    bool settingsVisible = false;
    bool guideExists = false;      // m_hud.emptyGuide != nullptr
    bool guideVisible = false;     // 控件自身位 ∧ scroll 可见（父链语义）
    size_t rowCount = 0;
    bool matched = true;
    std::string note;
};

struct ModelProbeRec {
    std::string phase;
    bool popupVisible = false;
    size_t listRows = 0;
    bool ctxKnown = false;
    std::string chipText;
    std::string note;
    bool matched = true;
};

struct SelectProbeRec {
    std::string phase;        // select-and-copy / negative-focus-elsewhere
    std::string clipText;     // 回读的剪贴板（截断到 60 字节进摘要）
    bool matched = true;
    std::string note;
};

struct DelProvProbeRec {
    std::string phase;
    size_t providersBefore = 0;
    size_t providersAfter = 0;
    std::string hint;
    bool matched = true;
    std::string note;
};

