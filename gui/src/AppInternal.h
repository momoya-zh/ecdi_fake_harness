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
#include <string>

// 前置声明（避免把 UiKit/Transcript 的整棵头树拖进每个包含者）
namespace ECDI {
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
inline constexpr int kSidebarRowH = 28;
inline constexpr int kSidebarGroupH = 18;  // 分组标题行高（分隔的高度，不带"条目"语义）
//   ★ 2026-10-25（Stage 1c）：整块侧栏几何上提。原先只在 App.cpp 用，搬出
//     AppHistory.cpp（历史会话列表建行）后成跨 TU 依赖；1e 拆 UI builder 时
//     BuildMainArea 侧栏段也要用同一组数。**值一字未改**。
//     行宽 158 而非容器宽 172 的原因见 App.cpp 原注释（滚动条占 14px）。
inline constexpr int kSidebarListW = 172;
inline constexpr int kSidebarRowW = 158;
inline constexpr int kSidebarRowGap = 4;
inline constexpr int kSidebarNameW = 106;  // 22(dot 右缘) → 128，给右侧 18px 删除钮留 6px 间隙
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
inline const ECDI::Color kProbeBg  = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeInk = ECDI::Color::FromRGBA8(0, 0, 0, 255);

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
std::string ConversationTitle();
std::string ConversationSubtitle();
std::string UnknownKindText(const Block& block);
std::string ReasoningTitle(const Block& block, bool expanded);

// ── 协议版本上限（协议 §9）──
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 RouteLastMessage）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
//   ★ 两个 TU 读它：App.cpp（RouteLastMessage 判版本）+ AppSummary.cpp（摘要里记上限）。
inline constexpr int kProtocolMaxSupported = 4;   // v4（P3.6）：model 热切换 + usage.context

}  // namespace internal
}  // namespace harness
