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
#include "AppConfig.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Color.h"
// ★★ 2026-10-21：`OnWindowResized` 的 override 签名需要完整类型。
//   `Application.h` 里只有前置声明（`class WindowResizedEvent;`）——
//   前置声明够写引用形参，但 override 检查要在派生类里比对基类签名，
//   完整定义更稳妥（且 `event.GetWidth()` 必须有定义）。
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
// ★★ 2026-10-21：`SynthesizeKey(ECDI::KeyCode, bool)` 的形参需要完整枚举，
//   而按值传参**不能**用前置声明（枚举无法前置声明）⇒ 必须包这个头。
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
// ★ P4 Phase B：SynthesizeKey 第三参（KeyModifier 按值）与 HandleGlobalKeyDown
//   的形参都需要完整枚举定义。
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/RingProgressBar.h"
#include "ECDI/Widget/ScrollView.h"
// ★ P4 Phase A（2026-10-25）：RefreshJumpButton 要读垂直条实际宽度让位，
//   GetVerticalScrollBar() 只回前置声明——调用其成员必须有完整类型。
#include "ECDI/Widget/ScrollBar.h"

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
    //     · grow：客户区变大 ⇒ 每一行的宽度**都**跟着变大，且 `m_scroll` 变高
    //     · shrink：客户区变小 ⇒ 行宽度**都**变小，且**不出现横向滚动条**
    //       （横向滚动条是"行比视口宽"的机器可读证据，`GetMaxOffsetX() > 0`）
    //     · caption：标题栏宽度 == 客户区宽度（三个系统钮必须贴着右上角）
    //     · roundtrip：回到原尺寸 ⇒ 行宽**逐位**回到原值（不是"差不多"）
    //     · negative：只改一个自变量（把 root 改窄但不调 `OnWindowResized`）⇒
    //       行宽必须**不**变，证明"行宽跟着窗口变"是那条重排链做的，不是巧合
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
};

class HarnessApp;   // 前置（HarnessApplication 转发用）

// Application 子类：只把本应用的三个定时器路由进 HarnessApp，
// 其余（光标闪烁 / 动画 tick）转发基类——与 ECDI ModelProbe 同款先例。
//
// ★★ 2026-10-21：新增 `OnWindowResized` 转发（窗口可拉伸）。
//   为什么**必须**在 Application 层接，而不是在某个 Widget 上接：
//   `WindowResizedEvent` 不是坐标事件（没有 HitTest 目标），框架把它交给
//   `Application::OnWindowResized`。而基类那一份**只**同步 `RootWidget` 的尺寸——
//   它不知道转录行是「按内容宽度测出来」的，也不知道 caption 是定宽的。
//   ⇒ 基类做完它那一步之后，我们还必须再走一遍**我们自己的**重排链。
//   ★ 调用 `ECDI::Application::OnWindowResized(event)` 在前：基类负责 root 尺寸，
//     我们的重排依赖它已经生效（漏掉就是按旧宽度重排一次，白跑）。
class HarnessApplication : public ECDI::Application {
public:
    void SetOwner(HarnessApp* owner) { m_owner = owner; }

protected:
    void OnTimer(const ECDI::TimerEvent& event) override;
    void OnWindowResized(const ECDI::WindowResizedEvent& event) override;
    // ★★ P4 Phase A（2026-10-25）：滚轮/键盘在基类派发**之后**刷新「跳到最新」
    //   （顺序 = 滚动先真发生，再读 offset 算显隐）。
    // ★★ P4 Phase B：OnKeyDown 在转基类**之前**先判全局快捷键（基类是派发器，
    //   单收件人语义下焦点控件会先吃掉 Ctrl 组合）——命中即 SetHandled 返回，
    //   不再派给焦点控件；未命中原样转基类再做 Phase A 的导航键刷新。
    void OnMouseWheel(const ECDI::MouseWheelEvent& event) override;
    void OnKeyDown(const ECDI::KeyDownEvent& event) override;

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
    // ★★ 2026-10-21：窗口客户区变了（用户拉伸窗口）。
    //   契约（与 `RebuildUi` 同一族，但**不**重建树）：
    //     ① 基类先把 root 尺寸同步好（由 `HarnessApplication` 保证顺序）；
    //     ② caption 是**定宽**创建的（`SetSize(1120, ...)`），必须跟着改宽；
    //     ③ 转录行宽度是按 `ContentWidth(m_scroll)` 量出来的 ⇒ 必须 `Relayout()`
    //        重测，否则大窗口里文字仍按 1120 时的宽度折行、只用到左侧一条；
    //     ④ `FollowTailIfPinned()`：用户在底部时，重排后要保持贴底（否则一拉伸
    //        就被甩到上面去）。
    //   ★ 不做 `RebuildTranscriptView`：尺寸变化不动模型，重建是白花的测量成本，
    //     而且会把滚动位置和展开态再跑一遍（`Relayout` 已足够）。
    void OnWindowResized(int width, int height);
    void SendPrompt(const std::string& text);     // P1.5：发送路径（按钮与自动发共用）
    // ★★ P4 Phase A（2026-10-25）：「跳到最新」按钮刷新（HarnessApplication 的
    //   滚轮/键盘 override 在基类派发完之后调）。全部只读查询 + 同值守卫的
    //   SetVisible（M9：同值不 Invalidate）⇒ 高频调用零重绘代价。
    void RefreshJumpButton();
    // tick 脏检查：offset 与缓存不等才真正刷新（拖滚动条滑块没有事件回调，
    //   靠 caret 闪烁定时器的节拍兜底——禁止无条件刷新，CountingPanel 读数纪律）。
    void DirtyRefreshJumpButton();
    // ★★ P4 Phase B（2026-10-25）：全局快捷键分派（HarnessApplication::OnKeyDown
    //   在转基类**之前**调用）。命中返回 true ⇒ 调用方 SetHandled 且不派给焦点控件。
    //   表：Ctrl+B 侧栏 · Ctrl+I 检视栏 · Ctrl+, 设置 · Ctrl+N 新建会话 ·
    //   Esc 按 设置 > ···菜单 优先级逐层关（模型浮窗 Phase H 接入后插中间层）。
    //   ★ 刻意不拦 Ctrl+A/C/X/V/Z（TextBox 编辑语义）与 Enter（PromptBar 发送）。
    bool HandleGlobalKeyDown(const ECDI::KeyDownEvent& event);
    // ★★ P4 Phase B：检视栏显隐的唯一入口（中栏头 `≡` 按钮与 Ctrl+I 共用；
    //   抽出前逻辑内联在按钮回调里，全局键没有可复用的路径）。
    void ToggleInspector();
    // ★★ P4 Phase E：tooltip 单例浮层。ShowTooltipFor 记锚点 + 起 400ms 定时器
    //   （kTooltipTimer 到点才显示——TipButton::OnMouseEnter 调）；leave/点击
    //   即 HideTooltip。浮层挂 m_scroll（最后子 = 最上层）。
    void ShowTooltipFor(const ECDI::Widget* anchor, const std::string& text);
    void HideTooltip();
    // kTooltipTimer 到点：若锚点仍在（未 leave），定位并显示浮层。
    void OnTooltipFire();

