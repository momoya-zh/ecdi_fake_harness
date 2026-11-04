#pragma once
// ============================================================
// harness GUI 内部共享声明（2026-10-25 App.cpp 拆分产物）
// ------------------------------------------------------------
// 为什么需要这个头：App.cpp 拆分前，路径转换与几何常量住在它的**匿名命名空间**
//   里（内部链接 ⇒ 其他 TU 不可见）。探针代码搬到 Probe*.cpp 之后仍然要用
//   其中少数几个（实测：PathToUtf8 5 处、WideFromUtf8Path 1 处、
//   kPinThreshold 1 处、kSidebarRowH/kSidebarGroupH 各 1 处），
//   于是把它们提到具名命名空间 + 头文件，**定义只留一份**。
//
// ★ 纪律：这里只放「多个 TU 都要用」的东西。只被一个 .cpp 用的常量/helper
//   继续留在那个 .cpp 的匿名命名空间里——搬到这里等于扩大耦合面。
// ★ 不许把这些实现写成 inline 塞进头（会拖慢每个 TU 的编译，且不利于
//   「改一行只重编一个 .cpp」）。
// ============================================================

#include "ECDI/Core/Color.h"

#include <filesystem>
#include <optional>
#include <string>

// 前置声明（避免把 UiKit/Transcript 的整棵头树拖进每个包含者）
namespace ECDI {
class Label;
class Panel;
class ScrollView;
}
struct Block;   // Transcript.h

