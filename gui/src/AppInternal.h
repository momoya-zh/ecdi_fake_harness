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

#include <filesystem>
#include <string>

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

}  // namespace internal
}  // namespace harness