    static constexpr int kReplayTimer = 100;   // ~30Hz（33ms）
    static constexpr int kCloseTimer = 101;    // --auto-close
    static constexpr int kSweepTimer = 102;   // --scroll-sweep 步进
    static constexpr int kTooltipTimer = 103; // ★ P4 E：tooltip 400ms 延迟

private:
    // 顶栏 / 侧栏 / 检查器 / 状态栏里的动态文本（非拥有——Widget 树所有）
    struct Hud {
        ECDI::Label* title = nullptr;        // 顶栏：harness — fixture
        ECDI::Label* rowChip = nullptr;      // 顶栏：autosize | listlayout
        ECDI::Label* backendChip = nullptr;  // 顶栏：gdi ▾ / gl ▾
        ECDI::Label* sessChip = nullptr;     // 顶栏：会话 id chip
        ECDI::Panel* statusDot = nullptr;    // 顶栏状态圆点（ok/dim 随 state 翻色）
        ECDI::Panel* sbDot = nullptr;        // 状态栏圆点
        // ★★ P3.5（2026-10-24）：设置面板（首启引导 / `···` 菜单「设置」入口）。
        //   面板占中栏 scroll 的槽位；显隐是模型侧状态 `m_settingsVisible`
        //   （重建后按它恢复——与 `m_sidebarVisible` 同一条纪律）。
        ECDI::Panel* settingsPanel = nullptr;    // 设置 sheet（P4 F 起挂 m_scroll 浮层）
        // ★★ P4 Phase F（2026-10-25）：模态遮罩（Button 铺满转录视口）。
        //   Button 而非 Panel：Panel::ContainsPoint 恒 false（输入透传）挡不住
        //   点击；Button 可命中 + 点击被消费（空回调）= 点击拦截。半透明色
        //   = ECDI #71/REQ-11 的首个半透明消费者（DrawRect 路径经 BlendAlphaSolid
        //   消费；双后端实证，见方案 §8 评审定案）。
        ECDI::Button* settingsMask = nullptr;    // 遮罩（点击吞掉；关闭走 Esc/Ctrl+,）
        // ★★ P4 F 修复（实测反馈）：字段区 ScrollView 句柄——浮层面板 Arrange
        //   递归摆字段后，还需 UpdateContentExtent 重算内容范围（否则 extent=0，
        //   字段区不可见/不可滚）。
        ECDI::ScrollView* settingsSv = nullptr;  // 设置面板内部滚动区
        ECDI::TextBox* settingsBase = nullptr;   // BaseURL（单行）
        ECDI::TextBox* settingsKey = nullptr;    // API Key（单行 + Password 掩码）
        ECDI::TextBox* settingsModel = nullptr;  // 模型名（单行）
        // ★ P3.6：容量挡位（上下文 200K/500K/1M；输出 4K/8K/16K/32K/64K）+ 提供商列表。
        ECDI::TextBox* settingsContext = nullptr;  // 上下文容量（token，数字）
        ECDI::TextBox* settingsMaxOut = nullptr;   // 输出上限 max_tokens（数字）
        ECDI::Panel* settingsProviderList = nullptr; // 提供商行容器（RebuildSettingsProviderList）
        ECDI::Label* settingsHint = nullptr;     // 状态/错误行（非敏感：不含 key）
        ECDI::Button* settingsSave = nullptr;    // 保存并连接
        // ★ P3.6：检查器「模型」卡片（提供商列表 + 切换）。
        ECDI::Panel* modelList = nullptr;        // 行容器（children 由 RebuildModelList 重建）
        // ★★ P4 Phase H（2026-10-25）：顶栏模型 chip + 下拉浮窗。
        ECDI::Button* modelChip = nullptr;       // 顶栏：`提供商 › 模型 · N% ▾`
        ECDI::Panel* modelPopup = nullptr;       // 浮窗壳（挂 m_scroll，顶部下拉）
        ECDI::TextBox* modelSearch = nullptr;    // 浮窗顶部搜索框
        ECDI::Panel* modelPopupList = nullptr;   // 行容器（按搜索词重建）
        ECDI::Label* modelPopupFoot = nullptr;   // 底注（仅 idle 发出 · 运行中排队）
        ECDI::Label* stateChip = nullptr;    // 顶栏：running / idle
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
        // ★★ 2026-10-20：中栏头 `↗`（导出转录）与 `···`（菜单开关）两个钮。
        //   此前 `↗`/`···` 都是**空回调**（`mkAct` 里 `(void)tip; return;`）——
        //   点得下去但什么也不发生，是纯粹装饰。这里给它们真实接上。
        //   ★ 句柄进 Hud 的理由同 `inspectorToggle`：探针要能合成点击。
        ECDI::Button* exportToggle = nullptr;    // 中栏头 `↗`：导出转录到文件
        ECDI::Button* moreToggle = nullptr;      // 中栏头 `···`：菜单显隐
        // ★★ 2026-10-20：`···` 的菜单条。
        //   ★ 为什么是「内联下拉条」而不是浮层：ECDI **没有**菜单/弹出层控件，
        //     也**没有**「跳过布局」机制（`grep IgnoreLayout|absolute` 零命中），
        //     而 `counting` 是 `VerticalLayout` ⇒ 挂上去的子控件必被布局接管。
        //     浮层只能挂在 root 上再在每次 `Arrange` 后手动重定位，那要跟整树重建、
        //     主题切换、侧栏收起三条路径同时对齐——一个漏了就错位。
        //     内联条让布局**自己**处理位置：菜单是 `counting` 的第 2 个子控件，
        //     藏在 `head` 与 `scroll` 之间，显隐只影响自己占不占槽。
        ECDI::Panel* moreMenu = nullptr;         // 菜单条容器（显隐切换）
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
        // ★★ P4 Phase A（2026-10-25）：「↓ 跳到最新」浮层按钮。
        //   ★ 挂在 **ScrollView 的直接子节点**（不是 ScrollContent）：ScrollView.h
        //     树结构注释明写「装饰与交互子节点必须待在偏移层之外」——滚动条就是
        //     这么挂的（固定视口、不随内容滚）。ScrollView 不接管 Arrange、
        //     ApplyLayout 只定位条与内容节点 ⇒ 没有布局会动它；HitTest 逆序
        //     遍历子节点 ⇒ 后加的它在最上层、优先命中。
        //   ★ 不挂 CountingPanel：counting 是 VerticalLayout，挂上去会被布局
        //     接管成占槽兄弟（设置面板/菜单条是「槽位」不是「浮层」——见上方
        //     moreMenu 的注释，那里记了为什么不能用浮层）。ScrollView 内部
        //     恰好是「无布局、手动定位」的口子，正好承载浮层。
        ECDI::Button* jumpLatest = nullptr;   // 视口右下角浮层（显隐 = 离开贴底态）
        // ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层（Panel+Label 挂 m_scroll
        //   最后子 = 最上层；ECDI 无 Tooltip 原语，harness 拼——见 UiKit::TipButton）。
        ECDI::Panel* tooltip = nullptr;           // 浮层壳（隐藏态常驻）
        ECDI::Label* tooltipText = nullptr;       // 提示文本（SetText 换文案）
        // ★★ P4 Phase C（2026-10-25）：空状态引导卡（挂 m_content 顶部）。
        //   ★ 空转录（rows 0 且无 pending 气泡）⇒ 显示；任何行/pending 到达即拆。
        //   ★ 修的死胡同：首启（firstRun）关掉设置面板后屏幕全空（App.cpp 原注释
        //     记录过该场景）——现在引导卡给出「打开设置」出口。
        ECDI::Panel* emptyGuide = nullptr;    // 空状态引导卡（m_content 子节点）
        // ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容。
        //   数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
        //   回放会话没有工作区 ⇒ 此段显示「无工作区」。
        ECDI::ScrollView* projectList = nullptr;  // 左栏：工作区目录滚动容器（收进「工作区」段）
        // ★★ 2026-10-21：`sessSession` / `sessCore` **已删除**（原注释说"待办：下一步
        //   清理时连同注释一起删"——这就是那一步）。
        //   删的理由不是"它俩占两行"，而是：它们属于"**读了但永不写**"这一类，
        //   而这类字段会让读者以为界面某处有一行会话 id/meta，实际没有。
        //   同一个家族的 `backendChip` 也已处理，并且加了一条**机检**
        //   （`--hud-probe` 的 `hud-built` 相）：凡是 `RefreshHud` 会写、
        //   而建树时从不赋值的 `Hud` 指针，都会被列出来并判红。
    };