namespace harness {
namespace internal {

// ── 路径 UTF-8 ⇄ 宽字符（Windows API 的边界转换）──
//   定义在 PathUtil.cpp。三者都**不抛异常**：失败时返回空串（调用方按空串处理）。
std::string Utf8FromWidePath(const std::wstring& value);
std::wstring WideFromUtf8Path(const std::string& value);
std::string PathToUtf8(const std::filesystem::path& value);

// ── 几何常量（原先散在 App.cpp 匿名 ns 顶部）──
//   ★ kPinThreshold：转录「贴底」判定阈值。跟随新行与「跳到最新」按钮的显隐
//     共用同一条语义，两处各写一个数会漂移，故只留一份。
inline constexpr int kPinThreshold = 60;   // 距底 ≤ 此值 = 贴底（跟随新行 / 按钮隐藏）
//   ★ 侧栏行几何：探针（--sidebar-probe 量行高/标题行高）与产品侧建行共用。
//   ★★ 2026-11-03（用户反馈「左侧栏过于紧凑」）：28→30 / 18→20 / 4→6。
//     只改**值**，消费者一个没动——探针的 `rowAt(i)->GetHeight() == kSidebarRowH`
//     读的就是这里，所以改完仍自洽（判据是「与常量相等」，不是「等于 28」）。
//     行高 30 的额外收益：14px 名称的行盒是 19（`uikit::LineBox`），(30−19)/2 = 5
//     两侧都是 5 ⇒ 顺带修掉了 28 时的 4/5 不对称（文字整体高 1px）。
//   ★★ 同日二次（用户反馈「圆角矩形纵向加高，水平不动」）：30→34 / 20→24。
//     垂直居中仍对称：LineBox(14)=20 ⇒ (34−20)/2 = 7/7；分组标签
//     （高 `kSidebarGroupH`−4 = 20）⇒ (24−20)/2 = 2/2。
//     竖向预算代价：会话行 stride 36→40，700 客户区底部 spring 约 −16~20px
//     （30 → ~10-14，仍正）；`maxRows` 分级台阶同步 40→44（见 RebuildSessionList）。
inline constexpr int kSidebarRowH = 34;
inline constexpr int kSidebarGroupH = 24;  // 分组标题行高（分隔的高度，不带"条目"语义）
//   ★ 2026-10-25（Stage 1c）：整块侧栏几何上提。原先只在 App.cpp 用，搬出
//     AppHistory.cpp（历史会话列表建行）后成跨 TU 依赖；1e 拆 UI builder 时
//     BuildMainArea 侧栏段也要用同一组数。**值一字未改**。
//     行宽 158 而非容器宽 172 的原因见 App.cpp 原注释（滚动条占 14px）。
inline constexpr int kSidebarListW = 172;
inline constexpr int kSidebarRowW = 158;
inline constexpr int kSidebarRowGap = 6;
//   ★ 2026-11-03（用户反馈「收起状态下工作区的间隔有些低」）：分组标题行前的
//     **段界呼吸量**。会话行之间仍用 `kSidebarRowGap`（组内紧凑 = 同属一组），
//     组与组之间压段距：标题连排（收起态）= 6 + 10 = 16，与侧栏分区间距
//     （AppUiMain `kSidebarSectionSpacer` 10 + `kRowGapSide` 7 = 17）同一档。
//     只加在**标题前**（列表首标题除外）——若加在会话行后，"组尾会话 ↔ 下一组
//     标题"与"标题 ↔ 自己第一个会话"就分不开层级。
inline constexpr int kSidebarGroupGap = 10;
//   ★ 2026-11-03：`kSidebarNameW`（原 106）已删——它是死常量（全库只剩两处 using，
//     真实名称宽在 `AppSession.cpp` 的 `BuildSessionRow` 按 `curRowW` 动态算）。
//   ★ 同日：`kSidebarMaxRows` 语义从「固定可见行数」降为「**下限**」——
//     `RebuildSessionList` 按窗口高度分级放大（4 → 10），理由与预算见那边的注释。
inline constexpr int kSidebarMaxRows = 4;

// ── 转录行几何（Stage 1d 上提）──
//   ★ 原先只在 App.cpp 用；1d 之后 AppTranscript.cpp（建行/量宽）与
//     AppHud.cpp（Relayout/RefreshHud 摆位）都要读同一组数。**值一字未改**。
inline constexpr int kListRowHeight = 28;   // listlayout 模式的固定行高（DIP）
inline constexpr int kRowGap = 10;          // 行间距（原型 C 的消息间距 23px ⇒ 折半）
inline constexpr int kRowPadX = 10;
//   ★ 画布左右留白（原型 C 的 22px 画布 padding）。此前行从 x=0 起排，
//     气泡/工具卡**贴着中栏边线**——这是"看起来挤"最直接的来源。
inline constexpr int kCanvasPadX = 16;
inline constexpr int kHeaderH = 32;         // tool 卡片标题高，接近原型 C 的 39px

// ── 转录版心宽度（2026-10-27 max-width 模型；2026-11-03 改**分级** max-width）──
//   ★ 模型（照 WorkBuddy 客户端的实际表现归纳，5 张不同窗口宽度的截图）：
//       · 可用宽 < 校准锚 ⇒ 内容列**占满**（只留 `kCanvasPadX` 画布边）；
//       · 校准锚 ~ 硬顶之间 ⇒ 按可用宽的 72% 渐宽（斜率见 `TranscriptLayoutWidth()`）；
//       · 可用宽 > 硬顶 ⇒ 内容列**固定在硬顶宽**，余量**左右平分**（居中版心）。
//     这 ≠「永远扣掉检视栏的 230px」——那样小窗口里也会平白留白，观感是"布局坏了"。
//   ★ 为什么 760 不能再当唯一上限（2026-11-03 用户反馈「中栏空旷，最大宽度设小了」）：
//     760 是按「1080 宽窗口 + 侧栏 195」校准的——锚值的取值理由原样保留：
//     `kRowGap`/气泡 78% 上限的前提下，760 让气泡最宽 ≈ 593（一行 40+ 汉字）。
//     但 1920 屏 + 检视栏收起时可用 ≈ 1490，版心只占 760 ⇒ 两侧各空 ~365px，肉眼就是"空旷"。
//     ⇒ 拆两个数：`kTranscriptWMin` 保住原校准点（**可用宽 ≤ 1056 时布局逐位不变**，
//     1056 = 760/0.72 ⇒ 既有探针在原校准尺寸下读数不动），`kTranscriptWMax` 给宽屏
//     一个真正的阅读上限（1080 ≈ 一行 55 个汉字）。
inline constexpr int kTranscriptWMin = 760;   // 校准锚 / 72% 斜率的下限档
inline constexpr int kTranscriptWMax = 1080;  // 硬顶（宽屏阅读上限）

// ── 右栏检视栏宽度（2026-10-27）──
//   ★ 定宽的**唯一真相源**，消费者只有 `BuildInspector`（设检视栏本体宽）。
//   ★ 版心**不再**按它扣减：那版"永远扣 230"会让小窗口里也平白窄一条，
//     观感像布局坏了（实测）。版心改走分级 max-width 模型（`kTranscriptWMin`/`kTranscriptWMax`）。
inline constexpr int kInspW = 230;

// ── 滚动条厚度（2026-10-27）──
//   ★ 默认主题值是 12（`DefaultTheme` 的 `ScrollBarStyle::thickness`），实测反馈"太宽、
//     视觉上突兀"⇒ 取 1/3。
//   ★ 它**不是纯视觉量**：`ScrollView` 的 viewport = `厚度 + 2`（`kBarInset` 四边内缩），
//     所以改它连带改 `GetMaxOffsetY()` 与内容可视宽——是几何量，必须单点可查。
//   ★ 全仓所有 `ScrollView` 统一走 `ApplyCompactScrollBars()`，不许各处散写：中栏与
//     侧栏列表条宽不一致会比"条宽"本身更显眼。
//   ★★ 2026-11-04：输入框（`TextBox` 内建垂直条，非 ScrollView 路径）也统一到此值
//     ——经 `GetVerticalScrollBar()->SetStyle` 设置（PromptBar.cpp `ComposerBox`
//     构造），值仍取这里，别在那边另写一个数。
//   ★ 下限自觉：`thickness <= 0` 时 ECDI 侧无断言保护（条宽 0 但仍扣 inset=2 ⇒
//     "看不见却占位"），别调成 0。
inline constexpr int kScrollBarThickness = 4;

// ★★ 2026-11-03：侧栏行宽公式**单点化**（此前 AppSession.cpp 三处 + AppHistory.cpp
//   两处 + ProbeSidebar 镜像一处，共六处各写一遍「max(kSidebarRowW, listW − 条厚 − 8)」；
//   首改就漏了历史列表两处——探针 histRowW != sessRowW 当场抓红，单点化的理由自证）。
//   值**与旧式逐位相同**（用户澄清：加宽是纵向，水平不动）。保留约束记录：
//   视口宽（竖条可见时）= 控件宽 − 条厚 − kBarInset(2，ECDI 内部常数) = listW − 6，
//   行宽/extent 必须**严格小于**视口，横向条才不出（§109「目录列表恒出条」事故）。
//   本式 −8 恒留 2px 气口。探针 `sidebarGeomMaxW`（listW − 条厚 = −4）是粗上界。
inline constexpr int SidebarRowW(int listW) {
    const int w = listW - kScrollBarThickness - 8;
    return w > kSidebarRowW ? w : kSidebarRowW;
}

// ── 侧栏目录列表的**内容宽**（2026-10-27）──
//   ★★ 病因（用户实测：文件夹列表"永远挂着横向滚动条，哪怕没超出"）：
//     `ScrollView` 的横向条判定是 `contentW > ViewportWidth()`，而
//     `ViewportWidth() = 控件宽 − 条厚 − kBarInset`（条可见时）。此前目录列表把
//     **extent 宽设成控件宽**（`kSidebarListW`）⇒ 垂直条出现的同一刻，横向就被
//     判成溢出（172 > 166）⇒ 恒出条。这是"两轮判定"的必然结果，**不是 ECDI 缺陷**。
//     （会话列表 / 历史列表 extent 用 `kSidebarRowW`=158 < 166，所以它们从不出条——
//       这也反证了根因在这一处。）
//   ★ 取值：控件宽 − 条厚 − 4。多扣的那 2px 是**余量**：ECDI 侧 `kBarInset` 是内部
//     常量、本仓读不到，留 2px 就不怕它边界相等（相等时判定用严格 `>`，仍会出条）。
//   ★ 行宽与 extent 共用这一个数：内容宽 = 内容真实需要的宽，不撒谎。
inline constexpr int kSidebarListInnerW = kSidebarListW - kScrollBarThickness - 4;

// ── 模型下拉浮窗的几何（2026-11-01，**单一真相源**）──
//   ★ 为什么必须集中：它是**两处**共建的——`BuildModelPopup()`（AppUiMain.cpp，建
//     容器/定坐标）与 `RebuildModelPopupList()`（AppSettings.cpp，按 y 游标塞行，
//     撞到 `kModelListMaxH` 就收敛）。各写一个数必然漂移。
//   ★★ 2026-11-01 的「点开只有一个空框」正是这个形状的后果：`list` 面板从未
//     `SetSize`（0×0），而 `Widget::Paint` 的第一条是 `if (!ctx.IsRectVisible(self))
//     return;`（Widget.cpp:605）⇒ 整棵列表子树零绘制，行建了、一个都没画。
//   ★ 272 → 300 是**给标题让位**，不是美学：标题吃掉 24px 后，列表可用高必须仍是
//     210——否则「能装几个模型」会随标题加不加而变，那是把视觉改动偷渡成行为改动。
inline constexpr int kModelPopupW = 340;
inline constexpr int kModelPopupH = 300;
inline constexpr int kModelPopupPadX = 10;
inline constexpr int kModelPopupTitleY = 10;
inline constexpr int kModelPopupTitleH = 16;
inline constexpr int kModelPopupSearchY = 32;
inline constexpr int kModelPopupSearchH = 26;
inline constexpr int kModelPopupListY = 64;
inline constexpr int kModelPopupListMaxH = 210;
inline constexpr int kModelPopupFootY = 276;
inline constexpr int kModelPopupFootH = 16;
// 下拉与锚（顶栏 chip）之间的垂直间隙——太小贴合得像嵌进顶栏，太大会让人怀疑
// 「这不是它的下拉」。6px 与 tooltip 的偏移同量级。
inline constexpr int kModelPopupDropGap = 6;

// ── 「跳到最新」按钮 + tooltip 延时（Stage 1d 上提）──
inline constexpr int kJumpBtnW = 104;
inline constexpr int kJumpBtnH = 30;
inline constexpr int kTooltipDelayMs = 400;

// ── 探针高对比配色（--probe-contrast，2026-10-08）──
//   动机（一次真实教训）：默认暗色主题下做像素探针时，转录画布的亮像素要与
//   **卡片圆角、1px 描边、滚动条、窗口边框**抢阈值 ⇒ 反复出现「文本行段 = 0」
//   「x 跨度到 899」这类**仪器噪声**，并据此误判过产品行为。改成白底黑字零描边后，
//   测量退化为**平凡问题**。★ 默认关闭 ⇒ 所有既有路径逐位等价，不参与交付外观。
//   ★ kProbeCanvas 留在 App.cpp（只有 Run/RebuildUi 设窗口底色用它）。
//   ★ kProbeCanvas 2026-10-25（Stage 1e）上提：Run 设窗口底色 + AppUiChrome 的
//     RebuildUi 也要设同一色，两处必须是同一个值。
inline const ECDI::Color kProbeCanvas = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeBg  = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeInk = ECDI::Color::FromRGBA8(0, 0, 0, 255);

// ── ★★ P4 E / 2026-10-30：tooltip 配色（深底浅字）──
//   两主题通用，**不进 `g_theme`**：它是浮层系统的固定视觉，不是随主题走的表面色。
//   ★ 2026-10-30 上提：tooltip 迁到 `Window::ShowOverlay`（Tooltip 层）后，浮层
//     改由 `AppHud.cpp` 的 `OnTooltipFire` 现建现挂 ⇒ 配色要跨 TU 共用（原来是
//     `AppUiMain.cpp` 的匿名 namespace 常量，只有建树期用）。
inline const ECDI::Color kTooltipInk = ECDI::Color::FromRGBA8(240, 241, 245);

// 探针态取高对比色，否则取常规令牌（调用点写法统一）。
//   ★ 这里破例写成 inline：一行谓词，AppTranscript 里 22 处调用点都在建行热路径上，
//     为它多一个 TU 外调不值得。头文件注释里「不许 inline 实现」那条针对的是
//     有实际体量的函数（编译期成本 + 改一行重编全部）。
inline ECDI::Color Pick(bool probe, const ECDI::Color& normal, const ECDI::Color& hi) {
    return probe ? hi : normal;
}

// ── 共享 UI 文本 / 测量 helper（定义在 UiCommon.cpp）──
//   这四个都是**单一真相源**：同一段文本/宽度在多处生成，各写一份必然漂移
//   （本项目踩过两次：UnknownKindText 首版两处各写一份、ReasoningTitle 三处各写一份）。
int ContentWidth(const ECDI::ScrollView* scroll);
// ── 滚动条瘦身（2026-10-27）：把两条都设成 `kScrollBarThickness` 并补一次重排 ──
//   ★ 为什么**两条都设**：这是**设计裁决**，不是绕行——全仓条宽必须一致（中栏 4px 而
//     侧栏 12px 比 "4px 本身"更显眼），所以两条各设一次，别指望"只改一条另一边跟着变"。
//   ★★ 2026-11-01 订正（ECDI `#84` / Phase 43）：本条原写作「因为 `BarThickness()` 只读
//     **垂直**条来扣 viewport，只改一条 ⇒ 另一轴画出来的厚度与扣掉的厚度不一致」——
//     那描述的是**上游缺陷**，现已被 `daed47c` 修掉（`BarThickness()` 删除，改为
//     `VerticalBarThickness()` / `HorizontalBarThickness()` 两轴各取各的，S1/S4/S5/S6）。
//     ⇒ 结论不变（两条照设），但**理由变了**：不再是为了躲 bug，而是为了统一观感。
//     ★ 同厚度路径上游是**位相同**的，所以本仓画面零变化。
//   ★ 为什么必须补重排：`ScrollBar::SetStyle` 只 `Invalidate()` 自身，而条几何 /
//     viewport / maxOffset 全在 `ApplyLayout` 里算，它只被 `SetSize` /
//     `SetContentExtent` / `SetScrollBarVisible` 触发 ⇒ 光设样式不重排 = 条变窄了、
//     视口还按旧厚度让位。★ 这条**与 #84 无关、仍然必需**：新头文件反而把它写明了
//     （改厚度不触发重排，且 `SetContentExtent` 在 extent 未变时提前返回救不了）。
//     函数内已做"宽高为 0 就跳过"的守卫（建树期尺寸未定，随后布局分配的第一次
//     `SetSize` 自然会用新厚度）。
void ApplyCompactScrollBars(ECDI::ScrollView& sv);
std::string ConversationTitle();
std::string ConversationSubtitle();
std::string UnknownKindText(const Block& block);
std::string ReasoningTitle(const Block& block, bool expanded);

// ── 权限门折叠（2026-10-27）──
//   ★★ 形态与思维链同机制（`CollapsiblePanel` + header 按钮 + 折叠体），但两处
//     刻意不同，别照抄：
//     ① **默认展开**——思维链是"可选阅读"，收起无害；权限门是"必须决策"，
//        收起等于把决策依据藏起来；
//     ② **按钮不进折叠体**——`--answer-probe` 用控件的绝对中心点合成点击
//        （`ProbeTranscript.cpp` 的 `clickCentreOf`），按钮一旦随折叠体隐藏，
//        探针必红，真用户也可能收起后找不到"允许"。
//   ★ 摘要**纯列宽估算**（ASCII 1 列 / 多字节 2 列），零测量：权限门可能在流式
//     路径上高频重建，任何 `MeasureText` 都会进评估读数口径（同"修法 D"纪律）。
//   ★ 三件套必须是**单一真相源**：建行、重建、点开收起三处都要生成同一段标题
//     文本（本仓已因"两处各写一份"踩过两次：UnknownKindText 与 ReasoningTitle）。
std::string AskSummary(const std::string& question, int maxCols = 48);
bool AskNeedsFold(const std::string& question, int maxCols = 48);
std::string AskTitle(const std::string& question, bool expanded, bool foldable);

// 「一行小标签」的唯一建法：返回裸指针（树拥有），供 HUD 动态 SetText。
//   ★ 2026-10-25（Stage 1e）上提：AppUiChrome（顶栏/底栏）与 AppUiMain（检查器）
//     都建这种标签，各写一份会漏掉「行高随字号走」这条 2026-10-16 的修复。
//   ★ 默认实参**只写在定义处**（UiCommon.cpp），这里故意不带——两个原因：
//     ① C++ 规定默认实参只能出现一次；② 门 2 要求定义行逐字节可比，
//     签名里少一个 `= false` 就算"变了"。所有调用点都显式传满 6 个参数。
ECDI::Label* AddLine(ECDI::Panel& parent, const std::string& text,
                     const ECDI::Color& color, bool heading, int width,
                     std::optional<float> fontSize);

// ── 协议版本上限（协议 §9）──
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 RouteLastMessage）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
//   ★ 两个 TU 读它：App.cpp（RouteLastMessage 判版本）+ AppSummary.cpp（摘要里记上限）。
//   ★ 2026-11-04：4 → 5（子代理：`agent` 消息 + I12/I13）。★ 注意这是**上限**不是下限——
//     protocol 3/4 的夹具照旧能放（它们只是没有 agent 维度）。
inline constexpr int kProtocolMaxSupported = 5;   // v5：子代理 agent 消息 + I12/I13

}  // namespace internal
}  // namespace harness
