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
    // ★★ 2026-11-07（live 协议臂 2d-b）：同行回读 picker / caps / 版本面。
    //   picker 两项直接回读真实按钮；workspaceCap/protocolVer 是从 hello 解析出的
    //   会话字段——判据建立在 GUI 的解析结果上，而不是门传给假 core 的参数上，
    //   「GUI 钳位/漏解析」在这条臂上会红。存在理由：2026-11-06 的
    //   「选择文件夹死钮」（liveReady 守卫 == 3）在整条 live 语料里结构性不可见。
    bool pickerEnabled = false;   // workspacePick->IsEnabled()
    bool pickerVisible = true;    // workspacePick->IsVisible()（§3.1 未声明 ⇒ 隐藏）
    bool workspaceCap = false;    // hello.caps 是否声明 workspace
    bool modelCap = false;        // hello.caps 是否声明 model（八版 2d-b 第四臂）
    int protocolVer = 0;          // hello.protocol 的 GUI 解析值
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
    // ★★ 2026-11-07（live 臂，HUD 脏位门 + TIMER-STARVATION 牙）：HUD 文字回读
    //   与唤醒投递计数。脏位不被消费 / 唤醒洪泛时，文本停格、wakePosts 暴涨。
    std::string sessCountText = "";   // 侧栏「N 个会话」Label 的 GetText()
    std::string headSubText = "";     // 头栏副标 Label 的 GetText()（含「会话 i/n」）
    size_t wakePosts = 0;             // 活动会话 ChildSource::WakePosts() 快照
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

