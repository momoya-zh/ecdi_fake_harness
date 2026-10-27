#pragma once
// 命令行选项（--replay / --core / 各探针开关）。
//
// ★ 2026-10-25（Stage 3 刀①）：从 `App.h` 整段搬出，逐字节未改。
//   动机：`main.cpp` 只需要这个结构体，却因为 `HarnessApp` 的私有成员一变就跟着重编。
//   依赖只有 `<filesystem>` / `<string>`，搬出来零成本。
//   字段说明留在原处（每条「为什么要有这个探针」的注释都在字段上方）。
//
// ★ 探针开关（26 个）与产品开关（`selectableRows` 等）都在这里，**不拆**：
//   拆开就等于在头文件层面宣布「探针是另一个程序」，而实际它们是同一份二进制的分支。

#include <filesystem>
#include <string>

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
    // ★★ P3.5（规格 §8.5）：无参启动 / 设置面板 / 配置路径覆盖（2026-10-24）。
    bool firstRun = false;                // 无 --replay/--core ⇒ 从配置直连，否则进首启引导
    std::filesystem::path configPath;     // --config <path>：覆盖配置文件位置（探针/测试隔离）
    bool settingsProbe = false;           // --settings-probe：设置面板 + 凭据注入判据
    std::string settingsUrl;              // --settings-url：探针 save 相的 BaseURL（默认假地址）
    std::string settingsKey;              // --settings-key：探针 save 相的 key（默认假 key）
    std::string settingsModel;            // --settings-model：探针 save 相的模型名
    std::string fixtureName;              // 摘要用：replayPath 文件名
    // ★★ P4 Phase A（2026-10-25）：「跳到最新」按钮取证（显隐状态机 + 合成点击回底）。
    //   默认 false ⇒ 零行为变化、摘要逐字节不变。
    bool jumpProbe = false;               // --jump-probe：回放 settle 后同步跑完即 Exit
    // ★★ P4 Phase B（2026-10-25）：全局快捷键取证（Ctrl+B/I/,/N、Esc 逐层关，
    //   两态对照 = 焦点在输入框内/外）。默认 false ⇒ 零行为变化。
    bool keyProbe = false;                // --key-probe：快捷键模型位/控件位 A==B 判据
    // ★★ P4 Phase C（2026-10-25）：空状态引导取证（空转录显示引导卡；
    //   负对照 = --instant 回放加载后不出现）。默认 false ⇒ 零行为变化。
    bool emptyProbe = false;              // --empty-probe：回放负对照 + firstRun 正对照
    // ★★ P4 Phase H（2026-10-25）：模型浮窗取证（Ctrl+M 开合 / 搜索过滤 / 同名
    //   标黄 / 回放下行点击的如实 notice / cot 的 hello.context 分母上屏）。
    //   默认 false ⇒ 零行为变化。
    bool modelProbe = false;              // --model-probe：模型浮窗 + 用量判据
    // ★★ P4 Phase G（2026-10-25）：转录可选中控件（尖峰开关）——assistant 正文
    //   行用只读 TextBox（方向键/选区/Ctrl+A/C 照常，TextBox.h:50 契约）替代
    //   Label。默认 false ⇒ 树与基线逐位同形。读数对比随尖峰跑批记录。
    bool selectableRows = false;          // --selectable-rows：assistant 行可选中复制
    // ★★ P4 Phase G：--selectable-rows 的功能判据（合成 Ctrl+A/Ctrl+C 回读
    //   剪贴板 == 块全文）。默认 false ⇒ 零行为变化。
    bool selectProbe = false;             // --select-probe：选中复制取证
    // ★★ P4 修复验证（2026-10-25，实测反馈：删除所选崩溃）：--config 隔离配置
    //   下合成「选中提供商 → 点删除所选」全链。默认 false ⇒ 零行为变化。
    bool delProvProbe = false;            // --delprov-probe：删除所选不崩判据
    // ★★ P4 Phase I（2026-10-26）：--preview-probe 预览链判据（regex 抽取 /
    //   workspace 纪律 / 截断闸 / Esc 关闭——各相 matched 写在 RunPreviewProbe 内）。
    bool previewProbe = false;            // --preview-probe：路径可点 + 只读预览判据
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
    // ★★ 2026-10-21：中栏头两个钮（`↗` 导出 / `···` 菜单）的取证。
    //   ★ 为什么必须有：这两件在此之前是**空回调**（`(void)tip; return;`），
    //     而"按钮存在 + 有 hover 反馈"在**任何**既有读数里都看不出区别——
    //     侧栏探针只看侧栏、主题探针只看重建，没有一个探针碰过中栏头。
    //   ★ 判据（脚本执行，逐条都有负对照）：
    //     · menu-closed：模型 false ∧ 控件不可见 ∧ 菜单条**不占槽**
    //       （判据取「转录区顶边 == 头底边」，而不是 `IsVisible()`——
    //        只改可见性不 Arrange 会让可见性正确而屏幕仍留着 34px 空洞）
    //     · menu-open：点 `···` 后模型 true ∧ 控件可见 ∧ 转录区顶边下移**正好 34**
    //     · menu-toggle-back：再点一次必须**回到原位**（不是"变窄了"就行）
    //     · menu-clear：点「清空视图」⇒ 转录行数不变（它重画视图，**不动模型**）
    //       ★ 负对照：同时断言 `timeline` 长度不变——若实现走成"清空模型"，
    //         行数会掉到 0，而那同样"看起来像重画了"
    //     · export-text：导出的文本**逐条**对得上 `timeline`，且含通知行
    //       ★ 负对照：`Transcript::Blocks()` 里**没有**那些通知行 ⇒ 证明导出源
    //         真的是 `timeline` 而不是块表（否则这条判据恒真）
    //     · export-file：真的落盘、内容与 `BuildTranscriptExportText()` 逐字节相等、
    //       且行尾是 LF（不是 CRLF）
    //   ★ 默认 false ⇒ 摘要逐字节不变（本仓铁律）。
    bool menuProbe = false;            // --menu-probe：中栏头导出/菜单判据
    // ★★ 2026-10-21（「先把 UI 完善」第 1 项）：窗口可拉伸的取证。
    //   ★ 为什么必须有：尺寸变化的失败**在既有读数里完全不可见**——`blocks`/
    //     `deltas`/`seqGaps` 与窗口多大毫无关系，而截图只能证明"某一刻看起来还行"。
    //     改前的实测（`.scratch/probe_resize_shots.py`）：1400x900 下转录行仍是
    //     1120 时的宽度（右侧一片白），800x560 下行比视口还宽（出现横向滚动条）。
    //   ★ 判据逐条（每条都能变红）：
    //     · 居中（四相共用）：内容列左右留白相等 —— `rowX == scrollW − rowX − rowWMax`。
    //       这是版心模型（`min(可视带宽, kMaxTranscriptW)` 居中）唯一的视觉承诺，
    //       且与窗口宽无关 ⇒ grow/shrink/roundtrip 断的是同一句话。
    //     · 封顶：行宽 ≤ `kMaxTranscriptW − 2×kCanvasPadX`（窗口再宽也不许变宽）
    //     · grow：未封顶时行宽**必须**跟着变宽；已封顶时**必须**一位不变
    //     · shrink：客户区变小 ⇒ 行宽变小，且**不出现横向滚动条**
    //       （横向滚动条是"行比视口宽"的机器可读证据，`GetMaxOffsetX() > 0`）
    //     · caption：标题栏宽度 == 客户区宽度（三个系统钮必须贴着右上角）
    //     · roundtrip：回到原尺寸 ⇒ 行宽与**左边界**逐位回到原值（同一尺寸只许
    //       有一个布局；版心宽依赖条可见性时这里是 626 → 622，回环的两个自洽解）
    //     · negative：只改一个自变量（把 root 改窄但不调 `OnWindowResized`）⇒
    //       行宽必须**不**变 ⇒ **居中必须被打破**（若仍居中，说明行宽是 `Arrange`
    //       顺手改的，上面三条证明不了任何事）
    //   ★ 2026-10-27 版心重构后，旧的 `scrollW − rowWMax` 恒等式判据**按设计失效**
    //     （封顶后差值必然随窗口变化）——那三相变红是判据过期，不是回归。
    //   ★ 默认 false ⇒ 摘要逐字节不变（本仓铁律）。
    bool resizeProbe = false;          // --resize-probe：窗口拉伸的布局判据
    // ★★ 2026-10-21（「UI 完善」第 5 项）：**Enter 发送 / Shift+Enter 换行**的取证。
    //   ★ 为什么必须有：改前输入框是单行的，按 Enter 什么也不发生。这件事在
    //     **所有既有读数里都不可见**——`blocks`/`deltas` 只反映"发了几条"，
    //     而探针从不按键，截图也证明不了"这个键有反应"。
    //   ★ 判据（每条都能变红）：
    //     · enter-sends：输入框里有文字时合成一次 `KeyDown(Enter)`
    //       ⇒ 会话里**多出一条** prompt，且输入框被清空
    //       （清空由 `App` 的 `SetOnSend` 做——这一条同时证明两条路径合流了）
    //     · shift-enter-newline：`KeyDown(Shift+Enter)` ⇒ **不发送**，
    //       且文本框里真的多了一个 `\n`
    //     · enter-empty：空框按 Enter ⇒ **不发送**（负对照：否则凭空多一条空气泡）
    //     · enter-whitespace：只有空白时按 Enter ⇒ **不发送**
    //       （★ 比"空串"强：`!empty()` 判不出这种"看起来有内容"
    //        ——用户敲了 Shift+Enter，剩下的是 `\n`）
    //     · enter-unfocused：焦点移出输入框再合成 Enter ⇒ **不发送**
    //       （★ 负对照：证明"发不发送"取决于事件真的派给了输入框，
    //        而不是探针直接调了 `Submit()`）
    //   ★ 默认 false ⇒ 摘要逐字节不变（本仓铁律）。
    bool enterProbe = false;           // --enter-probe：Enter 发送 / Shift+Enter 换行
    // ★★ 2026-10-21：输入框自动长高（--grow-probe）。
    //   判据（跑批脚本执行，见 `RunGrowProbe` 头注）：
    //     · type-newline：Shift+Enter 后高度**必须真的变高**（键盘接线判据）
    //     · grow：3 行 > 1 行，且提示行/按钮**跟着下移**（不是被压住）
    //     · max-clamp：30 行时高度封顶当前 root 高度对应的动态上限
    //     · resize-adapt / resize-return：真实改窗高后上限随之变化，并在还原时收敛
    //     · send-reset：Enter 发出后高度回到单行（`SetText` 路径的同步判据）
    //     · shrink：删掉换行后高度**必须变矮**（★ 唯一能打红 max(0,·) 陷阱的方向）
    //     · exact-fit：恰好装下时不出现内部滚动条（取整方向判据）
    bool growProbe = false;            // --grow-probe：多行自动长高
    // ★★ REQ-14（2026-10-27，ECDI Phase 37 verticalAlign）：单行 TextBox 垂直居中。
    //   判据（跑批脚本执行，见 `RunValignProbe` 头注）：
    //     · 每相交付态（Center）读数 == (H − lineH)/2（padding 代数消去后的居中律）
    //     · 翻转臂（Top 强制）= 剂量对照：设置框 0 → 6.5/4.5 是真跳变；
    //       composer 恰满（29 = 17+6×2）两读数相等——相等就是「Top 时也居中」的读数
    //     · 恢复臂：翻回 Center 与首测一致（状态确被还原）
    bool valignProbe = false;          // --valign-probe：单行垂直居中判据
    // ★★ 2026-10-21：`Hud` 覆盖率自检（--hud-probe）。
    //   ★ 为什么必须有：`RefreshHud` 里有二十来处 `if (m_hud.X)`，而
    //     **建树时漏掉某个字段**这件事在任何既有读数里都不可见——
    //     那个 if 永远为假，屏幕上就那么空着，代码读起来却像"有就更新"。
    //     已实测到三个实例（`backendChip` / `sessSession` / `sessCore`）。
    //   ★ 判据：被读过但从未赋值的字段集合必须为空；负对照把那集合造非空。
    //   ★ 默认 false ⇒ 摘要逐字节不变。
    bool hudProbe = false;             // --hud-probe：HUD 指针"读了但没建"判据
    // ★★ REQ-03（2026-10-24）：UI 线程的取数方式。
    //   · 默认（`legacyPoll == false`）＝ **推**：读线程经 `Application::PostToUi`
    //     投递一个「唤醒」，UI 线程在消息循环里 drain；tick 只做**非阻塞**兜底。
    //   · `--legacy-poll` ＝ **拉**：逐字保留改动前的形状（每 tick 首行等 8ms）。
    //   ★ 为什么留这个开关，而不是"改完就删旧的"：A/B 必须是**单一自变量**——
    //     同夹具、同 core 参数、同一个二进制，只差这一个布尔。否则「新路径更好」
    //     这句话没有对照物，只是把改动前的读数忘了。
    bool legacyPoll = false;           // --legacy-poll：REQ-03 对照世界（8ms 轮询）
    // ★★ REQ-03 探针（默认关 ⇒ 摘要逐字节不变）：
    //   `blockedUs` 只统计**带超时**的那次等待（= UI 线程真正被挂在 cv 上的时间），
    //   这是「拉」的代价；`linesViaWake` / `linesViaTick` 是两条通路各自吃了多少行；
    //   `latencyP50/P99` 是「读线程入队 → HandleLine」的墙钟差（需要开戳）。
    bool asyncProbe = false;           // --async-probe：REQ-03 机读读数
    // ★★ 2026-10-26（按模式分流）：开发者视图开关。
    //   · 无参启动（`firstRun && !HasAnyProbe()`）默认进入「用户模式」（隐藏评估胶囊、
    //     底栏、渲染探针卡、夹具历史列表，默认收起右侧检视栏，状态与提示改人类可读文案）；
    //   · 显式传 `--replay` / `--core` / 任一探针 / `--dev` 则进入「开发者模式」（100%
    //     保持既有树、布局与读数口径，所有回归探针零扰动）。
    //   · 运行期可通过 `Ctrl+Shift+D` 或 `···` 菜单随时切换。
    bool devMode = false;              // --dev：无参启动也强制进入开发者模式

    [[nodiscard]] bool HasAnyProbe() const {
        return cullProbe || scrollSweepStep > 0 || settingsProbe || jumpProbe ||
               keyProbe || emptyProbe || modelProbe || selectProbe || delProvProbe ||
               previewProbe || probeContrast || stateProbe || hitProbe ||
               reasoningProbe || orderProbe || rebuildProbe || sessionProbe ||
               sidebarProbe || answerProbe || themeProbe || historyProbe ||
               workspaceProbe || menuProbe || resizeProbe || enterProbe ||
               growProbe || valignProbe || hudProbe || legacyPoll || asyncProbe;
    }
};