    void BuildUi(ECDI::Window& window);
    void BuildTopBar(ECDI::Widget& root);
    void BuildMainArea(ECDI::Widget& root);
    void BuildBottom(ECDI::Widget& root);
    // ★★ 2026-10-21：**HUD 覆盖自检**——`Hud` 里被 `RefreshHud` 读的指针，
    //   建树时必须全部被赋值，否则那个 `if` 永远是死分支。
    //
    //   为什么要这条：`backendChip`（只读从不写，屏幕上从来没有这个胶囊）、
    //   `sessSession` / `sessCore`（Step 1 之后不再建，但字段与 if 都留着）
    //   是同一类缺陷的**三个实例**，而且它们在**任何**既有读数里都不可见——
    //   `RefreshHud` 写得若有其事，`if (ptr)` 的条件求值成功，只是永远为假。
    //   光靠"下次注意"修三个实例没有意义，要把它变成一个**会自己变红**的判据。
    //
    //   做法：在 `RefreshHud` 的**每个** `if (m_hud.X)` 上挂一条记录（`Touch`），
    //   收尾时把所有"碰过但从未赋值"的名字报出来。赋值在 `BuildUi` 期间发生，
    //   而 `RefreshHud` 在 `BuildUi` 之后才第一次跑 ⇒ 这个顺序使判据成立。
    //   ★ 只在 `--hud-probe` 下启用记录（`m_hudAudit`），默认零开销、零行为变化。
    void HudTouch(const char* name);          // RefreshHud 里每个 if 调一次
    // 返回"被读过但建树时从未赋值"的字段名列表（空 = 覆盖完整）。
    std::vector<std::string> HudUnbuilt() const;
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
        // ★★ P4 Phase D（2026-10-25）：answer 的**时刻**（"HH:MM"，本地时）。
        //   与 answer 同族：GUI 侧决策记录，不进协议（answer 回执是一次性的，
        //   这里的时刻是「用户何时做的决定」——审计信息，作者仍是 GUI）。
        //   sidecar 经 answerTimes 键往返；旧 sidecar 无此键 ⇒ 空 ⇒ 重建不显
        //   时刻（如实呈现「来自旧状态文件」，不编造）。
        std::string answerAt;
    };

    // 行记录：m_rows 用 deque（push_back 不失效指针——回调捕获依赖这一点）
    struct RowRec {
        ECDI::Widget* panel = nullptr;            // 行根（Panel），非拥有
        // ★★ P4 G（2026-10-25）：Label → TextWidget*（Label 与 TextBox 的共同
        //   基类）——尖峰：--selectable-rows 下 assistant 正文行用只读 TextBox
        //   （可选区/复制），其余行仍是 Label。GetText/SetText/SetWordWrap/
        //   AutoSize/SetTextColor 全在共同基类面。
        ECDI::TextWidget* label = nullptr;        // 主文本；tool 行 = 展开体文本，非拥有
        ECDI::Button* headerBtn = nullptr;        // tool 卡片标题按钮，非拥有
        ECDI::CollapsiblePanel* body = nullptr;   // tool 卡片展开体，非拥有
        ECDI::Button* allowBtn = nullptr;         // ask 行按钮，非拥有
        ECDI::Button* denyBtn = nullptr;
        // ★★ P4 Phase D：已答结论行（● 已允许/已拒绝 · 时刻）。BuildAskRow 预建
        //   （隐藏），ApplyAnsweredLook 填文本/色并显示——首答与重建同一函数 ⇒ 同形。
        ECDI::Label* conclusion = nullptr;
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
        // ★★ 2026-10-20：归档标志（m00001 第 6 步残留）。
        //   归档 = 「不活动但可打开且不占资源」。会话仍留在 m_sessions 里（模型完整），
        //   只是从侧栏主列表隐藏，移到「归档」区。点「↩」恢复 = 取消归档 + 切到它。
        //   ★ 为什么不用独立列表：m_sessions 的下标是会话身份的一部分（探针按
        //     下标断言），搬出去会让所有下标漂移；留在原位只改一个 bool 最安全。
        bool archived = false;
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
        // ★ P3.6（协议 v4）：提供商/模型热切换。
        std::string providerId;          // 当前提供商（spawn 的 --provider-id；model ack 更新）
        // ★★ P4 H：当前模型名（hello 不带——live 会话创建时从 config 预填，model
        //   ack 更新；回放会话为 "?"，chip 如实显示）。
        std::string modelName;
        bool hasModelCap = false;        // hello.caps 声明了 "model"
        int ctxCapacity = 0;             // 当下分母：hello.context ⇒ usage.context ⇒ model ack 逐级覆盖
        std::string pendingModelId;      // 已发出未确认的切换请求 id（I11 在途）
        std::string queuedModelProvider; // running/waiting 时排队；state 回 idle 后发出

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
    //   ★★ P4 Phase D（2026-10-25）：两钮**隐藏** + 行内结论标记（● 已允许/已拒绝
    //   + 时刻，绿/红）——替代原「选中者留色、另一个灰化」。理由：权限决定是
    //   审计事实，置灰两钮的形态既说不出结论也占着两个死按钮的位置。
    //   `when`（"HH:MM"）空 = 来自旧 sidecar 的重建——如实不带时刻段。
    void ApplyAnsweredLook(RowRec& rec, const std::string& choice,
                           const std::string& when);

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
    // `ArchiveSessionAt()` / `RestoreSessionAt()`：归档 / 恢复（**唯一入口**，点击与探针共用）。
    //   ★ 归档 = 置 `Session::archived = true`，会话留在 m_sessions 原位（下标不动），
    //     只是从侧栏主列表隐藏、移到「归档」区。
    //   ★ 归档活动会话 ⇒ 先切到邻居（同 DeleteSessionAt 的纪律），否则活动指针
    //     指向一个不在主列表里的会话，HUD 会显示一个用户看不到的会话。
    //   ★ 恢复 = 置 archived=false + SwitchToSession（用户点「↩」的心智模型是
    //     「打开它」，不是「取消归档但留在原地」）。
    bool ArchiveSessionAt(size_t index);
    bool RestoreSessionAt(size_t index);
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

    // ── ★★ 2026-10-20：中栏头两个钮（`↗` 导出 / `···` 菜单）─────────────
    // 这两件此前都是 `mkAct` 里的**空回调**：按钮有 hover/pressed 反馈，点下去
    //   什么也不发生。它们比其它「空回调」更难被发现——`↗`/`···` 看起来像真功能。
    //
    // `ExportTranscript()`：把活动会话的转录写成文本文件（弹出保存对话框）。
    //   ★ 导出的对象是 **`m_sess->timeline`**（视图看到的那一份），不是
    //     `Transcript::m_blocks`——后者不含通知行/问句行，导出会悄悄丢掉
    //     `[error]`/`[协议不兼容]` 这些「这次运行可不可信」的读数。
    //   ★ 导出是**只读投影**：不碰模型、不碰协议、不改 seq。回放会话也能导。
    //   ★ 返回 false = 用户取消或写入失败（取消不写通知行：取消不是事件）。
    bool ExportTranscript();
    // ★★ 2026-10-21：导出拆三段的产物（理由见 `.cpp` 里的头注）。
    //   `BuildTranscriptExportText()` 是**纯函数**：同样的模型出同样的文本，
    //   不碰控件、不碰文件、不弹对话框 ⇒ 探针可以直接读它，无需合成点击。
    //   `WriteTranscriptExport()` 只负责落盘；失败时把原因写进 `*error`。
    std::string BuildTranscriptExportText() const;
    bool WriteTranscriptExport(const std::filesystem::path& path, std::string* error);
    // 探针专用：非空 ⇒ `ExportTranscript()` 跳过保存对话框，直接写这个路径。
    //   ★ 唯一的存在理由：`GetSaveFileNameW` 是模态循环，探针点上去会挂死。
    //     默认空 ⇒ 产品路径零行为变化。
    std::filesystem::path m_menuProbeExportPath;
    // `ToggleMoreMenu()`：`···` 菜单条显隐（**唯一入口**，按钮与探针共用）。
    //   ★ 纪律同 `SetSidebarVisible`：瞬时切换 + 重排**父容器**，不用动画。
    //     `counting` 是 `VerticalLayout`，隐藏子不占槽 ⇒ 不 Arrange 的话
    //     菜单条消失的位置会留着一条 34px 空洞。
    void ToggleMoreMenu();
    void SetMoreMenuOpen(bool open);

    // ── ★★ P3.5（2026-10-24）：设置面板 / 无参启动 / 凭据注入（规格 §8.5）──
    // 显隐切换 = 瞬时 SetVisible + 父容器 Arrange（无动画，理由同 SetSidebarVisible）；
    // 显隐**同时**作用于 scroll 与 composer（设置面板占满中栏，二者腾槽）。
    void SetSettingsVisible(bool visible);
    // 把模型侧显隐投到控件上（BuildUi 首帧与 RebuildUi 重建共用同一条路径——
    // 「投影只存在于一条路径」正是 2026-10-21 起始幻影 34px 空洞的教训）。
    void ApplySettingsVisibility();
    // ★★ P4 F：遮罩 + 居中 sheet 的几何（sheet = clamp(视口-80, 280..620 ×
    //   240..560)）。浮层无父布局 ⇒ SetSize 后自己 Arrange（内部 stretch 生效）。
    void LayoutSettingsOverlay();
    // 设置面板「保存并连接」：校验 → CredWriteW → Save 配置 → 拉起 live 会话。
    // 失败 ⇒ settingsHint 显示**非敏感**原因（不含 key），面板保持打开。
    void OnSettingsSave();
    // 解析 ecdi-core.exe：config.corePath 覆盖 → cwd 相对 core/bin → exe 目录变体。
    bool ResolveCoreExecutable(std::filesystem::path& out) const;
    // 无参启动：配置 + 凭据都可用 ⇒ 填 options.corePath/coreArgs + m_coreApiKey
    // 返回 true（构造函数随后照常建 live 会话）；否则 false + 面板打开。
    bool TryAutoConnectFromConfig();
    void RunSettingsProbe();
    // ── ★★ P3.6（协议 v4）：模型热切换 ─────────────────────────
    void RebuildModelList();            // 检查器模型卡片行重建（tick 执行）
    void RebuildSettingsProviderList(); // 设置面板提供商行重建（编辑目标选择）
    void RequestModelSwitch(const std::string& providerId);
    void SendModelSwitch(const std::string& providerId);   // 立即发（调用方保证 idle）
    void SendPendingModelSwitchIfIdle();                   // state=idle 时的排队兑现点
    void CacheProviderKeys();           // 凭据库 → m_providerKeys（全部提供商）
    static std::string EnvSanitizeProviderId(const std::string& id);
    // ── ★★ P4 Phase H（2026-10-25）：顶栏 chip + 模型下拉浮窗 ─────────────
    //   浮窗挂 m_scroll（F 同款浮层口）；显隐 = m_modelPopupOpen 模型侧状态。
    void SetModelPopupOpen(bool open);   // 唯一入口（chip 点击 / Ctrl+M / 行点击关）
    void RebuildModelPopupList();        // 按搜索词重建行（开窗 + 搜索变化时）
    void UpdateModelChip();              // chip 文本（提供商 › 模型 · N% ▾）

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
    // ★★ P4 Phase C（2026-10-25）：空状态引导。RefreshEmptyGuide 幂等——
    //   空且无 → 建；非空且有 → 拆（RemoveChild 丢弃所有权）；空且有 → 宽度跟随。
    //   刷新点：MaterializePendingPrompts 尾部（覆盖全部行到达 + pending 变化）、
    //   Relayout 尾部（宽度跟随）；RebuildTranscriptView 拆树后指针置空。
    bool TranscriptIsEmpty() const;
    void RefreshEmptyGuide();
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
    // ★★ 2026-10-21：自绘标题栏的句柄（窗口可拉伸后必须跟着改宽）。
    //   为什么必须留句柄：它是按 `SetSize(1120, ...)` **定宽**创建的，而
    //   `VerticalLayout` 的跨轴填充要 `fillCrossAxis=true`——`root` 的布局是
    //   `VerticalLayout(0, true)`，理论上会把它拉到全宽；但 `ThemedCaptionBar`
    //   自己内部还按 `GetWidth()` 排三个系统钮，**排过一次就不会再排**
    //   （没有 SetSize 触发），所以宽变了必须显式 `SetSize` 一次。
    ECDI::Widget* m_caption = nullptr;             // 非拥有
    std::vector<PendingPromptView> m_pendingPromptViews; // 当前活动会话的 UI-only 气泡视图
    bool m_inspectorVisible = true;
    // ★★ 2026-10-20：`···` 菜单条的开合状态。
    //   ★ 为什么这个 bool 必须住在模型侧而不是控件里：`RebuildUi()`（主题切换）
    //     会把整棵树拆掉重建。若开合态只活在 `ecdi::Panel::IsVisible()` 上，
    //     一次换主题就会把用户刚拉开的菜单**静默合上**——正是 0c/Step 2 踩过的
    //     「重建即丢状态」同一坑。`RebuildUi()` 第⑩步会按它重新投影。
    bool m_moreMenuOpen = false;
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
    // ★★ P3.5（2026-10-24）：设置面板显隐是**模型侧状态**（与 m_sidebarVisible
    //   同一条纪律：只存在于控件上的显隐会被任何重建丢掉）。首启无配置 ⇒ true。
    bool m_settingsVisible = false;
    // 注入 core 的 API key（env block 通道，规格 §8.5 D5）。
    // ★ 不"用完即清"：每个新会话（＋按钮/临时会话）都要再拉一个 core，
    //   env 注入是每次 spawn 都走同一条路。key 留在 GUI 进程内存属可接受形态
    //   （同一信任域，positioning §4）；纪律是**绝不**进日志/摘要/转录。
    std::string m_coreApiKey;
    // 应用配置（非秘密 + 凭据目标名引用；文件在 %APPDATA%，结构上无密钥字段）。
    AppConfig m_appConfig;
    // 提供商 id → key（凭据库的本次进程缓存；spawn 注入 env 用。绝不进日志/摘要）。
    std::map<std::string, std::string> m_providerKeys;
    bool m_modelListDirty = false;   // 检查器模型列表重建（tick 执行，同 m_sidebarDirty 族）
    // ★★ P4 H：模型浮窗开合（模型侧状态——重建后按它恢复，同 m_moreMenuOpen）。
    bool m_modelPopupOpen = false;
    std::string m_editingProviderId; // 设置面板当前编辑的提供商（空 = 新增）
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

    // ★★ 2026-10-21：中栏头 `↗`/`···` 的取证（--menu-probe）。
    //   形状照抄 `RunSidebarProbe`：同步跑完即 Exit（无动画、无跨 tick 等待）。
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
    std::vector<MenuProbeRec> m_menuProbeLog;
    bool m_menuProbeDone = false;
    void RunMenuProbe();
    // 探针写入的临时文件（收尾会删掉：夹具目录/仓库保持零改动）
    std::filesystem::path m_menuProbeTempFile;
    // ★★ 2026-10-21：窗口拉伸探针（--resize-probe）。形状同 `RunMenuProbe`：
    //   同步跑完即 Exit。★ 它**会真的改窗口尺寸**（`SetBounds`），因此必须在
    //   `--out` 落盘之前跑完，且跑完要恢复原尺寸——否则摘要里的 paints/frames
    //   会带上探针自己制造的重排成本。
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
    std::vector<ResizeProbeRec> m_resizeProbeLog;
    bool m_resizeProbeDone = false;
    void RunResizeProbe();
    // 探针用：把窗口设成给定客户区尺寸（走真正的平台 `SetBounds`）。
    //   ★ 用它而不是直接 `root.SetSize`：后者绕过了 `Window::OnResized`，
    //     会把"事件真的到达了吗"这个命题整个跳过（同"合成点击 vs 直接调函数"）。
    void ProbeResizeClient(int width, int height);
    // ★★ 2026-10-21：窗口缩放的**下限**（客户区，DIP）。
    //   为什么要有：用户可以直接把窗口拖到很小。实测（`.scratch/scan_min_size.py`
    //   逐档扫 + `bound-880.png` / `bound-860.png` 两张截图）：
    //     · 860 宽 —— 中栏头第三个钮 `···` **被裁掉**（中栏 430 < 头部需要的 444）；
    //     · 880 宽 —— 三个钮都在，一切正常。
    //   ⇒ 下限不是"拍脑袋的舒适值"，是从**控件的固定尺寸**反推出来的：
    //       中栏头 = padding 14×2 + 标题 300 + 三个钮 28×3 + 间隙 8×4 = **444**
    //       两翼   = 侧栏 200 + 检查器 230                              = **430**
    //       客户区最小宽 = 444 + 430 = **874**（取整 880，留 6px 余量）
    //   高度同理：顶栏 36 + 会话头 62 + composer 92 + 状态栏 26 = **216** 固定，
    //   转录视口至少留 240（约 5 行）⇒ 客户区最小高 = **456**（取整 460）。
    //
    //   ★★ 2026-10-23（ECDI REQ-13 落地）：**下限由框架落实**，本仓不再自己夹。
    //     · 登记点：`App.cpp` 建窗处 `win.SetMinimumSize(kMinClientW, kMinClientH)`；
    //     · 机制（框架侧）：`WM_GETMINMAXINFO` 的 `ptMinTrackSize` +
    //       `WM_WINDOWPOSCHANGING` 的 `cx/cy` 夹，两者都在**应用尺寸之前**生效
    //       ⇒ 原生拖拽与程序化 `SetBounds` 都被拦住，且**没有回弹**；
    //     · 客户区↔整窗的换算也归框架（`SetMinimumSize` 收的是客户区 DIP，
    //       与 `GetClientSize` 同口径）——本仓原来自己补边框差值的那段已删。
    //   ★ 为什么删掉自夹而**不是**留作双保险：两处都夹 ⇒ 任何一侧失效都会被
    //     另一侧掩盖（`max()` 让"框架没拦住"看起来像"我们在工作"）。单点责任
    //     才可证伪，负对照见 `RunResizeProbe` ⑥（临时清零下限 ⇒ 请求必须真的落下去）。
    //   ★ 常量留在本仓是对的：下限值来自**本仓控件**的固定尺寸，不是框架的知识。
    static constexpr int kMinClientW = 880;
    static constexpr int kMinClientH = 460;

    // ★★ 2026-10-21：`Hud` 覆盖自检的状态（见 `HudTouch` 的声明注释）。
    //   `m_hudAudit` 默认 **false**：默认零开销、零行为变化（本仓铁律）。
    //   探针打开它之后，`RefreshHud` 里每个读取点登记一次字段名；
    //   收尾时把"登记过、但建树时从未赋值"的字段名报出来。
    bool m_hudAudit = false;
    std::vector<std::string> m_hudTouched;      // 读过的字段名（去重不必要，逐次记）

    // ★★ 2026-10-21：HUD 覆盖探针（--hud-probe）。同步跑完即 Exit。
    struct HudProbeRec {
        std::string phase;              // hud-built / hud-built-negative
        size_t touched = 0;             // RefreshHud 读过多少个 Hud 字段
        size_t unbuilt = 0;             // 其中多少"从未被赋值"
        std::string unbuiltNames;       // 逗号分隔的字段名（空 = 全覆盖）
        bool matched = true;
        std::string firstDivergence;
    };
    std::vector<HudProbeRec> m_hudProbeLog;
    bool m_hudProbeDone = false;
    void RunHudProbe();

    // ★★ P3.5（--settings-probe）：设置面板 + 凭据/配置/env 注入判据。
    //   相位：first-run 可见 / save（配置+凭据+key 不进配置文件）/
    //   session-live（保存后 live 会话拿到 hello）/ env-check（显式 env block
    //   真把变量带给子进程，用 cmd.exe /c set 直接取证）/ cleanup。
    struct SettingsProbeRec {
        std::string phase;
        bool matched = true;
        std::string firstDivergence;
    };
    std::vector<SettingsProbeRec> m_settingsProbeLog;
    bool m_settingsProbeDone = false;
    std::string m_settingsProbeKey;   // save 相输入的 key（判据 5 用：不得出现在配置文件）
    int m_settingsProbeStage = 0;     // 跨 tick 步进（save 点击后要等 hello 下一 tick 才到）
    std::chrono::steady_clock::time_point m_settingsProbeNextAt;

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
    std::vector<EnterProbeRec> m_enterProbeLog;
    bool m_enterProbeDone = false;
    void RunEnterProbe();
    // 合成一次真实按键：走 `Application::OnEvent`，与系统消息到达后同源。
    //   ★ 与 `SynthesizeClickAt` 同一纪律：不直接调 `PromptBar::Submit()`，
    //     否则"事件真的派给了输入框吗"这个命题被整个绕过。
    void SynthesizeKey(ECDI::KeyCode code, bool shift = false,
                       ECDI::KeyModifier extra = ECDI::KeyModifier::None);

    // ★★ 2026-10-21：输入框自动长高探针（--grow-probe）。
    //
    //   【它要证明什么】`PromptBar::DesiredInputHeight` 那套组合实现**真的接到了事件链上**，
    //   而不是"函数算得对、但没人调用它"（本仓 0c 记下的那个家族：
    //   函数对、按钮没接线）。故每一相都用 `SynthesizeKey` 打**真按键**，
    //   绝不直接调 `SyncInputHeight()`——直接调会把"文本变化真的会触发长高吗"整个绕过。
    //
    //   【为什么必须有一相盯"缩回去"】最容易踩的陷阱是用
    //   `GetMaxScrollOffset() + GetTextAreaHeight()` 当内容高：那个式子带 `max(0, ...)`，
    //   **不溢出时恒等于视口高** ⇒ 框只会变高、永远缩不回来（删掉换行也不变矮）。
    //   这种实现在"长高"相里是**全绿**的，只有在"变矮"相才会红。故 `shrink` 相不是补充，
    //   它是这个功能唯一的负对照方向。
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
    std::vector<GrowProbeRec> m_growProbeLog;
    bool m_growProbeDone = false;
    void RunGrowProbe();

    // ★★ P4 Phase A（--jump-probe，2026-10-25）：「跳到最新」按钮判据。
    //   形状同 RunGrowProbe：同步跑完即 Exit。
    //   ★ 判据（每条都能变红；不假设"加载完=贴底"——instant 大夹具填满时
    //     FollowTailIfPinned 判非 pinned 不跟随，属既有行为）：
    //     · initial-consistency —— 显隐 == 离开贴底带（不变量，任何初始态）；
    //     · after-click-tail —— 点击 ⇒ offset==max ∧ 隐藏；
    //     · after-wheel-up —— 合成两次上滚（80px > 60 阈值）⇒ 可见 ∧ 离开贴底带
    //       （负对照：滚轮没滚则"可见"是常显假绿）；
    //     · after-click-again —— 再点 ⇒ 回底 ∧ 隐藏（往返闭环）。
    //     · skipped-short-fixture —— 内容装不满视口时如实记跳过（matched=false，
    //       由跑批判据豁免；不许静默绿）。
    struct JumpProbeRec {
        std::string phase;        // initial-consistency / after-click-tail /
                                  // after-wheel-up / after-click-again / skipped
        bool visible = false;     // 按钮此刻 IsVisible()
        int offset = 0;
        int maxOffset = 0;
        bool matched = true;
        std::string note;         // 判据失败原因（ASCII——省 JSON 转义）
    };
    std::vector<JumpProbeRec> m_jumpProbeLog;
    bool m_jumpProbeDone = false;
    void RunJumpProbe();
    // 脏检查缓存：上次刷新时的 offset（DirtyRefreshJumpButton 用）。
    int m_jumpLastOffsetY = -1;

    // ★★ P4 Phase B（--key-probe，2026-10-25）：全局快捷键判据。
    //   形状同 RunJumpProbe：同步跑完即 Exit。
    //   ★ 判据（每条都能变红）：
    //     · ctrl-b（focused/unfocused 两态）—— 模型位翻转 ∧ 控件 IsVisible 跟随
    //       （A==B）∧ 再按翻回（往返）；
    //     · ctrl-comma / esc-settings —— Ctrl+, 开设置 ⇒ A==B；Esc ⇒ 关（A==B）；
    //     · esc-priority —— 设置+菜单同开 ⇒ 第一次 Esc 只关设置（优先级取证，
    //       负对照：若 Esc 无分层，这一相读数区分不出"全关了"）⇒ 第二次关菜单；
    //     · ctrl-n —— 会话数 +1；
    //     · passthrough —— 焦点在输入框合成 Backspace ⇒ 文本真的少一字符
    //       （负对照：拦截器若吞掉未列键，编辑路径必红）。
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
    std::vector<KeyProbeRec> m_keyProbeLog;
    bool m_keyProbeDone = false;
    void RunKeyProbe();

    // ★★ P4 Phase C（--empty-probe，2026-10-25）：空状态引导判据。
    //   同一开关按会话形态分流：回放模式只做负对照（加载/Ctrl+N 后不得出现），
    //   firstRun 空壳会话做正对照（关设置 ⇒ 引导出现 + 「打开设置」接线 + notice 拆除）。
    //   ★ 判据：
    //     · replay-loaded / after-new-session —— 引导不存在（负对照；Ctrl+N 的
    //       空会话带 notice ⇒ 按方案「notice 到达即拆」语义也不得出现）；
    //     · firstrun-open —— 首启设置面板开 ∧ 引导已建（可见性由 scroll 决定）；
    //     · closed-settings —— Ctrl+, 关设置 ⇒ scroll 可见 ∧ 引导可见；
    //     · click-reopens —— 合成点击引导上的「打开设置」⇒ 设置重开（接线判据）；
    //     · dismiss-on-notice —— AddNoticeRow（notice 渲染唯一入口；合成协议
    //       notice 需要真 core，firstRun 场景没有）⇒ 引导被拆。
    struct EmptyProbeRec {
        std::string phase;
        bool settingsVisible = false;
        bool guideExists = false;      // m_hud.emptyGuide != nullptr
        bool guideVisible = false;     // 控件自身位 ∧ scroll 可见（父链语义）
        size_t rowCount = 0;
        bool matched = true;
        std::string note;
    };
    std::vector<EmptyProbeRec> m_emptyProbeLog;
    bool m_emptyProbeDone = false;
    void RunEmptyProbe();
    // ★★ P4：click-reopens 相的命中者回读（「点不到」的取证读数）。
    std::string m_emptyProbeClickTarget;
    // ★★ P4 E：tooltip 延迟态（定时器在飞 = armed；到点显示）。
    const ECDI::Widget* m_tooltipAnchor = nullptr;   // 待显示锚点（TipButton）
    std::string m_tooltipPending;                    // 待显示文案

    // ★★ P4 Phase H（--model-probe，2026-10-25）：模型浮窗 + 用量判据。
    //   回放形态（cot：hello.context=128000 ⇒ 分母上屏走 v4 逐轮覆盖分母）：
    //     · ctx-denominator —— 右栏 ctxPct 文本非 "—"（分母真的上了屏）；
    //     · chip-text —— 顶栏 chip 含 `▾`（接线判据）；
    //     · ctrl-m / esc —— Ctrl+M 开浮窗（可见）⇒ Esc 关（优先级：设置 > 浮窗 > 菜单）；
    //     · search-filter —— 搜索框 SetText 后列表行数变少（负对照：清空恢复）；
    //     · click-row —— 合成点击一行 ⇒ 浮窗关 ∧ 「不是 live」notice（回放会话
    //       writer==null，RequestModelSwitch 如实拒绝——排队语义由 settings-probe
    //       的 switch-ok 相在 live 下验证）。
    struct ModelProbeRec {
        std::string phase;
        bool popupVisible = false;
        size_t listRows = 0;
        bool ctxKnown = false;
        std::string chipText;
        std::string note;
        bool matched = true;
    };
    std::vector<ModelProbeRec> m_modelProbeLog;
    bool m_modelProbeDone = false;
    void RunModelProbe();

    // ★★ P4 Phase G（--select-probe）：选中复制判据。形状同 RunJumpProbe。
    struct SelectProbeRec {
        std::string phase;        // select-and-copy / negative-focus-elsewhere
        std::string clipText;     // 回读的剪贴板（截断到 60 字节进摘要）
        bool matched = true;
        std::string note;
    };
    std::vector<SelectProbeRec> m_selectProbeLog;
    bool m_selectProbeDone = false;
    void RunSelectProbe();

    // ★★ P4（--delprov-probe）：删除所选判据。三路：
    //   dedup-load（同 id 8 份 ⇒ Load 去重剩 1）/ delete-one（2 家删所选剩 1，
    //   逐实例语义）/ keep-last（仅 1 家时拒绝删除且不崩）。
    struct DelProvProbeRec {
        std::string phase;
        size_t providersBefore = 0;
        size_t providersAfter = 0;
        std::string hint;
        bool matched = true;
        std::string note;
    };
    std::vector<DelProvProbeRec> m_delProvProbeLog;
    bool m_delProvProbeDone = false;
    void RunDelProvProbe();

    // ── ★★ REQ-03 探针读数（只在 `--async-probe` 下累加）────────────────
    //  ★ 口径：`m_apBlockedUs` 只统计**带超时**的那次等待（`NextWithin(..., 8)`），
    //    也就是 UI 线程真正挂在条件变量上的时间。这正是「拉」的代价，也是
    //    「推」要消掉的东西。★ 不统计 `timeoutMs == 0` 的调用——那不等，不是代价。
    size_t m_apTicks = 0;              // 经过 live 分支的 tick 次数
    size_t m_apBlockedTicks = 0;       // 其中真的阻塞过（等了 >0 µs）的 tick 数
    uint64_t m_apBlockedUs = 0;        // 累计阻塞 µs
    uint64_t m_apMaxBlockedUs = 0;     // 单次最长阻塞 µs
    size_t m_apLinesWake = 0;          // 由**唤醒回调**处理的行数
    size_t m_apLinesTick = 0;          // 由 tick 兜底处理的行数
    size_t m_apMaxLinesPerCallback = 0;  // 单次回调最多处理了多少行（预算是否生效）
    size_t m_apCallbacks = 0;          // 唤醒回调执行次数
    std::vector<uint64_t> m_apLatencyUs;  // 入队 → HandleLine 的墙钟差（开戳时才有）
    bool m_apEofSeen = false;
    // 安装唤醒时**值捕获**的 Application 指针（见 `InstallWakeHandler` 的注释）。
    void InstallWakeHandlers();
    void InstallWakeHandler(Session& session);
    // 唯一 drain 点（tick 兜底与唤醒回调共用；`fromWake` 只影响探针分类）。
    void DrainActiveLive(bool fromWake);
    // core 退出（EOF / bye 收尾）的统一处置——「拉」与「推」两条路径共用一份，
    // 否则两份迟早漂移（而这类漂移不会有任何读数报出来）。
    void MarkLiveEof();

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