// ★★ 2026-10-30（浮层迁移）：浮层拓扑取证（`--overlay-probe`）。
//   【为什么需要它】迁移把 6 处绕行换成 ECDI Phase 42 的浮层根之后，本仓多了一条
//     **结构不变量**：模态/tooltip/下拉都必须挂在 `Window::GetOverlayRoot()` 下，
//     且整树重建（切主题）不得让新旧两套同屏。此前**没有任何探针读过 `GetOverlayRoot()`**
//     ——旧判据只问「面板还在不在」，而"在"与"只有一套在"是两件事。
//   【读数设计】`overlayRootChildren` 是这套判据的核心：
//     浮层根**恒有 3 个孩子**（Normal/Modal/Tooltip 三个层，构造期建好），
//     托管挂载的遮罩与 sheet 直接挂上去 ⇒ 期望值 = 3 + 已挂载数。
//     ★ 这个恒等式能同时抓住两类缺陷：①旧的一套没收（多 2）；
//       ②`ShowOverlay` 的浮层重复注册（层里的孩子数对不上）。
struct OverlayProbeRec {
    std::string phase;          // closed / mount / modal-blank / rebuild / theme /
                                // tip / tip-palette / tip-through / popup /
                                // popup-rebuild / popup-dismiss（共 11 相）
    int overlayRootChildren = 0;   // 浮层根直接子节点数（期望 3 + mountedManaged）
    int mountedManaged = 0;        // 托管挂载的遮罩/sheet 数（按 GetParent 实测，不看指针非空）
    bool settingsOnOverlay = false;// 设置 sheet 的父节点 == 浮层根
    bool maskOnOverlay = false;    // 遮罩的父节点 == 浮层根
    bool tipOnOverlay = false;     // tooltip 的祖先链上有浮层根、且**不在** root 子树里
    bool popupOnOverlay = false;   // 下拉的祖先链上有浮层根
    bool settingsVisible = false;
    bool popupOpen = false;        // 模型位（不是控件位）
    bool tipAlive = false;         // `m_hud.tooltip` 非空
    bool anyRegistered = false;    // `HasActiveOverlay()`——托管挂载**不该**让它为真
    int sheetX = 0;                // sheet 左缘（居中判据）
    int sheetW = 0;
    int clientW = 0;               // 客户区宽（居中判据的分母）
    // ★★ 2026-11-01（用户反馈「模型胶囊点开的位置不对」）：下拉的**几何**读数。
    //   病灶：本探针此前只记「开没开 / 挂在哪」，**对位置一条断言都没有** ⇒ 弹窗被
    //   定位到转录视口正中，`--overlay-probe` 仍 10/10 全绿——这正是「结构绿了、
    //   视觉没绿」的标准形状。位置一旦是缺陷，就必须是读数。
    //   口径：两方都是**绝对坐标**（客户区），-1 = 控件不存在（与真实的 0 区分开）。
    int popupX = -1, popupY = -1, popupW = -1, popupH = -1;
    int chipX = -1, chipY = -1, chipW = -1, chipH = -1;
    // ★★ 2026-11-04（用户报告「深色模式下浮窗文本没改色」）：tooltip 的**配色**读数。
    //   病灶：`OnTooltipFire` 的 `.background = kDarkInk` 是**主题令牌**，而字色
    //   `kTooltipInk` 是字面量 ⇒ 深色下「近白底 + 近白字」，实测对比 **1.08:1**。
    //   此前本探针对 tooltip **只有结构断言**（挂载/点穿/单例），颜色一条没有
    //   ⇒ 屏幕上字已经溶解，10 相全绿。这是「结构绿了、视觉没绿」的第 N 次复现。
    //   口径：`tipBg` / `tipInk` 读的是控件**实际用于绘制的字段**（见 `uikit::TipPanel`），
    //        不是探针自己写进去的常量副本；`tipContrast` 是 WCAG 相对亮度比。
    //   ★ 为什么记读数而不只记 matched：对比度是**连续量**，失败时要知道差多少。
    double tipContrast = 0.0;      // bg↔ink 的 WCAG 对比度（<4.5 即不可读）
    std::string tipBg;             // "#rrggbb"（空 = 没读到）
    std::string tipInk;            // "#rrggbb"
    bool tipInvariant = false;     // 两个主题下 (bg,ink) 逐位相同（= 配色不随主题走）
    bool matched = true;
    std::string firstDivergence;
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

struct PreviewProbeRec {
    std::string phase;      // regex-cases / open-ok / escape-rejected / missing /
                            // no-workspace / too-large / line-cap / close-esc
    bool matched = true;    // 判据（写在 RunPreviewProbe 内，探针即门）
    std::string detail;     // matched=false 时的原因（其余相 = 关键读数）
};

struct PersistProbeRec {
    std::string phase;
    bool matched = true;
    std::string detail;
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
    // ★★ 2026-10-27：**最宽那一行**的左边界。版心模型的判据是"内容列居中"，
    //   即 `rowX == scrollW − rowX − rowWMax`——没有这个读数就只能断言"变宽了"，
    //   而"整列贴左但确实变宽了"照样能骗过旧判据（上一版实测左 16 / 右 48）。
    int rowX = 0;
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

// ★ REQ-14 / ECDI Phase 37（--valign-probe）：单行 TextBox 垂直居中的机读证据。
//   读回仪器 = GetCaretClientGeometry()（公开；其 rect.h == GetLineHeight() 同源，
//   rect.y − 控件绝对 y == GetTextTopInset()——Phase 37 △8 的 Y 原点，X/Y 单一来源）。
//   判据（跑批脚本执行，见 RunValignProbe 头注）：
//     · 每相 matched == true：交付态（Center）读数 == (H − lineH)/2
//       ——单行居中时 padding 在代数上消去：p + (H−2p−lineH)/2 = (H−lineH)/2。
//     · 翻转臂（Top 强制）：读数记录为剂量对照；Content 不超屏时 Top < Center，
//       恰满（composer 29 = 17+6×2）时相等——相等**本身**就是「Top 时也居中」的读数。
//     · 恢复臂：翻回 Center 后读数必须与首测一致（状态确被还原）。
struct ValignProbeRec {
    std::string phase;        // composer-flip / settings-fields / settings-steppers / model-search
    std::string box;          // 哪个框（composer/base/key/model-name/ctx/max-out/search）
    int boxH = 0;             // 控件高
    int lineH = 0;            // 行高（caret rect.h——GetLineHeight 同源，不硬编码）
    int relYTop = -1;         // Top 强制臂读数（-1 = 本相未做翻转）
    int relYCenter = -1;      // 交付态（Center）读数
    int relYRestored = -1;    // 翻回 Center 后读数（-1 = 未做翻转）
    int expectedCenter = 0;   // (H − lineH) / 2（四舍五入；与读数同用 lround）
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


struct TableProbeRec {
    std::string phase;
    bool matched = true;
    std::string note;
};

// ★★ 2026-11-08（富内容 B+，rich-content-plan §G）：块级全套解析判据。
//   ★ 为什么单独一套探针而不并进 --table-probe：table 那 6 相是二期的**既有判据**，
//     混进新相会让「表格没有回归」和「块级新增正确」两件事互相掩盖——
//     一个相 FAIL 时看不出是哪条线坏的。分开跑，各自 PASS 才是独立证据。
struct MdProbeRec {
    std::string phase;
    bool matched = true;
    std::string note;
};

struct CopyProbeRec {
    std::string phase;
    bool matched = true;
    std::string note;
};

// ★★ 协议 v5（2026-11-04）：子代理缩进 / 生命周期读数（--agent-probe）。
//   ★ 记录里有**两侧**的读数：`attributedBlocks` 来自模型（Transcript），
//     `indentRows` 来自视图（RowRec）——两者相等才是判据，单独任一侧都只是自说自话。
struct AgentProbeRec {
    std::string phase = "settle";   // 同步探针只有一相（classify 用它给相起名）
    size_t rows = 0;
    size_t indentRows = 0;
    size_t rootRows = 0;
    size_t gutterRows = 0;        // 真的建了竖线控件的缩进行数
    size_t attributedBlocks = 0;  // 模型侧：带 `agent` 归属的块数
    int indentPx = 0;             // 缩进行相对画布左边界的横移（实测，不是常数复读）
    size_t overflowRows = 0;      // 缩进行的右边界超出根列右边界的行数（必须 0）
    int maxRight = 0;             // 缩进行右边界最大值（与 colRight 比）
    size_t agents = 0;
    size_t agentsEnded = 0;
    size_t agentsOk = 0;
    size_t agentViolations = 0;   // I12 的 GUI 侧记账
    size_t unknownTypes = 0;      // `agent` 被吸收后应为 0
    // ★★ 2026-11-04（同一轮补）：右栏「代理」卡的读数。
    //   ★ 为什么必须有：卡是**全新 UI**，缩进探针照不到它——卡片错建（空卡、
    //     永远隐藏、把主代理行漏掉）时 `indentRows` 依然全对，屏幕上也依然
    //     有缩进行。判据：`cardRows == agents + 1`（含主代理那行）且
    //     `cardVisible == (agents > 0)`。
    size_t cardRows = 0;
    bool cardVisible = false;
    bool matched = true;          // 元断言（见 RunAgentProbe 头注）
    std::string note;
    std::vector<std::string> agentLines;   // "a1|名|end|ok|3轮|12行|18420ms"
};

// ★★ 协议 v5（2026-11-05）：ask 提案卡读数（--proposal-probe）。
//   ★ 两侧读数与 agent-probe 同一手法：`detailRows`/`diffRows` 等来自**视图**
//     （RowRec::proposal 建行期统计），`model*` 来自**模型**（timeline 条目的
//     detail 重新切行计数）——两条独立计算路径相等才是判据，同源复读是自说自话。
struct ProposalProbeRec {
    std::string phase = "settle";      // 同步探针，一相
    size_t askRows = 0;                // 视图：ask 行总数（回落路径的存量）
    size_t detailRows = 0;             // 视图：建了提案体的 ask 行
    size_t diffRows = 0;               // 视图：detail.diff 派生行（键值行不计）
    size_t plusRows = 0;               // 视图：'+' 行（建行期统计）
    size_t minusRows = 0;              // 视图：'-' 行
    size_t kvRows = 0;                 // 视图：run/task 键值行（渲染面，不进 diff 判据）
    size_t modelDetailRows = 0;        // 模型：askDetail.has 的 timeline 条目数
    size_t modelDiffRows = 0;          // 模型：detail.diff 重切行数
    size_t modelPlus = 0;              // 模型：'+' 行重数
    size_t modelMinus = 0;             // 模型：'-' 行重数
    size_t emptyDetailRows = 0;        // 有 detail 却没建出任何提案行（必须 0）
    bool matched = true;               // 元断言（见 RunProposalProbe 头注）
    std::string note;
};

// ★★ 协议 v6（2026-11-05）：后台作业卡片（--job-probe）。
//   判据分两臂（跑批各跑一支）：
//   正向（jobs/background-task.jsonl）：模型 jobsByCall=1 且视图工具行标题/正文
//     与模型字段逐项一致（标题含 j1+终态，正文含尾部输出 sample）。
//   负向（demo.jsonl）：没有 job 消息 ⇒ 任何工具行标题都不得出现「后台」。
struct JobProbeRec {
    std::string phase = "settle";      // 同步探针，一相
    size_t modelJobs = 0;              // 模型：jobsByCall 条数
    size_t viewRows = 0;               // 视图：能对上 jobsByCall 的 run_command 行
    size_t titleMatched = 0;           // 标题含 job id + 状态文案
    size_t bodyMatched = 0;            // 正文含协议 start.command（卡片确实挂在正确 call 上）
    size_t tailMatched = 0;            // 正文含模型 tail 的最后一行（输出真的画出来了）
    size_t staleRows = 0;              // 无 job 却带「后台」字样的行（负向判据）
    bool matched = true;
    std::string note;
};

// ★★ 2026-11-06：回合级折叠（--fold-probe）。
//   判据形态沿用 Proposal/Job 那两支的"模型侧 ⇔ 视图侧逐项对齐"，但这里多一条
//   **负向对照**是本特性的核心：一个"什么都不折"的实现也能让"摘要条存在"为假，
//   所以正臂必须证明**过程行真的从屏上消失了**，且消失的恰好是被折叠的那些。
//
//   正向（realistic / demo 等有多过程行的夹具）：
//     ① folds >= 1                     —— 确实折了回合（不是全绿的空转）
//     ② hiddenRows == foldHiddenCount  —— 模型说隐藏的，视图也隐藏（逐条对齐）
//     ③ visibleProcRows == procRows - hiddenRows  —— 视图上还能看见的过程行数对得上
//     ④ summaryOk                      —— 摘要条文案含「已完成」且步数 > 0
//     ⑤ foldRows >= 1                  —— 屏上真有一条摘要条（不是把所有行都藏了）
//     ⑥ rowsBefore == rowsAfter        —— 展开一次再折回来，行数**不变**
//                                         （这条守的是"折叠不改变条目数"这条不变量）
//     ⑦ expandRestores == folds        —— 展开后过程行全部回到可见
//   负向（subagent 之类只有 0~1 条过程行的夹具）：
//     folds == 0 且 hiddenRows == 0     —— 不该折的地方一行都不许折
struct FoldProbeRec {
    std::string phase = "settle";      // 同步探针，一相
    size_t folds = 0;                  // 模型：Kind::Fold 条目数
    size_t procRows = 0;               // 模型：可折叠过程行（foldable）条数
    size_t foldHiddenCount = 0;        // 模型：foldHidden 条数
    size_t hiddenRows = 0;             // 视图：foldHidden 且控件实际不可见
    size_t visibleProcRows = 0;        // 视图：foldable 且可见
    size_t foldRows = 0;               // 视图：摘要条行数
    size_t summaryOk = 0;              // 视图：文案含「已完成」+ 步数
    size_t rowsBefore = 0;             // 折叠态下的 rows.size()
    size_t rowsAfter = 0;              // 展开→再折叠后的 rows.size()
    size_t expandRestores = 0;         // 展开后可见过程行恢复的条数
    bool matched = true;
    std::string note;
};

// ★★ 2026-11-06：MCP 产品面可见性（--mcp-probe）。
//   两侧判据同一手法：模型侧读 `hello.mcp`（协议字段），视图侧读副标/卡片/
//   失败 system 块的实际控件。正向夹具必须**同时**有成功服务与失败服务——
//   只有成功时，副标里的「（K 个失败）」与卡片红行永远走不到。
struct McpProbeRec {
    std::string phase = "settle";      // 同步探针，一相
    size_t modelServices = 0;          // 模型：hello.mcp.services 条数
    size_t modelFailed = 0;            // 模型：hello.mcp.failed 条数
    size_t modelTools = 0;             // 模型：services[].tools 之和
    bool modelHas = false;             // 模型：hello 带 mcp 字段
    bool cardVisible = false;          // 视图：MCP 卡是否显形
    size_t cardRows = 0;               // 视图：卡片列表行数
    size_t serviceRows = 0;            // 视图：含每个服务名的行数（应 == modelServices）
    size_t toolCountRows = 0;          // 视图：行尾工具数与模型侧逐字相等的行数（八版 ⑦，应 == modelServices）
    size_t transportRows = 0;          // 视图：远程服务行里带 transport 字样的行数（应 == 远程服务数）
    size_t modelRemote = 0;            // 模型：transport 非 stdio 的服务数（六版）
    size_t failureRows = 0;            // 视图：含每个失败服务名的行数（应 == modelFailed）
    size_t renderedMcpNotices = 0;     // 视图：屏上含 "[MCP]" 的 system 行（失败/配置错误）
    bool subtitleHasMcp = false;       // 视图：会话头副标含 "MCP"
    bool subtitleMatched = false;      // 视图：副标含 McpStatusText() 逐字结果
    bool matched = true;               // 元断言（见 ProbeMcp.cpp 头注）
    std::string note;
};

// ★★ 2026-11-06：开发者模式与用户模式分流判据（--mode-probe）。
//   闭合 P4 Phase J 已知缺口（用户模式零探针覆盖）：
//     · baseline-dev: 初始处于开发者模式，所有评估控件/胶囊/底栏/历史区全部可见，三按钮宽 28px
//     · switch-user:  通过 Ctrl+Shift+D 切换至用户模式，评估控件全数隐藏，检视栏收起，三按钮宽 44px
//     · dev-restored: 再次切换切回开发者模式，所有控件完全恢复（无单向残留，可逆性）
//     · negative:     负对照（反向断言必红，证明断言有区分力）
struct ModeProbeRec {
    std::string phase;
    bool devMode = false;
    bool inspectorVisible = false;
    bool sessChipVisible = false;
    bool modeChipVisible = false;
    bool rowChipVisible = false;
    bool backendChipVisible = false;
    bool statusBarVisible = false;
    bool sessProgressVisible = false;
    bool replayBarVisible = false;
    bool historyHeaderVisible = false;
    bool historyListVisible = false;
    bool historyCountVisible = false;
    bool inspProbeCardVisible = false;
    std::string exportText;
    int exportWidth = 0;
    std::string inspectorText;
    int inspectorWidth = 0;
    std::string moreText;
    int moreWidth = 0;
    std::string devMenuText;
    bool matched = true;
    std::string note;
};
