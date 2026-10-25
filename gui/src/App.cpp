#include "App.h"
#include "Credentials.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <objbase.h>
// ★★ 2026-10-20：导出转录的保存对话框（`GetSaveFileNameW`）。
//   它与已有的 `SHBrowseForFolderW`（选文件夹）同属 Windows 壳对话框族，
//   但声明在 `commdlg.h` 而**不在** `ShlObj.h` 里 ⇒ 必须单独包，
//   且在 `gui/CMakeLists.txt` 里要链 `comdlg32`。
#include <commdlg.h>
#include <ctime>   // ★ P4 D：MarkAnswered 的 answerAt（本地 HH:MM）

#ifdef DrawText
#undef DrawText
#endif


#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
// ★ 侧栏分组标题的中间省略靠码点换算（`ByteOffsetToCodepointIndex`）：
//   `Label` 自己只做**尾部**裁剪，而路径的区分信息在尾部——详见 `ElideMiddleByCodepoints`。
#include "ECDI/Core/UTF8.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Render/BackendFactory.h"
// ★ --hit-probe（REQ-04 边界）：合成鼠标事件走**真实派发链**
//   （Application::OnEvent → OnMouseButtonDown/Up → FindTargetWidget → HitTest → Bubbling）
//   ——不注入系统输入、不动光标、不抢前台，故对用户零干扰（对比 SendInput 路线）。
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"   // ★ P4 A：合成滚轮（--jump-probe）
// ★★ 2026-10-21：Enter 发送探针要合成真按键（`SynthesizeKey`）⇒ 完整类型。
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/Layout.h"   // ★ P4 F：HarnessRootLayout 基类
#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Layout/VerticalLayout.h"

#include <set>   // ★ P4 F：HarnessRootLayout 的豁免集
#include "ECDI/Theme/PanelStyle.h"
// ★★ 2026-10-21：`--grow-probe` 的 exact-fit 相要读输入框**内部**滚动条的可见性
//   （`TextBox::GetVerticalScrollBar` 返回 `ScrollBar*`）⇒ 完整类型。
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"

#include "Theme.h"
#include "ThemeUsing.h"    // ★ 2026-10-25：主题令牌短名收口（原文件级 52 条 using）
#include "AppInternal.h"   // ★ 2026-10-25：跨 TU 共享的路径转换与几何常量
#include "UiKit.h"
#include "ThemedCaptionBar.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

// ★★ 2026-10-25：路径转换三兄弟搬到 `PathUtil.cpp`（`harness::internal`）。
//   为什么必须搬：它们原本是这个 TU 的**内部链接** helper，而拆分后
//   Probe*.cpp 也要转路径（RunWorkspaceProbe / RunMenuProbe / RunHistoryProbe）。
//   这里只留短名 ⇒ App.cpp 里 200+ 个调用点一个字都不用改。
using harness::internal::Utf8FromWidePath;
using harness::internal::WideFromUtf8Path;
using harness::internal::PathToUtf8;

// ★★ 2026-10-20：路径的**中间**省略（保留头与尾）。
//   为什么必须保留尾部：`ECDI::Label` 对超宽文本是**尾部丢弃**（单行 DrawText 裁剪），
//     而两个工作区的区别几乎总在叶子段（`.../proj` vs `.../other`）——尾部一丢，
//     两个分组标题在屏幕上就是同一行，“按工作区区分”退化成“按前缀区分”。
//   为什么这里**不**测量：本函数在 `RebuildSessionList` 的建树路径上，任何
//     `MeasureText` 都会进 `m_evalMeasureCalls`（评估仪器）⇒ 每次侧栏重建都会
//     污染 measure/paints 读数（既有基线里就有 measureCalls/measureChars）。
//     这里改用**码点预算**（分组标题恒为等宽字体，ASCII 每码点同宽），
//     再由 `--sidebar-probe` 侧的实测（`MeasureText` 后比对可用宽）证明真的放得下；
//     探针跑批不产基线，故那次测量不污染任何既有读数。
constexpr size_t kSidebarGroupTitleCp = 22;   // 22 码点 ≈ 11px Consolas 下 150px 宽

std::string ElideMiddleByCodepoints(const std::string& text, size_t maxCp) {
    if (text.empty()) return text;
    const size_t total = ECDI::ByteOffsetToCodepointIndex(text, text.size());
    if (total <= maxCp) return text;   // 放得下就**原样**返回（零改动即零回归）
    if (maxCp < 4) {
        // 退化：连 `X…Y` 都放不下，只能留尾部（尾部是唯一有信息量的一端）
        return text.substr(ECDI::CodepointIndexToByteOffset(text, total - maxCp));
    }
    // 头 1/4、尾 3/4：尾部信息更密（末级目录 + 叶子段）。
    const size_t headCp = maxCp / 4;
    const size_t tailCp = maxCp - headCp - 1;   // 减 1 = 省略号本身占一个码点
    const std::string head =
        text.substr(0, ECDI::CodepointIndexToByteOffset(text, headCp));
    const std::string tail =
        text.substr(ECDI::CodepointIndexToByteOffset(text, total - tailCp));
    return head + "\xe2\x80\xa6" + tail;   // U+2026 HORIZONTAL ELLIPSIS
}

constexpr int kListRowHeight = 28;   // listlayout 模式的固定行高（DIP）
constexpr int kRowGap = 10;          // 行间距（原型 C 的消息间距 23px ⇒ 折半，紧凑窗口下够用）
constexpr int kRowPadX = 10;
// ★ 画布左右留白（原型 C 的 22px 画布 padding）。此前行从 x=0 起排，
//   气泡/工具卡**贴着中栏边线**——这是"看起来挤"最直接的来源。
//   紧凑窗口（1120）下取 16，既留出呼吸感又不白吃掉横向空间。
constexpr int kCanvasPadX = 16;
constexpr int kHeaderH = 32;         // tool 卡片标题高，接近原型 C 的 39px

// ★★ P4 Phase A（2026-10-25）：转录贴底判定阈值。此前是 FollowTailIfPinned 里的
//   内联 60；「跳到最新」按钮的显隐与贴底跟随共用同一条语义（离开底部多少算
//   "翻上去了"），抽成常量避免两处各写一个数日后漂移。
//   ★ 2026-10-25：定义移到 `AppInternal.h`（探针 TU 也要用同一条阈值），此处只留短名。
using harness::internal::kPinThreshold;
// 「跳到最新」按钮尺寸（视口右下角浮层；原型 F 条）。
constexpr int kJumpBtnW = 104;
constexpr int kJumpBtnH = 30;
// ★★ P4 E：tooltip 配色（深底浅字——两主题通用，不进 g_theme：它是浮层
//   系统的固定视觉，不是随主题走的表面色）。
constexpr int kTooltipDelayMs = 400;
const ECDI::Color kTooltipInk = ECDI::Color::FromRGBA8(240, 241, 245);

// ★★ Step 1（2026-10-19）：侧栏会话列表的几何。
//   ★ 为什么行宽是 158 而不是容器的 172：`ScrollView` 的垂直条一旦出现，
//     viewport 会缩到 `172 - (12 + 2) = 158`（`ScrollView.cpp` 的 `BarFootprint`）。
//     行宽若写成 172，出条的那一刻行会被裁掉 14px（还会多出一条水平条）——
//     于是"会话多到要滚动"这件事本身会改变行的排版。
//   ★ 行高 28 与原型 C 的 navitem 同高；行间距 4（比正文的 10 紧，列表才有"一列"的感觉）。
constexpr int kSidebarListW = 172;
constexpr int kSidebarRowW = 158;
// ★ 2026-10-25：行高与标题行高移到 `AppInternal.h`（--sidebar-probe 要量这两条），此处留短名。
using harness::internal::kSidebarRowH;
constexpr int kSidebarRowGap = 4;
constexpr int kSidebarNameW = 106;   // 22(dot 右缘) → 128，给右侧 18px 删除钮留 6px 间隙
constexpr int kSidebarMaxRows = 4;
// ★★ 2026-10-20：工作区分组标题行的高度。
//   比会话行（28）矮得多：它是**分隔**，不是条目。若与行同高，列表中“工作区”与
//   “会话”就会长得一样高，用户无法从尺寸上区分两层结构。
// ★ 2026-10-25：定义移到 `AppInternal.h`（探针与产品侧共用同一条行高）。
using harness::internal::kSidebarGroupH;   // 标题行高（分隔的高度，不带"条目"语义）

// 视觉令牌唯一来源 = Theme.h（2026-10-06 v6：对齐用户高保真模型）
// ★★ 本 GUI 支持的协议主版本上限（协议 §9；2026-10-14 v2 落地）。
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 `RouteLastMessage`）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
//   ★ 2026-10-25：定义移到 `AppInternal.h`（AppSummary.cpp 也要读它），此处只留短名。
using harness::internal::kProtocolMaxSupported;

// ★ 2026-10-25：主题令牌短名（52 条 using）已收口到 `ThemeUsing.h`——
//   拆分后每个新 TU 都要这组短名，复制 5 遍迟早漂移。见该头文件的说明。

// ══════════════════════════════════════════════════════════════════
// ★ 探针专用高对比配色（2026-10-08）——--probe-contrast
// ──────────────────────────────────────────────────────────────────
// 动机（一次真实教训）：默认暗色主题下做像素探针时，转录画布的亮像素
//   要与**卡片圆角、1px 描边、滚动条、窗口边框**抢阈值 —— 于是反复出现
//   「文本行段 = 0」「x 跨度到 899」这类**仪器噪声**，并据此误判过产品行为。
//   改成「白底 + 黑字 + 零描边」后，测量退化为**平凡问题**：
//   统计暗像素的行段即得文本行数，无需任何阈值调参。
// ★ 默认关闭（probeContrast=false）⇒ 所有既有路径**逐位等价**，不参与交付外观。
namespace {
inline const ECDI::Color kProbeCanvas = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeBg     = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeInk    = ECDI::Color::FromRGBA8(0, 0, 0, 255);

// 探针态取高对比色，否则取常规令牌（调用点写法统一）
inline ECDI::Color Pick(bool probe, const ECDI::Color& normal, const ECDI::Color& hi) {
    return probe ? hi : normal;
}

// ★★ P4 F（2026-10-25）：root 专用布局——逐位复刻 VerticalLayout(0, true) 的
//   摆位算法（spacing=0 / fillCrossAxis / 隐藏子 C-VIS 停泊），但对 **Exempt 注册
//   的浮层**（设置遮罩/sheet）完全跳过：不进统计、不停泊、不摆位（几何归
//   LayoutSettingsOverlay）。
//   ★ 为什么必须换：原 VerticalLayout 把**可见的**浮层当固定尺寸子统计——
//   mask(整窗高)+sheet 一显示，主区就被挤没了（实测 counting 被挤到 0×0）。
//   ★ 隐藏子停泊语义保留（C-VIS-4）：非豁免的隐藏子照旧停 (−w,−h)。
class HarnessRootLayout : public ECDI::Layout {
public:
    void Exempt(ECDI::Widget* w) { m_exempt.insert(w); }

    void Arrange(ECDI::Widget& parent) override {
        const size_t count = parent.GetChildCount();
        int fixedTotal = 0;
        int totalStretch = 0;
        size_t visibleCount = 0;
        size_t visibleStretchCount = 0;
        for (size_t i = 0; i < count; ++i) {
            const ECDI::Widget* child = parent.GetChildAt(i);
            if (!child->IsVisible() || m_exempt.count(child) > 0) continue;
            ++visibleCount;
            if (child->GetStretch() > 0) {
                totalStretch += child->GetStretch();
                ++visibleStretchCount;
            } else {
                fixedTotal += child->GetHeight();
            }
        }
        const int remaining =
            (std::max)(0, parent.GetHeight() - fixedTotal);
        const int cross = (std::max)(0, parent.GetWidth());
        int y = 0;
        int allocated = 0;
        size_t stretchSeen = 0;
        for (size_t i = 0; i < count; ++i) {
            ECDI::Widget* child = parent.GetChildAt(i);
            if (m_exempt.count(child) > 0) continue;   // 浮层：视而不见
            if (!child->IsVisible()) {
                const int parkW = child->GetWidth();
                const int parkH = child->GetHeight();
                child->SetPosition(-parkW, -parkH);   // C-VIS-4 停泊
                continue;
            }
            if (child->GetStretch() > 0) {
                ++stretchSeen;
                int height = (stretchSeen == visibleStretchCount)
                                 ? remaining - allocated
                                 : static_cast<int>(static_cast<long long>(remaining) *
                                                    child->GetStretch() / totalStretch);
                allocated += height;
                child->SetSize(cross, height);
            } else {
                child->SetSize(cross, child->GetHeight());
            }
            child->SetPosition(0, y);
            y += child->GetHeight();
        }
    }

private:
    std::set<const ECDI::Widget*> m_exempt;
};
}   // namespace

int ContentWidth(const ECDI::ScrollView* scroll) {
    const int w = scroll ? scroll->GetWidth() : 0;
    return w > 80 ? w - 40 : 600;   // 扣除滚动条与边距；未布局前给初值
}

// ★★ 中栏会话头的标题/副标（2026-10-18，原型 C 的 `.conversation-head`）——
//   **单一真相源**：两处生成（建头、`RefreshHud` 里 hello/会话 id 到达后更新），
//   各写一份必然漂移（同 `UnknownKindText` 首版两处各写一份的教训）。
//   ★ 标题用**夹具文件名**（拉丁，可用 Semibold 出口加重）而非写死「harness 会话」——
//     用户要的是「知道自己在聊什么」，而系统标题栏在深色密排版里不构成答案。
//   ★ 副标三要素 = 协议版本 + 模式 + 工作区名（C 的 `在 ECDI_fake_harness · 今天 14:32`
//     在落地时换成真实来源，不写死时间：本 GUI 的时间轴由回放 `ts` 驱动，
//     墙上时钟与协议时间是两个量纲）。
std::string ConversationTitle() {
    return "harness 会话";
}

std::string ConversationSubtitle() {
    return "协议 v2 · 回放模式";
}

// ★★ 未知 `kind` 行的正文拼法（2026-10-13）——**单一真相源**。
//   为何要抽成函数：该文本在**两处**生成（`CreateBlockRow` 建行、
//   `RouteLastMessage` 的 delta 分支更新），首版在两处各写一份 ⇒ delta 一到
//   就把 `[未知 kind: …]` 前缀擦掉（实测截图可见）。同源即不可能再漂移。
std::string UnknownKindText(const Block& block) {
    std::string shown = block.kind.empty()
                            ? "[未知 kind: (字段缺失)]"
                            : "[未知 kind: " + block.kind + "]";
    shown += "\n";
    shown += block.text;
    if (!block.summary.empty()) shown += "\n(" + block.summary + ")";
    return shown;
}

// ★★ 思维链卡的标题拼法（协议 v2 §7，2026-10-14）——**单一真相源**。
//   为何要抽成函数：该文本在**三处**生成（建行、`block_end` 到达、点击展开/收起），
//   三处各写一份必然漂移（本项目已踩过同类坑：`UnknownKindText` 首版两处各写一份，
//   delta 一到就把前缀擦掉）。同源即不可能再漂移。
//
//   ★ 箭头字形（2026-10-14，**用户指定**）：用户嫌 `▼`/`▲` 难看，并要求
//     「类似于 `<` 尖括号这种……**向下的尖括号和向上的尖括号**的样子，但是要**钝角**，
//     大概 **120°** 的样子」，随后**直接给出字形**：**`﹀`(U+FE40) / `︿`(U+FE3F)**。
//
//   ★★ **我此前两次都测错了角度（根因 = 量具公式符号反了）**——记此以免重犯：
//     首版公式写成 `acos((k²-1)/(k²+1))`，**正确应为 `acos((1-k²)/(1+k²))`**
//     ⇒ 每个读数都被**镜像**成 `180° - 真值`。后果：
//       · 我说「`▼` 本来就是 118°、角度已达标」——**错**，它其实是 **62°（锐角）**；
//       · 我据此换上的 `∨`/`∧`，我说 127.7°/131.4°——**错**，其实是 **53.1°/49.1°（锐角）**
//         ⇒ 用户要**钝角**，我给的却是**锐角**，方向完全反了。
//     ⇒ 修正后实测（`.scratch/probe_chevron.py`，**先校准再测量**）：
//         `﹀` **134.5°** / `︿` **135.7°**（钝角、细笔画、上下仅差 **1.2°**）⇒ **采用**。
//     ★★ **教训（本轮最该记的一条）**：**测量函数本身是仪器，未校准的仪器会给出自信的错数**。
//       修正版先喂**解析式已知角度**的合成 V 形做校准（60/90/110/120/130/150，
//       误差 ≤1.4°），通过后才测真实字形。**不校准就不报数。**
//
//   ★ 字形可用性是**实测**的，不是按名字猜的（同一次取证）：`⌄`(U+2304) / `⯆`(U+2BC6) /
//     `〈`(U+2329) 等在当前字体**缺字**；缺字会被 Windows **静默字体链接**到别的 face
//     ⇒ 字重/基线错位，**比原样更难看**。⇒ 选字形必须实测存在性。
std::string ReasoningTitle(const Block& block, bool expanded) {
    // ★ 字节序列**由 Python 反查得到**（`chr(cp).encode('utf-8')`），**不靠心算**：
    //   U+FE40 ﹀ = EF B9 80 · U+FE3F ︿ = EF B8 BF
    //   （我第一版凭记忆写成 EF B8 80 / EF B9 80 —— **错了**，那是 U+FE00/U+FE40 的错配。
    //     教训：多字节字面量一律程序化生成/核对，别手写——同条 64「字面量不得凭阅读记忆重打」。）
    std::string out = expanded ? "\xEF\xB8\xBF "      // ︿ U+FE3F（展开）
                               : "\xEF\xB9\x80 ";     // ﹀ U+FE40（收起）
    out += block.ended ? "\xE5\xB7\xB2\xE5\xAE\x8C\xE6\x88\x90"      // 已完成
                       : "\xE6\x80\x9D\xE8\x80\x83\xE4\xB8\xAD";     // 思考中
    if (block.tokens > 0) {
        out += " \xC2\xB7 " + std::to_string(block.tokens) + " tokens";
    }
    return out;
}

// 侧栏/状态栏的一行小标签；返回裸指针（树拥有），供 HUD 动态 SetText
ECDI::Label* AddLine(ECDI::Panel& parent, const std::string& text,
                     const ECDI::Color& color, bool heading = false, int width = 170,
                     std::optional<float> fontSize = std::nullopt) {
    auto label = std::make_unique<ECDI::Label>(text);
    label->SetTextColor(color);
    // ★ 2026-10-18 v7：族名走主题（浅色 = 雅黑，深色 = 空/SimSun 基线）。
    //   原先只在传了 fontSize 时才 SetFont ⇒ 不传的那些落到空 family = SimSun，
    //   与显式指定的控件**同一界面两种中文字形**。
    //   默认值取 `ECDI::Font::size` 的出厂值 14.0f（原先不调 SetFont 时就是这个数）。
    label->SetFont(harness::SansFont(fontSize.value_or(14.0f)));
    // ★ 行高随字号走（2026-10-16 用户反馈「小节标题字太小」）：
    //   标题 11f→13f 后，13px 的行盒在原来的 15px 里会被**裁切**
    //   （`Font::size` 是 DIP 高度，`Font.h:17`）。标题行高 15→18 留出行距。
    //   正文行高 17 不变（其字号未动）。
    label->SetSize(width, heading ? 18 : 17);
    ECDI::Label* raw = label.get();
    parent.AddChild(std::move(label));
    return raw;
}

}   // namespace

// ── HarnessApplication ─────────────────────────────────────────

void HarnessApplication::OnTimer(const ECDI::TimerEvent& event) {
    if (m_owner) {
        // ★★ P4 Phase A：任何定时器节拍先做一次「跳到最新」脏检查（int 比较，
        //   offset 没变就返回）。拖滚动条滑块没有事件回调，靠 caret 闪烁定时器
        //   的节拍兜底是唯一覆盖口——故放在定时器 id 分流**之前**。
        m_owner->DirtyRefreshJumpButton();
        if (event.GetTimerId() == HarnessApp::kTooltipTimer) {
            m_owner->OnTooltipFire();   // ★ P4 E：400ms 到点，显示 tooltip
            return;
        }
        if (event.GetTimerId() == HarnessApp::kReplayTimer) {
            m_owner->OnReplayTick();
            return;
        }
        if (event.GetTimerId() == HarnessApp::kCloseTimer) {
            m_owner->OnAutoClose();
            return;
        }
        if (event.GetTimerId() == HarnessApp::kSweepTimer) {
            m_owner->OnSweepTick();
            return;
        }
    }
    ECDI::Application::OnTimer(event);   // 光标闪烁 / 动画 tick
}

// ★★ 2026-10-21：窗口拉伸 → 走我们自己的重排链。
//   ★ 顺序不可换：**先**让基类同步 `RootWidget` 尺寸（基类 `OnWindowResized` 干的就是
//     这一件事），**再**重排。反过来的话，`Relayout()` 会拿 `m_scroll` 的**旧宽度**
//     去测每一行 ⇒ 重排白跑一次，下一帧才发现宽度已经变了。
//   ★ 这里**不**重建树、**不**重建转录视图：尺寸不属于模型，`Relayout()` 已经覆盖。
void HarnessApplication::OnWindowResized(const ECDI::WindowResizedEvent& event) {
    ECDI::Application::OnWindowResized(event);   // ① root 尺寸同步（必须在重排之前）
    if (m_owner) m_owner->OnWindowResized(event.GetWidth(), event.GetHeight());
}

// ★★ P4 Phase A（2026-10-25）：滚轮 → 基类先把事件派给 ScrollView（滚动真的
//   发生），**再**刷新「跳到最新」按钮。顺序不可换：反了的话 RefreshJumpButton
//   读到的是滚动前的 offset，按钮显隐滞后一拍。
void HarnessApplication::OnMouseWheel(const ECDI::MouseWheelEvent& event) {
    ECDI::Application::OnMouseWheel(event);
    if (m_owner) m_owner->RefreshJumpButton();
}

// ★★ P4 Phase A：键盘单收件人（Application::OnKeyDown 只派给焦点控件），
//   导航键可能经 TextBox 的滚动（多行输入）间接改变转录状态——这里在派发
//   **之后**补一次按钮刷新。转录本身暂无键盘滚动路径（焦点在输入框时
//   PageUp/Home 等全被 TextBox 吃掉）。
// ★★ P4 Phase B：**之前**先判全局快捷键（顺序 = 拦截 → 未命中转基类派发 →
//   派发后补按钮刷新）。命中即 SetHandled 并 return——不调基类，焦点控件
//   （含输入框）不会再见到这个键，Ctrl+B 在打字中途也成立。
void HarnessApplication::OnKeyDown(const ECDI::KeyDownEvent& event) {
    if (m_owner && m_owner->HandleGlobalKeyDown(event)) {
        event.SetHandled();
        return;
    }
    ECDI::Application::OnKeyDown(event);
    if (!m_owner) return;
    switch (event.GetKeyCode()) {
        case ECDI::KeyCode::PageUp:
        case ECDI::KeyCode::PageDown:
        case ECDI::KeyCode::Home:
        case ECDI::KeyCode::End:
            m_owner->RefreshJumpButton();
            break;
        default:
            break;
    }
}

// ── 构造 / 运行 ────────────────────────────────────────────────

HarnessApp::HarnessApp(CliOptions options)
    : m_options(std::move(options)) {
    // ★★ P4 H：配置**无条件**先加载一次——此前只在 firstRun 直连路径里 Load，
    //   回放/--replay 模式的 providers 恒空（模型浮窗/右栏只读卡全 "—"）。
    //   失败只记日志：文件不存在 = 干净首启（TryAutoConnectFromConfig 语义不变）。
    {
        std::string cfgError;
        if (!m_appConfig.Load(cfgError)) {
            ECDI::Logger::Log(ECDI::LogLevel::Warning,
                              ECDI::UTF8ToWide("AppConfig: " + cfgError));
        }
    }
    // ★★ P3.5（规格 §8.5 D7）：无参启动的解析在**建会话之前**——
    //   配置 + 凭据都可用 ⇒ 填 corePath/coreArgs/m_coreApiKey，下面照常建 live 会话；
    //   否则 ⇒ 首启：设置面板打开（会话为空壳，保存成功后再建 live 会话）。
    if (m_options.firstRun) {
        if (!TryAutoConnectFromConfig()) {
            m_settingsVisible = true;   // 首启引导（或配置失效——hint 会说明原因）
        }
    }
    // ★★ 0d：构造第一个会话（源选择逻辑搬进 `CreateSessionFromOptions`，
    //   因为"新建会话"要复用同一段）。
    //   ★ 这里只**创建 + 设活动指针**，不调 `SwitchToSession`——此刻窗口还没建，
    //     `m_content` 为空，SwitchToSession 里的 RebuildTranscriptView 会解引用空指针。
    //     首会话的视图由 `Run()` 里的 `BuildUi` 建；之后 DrainInstant/Timed 增量填行。
    Session* first = CreateSessionFromOptions();
    if (first) {
        m_sess = first;
        m_activeIndex = m_sessions.size() - 1;
    }
    // first 为 nullptr（live 启动失败）⇒ m_sess 保持空 ⇒ Run 里报错退出（return 4）。
}

// ★ 析构保持 default：`m_sessions` 是 `vector<unique_ptr<Session>>`，
//   每个 Session 的 source/writer/rows 都是 RAII ⇒ 逐个自动析构。
//   live 子进程的收尸在 `Run()` 收尾显式做（bye → Stop），不在这里。
HarnessApp::~HarnessApp() = default;

// 按当前 CLI 选项建一个会话，push 进 m_sessions 并返回（**不**激活）。
//   ★ 这段就是原构造函数体：--core ⇒ 拉起子进程（真 live），否则夹具回放。
//   抽成方法是为了让 `NewSession()` 能复用同一套源创建逻辑。
//   ★ 返回 nullptr = live 启动失败（不 push 坏会话）；调用方必须判空。
HarnessApp::Session* HarnessApp::CreateSessionFromOptions() {
    auto session = std::make_unique<Session>();
    Session& s = *session;

    // ── 源选择（P1.5）：--core <exe> ⇒ 子进程真路径；否则夹具回放 ──
    if (!m_options.corePath.empty()) {
        auto child = std::make_unique<ChildSource>();
        std::string error;
        // ★ P3.5 D5 实施版：key 走**启动环境层**——GUI 进程
        //   SetEnvironmentVariableW("ECDI_CORE_KEY", …)，子进程经 lpEnvironment =
        //   nullptr 的标准继承拿到。不经协议、不经命令行、不经配置文件；
        //   与 dsh 的 launching-environment 层同构（调研记录见当日工作日志）。
        //   ★ 手搓环境块（父环境复制 + 覆盖 + CreateProcessW 显式传入）实测在
        //     当前启动环境下恒报 87（ERROR_INVALID_PARAMETER），已废弃——偏差与
        //     未决问题记在规格 §8.5.4 R-5。
        //   ★ 保留不"用完即清"：每个新会话（＋按钮/临时会话）都要再拉一个 core。
        //     key 留在 GUI 进程内存属可接受形态（同一信任域，positioning §4）；
        //     纪律是**绝不**进日志/摘要/转录。
        // ★ P3.6：为**每一家**提供商注入 ECDI_PROVIDER_<SAN>_{ID,URL,KEY}
        //   （core 按同一 sanitize 规则对齐；model 切换按 id 寻址）。
        //   ECDI_CORE_KEY 仍指向初始提供商（独立调试兼容）。
        CacheProviderKeys();
        for (const auto& pc : m_appConfig.providers) {
            const auto keyIt = m_providerKeys.find(pc.id);
            if (keyIt == m_providerKeys.end()) continue;
            const std::string san = EnvSanitizeProviderId(pc.id);
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_ID").c_str(),
                ECDI::UTF8ToWide(pc.id).c_str());
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_URL").c_str(),
                ECDI::UTF8ToWide(pc.baseUrl).c_str());
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_KEY").c_str(),
                ECDI::UTF8ToWide(keyIt->second).c_str());
        }
        if (!m_coreApiKey.empty()) {
            SetEnvironmentVariableW(L"ECDI_CORE_KEY", ECDI::UTF8ToWide(m_coreApiKey).c_str());
        }
        if (!child->Start(m_options.corePath, m_options.coreArgs, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ChildSource: " + error));
            // 源启动失败 ⇒ 不 push（避免留下一个拉不动的会话），如实返回 nullptr
            return nullptr;
        }
        s.child = child.get();
        s.source = std::move(child);
        s.writer = std::make_unique<ProtocolWriter>(*s.source);
        s.label = m_options.corePath.filename().generic_string() + " (live)";
        // ★ Step 5：来源必须是**显式字段**，不能再靠"corePath 空不空"推断——
        //   同一个进程里此后可以同时有 live / 启动 replay / 历史 replay。
        s.originKind = "live";
        s.originPath = m_options.corePath;
        // ★ P4 H：模型名预填（hello 不带 model 字段；ack 到达前 chip 显示它）。
        if (const ProviderConfig* def = m_appConfig.DefaultProvider()) {
            s.modelName = def->model;
        }
        if (m_sessions.empty()) {
            // 首个 live 会话：把 fixtureName 也定下来（窗口标题 / 摘要用）
            m_options.fixtureName = s.label;
        }
    } else {
        auto replay = std::make_unique<ReplaySource>();
        std::string error;
        if (!replay->Load(m_options.replayPath, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ReplaySource: " + error));
        }
        s.pendingTruncated = replay->TruncatedError();
        s.replay = replay.get();
        s.source = std::move(replay);
        s.label = m_options.fixtureName;
        // ★ Step 5：`--replay` 启动的会话与"点开的历史"是两种来源。
        //   区分它们有实际后果：只有后者会在打开时读 sidecar（前者是用户
        //   显式指定的夹具，跑批基线必须可复现，不能被历史状态污染）。
        s.originKind = "replay";
        s.originPath = m_options.replayPath;
    }

    // 会话名：首个用 fixtureName；后续在 NewSession 里改成"会话 N"。
    if (s.label.empty()) s.label = "会话 " + std::to_string(m_sessions.size() + 1);

    m_sessions.push_back(std::move(session));
    // ★★ REQ-03：运行期新建的 live 会话也要接上唤醒。构造期 `m_application` 还是空
    //   ⇒ 本调用在那里是 no-op，由 `Run()` 的 `InstallWakeHandlers()` 统一补装。
    InstallWakeHandler(*m_sessions.back());
    return m_sessions.back().get();
}

// ＋ 新建会话的真实行为（0d）。
//   ★ live 模式：再拉起一个 core 子进程 —— 这才是"真的新建一个会话"。
//   ★ 回放模式：没有 core 会对着一个空会话说话，回放夹具是一次性的。硬塞一个
//     假回复就是伪造读数（本仓最守不住的线）。故如实建一个**空会话**并说明情况——
//     "能点、有状态、切得过去"，但绝不假装 core 回了话。
void HarnessApp::NewSession() {
    // 先把当前活动会话的 timed 时钟冻结（否则它被切走后还在按墙上时间"到期"）
    PauseActiveTimedClock();

    const bool live = !m_options.corePath.empty();
    if (live) {
        // 真新建：拉一个新 core。创建失败（nullptr）⇒ 如实提示，活动会话不变。
        Session* created = CreateSessionFromOptions();
        if (!created) {
            AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
            Relayout();
            RequestRepaint();
            ResumeSessionTimedClock(*m_sess);
            return;
        }
        created->label = "会话 " + std::to_string(m_sessions.size());
        // ★★ 2026-10-20（用户裁决 m08553）：继承**活动会话**的工作区。
        //   ★ 必须在 `SwitchToSession(idx)` 之前抓：那之后 `m_sess` 已经指向新会话，
        //     `InheritedWorkspacePath()` 会去读新建会话自己的（空）外键，永远继承不到东西。
        //   ★ 也必须在此刻抓（而不是等 hello 回来时现查）：hello 是异步到达的，
        //     到那时活动会话可能已被用户又切走了——继承口径必须钉在"点新建的那一刻"。
        created->workspaceInheritedPath = InheritedWorkspacePath();
    } else {
        // 回放模式：建一个空会话（无 source ⇒ 不拉取；OnReplayTick 见空 source 直接返回）
        auto session = std::make_unique<Session>();
        session->label = "会话 " + std::to_string(m_sessions.size() + 1);
        // ★ Step 5：空会话是**第四类来源**。它没有 originPath ⇒ 没有 sidecar。
        session->originKind = "new";
        m_sessions.push_back(std::move(session));
    }

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);

    if (live) {
        // ★ 协议 §4：GUI 在收到 core 的 hello 之后回 hello（seq 0）。首会话在 Run() 里发；
        //   运行期新建的会话必须在这里补发，否则新 core 永远等不到握手。
        Session& s = *m_sessions[idx];
        if (s.writer) s.writer->Hello("0.0.1");
        if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    } else {
        // 回放空会话：给一条诚实提示（不伪造 core 回复）
        AddNoticeRow("[新建会话：回放模式无 core，空会话——切回上一个会话看历史]", kDarkMuted);
        Relayout();
        RequestRepaint();
    }
    // ★ Step 1：会话数变了 ⇒ 列表必须重建（新行 + 每行的 `×` 可用性都可能变）。
    //   只置脏：本函数可能由「＋ 新建会话」按钮的点击回调进来（派发栈上，不能拆树）。
    m_sidebarDirty = true;
}

// 切换活动会话（0d）：拆当前视图 → 指向目标会话 → 按目标会话模型重建。
void HarnessApp::SwitchToSession(size_t index) {
    if (index >= m_sessions.size()) return;
    if (index == m_activeIndex && m_sess == m_sessions[index].get()) return;   // 已在该会话

    // ★★ 2026-10-20：临时会话在切走时消失（用户裁决 m09443）。
    //   必须在改 `m_activeIndex` **之前**：此时 `m_sess` 还指着旧会话，
    //   才能判断"旧会话是不是临时的"。
    MaybeDeleteTemporarySession();

    // ① 冻结当前活动会话的 timed 时钟（切走 ⇒ 它不该继续"到期"）
    PauseActiveTimedClock();
    // ①b ★ 在**还指着旧会话**时把视图状态同步进旧会话的模型。
    //    顺序要点：RebuildTranscriptView 内部也会 SyncRowViewState，但那时 m_sess
    //    已指向新会话——同步的是新会话的旧行，旧会话的状态就没人管了。眼下不出事
    //    只是因为 ToggleRow/MarkAnswered 每次点击都已写回模型；把正确性押在
    //    "别处恰好也写了"上是本仓最反对的形状，故在这里显式补上。
    SyncRowViewState();

    // ② 切换活动指针。m_sess 是全部 200+ 处调用点的"当前会话"锚，
    //    改这一个指针，OnReplayTick/RefreshHud/… 立即指向新会话。
    m_activeIndex = index;
    m_sess = m_sessions[index].get();

    // ③ 按新会话的模型重建转录视图（复用 0c 的 RebuildTranscriptView）。
    //    ★ 这正是 0c 存在的全部理由：没有"从模型重建"，切会话就无处安放视图。
    //    传 nullptr ⇒ 按 timeline 正向重建（不是负对照）。
    //    ★ 顺带清掉与旧会话绑定的渲染缓存：环色档位（否则新会话首帧可能不重绘）。
    m_ctxRingLevel = -1;
    RebuildTranscriptView();

    // ④ 恢复新会话的 timed 时钟（补偿它被冻结的墙上时间）
    ResumeSessionTimedClock(*m_sess);

    // ⑤ HUD / 进度 / 使能全部跟着新会话刷新
    // ★ Step 1：活动项变了 ⇒ 侧栏的选中态也变了。同样只置脏、不在这里重建
    //   （本函数可能从行点击的回调里进来，那时拆列表 = 拆掉正在执行回调的那个 Button）。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 1（2026-10-19）：侧栏会话列表 = 第二条「模型 → 视图」通道
//
// 为什么整段可以抽成纯函数：一行会话的全部输入只有 `(label, active, canDelete)`
//   —— 它不读 `m_sess`、不读转录、不读协议状态。所以「建一行」与「列表整体重建」
//   可以分开：前者是纯的（探针能反复调用而不改变任何状态），后者只负责拆+装。
//
// ★ 行根为什么必须是 `Button` 而不是 Panel（这是本项目最贵的一条教训）：
//   `Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），而 `Widget::HitTest` 的返回
//   契约是「目标自身必须 ContainsPoint」⇒ **Panel 行永远点不到**。要"整行可点"，
//   行根只能是 Button（或自建 Widget 子类，但那要重写一遍焦点/按下/悬停视觉）。
//
// ★★ 为什么行内「×」不会连带触发整行的切换（这条依赖框架契约，不是巧合）：
//   ECDI Phase 35 起 `Button::OnMouseButtonDown` 与 `OnMouseButtonUp` 都会
//   `event.SetHandled()`（`Button.cpp:126-181`），而 `Application` 的冒泡循环每次派发后
//   都查 `IsHandled()` 再决定是否继续上溯（`Application.cpp:324/361/390`）。⇒ 点「×」时
//   事件在子按钮处就被消费掉，父行按钮的 `OnMouseButtonUp` 根本不会被调用。
//   `EventConsumptionTests.cpp:817-841` 有这条语义的回归用例（嵌套点击/消费/移除）。
//
// ★★★ 为什么回调里**不能**同步重建列表（UAF，本步唯一的真陷阱）：
//   重建 = `content.RemoveChild(...)` 拆掉全部行，而正在执行 `OnMouseButtonUp` 的那个
//   Button **就在被拆的子树里** ⇒ 回调返回后回到 `RaiseClick()` 的栈帧，`this` 已析构。
//   框架的官方出口是 `Widget::RequestRemove()`（派发中只摘树、延迟析构，`Widget.cpp:231-289`），
//   但它只保护**你调它的那一个节点**，保护不了"我顺手把整棵列表重建了"。
//   ⇒ 本实现的规矩：**点击回调只改模型 + 置 `m_sidebarDirty`，重建留到 tick 里做**
//     （timer 回调不在事件派发栈上，此时拆树是安全的）。这条规矩比"记得用 RequestRemove"
//     更不容易被后人破坏，因为它在结构上就不给同步重建留位置。
// ═══════════════════════════════════════════════════════════════════════════

std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionRow(size_t index, bool active) {
    // ★ 纯函数：只读 m_sessions[index] 与传入的 active，不写任何成员。
    auto row = std::make_unique<ECDI::Button>();
    const Session* session = m_sessions[index].get();
    // ★★ 2026-10-20：归档。非归档行有「↓」(归档) + 「×」(删除)；
    //   归档行有「↩」(恢复) + 「×」(删除，若 canDelete)。
    const bool canDelete = m_sessions.size() > 1;
    const bool isArchived = session->archived;

    // 选中态 = 三种视觉同时变（底色/圆点/文字色），但**只有文字色可读回**：
    //   ECDI 的 Button/Panel 都没有 style getter（`Button::SetStyle` 只写不读），
    //   所以探针要靠 `Label::GetTextColor()` 判"这一行是不是选中项"。
    //   ⇒ 文字色不是纯装饰，它同时承担**读数**职责，改配色时别把它改没了。
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = active ? kUserBg : kPanel2,
        .border = active ? harness::kDarkAccent : kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,   // 行根不带文字，显式给值防依赖零值
        .foreground = active ? kAccentInk : kDarkInk});

    auto dot = uikit::Dot(7, active ? harness::kDarkAccent : kLine);
    dot->SetPosition(8, (kSidebarRowH - 7) / 2);
    row->AddChild(std::move(dot));

    // 名称 = `Session::label`（「会话 N」/ 夹具名 /「xxx (live)」）。
    // ★ 不做截断：文本原样进控件，探针才能拿它跟模型逐字比对（A==B 判据）；
    //   装不下的部分由渲染层的控件矩形裁剪（视觉上是截断，数据上没有）。
    // ★ 宽度随「有没有 ×」变：`×` 占 18 + 右边距 6 ⇒ 有删除钮时名称只能到 104，
    //   没有时（只剩一个会话）可以一直铺到行右边距。写死 104 会让单会话下的
    //   `demo-session.jsonl` 尾部被裁掉一个字符——**探针读文字读不到这件事**
    //   （`GetText()` 返回完整文本，裁剪发生在绘制层），只能靠截图发现。
    // ★ 宽度随「有没有按钮」变：两个按钮占 20+18+6=44 ⇒ 名称到 90；
    //   一个按钮占 18+6=24 ⇒ 名称到 110；无按钮 ⇒ 铺到行右边距。
    const int nameW = (canDelete || isArchived)
        ? ((canDelete && !isArchived) || (isArchived && canDelete) ? 90 : 110)
        : (kSidebarRowW - 22 - 2);
    auto name = uikit::Label(session->label.empty() ? "会话" : session->label,
                             nameW, 16, active ? kAccentInk : kDarkInk,
                             harness::kFontSmall);
    name->SetPosition(22, (kSidebarRowH - 16) / 2);
    // ★ 名称标签禁用 ⇒ 它**不参与命中**（`Widget::HitTest` 的 `!IsEnabled()` 早退，
    //   `Widget.cpp:321`），鼠标落点直接归行根 Button。两个后果都是想要的：
    //   ① 行根的 hover/press 视觉真的会亮（若 Label 抢了命中，`UpdateHoverState`
    //      只通知 Label，祖先收不到 `OnMouseEnter` ⇒ 整行点上去毫无反馈）；
    //   ② 探针/自动化点"行中心"时命中的一定是行根，读数不依赖文本是否恰好被点中。
    //   ★ `IsEnabled` 全库只 gate 命中 / 焦点收集 / 键派发，**无任何绘制消费者**
    //     （`Widget.h:197`）⇒ 禁用不会让文字变灰，视觉零变化。
    name->SetEnabled(false);
    row->AddChild(std::move(name));

    // ★★ 2026-10-20：归档行的按钮布局。
    //   非归档 + canDelete：「↓」(归档) 在 x=114，「×」(删除) 在 x=134。
    //   归档 + canDelete：「↩」(恢复) 在 x=114，「×」(删除) 在 x=134。
    //   归档 + !canDelete：「↩」(恢复) 在 x=134（唯一按钮）。
    if (isArchived) {
        auto restore = std::make_unique<ECDI::Button>("↩");
        restore->SetSize(18, 18);
        const int restoreX = canDelete ? (kSidebarRowW - 44) : (kSidebarRowW - 24);
        restore->SetPosition(restoreX, (kSidebarRowH - 18) / 2);
        restore->SetFont(harness::SansFont(harness::kFontSmall));
        restore->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        restore->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    RestoreSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(restore));
    } else if (canDelete) {
        auto archive = std::make_unique<ECDI::Button>("↓");
        archive->SetSize(18, 18);
        archive->SetPosition(kSidebarRowW - 44, (kSidebarRowH - 18) / 2);
        archive->SetFont(harness::SansFont(harness::kFontSmall));
        archive->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        archive->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    ArchiveSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(archive));
    }

    if (canDelete) {
        // ★ `×`(U+00D7) 是 symcand 实测两主题都有字形的少数符号之一
        //   （`✕`(U+2715)/`✖` 在浅色 YaHei 缺字形 ⇒ 会画成空白）。
        auto del = std::make_unique<ECDI::Button>("×");
        del->SetSize(18, 18);
        del->SetPosition(kSidebarRowW - 24, (kSidebarRowH - 18) / 2);
        del->SetFont(harness::SansFont(harness::kFontSmall));
        del->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        // ★ 捕获的是**会话身份**（Session*），不是 index：删除会让下标整体前移，
        //   捕获下标的行会"点了第 2 行却删掉第 3 个会话"。指针由 unique_ptr 持有、
        //   从不搬动 ⇒ 点击时现查下标才是对的（查不到就什么都不做，见下）。
        del->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    DeleteSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(del));
    }

    // 整行点击 = 切到该会话（非归档）或恢复（归档）。同样按身份解析下标。
    row->SetOnClick([this, session] {
        if (session->archived) {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    RestoreSessionAt(i);
                    break;
                }
            }
        } else {
            SwitchToSessionByPtr(session);
        }
    });
    return row;
}

// ★★ 2026-10-20：工作区分组标题行。
//   两种行长得不一样，但**只差在尺寸与颜色**，不需新控件类型：
//   `SnapshotSessionList` 靠“行根是不是 Button”区分两类行（第 5 段 `kind`）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionGroupRow(const std::string& title,
                                                               const std::string& workspaceId) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetSize(kSidebarRowW, kSidebarGroupH);
    // 透明底 + 无边框：标题是分隔，不是按钮。透明度用 alpha=0（与 `×` 子钮同一手法）。
    row->SetStyle(ECDI::PanelStyleOverride{.background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
                                           .cornerRadius = 0.0f,
                                           .borderWidth = 0.0f});

    // 标题文字 = 工作区路径（= core 回显的规范化结果，不是 GUI 自己拼的）。
    //   ★ 用 Mono 字体：路径里的 `l/1/I`、`O/0` 必须一眼可分，否则两个相似路径的
    //     工作区分组在屏幕上就等于同一行（这正是“按工作区区分”要解决的那个问题）。
    //   ★★ 必须**自己**做中间省略（`ElideMiddleByCodepoints`）：`ECDI::Label` 对超宽
    //     文本是**尾部丢弃**——截图实测 `D:/Project/ECDI_fake_harness` 被画成
    //     `D:/Project/ECDI_fake_harn`（少了 3 个字），而两个工作区的差异几乎总在叶子段。
    //     尾部一丢，“按工作区区分”就退化成“按前缀区分”：两个都在 `D:/Project/` 下的
    //     工作区在屏幕上会是同一行。把省略放在中间，尾部至少保留叶子段。
    // ★ 有「＋」按钮时标题宽度让出 22px（按钮 18 + 间隙 4）。
    const int labelW = workspaceId.empty() ? (kSidebarRowW - 8) : (kSidebarRowW - 30);
    // ★★ 省略预算必须跟着 labelW 走：有按钮时可用宽度少了 22px，
    //   若仍用 22 码点（≈132px）而 labelW 只有 128px，绘制层会裁掉尾部。
    const size_t elideCp = workspaceId.empty() ? kSidebarGroupTitleCp : (kSidebarGroupTitleCp - 1);
    const std::string shown = ElideMiddleByCodepoints(title, elideCp);
    auto label = uikit::Label(shown, labelW, kSidebarGroupH - 4, kDarkMuted,
                              harness::kFontMicro, true);
    label->SetPosition(8, 2);
    // 禁用：标题不参与命中（`Panel::ContainsPoint` 已经恒 false，这里只是让
    //   “文字不是交互对象”也成立；无绘制消费者 ⇒ 不变灰）。
    label->SetEnabled(false);
    row->AddChild(std::move(label));

    // ★★ 「＋」按钮：拉起该工作区的临时会话（m09443）。
    //   行根是 Panel（`ContainsPoint` 恒 false）⇒ 只有这个子 Button 能被点到。
    //   回调捕获 workspaceId（稳定字符串），不捕获下标。
    if (!workspaceId.empty()) {
        auto plus = std::make_unique<ECDI::Button>();
        plus->SetText("＋");
        plus->SetSize(18, 18);
        plus->SetPosition(kSidebarRowW - 24, (kSidebarGroupH - 18) / 2);
        plus->SetFont(harness::SansFont(harness::kFontSmall));
        plus->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        plus->SetOnClick([this, workspaceId] {
            NewTemporarySession(workspaceId);
        });
        row->AddChild(std::move(plus));
    }
    return row;
}

std::vector<size_t> HarnessApp::GroupTitlesForDisplay() const {
    std::vector<size_t> order;
    order.reserve(m_workspaces.size());
    // 创建序 = `m_workspaces` 下标序（只追加、不删、不重排）；显示序 = 它的逆序。
    //   `rbegin/rend` 而不是 `std::sort`：排序需要一个键，而这里根本没有键——
    //   唯一的规则就是“后建在上”，那就是逆序本身。
    for (auto it = m_workspaces.rbegin(); it != m_workspaces.rend(); ++it) {
        order.push_back(static_cast<size_t>(m_workspaces.rend() - it - 1));
    }
    return order;
}

void HarnessApp::RebuildSessionList(const std::vector<size_t>* orderOverride) {
    if (!m_hud.sessList) return;
    ECDI::Widget& content = m_hud.sessList->GetContentView();

    // ★ 与 RebuildTranscriptView 同一条纪律：拆干净再装，不做增量比对。
    //   `RemoveChild` 会先 `ForgetSubtree` 注销焦点/捕获/hover 再交回所有权
    //   （`Widget.cpp` RemoveChild），故这里不需要手工清理状态。
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    const size_t n = m_sessions.size();
    // ★★ 2026-10-20：行高不再统一（分组标题 `kSidebarGroupH`=18、会话行 `kSidebarRowH`=28）
    //   ⇒ 位置只能逐行累加。再用「下标 × 步进」会把标题按会话行高排，越往下偏得越多。
    int y = 0;
    int lastGap = 0;
    auto place = [&](std::unique_ptr<ECDI::Widget> row, int h) {
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += h + kSidebarRowGap;
        lastGap = kSidebarRowGap;
    };

    if (m_workspaces.empty()) {
        // ★ 一个工作区都没有 ⇒ **平铺**，与分组功能出现之前逐字节同形。
        //   为什么不画一个“未归属工作区”标题凑数：没有工作区实体时那行标题只是
        //   在给“什么都没有”起名字，纯装饰；何况回放会话**永远不会**有工作区
        //   （`MaybeStartWorkspaceSync` 首行 `!writer` 就 return），给它们编一个
        //   “未归属”分组等于在界面上宣告一个不存在的关系。
        for (size_t i = 0; i < n; ++i) {
            size_t idx = i;
            if (orderOverride) {
                if (orderOverride->size() != n) break;   // 形状不对就不画（探针会给同长度）
                idx = (*orderOverride)[i];
                if (idx >= n) break;
            }
            if (m_sessions[idx]->archived) continue;  // ★★ 归档会话不进主列表
            const bool active = (idx == m_activeIndex);
            place(BuildSessionRow(idx, active), kSidebarRowH);
        }
    } else {
        // ★ 分组：标题按**创建序逆序**（后建在上，用户裁决 m08553），组内会话按
        //   `m_sessions` 序（创建序正序）。两个方向都是固定的，不按“最近使用”重排——
        //   任何动态排序都会让同一个工作区在两次刷新之间换位置，用户会以为它是两个。
        //   ★ 分组模式下 `orderOverride` **不生效**：它表达的是“会话次序”，
        //     而分组结构下会话次序由“属于哪个组”决定。分组模式的负对照改用
        //     “换工作区路径”（见 `RunSidebarProbe` ⑬），只改一个自变量。
        std::vector<bool> shown(n, false);
        for (size_t wi : GroupTitlesForDisplay()) {
            const Workspace& ws = *m_workspaces[wi];
            place(BuildSessionGroupRow(ws.path, ws.id), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (m_sessions[i]->workspaceId != ws.id) continue;
                if (m_sessions[i]->archived) { shown[i] = true; continue; }  // ★★ 归档会话不进主列表
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
                shown[i] = true;
            }
        }
        bool anyOrphan = false;
        for (size_t i = 0; i < n; ++i) {
            if (!shown[i] && !m_sessions[i]->archived) { anyOrphan = true; break; }
        }
        if (anyOrphan) {
            // ★ 外键指向一个不存在的工作区也落在这里（`WorkspaceById` 返回 nullptr 时
            //   就是这么处理的，两处口径一致）：宁可显示“未归属”，也不能把它悄悄
            //   塞进第一个工作区——那会把归错桶伪装成正常。
            place(BuildSessionGroupRow("未归属工作区", std::string()), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (shown[i] || m_sessions[i]->archived) continue;
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
            }
        }
    }

    // ★★ 2026-10-20：「归档」区。有归档会话时才画标题 + 行。
    //   归档会话留在 m_sessions 原位（下标不动），只是从主列表隐藏、移到这里。
    //   行用 BuildSessionRow（archived=true 分支画「↩」+「×」）。
    {
        bool anyArchived = false;
        for (size_t i = 0; i < n; ++i) {
            if (m_sessions[i]->archived) { anyArchived = true; break; }
        }
        if (anyArchived) {
            place(BuildSessionGroupRow("归档", std::string()), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (!m_sessions[i]->archived) continue;
                place(BuildSessionRow(i, false), kSidebarRowH);
            }
        }
    }
    const int totalH = y > 0 ? y - lastGap : 0;
    // 高度上限仍按“4 个会话行”折算成像素：分组标题不算一行（它是分隔，不是条目），
    //   否则两个工作区就会把可见区吃掉一半。
    const int visibleH = std::min(totalH, kSidebarMaxRows * (kSidebarRowH + kSidebarRowGap)
                                              - kSidebarRowGap);
    // ★ 先 SetSize（决定 viewport）再 SetContentExtent（决定要不要出条）：
    //   反过来的话第一帧会按旧 viewport 判一次"不需要滚动条"，出条后行宽被挤。
    m_hud.sessList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.sessList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.sessList->SetContentOffset(0, m_hud.sessList->GetScrollOffsetY());

    // 列表高度是**变**的（会话越多越高，到 4 行封顶）⇒ 侧栏那条 VerticalLayout
    // 必须重排一次，否则下面的「项目」段还停在旧位置。
    if (ECDI::Widget* parent = m_hud.sessList->GetParent()) parent->Arrange();
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 5（2026-10-19）：历史会话 —— 把**磁盘上的文件**变成会话
// ───────────────────────────────────────────────────────────────────────────
// 这一节与上面 Step 1 的会话列表是**两种东西**，别混：
//   · `m_sessions`  = 已经打开的会话（内存里的模型）；
//   · 这里的历史区  = 磁盘上**可打开的文件**（`protocol/fixtures/**/*.jsonl`）。
//   点一行历史 ⇒ 多出一个已打开会话（列表里多一行），两者数量不必相等。
//
// ★ 为什么"打开"必须是**真回放**而不是"读到底、GUI 自己造 Block"：
//   后者会让 GUI 变成第二个转录作者，与 `PROTOCOL.md:244`「GUI 不产生转录块，
//   唯一作者是 core」直接冲突；并且坏文件的读数（`seqGaps` / `unknownKinds` /
//   `protocolIncompatible`）全都在**消费路径**上产生，绕开消费就没有读数了
//   ——历史区恰恰是最该照见坏文件的地方。跳到末尾只是**速度**参数。
//
// ★ 为什么历史区**只列不删**：`protocol/fixtures/` 下每个文件都有 sha256 基线，
//   删除 UI 一旦能碰它们，所有回归基线就都开口子了。
// ═══════════════════════════════════════════════════════════════════════════

std::filesystem::path HarnessApp::HistoryRoot() const {
    namespace fs = std::filesystem;
    // ① 从当前夹具路径往上找：`<ancestor>/protocol/fixtures` 存在就用它。
    //    为什么从夹具路径找而不是从 cwd 找：跑批脚本常常在仓库根跑，cwd 恰好对；
    //    但用户也可能从别处启动、只给一个绝对 `--replay`，此时 cwd 是错的，
    //    而夹具自己的位置永远是对的（它必然在仓库里）。
    std::error_code ec;
    if (!m_options.replayPath.empty()) {
        fs::path p = fs::absolute(m_options.replayPath, ec);
        if (ec) { p = m_options.replayPath; ec.clear(); }
        for (fs::path dir = p.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            const fs::path probe = dir / "protocol" / "fixtures";
            if (fs::is_directory(probe, ec) && !ec) return probe;
            ec.clear();
            if (dir == dir.root_path()) break;
        }
    }
    // ② 退到 cwd 相对路径（开发时最常见的形态）。
    const fs::path cwdProbe = fs::path("protocol") / "fixtures";
    if (fs::is_directory(cwdProbe, ec) && !ec) return fs::absolute(cwdProbe, ec);
    return std::filesystem::path();
}

std::vector<std::filesystem::path> HarnessApp::FindHistoryFiles() const {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const fs::path root = HistoryRoot();
    if (root.empty()) return out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        // ★ 跳过 `__pycache__`：`cot/` 下真实存在（生成器跑过就会有），
        //   它不是夹具、也不该出现在历史里。
        if (p.filename() == "__pycache__") { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (p.extension() != ".jsonl") continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [&root](const fs::path& a, const fs::path& b) {
        return a.lexically_relative(root).generic_string()
             < b.lexically_relative(root).generic_string();
    });
    return out;
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildHistoryRow(const std::filesystem::path& file) {
    // ★ 行根同样是 Button：`Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），
    //   可点的行根只能是 Button（同 `BuildSessionRow` 的理由）。
    auto row = std::make_unique<ECDI::Button>();
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,
        .foreground = kDarkInk});
    // 显示**相对仓库夹具根的路径**（`realistic/realistic-session.jsonl`），
    // 而不是裸文件名：六份夹具里 `cot-session.jsonl` 与 `demo-session.jsonl`
    // 不重名，但真实使用中同名文件很常见，裸名会让人点错。
    std::string shown = file.filename().generic_string();
    const std::filesystem::path root = HistoryRoot();
    if (!root.empty()) {
        const std::filesystem::path rel = file.lexically_relative(root);
        if (!rel.empty()) shown = rel.generic_string();
    }
    auto label = uikit::Label(shown, kSidebarRowW - 16, 16, kDarkInk, harness::kFontSmall);
    label->SetPosition(8, (kSidebarRowH - 16) / 2);
    // 禁用 ⇒ 不参与命中（点行中心一定命中行根），且 `IsEnabled` 无绘制消费者 ⇒ 视觉零变化。
    label->SetEnabled(false);
    row->AddChild(std::move(label));
    // ★ 回调**只置脏**，真打开留到 tick：本按钮此刻正被派发链持有，
    //   而"打开"会 `RemoveChild` 掉整棵侧栏/转录区（同 `m_sidebarDirty` 的 UAF 理由）。
    row->SetOnClick([this, file] {
        m_historyOpenPending = file;
        m_historyDirty = false;   // 打开会连列表一起重建，不必再单独刷一次
    });
    return row;
}

void HarnessApp::RebuildHistoryList() {
    if (!m_hud.historyList) return;
    if (!m_historyScanned) {
        m_historyFiles = FindHistoryFiles();
        m_historyScanned = true;
    }
    ECDI::Widget& content = m_hud.historyList->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }
    const size_t n = m_historyFiles.size();
    const size_t rowStride = static_cast<size_t>(kSidebarRowH + kSidebarRowGap);
    int y = 0;
    for (size_t i = 0; i < n; ++i) {
        auto row = BuildHistoryRow(m_historyFiles[i]);
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += static_cast<int>(rowStride);
    }
    const int totalH = n > 0 ? static_cast<int>(n * rowStride) - kSidebarRowGap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * static_cast<int>(rowStride)
                                              - kSidebarRowGap);
    m_hud.historyList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.historyList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.historyList->SetContentOffset(0, m_hud.historyList->GetScrollOffsetY());
    if (m_hud.historyCount) {
        m_hud.historyCount->SetText(std::to_string(n) + " 个历史文件");
    }
    if (ECDI::Widget* parent = m_hud.historyList->GetParent()) parent->Arrange();
}

std::filesystem::path HarnessApp::UiStatePath(const Session& s) const {
    // ★ 与夹具**同目录**、名字加 `.ui.json` 后缀：`demo-session.jsonl` →
    //   `demo-session.jsonl.ui.json`。同目录是为了"夹具搬走，状态跟着走"；
    //   加后缀而不是另建目录，是为了让 `rm *.jsonl` 这种清理动作**不会**顺手删掉状态。
    // ★ 不用 `evals/` 里那种 `<x>.jsonl.json`：那是**评估摘要**（旧产物，来自 `f243579`），
    //   与本状态文件语义完全不同，同名会让两者互相覆盖。
    if (s.originPath.empty()) return std::filesystem::path();
    std::filesystem::path p = s.originPath;
    p += ".ui.json";
    return p;
}

bool HarnessApp::LoadUiState(Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value root;
    std::string error;
    if (!json::Parse(text, root, error)) {
        // 坏状态文件不该让会话打不开：如实记一条日志，按"没有决策记录"继续。
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: " + error + " (" +
                                           path.generic_string() + ")"));
        return false;
    }
    // answers: { "<ask id>": "allow" | "deny" }
    if (const json::Value* answers = root.Find("answers");
        answers && answers->IsObject()) {
        // ★★ P4 D：answerTimes（id → "HH:MM"）是可选键——旧 sidecar 没有 ⇒
        //   answerAt 留空，重建的结论行如实不带时刻段。
        const json::Value* times = root.Find("answerTimes");
        for (const auto& kv : answers->object) {
            if (!kv.second.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Ask) continue;
                if (entry.id != kv.first) continue;
                entry.answered = true;
                entry.answer = kv.second.text;
                if (times && times->IsObject()) {
                    const auto it = times->object.find(kv.first);
                    if (it != times->object.end() && it->second.IsString()) {
                        entry.answerAt = it->second.text;
                    }
                }
            }
        }
    }
    // expanded: ["b1", ...]（只记展开的，收起是默认态）
    if (const json::Value* expanded = root.Find("expanded");
        expanded && expanded->IsArray()) {
        for (const json::Value& id : expanded->array) {
            if (!id.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Block) continue;
                if (entry.id == id.text) entry.expanded = true;
            }
        }
    }
    return true;
}

void HarnessApp::SaveUiState(const Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return;
    json::Value root;
    root.type = json::Value::Type::Object;
    root.object["version"] = json::Value();       // 先占位，下面逐步覆盖
    root.object["version"].type = json::Value::Type::Number;
    root.object["version"].number = 1.0;
    root.object["answers"].type = json::Value::Type::Object;
    root.object["answerTimes"].type = json::Value::Type::Object;
    root.object["expanded"].type = json::Value::Type::Array;
    bool anyAnswerTime = false;
    for (const TimelineEntry& entry : s.timeline) {
        if (entry.kind == TimelineEntry::Kind::Ask && !entry.answer.empty()) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.answer;
            root.object["answers"].object[entry.id] = v;
            if (!entry.answerAt.empty()) {
                json::Value t;
                t.type = json::Value::Type::String;
                t.text = entry.answerAt;
                root.object["answerTimes"].object[entry.id] = t;
                anyAnswerTime = true;
            }
        } else if (entry.kind == TimelineEntry::Kind::Block && entry.expanded) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.id;
            root.object["expanded"].array.push_back(v);
        }
    }
    // ★ 无任何时刻记录（旧状态文件升级后首写）就不落空键——读侧按缺键处理。
    if (!anyAnswerTime) root.object.erase("answerTimes");
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: 写入失败 " + path.generic_string()));
        return;
    }
    out << root.Pretty(2) << "\n";
}

void HarnessApp::ApplyUiStateToView(Session& s) {
    // ★ 为什么需要这一步（顺序陷阱）：`RebuildTranscriptView` 的第一行是
    //   `SyncRowViewState()`（视图 → 模型）。刚抽干出来的行全是 `expanded=false` /
    //   `answered=false` ⇒ 若只把 sidecar 写进模型就重建，同步会**立刻把刚读进来的
    //   状态覆盖掉**（模型被更旧的视图抹平）。故先把同一批状态也写进视图行。
    for (RowRec& r : s.rows) {
        if (r.timelineIdx >= s.timeline.size()) continue;
        const TimelineEntry& entry = s.timeline[r.timelineIdx];
        if (r.ask && entry.answered) {
            r.answered = true;
            ApplyAnsweredLook(r, entry.answer, entry.answerAt);   // 同一条视觉路径（Step 3）
        }
        if (r.body && entry.expanded) {
            r.expanded = true;
            r.body->SetExpanded(true);
        }
    }
}

bool HarnessApp::OpenHistorySession(const std::filesystem::path& file) {
    std::string error;
    auto replay = std::make_unique<ReplaySource>();
    if (!replay->Load(file, error)) {
        // ★ 如实报错，不建半好会话（半好会话会让"打不开"看起来像"打开了一个空会话"）。
        AddNoticeRow("[打开失败] " + file.filename().generic_string() + " — " + error,
                     kErrorDark);
        Relayout();
        RequestRepaint();
        return false;
    }

    PauseActiveTimedClock();

    auto session = std::make_unique<Session>();
    Session& s = *session;
    s.pendingTruncated = replay->TruncatedError();
    s.replay = replay.get();
    s.source = std::move(replay);
    s.label = file.filename().generic_string();
    // ★★ 来源必须记下来：这是本步新增的第三类会话（live / 启动时 --replay / 历史）。
    //   没有它，后续两个 tick 里就分不清"这个会话该不该读 sidecar"。
    s.originKind = "replay-history";
    s.originPath = file;
    m_sessions.push_back(std::move(session));
    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);   // 内部会 RebuildTranscriptView（此刻还是空的，无妨）

    // ── 跳到末尾：把源抽干（复用 `DrainInstant` 的消费语义）──
    //   ★ 为什么必须走 `HandleLine`（而不是直接读行数）：坏文件的读数
    //     （seqGaps/unknownKinds/protocolIncompatible）全在消费路径上产生。
    std::string line;
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    m_sess->instantDone = true;   // 已抽干 ⇒ 别让 DrainInstant 再跑一遍
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }

    // ── GUI 侧状态（sidecar）──
    const bool hadState = LoadUiState(*m_sess);
    if (hadState) {
        ApplyUiStateToView(*m_sess);
    } else {
        // ★ 缺失时必须**说出来**，不能静默当作"未回答"：Step 3 修的正是
        //   "历史把授权决定抹平成未回答"这条病，换个位置不能复发。
        bool anyAsk = false;
        for (const TimelineEntry& entry : m_sess->timeline) {
            if (entry.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        if (anyAsk) {
            AddNoticeRow("[此会话没有决策记录：磁盘上没有 " +
                         UiStatePath(*m_sess).filename().generic_string() +
                         "，权限门一律按未回答显示]", kDarkMuted);
        }
    }

    // 抽干后再重建一次：此刻 timeline 才有内容，且 sidecar 已写进模型与视图。
    RebuildTranscriptView();
    Relayout();
    FollowTailIfPinned();
    m_sidebarDirty = true;    // 会话列表要多出一行
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
    return true;
}

std::vector<std::string> HarnessApp::SnapshotSessionList() const {
    std::vector<std::string> out;
    if (!m_hud.sessList) return out;
    const ECDI::Widget& content = m_hud.sessList->GetContentView();
    for (size_t i = 0; i < content.GetChildCount(); ++i) {
        const ECDI::Widget* row = content.GetChildAt(i);
        if (row == nullptr) continue;
        std::string name;
        std::string selected = "0";
        size_t buttons = 0;
        for (size_t c = 0; c < row->GetChildCount(); ++c) {
            const ECDI::Widget* child = row->GetChildAt(c);
            if (dynamic_cast<const ECDI::Button*>(child)) ++buttons;
            if (const auto* lbl = dynamic_cast<const ECDI::Label*>(child)) {
                // 第一个 Label 就是名称（圆点是 Panel、`×` 是 Button，都不满足本分支）
                if (name.empty()) {
                    name = lbl->GetText();
                    selected = (lbl->GetTextColor() == kAccentInk) ? "1" : "0";
                }
            }
        }
        // 字段序：名称 · 选中 · 行内按钮数（×）· 行矩形 · ★行类（kind）。
        // 行矩形也算视图的一部分：「画了几行」和「画在哪」都能被抓出来。
        // ★★ 2026-10-20：`kind` **只能追加在尾部**。若插在中间，所有历史读数会在
        //   字段位上静默串味（旧日志把 geometry 当 kind 读，不报错但全错）。
        //   判据来源 = 行根类型：会话行是 Button（`Panel::ContainsPoint` 恒 false，
        //   可点行的根只能是 Button），分组标题行是 Panel。这就是两类行在视图层的
        //   唯一区别，不靠尺寸（尺寸会随主题变）。
        char geo[64];
        std::snprintf(geo, sizeof(geo), "%d,%d,%d,%d", row->GetX(), row->GetY(),
                      row->GetWidth(), row->GetHeight());
        const char* kind = dynamic_cast<const ECDI::Button*>(row) ? "session" : "group";
        out.push_back(name + '\x1f' + selected + '\x1f' + std::to_string(buttons) + '\x1f'
                      + geo + '\x1f' + kind);
    }
    return out;
}

HarnessApp::SidebarCompare HarnessApp::CompareSessionList(
    const std::vector<std::string>& before, const std::vector<std::string>& after) const {
    static const char* kFieldName[] = {"name", "selected", "buttons", "geometry", "kind"};
    SidebarCompare cmp;
    if (before.size() != after.size()) {
        cmp.matched = false;
        cmp.firstDivergence = "rowCount " + std::to_string(before.size()) + " -> "
                              + std::to_string(after.size());
        return cmp;
    }
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] == after[i]) continue;
        cmp.matched = false;
        // 逐字段定位第一处差异（与转录区 CompareRows 同款可读性：别只报"不一样"）
        auto split = [](const std::string& s) {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : s) {
                if (ch == '\x1f') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(ch);
            }
            parts.push_back(cur);
            return parts;
        };
        const std::vector<std::string> a = split(before[i]);
        const std::vector<std::string> b = split(after[i]);
        std::string detail = "row " + std::to_string(i);
        // 段落数必须与 `kFieldName` 同长：写死 4 会让新增的 `kind` 永远不参与比对，
        //   跟“没加”一样（而且不报错）。
        constexpr size_t kFieldCount = sizeof(kFieldName) / sizeof(kFieldName[0]);
        for (size_t f = 0; f < a.size() && f < b.size() && f < kFieldCount; ++f) {
            if (a[f] == b[f]) continue;
            detail += std::string(" ") + kFieldName[f] + " [" + a[f] + "] -> [" + b[f] + "]";
            break;
        }
        cmp.firstDivergence = detail;
        return cmp;
    }
    return cmp;
}


bool HarnessApp::SwitchToSessionByPtr(const Session* target) {
    if (target == nullptr) return false;
    for (size_t i = 0; i < m_sessions.size(); ++i) {
        // ★ 只比地址、不解引用：这一行可能在会话刚被删掉、列表还没重建的那一帧里
        //   被点到（时序见 RebuildSessionList 头注的 UAF 段），此时 target 已悬垂，
        //   任何解引用都是 UB；比地址是安全的，查不到就什么都不做。
        if (m_sessions[i].get() == target) {
            SwitchToSession(i);
            return true;
        }
    }
    return false;
}

bool HarnessApp::DeleteSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    // ★ 禁止删到零会话：`m_sess` 变空指针会让全仓 200+ 处 `m_sess->` 解引用崩溃。
    //   "支持零会话"是一个独立的架构决定（每个调用点加判空），不该被一个删除按钮带进来。
    if (m_sessions.size() <= 1) return false;

    const bool deletingActive = (index == m_activeIndex);
    if (deletingActive) {
        // ★★ 顺序不能换：先把活动指针挪到邻居（SwitchToSession 内部会
        //    SyncRowViewState → 重建转录区 → 完成切换），**之后**才 erase。
        //    反过来的话，`SwitchToSession` 会在旧 Session 已被销毁的情况下
        //    去读它的 rows/timeline（那里存的是裸指针）⇒ UAF。
        const size_t neighbor = (index + 1 < m_sessions.size()) ? index + 1 : index - 1;
        SwitchToSession(neighbor);
    }

    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(index));

    // erase 把后面的元素整体前移 ⇒ 活动下标可能要跟着减一。
    // 注意 `m_sess` 本身仍指向**同一个 Session 对象**（unique_ptr 换的是所有权位置，
    // 不是对象地址），故这里只需修正下标；用下标重取一遍指针是防御性写法。
    if (m_activeIndex > index) --m_activeIndex;
    if (m_activeIndex >= m_sessions.size()) m_activeIndex = m_sessions.size() - 1;
    m_sess = m_sessions[m_activeIndex].get();

    // ★ 不在事件派发栈上拆树（见本段头注的 UAF 段）：只置脏，重建交给下一个 tick。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

// ★★ 2026-10-20：归档 / 恢复（m00001 第 6 步残留）。
//   归档 = 「不活动但可打开且不占资源」。会话留在 m_sessions 原位（下标不动），
//   只是从侧栏主列表隐藏、移到「归档」区。
bool HarnessApp::ArchiveSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    Session* target = m_sessions[index].get();
    if (target->archived) return false;  // 已归档，不重复操作

    const bool archivingActive = (index == m_activeIndex);
    if (archivingActive) {
        // ★ 归档活动会话 ⇒ 先切到邻居（同 DeleteSessionAt 的纪律）。
        //   邻居必须是**非归档**的：切到一个归档会话会让活动指针指向
        //   一个不在主列表里的会话，HUD 会显示用户看不到的东西。
        size_t neighbor = static_cast<size_t>(-1);
        for (size_t i = index + 1; i < m_sessions.size(); ++i) {
            if (!m_sessions[i]->archived) { neighbor = i; break; }
        }
        if (neighbor == static_cast<size_t>(-1)) {
            for (size_t i = 0; i < index; ++i) {
                if (!m_sessions[i]->archived) { neighbor = i; break; }
            }
        }
        if (neighbor == static_cast<size_t>(-1)) {
            // ★ 所有其他会话都已归档 ⇒ 不能归档最后一个可见会话。
            //   理由同「禁删到零会话」：活动指针必须指向一个用户能看见的会话。
            return false;
        }
        // ★ 若被归档的是临时会话，先取消临时标志：否则 SwitchToSession 里的
        //   MaybeDeleteTemporarySession 会把它**删掉**而不是归档。
        target->temporary = false;
        SwitchToSession(neighbor);
    }

    target->archived = true;
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

bool HarnessApp::RestoreSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    Session* target = m_sessions[index].get();
    if (!target->archived) return false;  // 未归档，不重复操作

    target->archived = false;
    // ★ 恢复 = 取消归档 + 切到它（用户点「↩」的心智模型是「打开它」）。
    SwitchToSession(index);
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

// ★★ Step 2（2026-10-19）：侧栏显隐。
//   用户口径（m00001 第 2 步）：「最便宜的一个。但不要用 CollapsiblePanel 的 200ms
//   动画——中间帧会污染 frameCount/paints。用 SetVisible(false) + 父 Arrange() 瞬时切换」。
void HarnessApp::SetSidebarVisible(bool visible) {
    if (!m_hud.sidebar) return;
    if (m_sidebarVisible == visible && m_hud.sidebar->IsVisible() == visible) return;

    m_sidebarVisible = visible;          // ① 模型侧先落（重建后按它恢复）
    m_hud.sidebar->SetVisible(visible);  // ② 值变化时才 Invalidate（Widget.cpp:427-446）

    // ③ 重排父容器：布局**只在 Arrange 时**跳过隐藏子（C-VIS-1）把它们停泊到负区、
    //    并且不占主轴槽位与间隙 ⇒ 不 Arrange，中栏就吃不到那 200px。
    //    `HorizontalLayout` 里中栏 stretch=1 吃满余宽，故侧栏一藏，中栏立刻变宽 200。
    //    ★ Arrange 本身**不 Invalidate**（Widget.cpp 头注释）——重绘由 ② 的
    //      SetVisible 负责，这里不额外 RequestRepaint，避免虚增 invalidations。
    if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();

    // ④ 中栏变宽 ⇒ 转录行宽变 ⇒ 自动换行必须重算，否则文字仍按旧宽度折行，
    //    留在更宽的气泡里（看上去像"内容变短了"，一类只在视觉上暴露的缺陷）。
    Relayout();
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// ★★ P4 Phase B（2026-10-25）：检视栏显隐的唯一入口（中栏头 `≡` 按钮与 Ctrl+I
//   共用）。抽出前逻辑内联在按钮回调里——全局快捷键没有可复用的路径，两份
//   实现迟早漂移（SetSidebarVisible 的"唯一入口"纪律）。
void HarnessApp::ToggleInspector() {
    if (!m_hud.inspector) return;
    m_inspectorVisible = !m_inspectorVisible;
    m_hud.inspector->SetVisible(m_inspectorVisible);
    if (ECDI::Widget* parent = m_hud.inspector->GetParent()) parent->Arrange();
    Relayout();
    RequestRepaint();
}

// ★★ P4 Phase B（2026-10-25）：全局快捷键分派（HarnessApplication::OnKeyDown
//   在转基类**之前**调用；命中返回 true ⇒ 事件到此为止，不派给焦点控件）。
//   ★ 刻意不拦 Ctrl+A/C/X/V/Z（TextBox 编辑语义）与 Enter（PromptBar 发送）。
bool HarnessApp::HandleGlobalKeyDown(const ECDI::KeyDownEvent& event) {
    const ECDI::KeyCode key = event.GetKeyCode();
    if (key == ECDI::KeyCode::Escape) {
        // 逐层关：设置 > 模型浮窗（P4 H）> ···菜单。
        // 无层可关 ⇒ 返回 false 让事件照常派发（Esc 不悬空拦截）。
        if (m_settingsVisible) {
            SetSettingsVisible(false);
            return true;
        }
        if (m_modelPopupOpen) {
            SetModelPopupOpen(false);
            return true;
        }
        if (m_moreMenuOpen) {
            SetMoreMenuOpen(false);
            return true;
        }
        return false;
    }
    if (!event.IsCtrlDown()) return false;
    switch (key) {
        case ECDI::KeyCode::B:
            SetSidebarVisible(!m_sidebarVisible);
            return true;
        case ECDI::KeyCode::I:
            ToggleInspector();
            return true;
        case ECDI::KeyCode::Comma:
            SetSettingsVisible(!m_settingsVisible);
            return true;
        case ECDI::KeyCode::N:
            NewSession();
            return true;
        case ECDI::KeyCode::M:   // ★ P4 H：模型浮窗开合（同顶栏 chip 点击）
            SetModelPopupOpen(!m_modelPopupOpen);
            return true;
        default:
            return false;
    }
}

// 冻结活动会话的 timed 锚点：记下暂停时刻（幂等——已暂停则不动）。
void HarnessApp::PauseActiveTimedClock() {
    if (!m_sess || !m_options.timed) return;
    if (!m_sess->timedPaused) {
        m_sess->timedPaused = true;
        m_sess->timedPausedAt = std::chrono::steady_clock::now();
    }
}

// 恢复某会话的 timed 锚点：把锚点整体后移"被冻结了多久"。
//   ★ 为何需要补偿：elapsedMs = now - anchor。会话被切走期间 anchor 不动而 now 前进，
//     切回来若不补偿，elapsedMs 会凭空多出"离开的那段墙上时间" ⇒ 一次性抽干夹具。
void HarnessApp::ResumeSessionTimedClock(Session& s) {
    if (!m_options.timed) return;
    if (s.timedPaused) {
        const auto pausedFor = std::chrono::steady_clock::now() - s.timedPausedAt;
        s.timedAnchor += pausedFor;
        s.timedPaused = false;
    }
}

int HarnessApp::Run() {
    // ★ 0d：会话创建失败时 m_sess 为**空指针**（构造失败不再 push 坏会话），
    //   故先判空再解引用。
    if (!m_sess || !m_sess->source) {
        MessageBoxW(nullptr,
                    L"消息源不可用。\n"
                    L"--core 指定的假 core 启动失败，或 --replay 夹具加载失败。\n"
                    L"详见日志。",
                    L"harness_gui", MB_ICONERROR | MB_OK);
        return 4;
    }
    HarnessApplication application;
    m_application = &application;
    application.SetOwner(this);

    // ── 后端选择（Phase 26 批二：--backend gl；默认 GDI 逐位零回归）──
    // ★ D7：显式请求 GL 但初始化失败 ⇒ 明确报错退出，不静默回退 GDI——
    //   否则「性能对照」可能实际跑的是 GDI，读数却记在 GL 头上。
    ECDI::RenderServices services = m_options.useGl
        ? ECDI::CreateGLRenderServices()
        : ECDI::CreateDefaultRenderServices();

    // ── Phase 27 接入：--cull-probe 包一层 PaintProbe（透传 + 逐帧命令计数）──
    // 2026-10-05 升级为探针套件：再包一层 MeasureProbe（文本测量成本）。
    // 包装后 GL 就绪检查走探针 IsReady()（转发内部后端），不改变行为。
    if (m_options.cullProbe) {
        auto probe = std::make_unique<ECDI::PaintProbe>(std::move(services.renderer));
        m_probe = probe.get();
        services.renderer = std::move(probe);

        auto measureProbe = std::make_unique<ECDI::MeasureProbe>(std::move(services.measurer));
        m_measureProbe = measureProbe.get();
        services.measurer = std::move(measureProbe);
    }
    ECDI::RenderingBackend* const requestedBackend = services.renderer.get();

    const std::string windowTitle = "ECDI harness - " + m_options.fixtureName;
    ECDI::Window& win = application.Create(windowTitle, 1120, 760, std::move(services));
    // 使用 ECDI CaptionBar（参照 examples/ModelProbe）：逻辑命中区和实体栏高保持一致，
    // 必须在 Show()/Application::Run() 之前配置窗口 chrome。
    win.SetChromeMode(ECDI::ChromeMode::Borderless);
    win.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);
    // ★★ 2026-10-23（ECDI REQ-13 落地）：窗口下限交给**框架**，不再由本仓在
    //   `OnWindowResized` 里夹。判据与理由见 `App.h` 的 `kMinClientW` 注释。
    //   ★ 为什么必须在 `Show()` 之前登记：下限本身运行期改也有效（框架契约），
    //     但在建窗时一次登记好，才能让**第一帧**就落在合规尺寸上——否则窗口会
    //     先以 1120×760 之外的任何尺寸出现一瞬。1120×760 本身就在下限之上，
    //     故此处登记不改变任何既有读数。
    win.SetMinimumSize(kMinClientW, kMinClientH);

    if (m_options.useGl) {
        if (requestedBackend == nullptr || !requestedBackend->IsReady()) {
            MessageBoxW(nullptr,
                        L"OpenGL (WGL) backend initialization failed.\n"
                        L"--backend gl was requested; not silently falling back to GDI.\n"
                        L"Check that OpenGL is available, or run without --backend gl.",
                        L"harness_gui", MB_ICONERROR | MB_OK);
            m_application = nullptr;
            return 3;
        }
        m_backendName = "gl";
    }
    m_window = &win;
    // ★ 2026-10-18 v7：主题必须在 **BuildUi 之前** 定下来——令牌是引用别名，
    //   UI 建好时颜色值已被拷进各控件的 PanelStyle/ButtonStyle，之后再切不会重绘。
    harness::SwitchTheme(m_options.lightTheme);
    // 窗口底色 = 画布色（默认白——resize/缝隙漏白全靠它兜底）
    // ★ --probe-contrast：白底（探针测量用；默认路径逐位不变）
    win.SetBackgroundColor(m_options.probeContrast ? kProbeCanvas : harness::kContentBg);
    BuildUi(win);
    RefreshHud();  // Before hello, live controls are deliberately gated closed.
    uikit::WarnZeroSized(win.GetRootWidget());   // F2/D9 巡检：漏 SetSize 的 0×0 静默不可见 → 告警

    // P1.5 live：收到 core 的 hello 之后回 hello（协议 §4：seq 0）
    if (m_sess->writer) {
        m_sess->writer->Hello("0.0.1");
    }
    win.Show();
    if (m_promptBar && m_promptBar->input()) {
        win.SetFocusedWidget(m_promptBar->input());   // 打开即可打字（验收 A2）
    }

    // ★★ REQ-03：装唤醒**必须在这里**——UI 树已经建好（`BuildUi` + `Show` 之后），
    //   否则第一个唤醒回调里的 `HandleLine` 会去碰还不存在的控件行。
    //   此刻消息循环尚未进入，投递只会在队列里等着，天然没有竞态。
    InstallWakeHandlers();
    // ★ 对照世界（`--legacy-poll`）也要开戳：否则「推更快」只剩结构论证，
    //   没有同口径读数可对。`InstallWakeHandler` 里已经给默认世界开过（幂等）。
    if (m_options.asyncProbe) {
        for (auto& session : m_sessions) {
            if (session->child) session->child->EnableLineStamps(true);
        }
    }
    win.GetPlatformWindow().StartTimer(kReplayTimer, 33);
    if (m_options.timed) {
        // ★ 0d：timed 锚点逐会话（协议 §8.2：ts 是唯一真相源）。
        //   放在 Session 里，否则新建/切回的会话会拿别人的锚点算 elapsedMs。
        m_sess->timedAnchor = std::chrono::steady_clock::now();
    }
    if (m_options.autoCloseMs > 0) {
        win.GetPlatformWindow().StartTimer(kCloseTimer,
                                           static_cast<unsigned int>(m_options.autoCloseMs));
    }
    MaybeSendAutoPrompt();  // Replay keeps its prior immediate note; live waits for workspace ack.
    // v3 auto-prompt waits for the core-confirmed initial workspace path.
    // ★ 2026-10-20：探针需要更长的安全超时，但**不得写回 `m_options`**——那是 CLI 选项，
    //   改写它等于让 `--auto-close` 在探针下静默失真（写摘要时读到的是被篡改后的值）。
    //   只重设计时器，选项本身不动。
    if (m_options.workspaceProbe && m_options.autoCloseMs < 90000) {
        win.GetPlatformWindow().StartTimer(kCloseTimer, 90000);
    }

    const int code = application.Run();

    // P1.5 live：关窗前发 bye（协议 §4：guest 即将关闭，请 core 退出）→ 关 stdin → 收尸。
    // ★ 0d：必须遍历**所有**会话——多会话下活动指针只指一个，而每个 live 会话
    //   各拉起过一个 core 子进程。只 Bye/Stop 活动会话 ⇒ 其余 core 变僵尸
    //   （这正是 P1.5 验收里"no zombies"那一项要防的）。
    for (auto& session : m_sessions) {
        if (session->writer) session->writer->Bye();
        if (session->child) session->child->Stop();
    }

    WriteSummary();
    m_application = nullptr;
    return code;
}

void HarnessApp::BuildUi(ECDI::Window& window) {
    ECDI::Widget& root = window.GetRootWidget();
    // ★★ P4 F 修复（实测反馈）：root 布局换 HarnessRootLayout——逐位复刻
    //   VerticalLayout(0,true) 但跳过 Exempt 注册的全窗浮层（设置遮罩/sheet，
    //   它们挂 root 盖全窗）。原布局把**可见的**浮层当固定尺寸子统计
    //   （mask 整窗高 + sheet 560 高），一开设置主区就被挤没了。浮层几何
    //   归 LayoutSettingsOverlay。BuildUi 尾部转移挂载后注册豁免。
    auto rootLayout = std::make_unique<HarnessRootLayout>();
    HarnessRootLayout* const rootLayoutRaw = rootLayout.get();
    root.SetLayout(std::move(rootLayout));

    // CaptionBar 是 root 的直接子项：保持贴边全宽，空白区可拖动，按钮由 harness 自绘
    // （ECDI 原生 CaptionBar 把颜色写死，跟不了主题 —— 见 ThemedCaptionBar.h 头注）。
    auto caption = std::make_unique<harness::ThemedCaptionBar>(
        window, "ECDI harness - " + m_options.fixtureName);
    m_caption = caption.get();
    // ★★ 2026-10-21：宽度不再写死 1120 —— 取当前客户区宽度。
    //   写死的后果：窗口一拉伸，标题栏（它自己画最小化/最大化/关闭三个钮）就停在
    //   1120 宽的位置，右侧留一条同色空白、三个钮不在窗口右上角。
    //   ★ 这里读的是**构造时**的真实宽度；运行期变化由 `OnWindowResized` 接手。
    caption->SetSize(std::max(320, window.GetRootWidget().GetWidth()),
                     ECDI::CaptionBar::kDefaultHeight);
    root.AddChild(std::move(caption));

    BuildTopBar(root);
    BuildMainArea(root);
    BuildBottom(root);
    // ★ Step 1：列表的**首帧**填充。放在 `root.Arrange()` 之前是安全的——
    //   `RebuildSessionList` 自己会把侧栏重排一遍（列表高度会变，下方分区要跟着让位）。
    //   ★ 这里**直接**调而不是置 `m_sidebarDirty`：此刻还没有窗口定时器，
    //     置脏要等第一个 tick 才生效 ⇒ 首帧会是空列表（用户会看到一闪而过的空栏）。
    RebuildSessionList();
    // ★ Step 5：历史区同样要首帧填上（同一理由：此刻还没有定时器，置脏要等一 tick）。
    RebuildHistoryList();
    // ★★ 2026-10-20：「项目」段同样要首帧填上（同理由）。
    RebuildProjectList();
    // ★★ P4 F 修复（实测反馈「变色应该是整个变色」）：遮罩 + sheet 转移到
    //   **root 末尾**（最后子 = 最上层，盖过 caption/counting 全窗——含顶栏/
    //   侧栏/检查器；此前挂 m_scroll 只盖转录视口）。转移必须在 root.Arrange()
    //   **之前**：Arrange 递归驱动子树，挂好后 mask/sheet 尺寸交给
    //   LayoutSettingsOverlay（Relayout 尾部）摆。
    if (m_hud.settingsMask && m_hud.settingsMask->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.settingsMask));
        rootLayoutRaw->Exempt(m_hud.settingsMask);   // 布局视而不见
    }
    if (m_hud.settingsPanel && m_hud.settingsPanel->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.settingsPanel));
        rootLayoutRaw->Exempt(m_hud.settingsPanel);
    }
    root.Arrange();
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建——运行期切主题的唯一可行路径。
//   【为什么非拆不可】`Theme.h` 的令牌是引用别名（`inline const Color& kSidebarBg =
//   g_theme.sidebarBg;`），而 ECDI 的 `PanelStyle`/`ButtonStyle` 是**值语义**：
//   `BuildUi` 那一刻把颜色拷进每个控件的 style，之后改 `g_theme` 不会让任何已存在
//   的控件换色（`Theme.h:19-20` 记的正是这条，`Run()` 里那句"必须在 BuildUi 之前"
//   是它的直接后果）。⇒ 要让树换色，只能让树按新令牌重新长一遍。
//   【为什么这是四步里唯一动仪器的】树的子节点之一就是 `CountingPanel`——`paints`
//   与 `frameCount` 的**唯一**来源。拆树会析构它，新面板从 0 开始数 ⇒ 不续接
//   计数器，一次主题切换就把"重绘 22 次"变成"重绘 3 次"，评估基线当场失效。
//   `CountingPanel::SeedCounters` 就是为此存在（它不做例外式的"永不重建"——
//   那会让判据退化成"除了那棵树都行"）。
void HarnessApp::RebuildUi() {
    if (!m_window) return;

    // ① 视图侧的用户状态先写回模型。`RebuildTranscriptView` 内部也会调一次，
    //    但这里动的是**整棵树**（连 `m_scroll` 都要换），必须在拆之前再保一次，
    //    否则行的展开/已答会跟着旧控件一起消失（0c 的 `buildTitle` 就是这类回退）。
    SyncRowViewState();
    m_pendingPromptViews.clear();  // old UI widgets are about to be destroyed; Session models survive.

    // ② 评估计数器快照。**必须在拆树之前**读——旧面板一析构就读不到了。
    const size_t paints = m_counting ? m_counting->PaintCount() : 0;
    std::vector<uint64_t> frameUs;
    if (m_counting) frameUs = m_counting->FrameDurationsUs();

    // ③ 用户输入与焦点。输入文本是**唯一无法从模型重建**的界面状态
    //    （`TimelineEntry` 只描述已发生的转录，不描述还没发出去的草稿）。
    //    ★ 光标位置无公开写入点（`TextBox` 只有 `MoveCaret`/`MoveCaretToEnd`，
    //      没有 `SetCaret(size_t)`）⇒ 只能恢复到"末尾"，这是本步的**已知损失**。
    const std::string draft = (m_promptBar && m_promptBar->input())
                                  ? m_promptBar->input()->GetText()
                                  : std::string();

    // ④ 拆掉 root 的全部子节点。`RemoveChild` 走 `Window::ForgetSubtree`
    //    （焦点/捕获/hover 一并注销）⇒ 拆完窗口处于"无焦点"状态，这正是
    //    `Application::OnTimer` 会在下一个 tick 直接 return 的原因
    //    （定时器只派给焦点控件，`Application.cpp:303-339`）——**所以后面必须重设焦点**，
    //    否则回放 tick 停摆、探针再也跑不动。这是本步的头号结构性风险。
    ECDI::Widget& root = m_window->GetRootWidget();
    while (root.GetChildCount() > 0) {
        root.RemoveChild(root.GetChildAt(0));
    }

    // ⑤ 让 `g_theme` 与 `g_themeLight` 重新对齐（`SwitchTheme` 只是重新赋值，
    //    幂等），然后按新令牌把整棵树建出来。
    harness::SwitchTheme(harness::g_themeLight);
    m_window->SetBackgroundColor(m_options.probeContrast ? kProbeCanvas : harness::kContentBg);
    BuildUi(*m_window);

    // ⑥ 续接计数器。`BuildUi` 刚建出的新面板计数为 0；把 ①② 的快照写回去，
    //    于是"重建"在评估读数上是**透明的**（paints 一位不变）。
    if (m_counting) {
        m_counting->SeedCounters(paints, std::move(frameUs));
    }

    // ⑦ 转录区从模型重建。`BuildUi` 只建了空容器（启动时转录本来就是空的），
    //    运行期重建必须自己把 `timeline` 铺回去——`RebuildTranscriptView` 做的就是
    //    这件事（它同时把滚动位置保持在原处）。
    RebuildTranscriptView();

    // ⑧ 恢复草稿。`SetText` 不触发 `RaiseTextChanged`（`TextBox.cpp:121-141`），
    //    故不会误触发任何发送逻辑；`MoveCaretToEnd` 是唯一可用的光标落点。
    if (m_promptBar && m_promptBar->input()) {
        if (!draft.empty()) {
            m_promptBar->input()->SetText(draft);
            m_promptBar->input()->MoveCaretToEnd();
        }
        // ★★ 2026-10-21：草稿恢复**必须显式重算高度**。
        //   `SetText` 不走 `RaiseTextChanged`（D7 契约），而 `ComposerBox` 的自动长高
        //   挂在 `OnTextChanged` 上 ⇒ 这里若不同步，一个 3 行的草稿会在重建后
        //   被塞回单行高（垂直压扁、光标跑到框外）——而 `CompareUi` 的
        //   `inputText` 通道**读的是文本，看不见高度**，所以这个缺陷在整树重建
        //   探针里是全绿的。这正是"读文本看不见几何"那条老账。
        m_promptBar->SyncInputHeight();
        // ⑨ 焦点交还输入框：既恢复 caret 闪烁（证明焦点真的回来了），
        //    也让 `Application::OnTimer` 重新有派发目标（否则 tick 停摆）。
        m_window->SetFocusedWidget(m_promptBar->input());
    }

    // ⑩ 侧栏显隐是**模型侧状态**（`m_sidebarVisible`），新树默认全可见 ⇒ 按它恢复。
    if (!m_sidebarVisible && m_hud.sidebar) {
        m_hud.sidebar->SetVisible(false);
        if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();
    }
    // ⑩c P3.5：设置面板显隐同为模型侧状态 ⇒ 走唯一的投影路径对齐。
    ApplySettingsVisibility();
    // ★★ 2026-10-20⑩b / 2026-10-21 订正：`···` 菜单条的显隐同样是模型侧状态。
    //   ★ 原文写的是「新树的菜单条是**可见**的（`BuildUi` 建完还没投影）」——
    //     那句在 2026-10-21 之后**已经不成立**：`BuildMainArea` 现在建完就按
    //     `m_moreMenuOpen` 投影了（缺陷经过见那一段的注释）。留着旧说法会让人以为
    //     这里在补一件 `BuildUi` 没做的事，而实际上两处做的是同一件事。
    //   ★ 之所以**仍然保留**这一步：`BuildUi` 那句只在**建树那一刻**读模型，
    //     而这里是整树重建收尾前的最后一道对齐；代价是一次赋值 + 一次 Arrange，
    //     且 `SetVisible` 同值不 Invalidate（`Widget.cpp:427-446`）。
    //   ★ 仍然直接打 `SetVisible` 而不调 `SetMoreMenuOpen`：后者带同值守卫
    //     （`m_moreMenuOpen == open` 直接返回），这里要的是「把控件状态对齐到模型」，
    //     两者不是一回事；且它还会 Relayout/Repaint，此处收尾会统一做。
    if (m_hud.moreMenu) {
        m_hud.moreMenu->SetVisible(m_moreMenuOpen);
        if (ECDI::Widget* parent = m_hud.moreMenu->GetParent()) parent->Arrange();
    }

    // ⑪ 收尾：环进度条的颜色级别缓存要作废（新控件 + 新令牌），HUD 全量刷一次。
    m_ctxRingLevel = -1;
    ++m_uiRebuilds;
    Relayout();
    FollowTailIfPinned();
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// 顶栏 HUD（hi-fi .titlebar）：≡ | harness | 会话 chip | 后端 chip | … | 行模式 chip | 圆点+状态
void HarnessApp::BuildTopBar(ECDI::Widget& root) {
    auto bar = std::make_unique<ECDI::Panel>();
    bar->SetSize(1000, 36);
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kDarkTopBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    bar->SetLayout(std::make_unique<ECDI::HorizontalLayout>(10, true, 8));

    // ★★ Step 2（2026-10-19）：`≡` 原本是 `Label`——**点不到**（Label 没有
    //   OnMouseButtonUp，基类 `Widget` 的所有鼠标回调都是空实现，
    //   `Widget.cpp:405-421`）。改成 Button 才有回调。
    //   ★ 视觉必须与原来的 Label 一致：22×20、kDarkMuted、SansFont(14)。
    //     `Label` 基线是"水平左对齐+垂直居中"（`TextWidget::CalculateTextPosition`
    //     的默认），而 Button 的水平位置由 `ButtonStyle.textAlignment` 驱动、主题
    //     缺省是 **Center**（ButtonStyle.h:36 的 C30-3）——所以这里显式写 Left，
    //     否则字形会从 22px 宽的左边缘挪到中间，与改前的截图对不上。
    auto menu = std::make_unique<uikit::TipButton>(
        "≡", "侧栏显隐（Ctrl+B）",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    menu->SetTextColor(kDarkMuted);
    // ★ 不调 SetFont：原 `Label("≡")` 用的是**主题默认字体**（TextWidget 的
    //   TextStyle 缺省），Button 的 `ApplyTheme` 同样先走 `TextWidget::ApplyTheme`
    //   ⇒ 不覆盖就与改前同源。这里曾经多写了一行 `SansFont(14.0f)`，截图逐像素
    //   比对时发现字形整体下移 2px、笔画变短（y=14/17/20 → 16/19/22）——
    //   `SansFont` 注入的是 `g_theme.sansFamily`，与主题缺省 family 不是同一个字体。
    menu->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kDarkTopBg,   // 与顶栏同色 ⇒ 看起来仍是裸字形
        .border = harness::kDarkTopBg,
        .borderWidth = 0.0f,
        .cornerRadius = 0.0f,
        // ★ hover/pressed 必须显式钉成与 background 同色：不写就落回主题缺省，
        //   鼠标掠过左上角会闪一块与顶栏不同的色，还会触发 200ms 背景动画
        //   （`Button::AnimateBackgroundTo` 只在目标 != 当前时启动 ⇒ 同色即零动画、
        //   零额外重绘；这条直接决定 paints/invalidations 读数是否被污染）。
        .pressedBackground = harness::kDarkTopBg,
        .hoverBackground = harness::kDarkTopBg,
        .textAlignment = ECDI::TextAlignment::Left});
    menu->SetSize(22, 20);
    m_hud.sidebarToggle = menu.get();
    // ★ 点完把焦点还给输入框：`Button::CanFocus() == true` ⇒ 派发链里
    //   `Application::OnMouseButtonDown` 已经 `SetFocusedWidget(target)`，
    //   于是输入框失焦 ⇒ **caret 停闪、边框从聚焦色变回常态色**。
    //   截图逐像素比对抓到的正是这条（展开态 vs 再展开态差 1480 px，全部落在
    //   输入框上下边框那一行 `#e86d59` → `#e9e3e0`）。
    //   `≡` 是**窗体级开关**，不接收文本输入，没有理由拿走键盘焦点。
    menu->SetOnClick([this] {
        SetSidebarVisible(!m_sidebarVisible);
        if (m_window && m_promptBar) {
            m_window->SetFocusedWidget(m_promptBar->input());
        }
    });
    bar->AddChild(std::move(menu));

    auto title = std::make_unique<ECDI::Label>("harness");
    m_hud.title = title.get();
    title->SetTextColor(kDarkInk);
    {
        // ★ 2026-10-18 原型 C：品牌字 15px/700。**"harness" 是纯拉丁** ⇒ 这是
        //   `SemiFont`（`Segoe UI Semibold`）唯一合法的用处——中文串绝不能用
        //   （GDI 回落宋体 / GL 解析 msyhbd.ttc ⇒ 两链不同 face，见 Theme.h）。
        title->SetFont(harness::SemiFont(harness::kFontBrand));
    }
    title->SetSize(72, 20);
    bar->AddChild(std::move(title));

    // chip = 药丸 Panel + 内嵌 Label；accent 变体描边用 --accent、字用 #cfe1ff
    auto makeChip = [](std::unique_ptr<ECDI::Label> label, int width, bool accent) {
        auto pill = std::make_unique<ECDI::Panel>();
        pill->SetSize(width, 20);
        pill->SetStyle(ECDI::PanelStyleOverride{
            .background = kDarkPillBg,
            .cornerRadius = kRadiusPill,
            .borderWidth = 1.0f,
            .borderColor = accent ? harness::kDarkAccent : kLine});
        pill->AddChild(std::move(label));
        return pill;
    };
    // ★ 用 `uikit::Centered`（`CenteredLabel`）而非裸 `Label`（2026-10-16 用户反馈
    //   「s-cot-12 / replay 这几个在胶囊内的字位置不对，贴左了，应该居中」）。
    //   根因：`ECDI::Label` **不** override `CalculateTextPosition` ⇒ 基类默认**左对齐**
    //   （`Label.h` 只声明 `OnPaint`；全库只有 `Button` 做水平对齐）。
    //   胶囊是定宽容器 ⇒ 文字必须居中才不显偏。
    auto chipLabel = [](const std::string& text, const ECDI::Color& color, int width) {
        return uikit::Centered(text, width, 20, color, 11.0f, /*mono=*/true);
    };

    auto sessChipLabel = chipLabel("s-____", kAccentInk, 84);
    m_hud.sessChip = sessChipLabel.get();
    bar->AddChild(makeChip(std::move(sessChipLabel), 84, true));
    bar->AddChild(makeChip(chipLabel("replay", kDarkMuted, 64), 64, false));

    auto grow = std::make_unique<ECDI::Panel>();
    grow->SetStretch(1);
    grow->SetSize(10, 10);
    bar->AddChild(std::move(grow));

    // ★ 同为胶囊 ⇒ 同样居中（见上方 `chipLabel` 注释）
    auto rowLabel = uikit::Centered("autosize", 80, 20, kDarkMuted, 11.0f, /*mono=*/false);
    m_hud.rowChip = rowLabel.get();
    bar->AddChild(makeChip(std::move(rowLabel), 80, false));

    // ★★ 2026-10-21：后端读数胶囊（`gdi` / `gl`）。
    //
    //   【改前是什么样】`Hud::backendChip` **只被读过、从没被写过**：`RefreshHud`
    //     里有 `if (m_hud.backendChip) { ...SetText(m_backendName + " ▽") }`，
    //     而建树处**根本没有这个控件** ⇒ 那个分支永远不成立，屏幕上从来没有它。
    //     也就是说：一个"看起来存在的读数"实际上不存在，而代码里读得若有其事。
    //     这与 `sessSession` / `sessCore` 是同一类（读了但永不写），
    //     故一并处理，并且把这一类做成了可机检的判据（`--hud-probe`）。
    //
    //   【为什么**不**画 `▽`】原代码拼的是 `m_backendName + " ▽"`，`▽` 是个
    //     "可以点开选"的暗示。但后端**在 `Create` 时就定死了**（`--gl` /
    //     `--backend`，运行期没有任何切换路径：全仓只有一处给 `m_backendName`
    //     赋值，就在 GL 就绪检查之后），切后端要重建整个 `PlatformWindow`。
    //     ⇒ 画一个下拉箭头等于承诺一个不存在的菜单——这正是本步要清掉的那类
    //     "看着能点、其实不能"。所以这里**只显示名字**，不带箭头。
    //   ★ 也不做成 Button：它没有动作可做。做成按钮就是把人引到一条死路上。
    //   ★ **必须**把句柄存进 `Hud`：否则 `RefreshHud` 里那个 `if` 又变成死分支
    //     ——那正是它改前的状态。建树时赋值是这条链唯一的写入点。
    {
        auto backendLabel = chipLabel(m_backendName, kDarkMuted, 52);
        m_hud.backendChip = backendLabel.get();
        bar->AddChild(makeChip(std::move(backendLabel), 52, false));
    }

    bar->AddChild(uikit::DotCell(8, kOkDark, 20, &m_hud.statusDot));

    auto state = std::make_unique<ECDI::Label>("idle");
    m_hud.stateChip = state.get();
    state->SetTextColor(kDarkMuted);
    {
        state->SetFont(harness::SansFont(12.0f));
    }
    state->SetSize(60, 20);
    bar->AddChild(std::move(state));

    // ★★ P4 Phase H（2026-10-25）：顶栏模型 chip（`提供商 › 模型 · N% ▾`）。
    //   数据常驻（RefreshHud 里 UpdateModelChip）；点击开下拉浮窗（Ctrl+M 同）。
    auto modelChip = std::make_unique<uikit::TipButton>(
        "— ▾", "模型切换（Ctrl+M）——仅 idle 发出，运行中自动排队",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    m_hud.modelChip = modelChip.get();
    modelChip->SetTextColor(kDarkInk);
    modelChip->SetFont(harness::SansFont(harness::kFontSmall));
    modelChip->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = harness::kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    modelChip->SetSize(200, 22);
    modelChip->SetOnClick([this] { SetModelPopupOpen(!m_modelPopupOpen); });
    bar->AddChild(std::move(modelChip));

    // ★★ Step 4 / 0e（2026-10-19）：运行期主题切换控件。
    //   【为什么必须放在顶栏】它是**全局**状态的中枢，不是某个面板的属性；
    //   侧栏是会话的容器、检查器是读数的容器，都不该承载"整个程序长什么样"。
    //   【为什么是 Button】切换的唯一正确路径是"点一下 → 回调 → `RebuildUi`"。
    //   若做成 Label 加别处的热键，就没法用合成点击验证它——`RunThemeProbe` 的
    //   `toggle` 相走的正是 `SynthesizeClickAt` → 真实派发链 → `RaiseClick`。
    //   【为什么这里可以调 SetFont 而 `≡` 不行】`≡` 的 bug 是"用了与主题缺省不同
    //   的 family 导致字形漂移"；本钮显式指定 `SansFont`，两侧（深/浅）都按同一
    //   规则现算，不存在"和谁不一样"的问题。
    auto themeBtn = std::make_unique<uikit::TipButton>(
        "主题", "深色 / 浅色切换",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    m_hud.themeToggle = themeBtn.get();
    themeBtn->SetTextColor(kDarkInk);
    themeBtn->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    themeBtn->SetSize(48, 20);
    themeBtn->SetFont(harness::SansFont(harness::kFontSmall));
    themeBtn->SetOnClick([this] {
        // 真翻转在回调里做（只改 `g_theme` 与 `g_themeLight`，不碰任何控件）。
        harness::SwitchTheme(!harness::g_themeLight);
        // ★ **绝不在回调里重建**：这个 lambda 正在 `Button::RaiseClick` 的栈上执行，
        //   而重建会 `root.RemoveChild` 掉**当前这个按钮**——那就是拿正在执行的
        //   对象去析构它自己（与 Step 1 的 `m_sidebarDirty` 完全同族，见其头注）。
        //   ECDI Phase 35 的 `RequestRemove` 延迟析构只保护走它那条路径的调用；
        //   `RemoveChild` 是同步析构，必须由我们保证不在派发栈上调用。
        m_uiRebuildPending = true;
    });
    bar->AddChild(std::move(themeBtn));

    root.AddChild(std::move(bar));
}

// 主区（hi-fi .main）：左栏（新建会话/会话/回放/工作区）+ 中栏（转录 + Composer）+ 右栏检查器
void HarnessApp::BuildMainArea(ECDI::Widget& root) {
    auto main = std::make_unique<ECDI::Panel>();
    main->SetStretch(1);
    main->SetLayout(std::make_unique<ECDI::HorizontalLayout>(0, true, 0));

    // ── 左栏（hi-fi .side）──
    auto side = std::make_unique<ECDI::Panel>();
    m_hud.sidebar = side.get();   // Step 2：显隐入口要能摸到侧栏本体
    side->SetSize(200, 100);
    side->SetStyle(ECDI::PanelStyleOverride{
        .background = kSidebarBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    side->SetLayout(std::make_unique<ECDI::VerticalLayout>(harness::kRowGapSide, false, harness::kPadFrame));

    auto newSess = std::make_unique<ECDI::Button>("＋ 新建会话");
    newSess->SetTextColor(kAccentInk);
    newSess->SetStyle(ECDI::ButtonStyleOverride{
        .background = kUserBg,            // --accent-soft 与 user 气泡同值 #263449
        .border = harness::kDarkAccent,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn});
    newSess->SetSize(172, 32);
    newSess->SetFont(harness::SansFont(harness::kFontBody));
    newSess->SetOnClick([this] {
        // ★★ 0d：不再是「仅记录」——真的新建一个会话并切过去。
        //   live 模式拉起新 core；回放模式建空会话并如实提示（不伪造转录）。
        NewSession();
    });
    side->AddChild(std::move(newSess));

    // ★ 2026-10-18 原型 C 形态：分区标签 10px/700 letter-spacing ⇒ 落地为
    //   **11px + muted + 不加空格**（中文不进 10px：§3.3 实测会糊；`"会 话"`
    //   那种手撒空格是伪装 letter-spacing，等宽观感下反而散）。
    //   ★ 分区名 `工作区`/`项目` 直接沿用 C 的措辞（C 左侧第一段就叫「工作区」）。
    AddLine(*side, "工作区", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto path = std::make_unique<ECDI::Label>("等待 core 确认实际路径");
        m_hud.workspacePath = path.get();
        path->SetTextColor(kDarkInk);
        path->SetFont(harness::MonoFont(10.0f));
        path->SetWordWrap(true);
        path->SetSize(172, 34);
        side->AddChild(std::move(path));

        auto status = uikit::Label("未连接", 172, 14, kDarkMuted, harness::kFontMicro);
        m_hud.workspaceStatus = status.get();
        side->AddChild(std::move(status));

        auto pick = std::make_unique<ECDI::Button>("选择文件夹…");
        m_hud.workspacePick = pick.get();
        pick->SetTextColor(kDarkInk);
        pick->SetFont(harness::SansFont(harness::kFontSmall));
        pick->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .hoverBackground = kToolHeaderHover});
        pick->SetSize(172, 26);
        pick->SetOnClick([this] { ChooseWorkspaceFolder(); });
        side->AddChild(std::move(pick));

        // ★★ 2026-10-20（m09704）：目录列表收进「工作区」段（原「项目」段）。
        //   路径只出现一次，目录内容紧跟其后，语义 = 「这个工作区在哪 + 里面有什么」。
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.projectList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }

    // ★★ Step 1（2026-10-19）：会话列表替换掉此前**单行硬编码**的会话条目。
    //   改动前的形态是一行 `uikit::Row` + 两个 Label（id / core 版本），只能显示
    //   活动会话——`m_sessions` 里有 N 个会话时，界面上永远只看得见 1 个。
    //   ⇒ 现在是 `m_sessions → 控件树` 的**纯函数渲染**（`RebuildSessionList`），
    //     点一行切会话、行尾 `×` 删会话、选中态由组件自身可见性表达。
    //   ★ 为什么用 `ScrollView` 而不是直接往 `side` 里塞行：会话数无上限，
    //     而侧栏是定宽定高的列。列表自身滚动比"整个侧栏滚动"正确得多
    //     （后者的「工作区/项目」分区会跟着一起滚走）。
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.sessList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }
    {
        // 计数行：`2 个会话`。这一行是"侧栏到底画了几个"的**文字读数**——
        // 它和列表同源于 m_sessions，任意一侧错位都会立刻显形（探针也读它）。
        // ★ 直接挂在 side 下（不经 Row 包一层）：它是单标签，多一层容器只会
        //   在 VerticalLayout 里多占一个槽位、多一点间距。
        auto cnt = uikit::Label("1 个会话", kSidebarListW, 14, kDarkMuted, harness::kFontMicro);
        m_hud.sessCount = cnt.get();
        side->AddChild(std::move(cnt));
    }

    // ★★ 回放进度条（I4 评估钩子）**保留**，但改成 C 那种「细线段」形态：
    //   原型 C 没有这一块，而它是协议 v2 的评估读数 ⇒ 折中方案是**不单独占一个
    //   分区**，改成会话条目下方的一条 3px 细轨 + 右侧计数，视觉上从「一个区块」
    //   退化为「一行元信息」，既保住读数又不破坏 C 的节奏。
    {
        auto row = uikit::Row(172, 16, 8);
        auto prog = std::make_unique<ECDI::Label>("0 / 0");
        m_hud.sessProgress = prog.get();
        prog->SetTextColor(kDarkMuted);
        prog->SetFont(harness::SansFont(harness::kFontSmall));
        prog->SetSize(172, 16);
        row->AddChild(std::move(prog));
        side->AddChild(std::move(row));
        auto bar = std::make_unique<ECDI::ProgressBar>();
        m_replayBar = bar.get();
        bar->SetSize(172, 3);
        bar->SetStyle(ECDI::ProgressBarStyleOverride{
            .trackColor = kTrackBg, .fillColor = harness::kDarkAccent,
            .cornerRadius = 1.5f});
        side->AddChild(std::move(bar));
    }


    // ── ★★ Step 5：历史会话（磁盘上的夹具 = 可打开的文件）──
    //   ★ 这一段与上面「工作区」里的会话列表是**两种东西**：上面是已打开的会话，
    //     这里是**还没打开的文件**。点一行会多出一个已打开会话。
    //   ★ 只列不删：`protocol/fixtures/` 下每个文件都有 sha256 基线，
    //     删除 UI 一旦能碰它们，所有回归基线就都开口子了。
    AddLine(*side, "历史", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.historyList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }
    {
        auto cnt = uikit::Label("0 个历史文件", kSidebarListW, 14, kDarkMuted,
                                harness::kFontMicro);
        m_hud.historyCount = cnt.get();
        side->AddChild(std::move(cnt));
    }

    // ── 侧栏底部身份行（C 的 `.side-bottom`）──
    //   `margin-top:auto` 在 ECDI 里没有等价物（布局无弹性余量分配）⇒ 用
    //   一根 1px 分隔线 + 紧跟的身份行近似「贴在底部的一行」。
    {
        auto sep = std::make_unique<ECDI::Panel>();
        sep->SetSize(172, 1);
        sep->SetStyle(ECDI::PanelStyleOverride{
            .background = kLine, .borderWidth = 0.0f});
        side->AddChild(std::move(sep));
        auto row = uikit::Row(172, 24, 8, 8);
        row->AddChild(uikit::Dot(14, kPanel2));
        auto who = uikit::Label("本地工作区", 100, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(who));
        auto dots = uikit::Label("· · ·", 40, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(dots));
        side->AddChild(std::move(row));
    }

    main->AddChild(std::move(side));

    // ── 中栏（转录 + Composer）──
    auto counting = std::make_unique<CountingPanel>();
    m_counting = counting.get();
    m_counting->SetStretch(1);
    m_counting->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true, 0));
    m_counting->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? kProbeBg : kContentBg});

    // ── 中栏会话头（原型 C 的 `.conversation-head`，62px）──────────
    // ★ 为什么加：原型 C 中栏顶部有「标题 + 元信息 + 操作」这一带，而现状
    //   ScrollView 直接从第一行开始，**标题只存在于系统标题栏**（用户看不到
    //   自己在聊什么）。C 的 `.head-actions` 是 3 个 iconbtn，但 `✳ ⚙ ◉ ⋯ ⌁ ☷ ✓ ⌂ ✕ ↻ ▦ ⌕`
    //   这一整批图标在两个主题的 face 里**都没有字形**（`.scratch/symcand.py` 实测）
    //   ⇒ 落地为**文字按钮**（诚实降级：不画没有字形的图标）。
    {
        auto head = std::make_unique<ECDI::Panel>();
        head->SetSize(600, 62);
        head->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kContentBg,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kLine});
        head->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, true, harness::kPadFrame));

        // 标题（拉丁 = 夹具名，可用 Semibold 出口）+ 元信息
        auto titles = std::make_unique<ECDI::Panel>();
        titles->SetSize(300, 36);
        titles->SetLayout(std::make_unique<ECDI::VerticalLayout>(2, false, 0));
        {
            auto h1 = std::make_unique<ECDI::Label>(ConversationTitle());
            m_hud.convTitle = h1.get();
            h1->SetTextColor(m_options.probeContrast ? kProbeInk : kDarkInk);
            // ★ 加重走 `SemiFont`（`Segoe UI Semibold`）——这是**拉丁专用**出口：
            //   `Microsoft YaHei Bold` 在 GDI 回落宋体、GL 解析到 msyhbd.ttc，
            //   两链不同 face ⇒ 中文串绝不能用。标题目前是「harness 会话」混排串，
            //   **故意不用 SemiFont**（见 Theme.h 里那条硬约束的注释）。
            h1->SetFont(harness::SansFont(harness::kFontTitle));
            h1->SetSize(300, 18);
            titles->AddChild(std::move(h1));
            auto sub = std::make_unique<ECDI::Label>(ConversationSubtitle());
            m_hud.convSub = sub.get();
            sub->SetTextColor(m_options.probeContrast ? kProbeInk : kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontMicro));
            sub->SetSize(300, 14);
            titles->AddChild(std::move(sub));
        }
        head->AddChild(std::move(titles));

        auto grow = std::make_unique<ECDI::Panel>();
        grow->SetStretch(1);
        grow->SetSize(10, 10);
        head->AddChild(std::move(grow));

        // 操作钮：C 是 3 个 iconbtn（↗ ☷ ···）——`↗` 两主题都有字形，
        // `☷` 与 `⋯`(U+22EF) 都没有 ⇒ 后两个用 ASCII 文字（诚实降级，不画缺字图标）。
        // ★★ 2026-10-20：动作从「`bool toggleInspector`」改为枚举。
        //   原来一参布尔只能表达「是不是检查器」，于是 `↗`/`···` 只能落到
        //   `(void)tip; return;`（空回调）。加动作就得加枚举——否则第三个布尔参
        //   会变成 `mkAct("···", "更多", false, true)` 那种读不懂的调用。
        enum class HeadAct { Export, Inspector, Menu };
        auto mkAct = [&head, this](const std::string& text, const std::string& tip,
                                   HeadAct act) {
            // ★★ P4 E：tip 参数终于有了去处（此前 `(void)tip` 闲置——ECDI 无
            //   tooltip 控件，mkAct 一直带着它等今天）。
            auto b = std::make_unique<uikit::TipButton>(
                text, tip,
                [this](uikit::TipButton* tb) { ShowTooltipFor(tb, tb->tip()); },
                [this](uikit::TipButton*) { HideTooltip(); });
            b->SetTextColor(kDarkMuted);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            // ★ 指示符顺序必须照抄 `ButtonStyleOverride` 的成员声明顺序
            //   （`ECDI/Theme/ButtonStyle.h:44-51`：background / border / borderWidth /
            //   cornerRadius / pressedBackground / hoverBackground / textAlignment /
            //   foreground）。MSVC 对乱序 designated init 报 C7560，且 GCC/Clang 的
            //   C++20 扩展也不保证顺序 ⇒ 一律按声明序写。
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(28, 24);
            if (act == HeadAct::Inspector) m_hud.inspectorToggle = b.get();
            else if (act == HeadAct::Export) m_hud.exportToggle = b.get();
            else m_hud.moreToggle = b.get();
            b->SetOnClick([this, tip, act] {
                (void)tip;   // 提示文字目前无处显示（ECDI 无 tooltip 控件）；保留参数为将来
                switch (act) {
                case HeadAct::Export:
                    (void)ExportTranscript();
                    return;
                case HeadAct::Menu:
                    ToggleMoreMenu();
                    return;
                case HeadAct::Inspector:
                    break;
                }
                ToggleInspector();   // ★ P4 B：唯一入口（原内联逻辑抽出，Ctrl+I 共用）
            });
            head->AddChild(std::move(b));
        };
        mkAct("↗", "导出转录", HeadAct::Export);
        mkAct("≡", "检查器", HeadAct::Inspector);
        mkAct("···", "更多", HeadAct::Menu);
        counting->AddChild(std::move(head));
    }

    // ── ★★ 2026-10-20：`···` 菜单条（内联下拉）───────────
    // 位置：`head` 与 `scroll` **之间**——点 `···` 后在头部正下方撑开一行，
    //   下方转录区自然下移。为什么不做真浮层见 `Hud::moreMenu` 的注释（ECDI
    //   没有菜单控件也没有跳过布局机制，浮层要跟三条 Arrange 路径手工对齐）。
    // ★★ 【2026-10-21 实测缺陷】建树时必须**按模型值**投影显隐，不能留默认可见。
    //   第一次落地时这里只写了 `SetSize`，把「默认隐藏」当成注释里的意图；
    //   而 `Widget` 的缺省状态是**可见**（`m_state.visible` 初值）⇒ 启动第一帧
    //   菜条就撑在头下面。截图证据 `.scratch/mid-shots/before-open-mid.png`：
    //   四个菜单项（导出/归档/清空视图）在**谁也没点 `···`** 时就挂在屏上，
    //   转录区被顶下去 34px。`RebuildUi` ⑩b 里那条投影**只救重建路径**，
    //   救不了首帧（`BuildUi` 跑完就直接 `Arrange` 了）。
    //   ⇒ 唯一正确的写法是建完立刻投影一次模型值：`m_moreMenuOpen` 的初值是 false，
    //     于是首帧是关的；而「菜单开着时换主题」走的也是同一句（重建后 ⑩b 再投影）。
    //   ★ 这里打 `SetVisible` 而不是 `SetMoreMenuOpen`：后者带同值守卫
    //     （`m_moreMenuOpen == open` 就返回），首帧两条都是 false ⇒ 会直接返回、
    //     **一句 SetVisible 都不发**，缺陷原样留下。守卫防的是「重复动作」，
    //     而这里要的是「把控件状态对齐到模型」，两者不是一回事。
    {
        auto menu = std::make_unique<ECDI::Panel>();
        m_hud.moreMenu = menu.get();
        menu->SetVisible(m_moreMenuOpen);   // ★ 见上：缺这一句 = 首帧露出 34px 空洞
        menu->SetSize(600, 34);
        menu->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kPanel2,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kLine});
        menu->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 8));

        // 三件都是**真实动作**（本步的目的：不再有空回调）：
        //   导出 = 同 `↗`；归档 = 同侧栏的「↓」；清空视图 = 重建视图（不碰模型）。
        auto mkMenuItem = [&menu, this](const std::string& label, const char* tip,
                                        std::function<void()> action) {
            auto b = std::make_unique<ECDI::Button>(label);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(84, 24);
            (void)tip;
            b->SetOnClick([this, action = std::move(action)] {
                // ★ 先合菜单再做事：动作本身可能弹对话框（导出）/ 改活动会话（归档），
                //   菜单留在原处会让人以为下一步还能点。
                SetMoreMenuOpen(false);
                action();
            });
            menu->AddChild(std::move(b));
        };
        mkMenuItem("导出", "导出转录到文件", [this] { (void)ExportTranscript(); });
        mkMenuItem("设置", "提供商连接设置（BaseURL / API Key / 模型）", [this] {
            // 打开时预填非秘密字段（BaseURL/模型来自配置）；key 字段**永不回填**
            // ——凭据库里有值时 hint 说明"留空保持不变"。
            if (m_hud.settingsBase) {
                const ProviderConfig* edit =
                    m_appConfig.Find(m_editingProviderId);
                if (!edit) edit = m_appConfig.DefaultProvider();
                if (edit) {
                    m_editingProviderId = edit->id;
                    m_hud.settingsBase->SetText(edit->baseUrl);
                    m_hud.settingsModel->SetText(edit->model);
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(
                            edit->context > 0 ? std::to_string(edit->context) : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(
                            edit->maxOutput > 0 ? std::to_string(edit->maxOutput) : "");
                    }
                    RebuildSettingsProviderList();
                }
                if (m_providerKeys.empty()) {
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText("填写提供商连接信息，保存后连接");
                } else {
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText(
                        "已保存的密钥不回显——留空则保持不变");
                }
            }
            SetSettingsVisible(true);
        });
        mkMenuItem("归档", "归档当前会话", [this] {
            if (!m_sess) return;
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == m_sess) {
                    (void)ArchiveSessionAt(i);
                    return;
                }
            }
        });
        // ★ 「清空视图」= 从模型重建视图（`RebuildTranscriptView`）。
        //   ★ 不写成「清空 m_sess->timeline」：那会**真的删掉转录模型**——
        //     而 `timeline` 是历史会话/重建/导出的唯一来源，删了就找不回来。
        //     这里要的是「视图错乱了就重画一遍」，模型的作者不是这个按钮。
        mkMenuItem("清空视图", "从模型重画视图", [this] { RebuildTranscriptView(); });
        counting->AddChild(std::move(menu));
    }

    // ── ★★ P3.5：设置面板（★★ P4 F 起改模态浮层：遮罩+居中 sheet 挂 m_scroll；
    //   显隐见 ApplySettingsVisibility——转录/composer 不再腾槽）──
    //   显隐是模型侧状态 `m_settingsVisible`（重建后恢复，同 m_sidebarVisible 纪律）。
    std::unique_ptr<ECDI::Widget> settingsSheetOwned;   // 暂存——scroll 建好后转移挂载
    {
        auto panel = std::make_unique<ECDI::Panel>();
        m_hud.settingsPanel = panel.get();
        panel->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 14));
        // ★ P3.6 观感：卡片底色 + 描边，与周边（kSidebarBg）明显区隔；
        //   尺寸由 LayoutSettingsOverlay 按视口钳制（初始值仅为基准）。
        panel->SetSize(560, 620);
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kCardBg,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kCardBorder});

        auto title = std::make_unique<ECDI::Label>("连接设置");
        title->SetTextColor(kDarkInk);
        title->SetFont(harness::SansFont(16.0f));
        title->SetSize(520, 26);
        panel->AddChild(std::move(title));

        // ★★ P4 F：字段区收进 ScrollView——sheet 高被视口钳制（vh-80）时字段
        //   可滚（修最小窗 880×460 下原 620 固定高的溢出）。底部保存行/hint
        //   留在 panel（固定底栏，不随字段滚走）。
        auto sv = std::make_unique<ECDI::ScrollView>();
        m_hud.settingsSv = sv.get();   // ★ P4 F 修复：LayoutSettingsOverlay 要补 extent
        sv->SetSize(520, 400);   // 基准尺寸（F2/D9 契约）；stretch=1 吃剩余高
        sv->SetStretch(1);
        sv->SetScrollStep(28);
        ECDI::Widget* content = &sv->GetContentView();
        content->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 4));
        panel->AddChild(std::move(sv));

        auto addLabel = [&content](const std::string& text) {
            auto l = std::make_unique<ECDI::Label>(text);
            l->SetTextColor(kDarkMuted);
            l->SetFont(harness::SansFont(harness::kFontSmall));
            l->SetSize(520, 20);
            content->AddChild(std::move(l));
        };
        auto addInput = [this, &content](ECDI::TextBox** slot, bool password) {
            auto box = std::make_unique<ECDI::TextBox>();
            *slot = box.get();
            (*slot)->SetSingleLine(true);
            if (password) {
                (*slot)->SetEchoMode(ECDI::TextBox::EchoMode::Password);
            }
            // ★★ P4 修复（实测反馈）：白卡上无边框的输入框与背景融为一体、
            //   几乎不可辨——灰底 + 描边（与挡位钮同款对比），实测可辨。
            (*slot)->SetStyle(ECDI::TextBoxStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .cornerRadius = harness::kRadiusBtn,
                .borderWidth = 1.0f});
            (*slot)->SetSize(520, 30);
            content->AddChild(std::move(box));
        };
        auto mkSmallButton = [&content](const char* text, int w, int h,
                                      std::function<void()> fn) {
            auto b = std::make_unique<ECDI::Button>(text);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(w, h);
            b->SetOnClick(std::move(fn));
            content->AddChild(std::move(b));
        };
        auto addPresetRow = [this, &content, &mkSmallButton](
                                const std::string& label, ECDI::TextBox** slot,
                                std::initializer_list<const char*> presets) {
            auto cap = std::make_unique<ECDI::Label>(label);
            cap->SetTextColor(kDarkMuted);
            cap->SetFont(harness::SansFont(harness::kFontSmall));
            cap->SetSize(520, 20);
            content->AddChild(std::move(cap));
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 26);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto box = std::make_unique<ECDI::TextBox>();
            *slot = box.get();
            (*slot)->SetSingleLine(true);
            (*slot)->SetSize(120, 26);
            row->AddChild(std::move(box));
            for (const char* p : presets) {
                auto b = std::make_unique<ECDI::Button>(p);
                b->SetTextColor(kDarkInk);
                b->SetFont(harness::SansFont(harness::kFontSmall));
                b->SetStyle(ECDI::ButtonStyleOverride{
                    .background = kPanel2,
                    .border = kLine,
                    .borderWidth = 1.0f,
                    .cornerRadius = kRadiusBtn,
                    .pressedBackground = kToolHeaderPressed,
                    .hoverBackground = kToolHeaderHover});
                b->SetSize(56, 26);
                ECDI::TextBox* target = *slot;
                b->SetOnClick([this, target, text = std::string(p)] {
                    if (target) target->SetText(text);
                });
                row->AddChild(std::move(b));
            }
            content->AddChild(std::move(row));
        };

        auto listCap = std::make_unique<ECDI::Label>(
            "提供商（点击选择编辑；● = 当前连接）");
        listCap->SetTextColor(kDarkMuted);
        listCap->SetFont(harness::SansFont(harness::kFontSmall));
        listCap->SetSize(520, 20);
        panel->AddChild(std::move(listCap));
        {
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.settingsProviderList = list.get();
            list->SetSize(520, 108);
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            content->AddChild(std::move(list));
        }
        {
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 26);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto b1 = std::make_unique<ECDI::Button>("新增提供商");
            b1->SetTextColor(kDarkInk);
            b1->SetFont(harness::SansFont(harness::kFontSmall));
            b1->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b1->SetSize(110, 26);
            b1->SetOnClick([this] {
                m_editingProviderId.clear();
                if (m_hud.settingsBase) {
                    m_hud.settingsBase->SetText("");
                    m_hud.settingsModel->SetText("");
                    if (m_hud.settingsContext) m_hud.settingsContext->SetText("");
                    if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("");
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText("填写新提供商信息，保存后加入列表");
                }
            });
            row->AddChild(std::move(b1));
            auto b2 = std::make_unique<ECDI::Button>("删除所选");
            b2->SetTextColor(kDarkInk);
            b2->SetFont(harness::SansFont(harness::kFontSmall));
            b2->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b2->SetSize(110, 26);
            b2->SetOnClick([this] {
                const ProviderConfig* cur = m_appConfig.Find(m_editingProviderId);
                if (!cur) {
                    m_hud.settingsHint->SetText("先在列表中选择要删除的提供商");
                    return;
                }
                // ★★ P4 修复（实测反馈：点删除未响应后崩溃）：
                //   ① 原按 id 过滤=「同名全家一次删光」——id 由 BaseURL host 派生，
                //     同端点多份配置 id 相同（实测用户配置里有 8 份同 id）⇒ 全删后
                //     providers[0] 在**空向量上越界**（UB ⇒ 未响应/崩溃）。改「删
                //     所选的**一个实例**」（第一个匹配位）——同 id 多份时逐次删。
                //   ② 删后为空的守卫用**删除结果**判断（原 size()<=1 判的是删前，
                //     8→0 的场景它拦不住）。
                bool removed = false;
                for (auto it = m_appConfig.providers.begin();
                     it != m_appConfig.providers.end(); ++it) {
                    if (it->id == m_editingProviderId) {
                        m_appConfig.providers.erase(it);
                        removed = true;
                        break;
                    }
                }
                if (!removed || m_appConfig.providers.empty()) {
                    m_hud.settingsHint->SetText("至少保留一个提供商");
                    std::string reloadError;
                    m_appConfig.Load(reloadError);   // 放弃本次内存改动，回配置文件态
                    return;
                }
                if (m_appConfig.defaultProviderId == m_editingProviderId) {
                    m_appConfig.defaultProviderId = m_appConfig.providers.front().id;
                }
                std::string saveError;
                if (!m_appConfig.Save(saveError)) {
                    m_hud.settingsHint->SetText("配置写入失败：" + saveError);
                    return;
                }
                m_editingProviderId = m_appConfig.defaultProviderId;
                RebuildSettingsProviderList();
                m_modelListDirty = true;
                m_hud.settingsHint->SetText("已删除");
            });
            row->AddChild(std::move(b2));
            panel->AddChild(std::move(row));
        }

        // ★ 无 placeholder：TextBox 没有 Placeholder API（ENTER-SEND 起登记的
        //   已知缺口），字段含义由上方 Label 承担——不去发明第二套提示机制。
        addLabel("Base URL（OpenAI 兼容端点根，如 https://api.deepseek.com/v1）");
        addInput(&m_hud.settingsBase, false);
        addLabel("模型名（如 deepseek-flash / glm-5.2 / u2-flash）");
        addInput(&m_hud.settingsModel, false);
        addPresetRow("上下文容量挡位（token）", &m_hud.settingsContext,
                     {"200000", "500000", "1000000"});
        addPresetRow("输出上限挡位（max_tokens）", &m_hud.settingsMaxOut,
                     {"4096", "8192", "16384", "32768", "65536"});
        addLabel("API Key（存入 Windows 凭据管理器；不进配置文件、不经协议）");
        addInput(&m_hud.settingsKey, true);

        {
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 30);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto save = std::make_unique<ECDI::Button>("保存并连接");
            m_hud.settingsSave = save.get();
            save->SetTextColor(kDarkInk);
            save->SetFont(harness::SansFont(14.0f));
            save->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            save->SetSize(140, 30);
            save->SetOnClick([this] { OnSettingsSave(); });
            row->AddChild(std::move(save));

            auto closeBtn = std::make_unique<ECDI::Button>("关闭");
            closeBtn->SetTextColor(kDarkInk);
            closeBtn->SetFont(harness::SansFont(14.0f));
            closeBtn->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            closeBtn->SetSize(140, 30);
            closeBtn->SetOnClick([this] {
                SetSettingsVisible(false);
                bool anyLive = false;
                for (const auto& sp : m_sessions) {
                    if (sp->originKind == "live") { anyLive = true; break; }
                }
                if (!anyLive && m_sess) {
                    AddNoticeRow("[未连接提供商：用 ··· 菜单的「设置」填写连接信息]",
                                 kDarkMuted);
                    Relayout();
                    RequestRepaint();
                }
            });
            row->AddChild(std::move(closeBtn));
            panel->AddChild(std::move(row));
        }

        auto hint = std::make_unique<ECDI::Label>("");
        m_hud.settingsHint = hint.get();
        hint->SetTextColor(kDarkMuted);
        hint->SetFont(harness::SansFont(harness::kFontSmall));
        hint->SetSize(520, 18);
        panel->AddChild(std::move(hint));

        // ★★ P4 F：不进 counting（那会被 VerticalLayout 接管成槽位兄弟）——
        //   暂存，scroll 建好后与遮罩一起转移为视口浮层。
        settingsSheetOwned = std::move(panel);
    }

    auto scroll = std::make_unique<ECDI::ScrollView>();
    m_scroll = scroll.get();
    m_scroll->SetStretch(1);
    m_scroll->SetScrollStep(40);
    m_content = &m_scroll->GetContentView();
    if (m_options.rowModeList) {
        // listlayout 模式：固定行高、不测量（规格 §5.3 的 B 组）
        m_content->SetLayout(std::make_unique<ECDI::ListLayout>(kListRowHeight, 2));
    }
    // ★★ P4 Phase A（2026-10-25）：「↓ 跳到最新」按钮——浮在视口右下角。
    //   ★ 挂法依据 ScrollView.h 的树结构注释：「装饰与交互子节点必须待在偏移层
    //     之外」——滚动条就是这么挂的（直接子节点、固定视口、不随内容滚）。
    //     ScrollView 不接管 Arrange、ApplyLayout 只定位条与内容节点 ⇒ 没有任何
    //     布局会动它；HitTest 逆序遍历子节点 ⇒ 后加的它在最上层、优先命中。
    //   ★ 几何/显隐的唯一刷新口 = RefreshJumpButton（贴底态 = 隐藏）。
    auto jump = std::make_unique<ECDI::Button>("↓ 跳到最新");
    m_hud.jumpLatest = jump.get();
    jump->SetVisible(false);   // 初始不可见；加载/重排后由 RefreshJumpButton 按贴底态刷新
    jump->SetTextColor(kDarkInk);
    jump->SetFont(harness::SansFont(harness::kFontSmall));
    jump->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = harness::kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    jump->SetSize(kJumpBtnW, kJumpBtnH);
    jump->SetOnClick([this] {
        // 点击 = 显式回底：offset 到底即自然恢复 pinned（跟随语义不变，本按钮只补
        // "看得见、点得回"）。RefreshJumpButton 会随即把它藏起来。
        if (!m_scroll) return;
        m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY());
        RefreshJumpButton();
    });
    scroll->AddChild(std::move(jump));   // ScrollView 的最后子节点 = 最上层
    // ★★ P4 F（2026-10-25）：设置 = 模态浮层（半透明遮罩 + 居中 sheet），与
    //   jump 按钮同款浮层口（ScrollView 直接子节点：无布局接管、固定视口、
    //   HitTest 逆序后加者在上）。子序 [.., jump, mask, sheet] ⇒ 设置打开时
    //   sheet 最先命中、mask 次之（jump 被盖住点不到——模态语义），转录在
    //   半透明遮罩后继续渲染（用户验收过原型这条）。
    //   ★★ 修复（实测反馈「变色应该是整个变色」）：mask+sheet 此后由 BuildUi
    //   尾部转移到 **root 末尾**（盖全窗，含顶栏/侧栏/检查器）——此处先挂
    //   scroll 只是建树期的临时归属。
    auto mask = std::make_unique<ECDI::Button>();
    m_hud.settingsMask = mask.get();
    mask->SetVisible(false);
    mask->SetOnClick([] {});   // 点击被 Button 消费但不做事——防误触，关闭走 Esc/Ctrl+,
    mask->SetStyle(ECDI::ButtonStyleOverride{
        // ★ ECDI #71/REQ-11 首个半透明消费者（评审定案）：DrawRect 路径经
        //   BlendAlphaSolid 消费 alpha；双后端（--backend gdi|gl）实证渲染，
        //   任一后端不出半透明 ⇒ 退不透明深色（不阻塞本 Phase，现象登记 #71）。
        .background = ECDI::Color::FromRGBA8(15, 17, 23, 150),
        .cornerRadius = 0.0f,
        .pressedBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150),
        .hoverBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150)});
    scroll->AddChild(std::move(mask));
    if (settingsSheetOwned) {
        m_hud.settingsPanel->SetVisible(false);   // 显隐统一走 ApplySettingsVisibility
        scroll->AddChild(std::move(settingsSheetOwned));
    }
    // ★★ P4 Phase H（2026-10-25）：模型下拉浮窗——顶部下拉（视口坐标系内
    //   右上角），z 序在 mask/sheet 之上、tooltip 之下。搜索框 + 行列表 + 底注。
    {
        auto pop = std::make_unique<ECDI::Panel>();
        m_hud.modelPopup = pop.get();
        pop->SetVisible(false);
        pop->SetStyle(ECDI::PanelStyleOverride{
            .background = kCardBg,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 1.0f,
            .borderColor = kCardBorder});
        auto search = std::make_unique<ECDI::TextBox>();
        m_hud.modelSearch = search.get();
        search->SetSingleLine(true);
        search->SetSize(320, 26);
        search->SetPosition(10, 8);
        search->SetOnTextChanged([this](const std::string&) { RebuildModelPopupList(); });
        pop->AddChild(std::move(search));
        auto list = std::make_unique<ECDI::Panel>();
        m_hud.modelPopupList = list.get();
        list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
        list->SetPosition(10, 40);
        pop->AddChild(std::move(list));
        auto foot = std::make_unique<ECDI::Label>(
            "仅 idle 发出 · 运行中自动排队（本轮输出完生效）");
        m_hud.modelPopupFoot = foot.get();
        foot->SetTextColor(kDarkMuted);
        foot->SetFont(harness::SansFont(harness::kFontMicro));
        foot->SetSize(320, 16);
        foot->SetPosition(10, 250);
        pop->AddChild(std::move(foot));
        pop->SetSize(340, 272);
        scroll->AddChild(std::move(pop));
    }
    // ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层——最后子 = 最上层（盖过
    //   jump/遮罩/sheet/模型浮窗）。隐藏态常驻，TipButton enter 起 400ms 定时器后显示。
    {
        auto tip = std::make_unique<ECDI::Panel>();
        m_hud.tooltip = tip.get();
        tip->SetVisible(false);
        tip->SetStyle(ECDI::PanelStyleOverride{
            .background = kDarkInk,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 0.0f});
        auto txt = std::make_unique<ECDI::Label>();
        m_hud.tooltipText = txt.get();
        txt->SetTextColor(kTooltipInk);
        txt->SetFont(harness::SansFont(harness::kFontSmall));
        txt->SetSize(400, 16);
        txt->SetPosition(8, 5);
        tip->AddChild(std::move(txt));
        scroll->AddChild(std::move(tip));
    }
    m_counting->AddChild(std::move(scroll));

    // Composer 收进中栏（hi-fi .composer 在 .mid 内）
    auto composer = std::make_unique<PromptBar>();
    m_promptBar = composer.get();
    composer->SetSize(600, 92);
    composer->SetOnSend([this](const std::string& text) {
        SendPrompt(text);
        if (m_promptBar && m_promptBar->input()) {
            m_promptBar->input()->SetText("");   // 已发出/记录，输入框清空（聊天惯例）
            // ★★ 2026-10-21：清空是 `SetText` 路径 ⇒ 自动长高不会自己知道。
            //   不同步的症状很难看：发出一条 3 行的消息后，框里已经空了，
            //   但它仍占 3 行高、提示行与按钮被推到下面 —— 看起来像"卡住了"。
            m_promptBar->SyncInputHeight();
        }
    });
    composer->SetOnStop([this](const std::string&) {
        if (m_sess->writer) {
            const bool ok = m_sess->writer->Cancel();
            AddNoticeRow(ok ? "[cancel → core]" : "[cancel 发送失败]",
                         ok ? kDarkMuted : kErrorDark);
        } else {
            AddNoticeRow("[cancel 仅记录]", kDarkMuted);
        }
        Relayout();
        RequestRepaint();
    });
    counting->AddChild(std::move(composer));

    // ★ P3.5：设置面板的显隐投影（中栏三件都建完了，唯一投影路径此刻可用）。
    ApplySettingsVisibility();
    // ★★ P4 H：模型浮窗显隐投影（重建后按模型位恢复——同 ApplySettingsVisibility
    //   的「投影只存在于一条路径」纪律）+ chip 首帧数据。
    SetModelPopupOpen(m_modelPopupOpen);
    UpdateModelChip();
    // ★ P3.6：模型列表首帧/重建后置脏（tick 里统一重建——列表行是动态的）。
    m_modelListDirty = true;
    main->AddChild(std::move(counting));

    // ── 右栏检查器（hi-fi .insp：卡片化）──
    auto insp = std::make_unique<ECDI::Panel>();
    m_hud.inspector = insp.get();
    insp->SetVisible(m_inspectorVisible);
    insp->SetSize(230, 100);
    insp->SetStyle(ECDI::PanelStyleOverride{
        .background = kInspBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    insp->SetLayout(std::make_unique<ECDI::VerticalLayout>(14, false, harness::kPadFrame));

    auto makeCard = [](int h) {
        auto card = std::make_unique<ECDI::Panel>();
        card->SetSize(200, h);
        card->SetStyle(ECDI::PanelStyleOverride{
            .background = kContentBg, .cornerRadius = kRadiusCard2,
            .borderWidth = 1.0f, .borderColor = kCardBorder});
        return card;
    };
    auto cardTitle = [](ECDI::Panel& card, const char* title) {
        card.AddChild(uikit::Label(title, 178, 14, kDarkMuted, harness::kFontMicro));
    };
    auto kvRow = [](ECDI::Panel& parent, const std::string& k,
                    ECDI::Label** vOut, int kw = 56) {
        auto row = uikit::Row(178, 16, 10);
        row->AddChild(uikit::Label(k, kw, 16, kDarkMuted));
        auto vl = uikit::Label("—", 110, 16, kDarkInk, 10.5f, /*mono=*/true);
        *vOut = vl.get();
        row->AddChild(std::move(vl));
        parent.AddChild(std::move(row));
    };

    // 卡片白底和内置标题对齐原型 C 的信息层级；统计项仍保留 harness 专用读数。
    {
        auto card = makeCard(108);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "渲染探针");
        kvRow(*card, "重绘", &m_hud.evalPaints);
        kvRow(*card, "命令/帧", &m_hud.probeCmds);
        kvRow(*card, "执行", &m_hud.probeExec);
        insp->AddChild(std::move(card));
    }

    {
        // 标题收进白卡，与原型 C 的卡片层级一致。上下文环仍保留为评估读数。
        //   算式：48（环行）+ 8（间距）+ 16（输入）+ 8 + 16（输出）+ 2×10（卡片内边距）= 124。
        auto card = makeCard(140);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 10));
        cardTitle(*card, "模型");
        {
            // ★ P3.6：提供商切换列表（行由 RebuildModelList 重建；
            //   点击 = RequestModelSwitch，running 时排队——I11 语义）。
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.modelList = list.get();
            list->SetSize(190, 110);
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            card->AddChild(std::move(list));
        }
        insp->AddChild(std::move(card));
    }

    {
        // 标题收进白卡，与原型 C 的卡片层级一致。上下文环仍保留为评估读数。
        //   算式：48（环行）+ 8（间距）+ 16（输入）+ 8 + 16（输出）+ 2×10（卡片内边距）= 124。
        auto card = makeCard(140);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 10));
        cardTitle(*card, "用量");
        // ★★ 协议 v2（2026-10-14）上下文占用百分比——**用户要求**。
        //   ★ 为什么必须有 `hello.context`：百分比**需要分母**，而 v1 协议里
        //     **没有任何容量字段** ⇒ 此前两根条只能拿 `max(input, output)` 当分母，
        //     那是**拿输入跟输出比**（两个不同量纲的东西相除）⇒ 读数无意义。
        //     现分母 = `hello.context`，缺字段 ⇒ 显示 `—`（**不猜**）。
        //   ★★ 2026-10-16：**环形兑现**。此前这里是「百分比条」，理由是 ECDI 后端
        //     只有 6 个绘制原语、**没有圆弧**（当时实测为真，并据此登记账本 `REQ-08`）。
        //     ECDI **Phase 33** 落地了框架首个曲线原语 `DrawArc`（纯虚，见
        //     `RenderingBackend.h:92-112` 的完整契约）与其首个消费者 `RingProgressBar`
        //     ⇒ 本条**挂账前提（"无消费者不动"）改变**，本项目作为首个真实消费者兑现它。
        //   ★ 轨道色**不用 `kTrackBg`**：它与卡片底色 `kPanel2` **同值**（`Theme.h:82`），
        //     5px 细条上无所谓（只看得见已完成段），但**环会因此看不见整圆**、
        //     只剩一段悬空的弧 ⇒ 无法判断"还剩多少"。故轨道改用 `kLine`（亮一档）。
        //   ★ 环内**不叠百分比文字**（用户裁决）：`RingProgressBar` 是纯视觉控件
        //     （`RingProgressBarStyle.h:10` 明写无 TextStyle），叠字要靠应用侧手算居中，
        //     而 ECDI 布局无居中能力 ⇒ 数值仍由右侧 `ctxPct` 标签承担。
        auto urow = [&card](const std::string& k, ECDI::Label** vOut,
                            ECDI::ProgressBar** barOut, const ECDI::Color& fill) {
            auto row = uikit::Row(178, 16, 8);
            row->AddChild(uikit::Label(k, 40, 16, kDarkMuted));
            auto bar = std::make_unique<ECDI::ProgressBar>();
            *barOut = bar.get();
            bar->SetSize(70, 5);
            bar->SetStyle(ECDI::ProgressBarStyleOverride{
                .trackColor = kTrackBg, .fillColor = fill,
                .cornerRadius = 2.0f});
            row->AddChild(std::move(bar));
            auto vl = uikit::Label("—", 52, 16, kDarkInk, 11.0f, /*mono=*/true);
            *vOut = vl.get();
            row->AddChild(std::move(vl));
            card->AddChild(std::move(row));
        };
        // 上下文行置顶：它是**决策相关**读数（"还能聊多久"），输入/输出只是明细。
        // ★ 本行**不走 `urow`**：环要 44px 见方，而 `HorizontalLayout` 的
        //   `fillCrossAxis=false` 是**顶对齐**（`HorizontalLayout.h:15`；`UiKit.h:74`
        //   明写「ECDI 布局无居中」）⇒ 16px 的文字会贴到环顶。故左右各套一个
        //   **48px cell** 手动垂直居中（沿用 `UiKit.h:76-89` `DotCell` 的既有手法）。
        {
            auto row = uikit::Row(178, 48, 8);
            auto cell = [](std::unique_ptr<ECDI::Widget> inner) {
                auto c = std::make_unique<ECDI::Panel>();   // 无 layout ⇒ 保留子控件 SetPosition
                c->SetSize(inner->GetWidth(), 48);
                inner->SetPosition(0, (48 - inner->GetHeight()) / 2);
                c->AddChild(std::move(inner));
                return c;
            };
            row->AddChild(cell(uikit::Label("上下文", 40, 16, kDarkMuted)));

            auto ring = std::make_unique<ECDI::RingProgressBar>();
            m_ctxRing = ring.get();
            ring->SetSize(44, 44);            // min(w,h) 作直径（RingProgressBar.h:16）
            ring->SetStyle(ECDI::RingProgressBarStyleOverride{
                .trackColor = kRingTrack,     // ★ 独立令牌：kLine 浅色下压白卡只有 1.09:1，整圈会消失
                .fillColor = harness::kOkDark,
                .thickness = 5.0f});
            row->AddChild(cell(std::move(ring)));

            auto vl = uikit::Label("—", 52, 16, kDarkInk, 11.0f, /*mono=*/true);
            m_hud.ctxPct = vl.get();
            row->AddChild(cell(std::move(vl)));
            card->AddChild(std::move(row));
        }
        urow("输入", &m_hud.usageInV, &m_usageBarIn, harness::kDarkAccent);
        urow("输出", &m_hud.usageOutV, &m_usageBarOut, harness::kDarkAccent);
        insp->AddChild(std::move(card));
    }

    {
        auto card = makeCard(100);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "协议健康");
        // ★ 第三行（2026-10-13）：未知 kind 单独一行。
        //   为何不并进 healthViol：实测把「未知kind N」追加到同一 label 后**被裁掉**
        //   （该 label 宽 150，`违规 N · 未知 N · 未知kind N` 放不下，截图可见「未知k」）。
        //   单独一行既避免裁切，也让「协议未规定的事」有一处固定的可见落点。
        auto hrow = [&card](ECDI::Label** vOut, const char* initial = "—") {
            auto row = uikit::Row(178, 16, 8);
            row->AddChild(uikit::Dot(8, kOkDark));
            auto vl = uikit::Label(initial, 150, 16, kDarkInk);
            *vOut = vl.get();
            row->AddChild(std::move(vl));
            card->AddChild(std::move(row));
        };
        hrow(&m_hud.healthSeq);
        hrow(&m_hud.healthViol);
        hrow(&m_hud.healthKind, "未知 kind · 0");
        insp->AddChild(std::move(card));
    }

    {
        auto card = makeCard(92);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "权限历史");
        for (size_t i = 0; i < 3; ++i) {
            auto row = uikit::Row(178, 14, 8);
            auto chip = uikit::Label("", 42, 14, kDarkMuted, 10.5f, /*mono=*/true);
            m_hud.histChip[i] = chip.get();
            row->AddChild(std::move(chip));
            auto what = uikit::Label("", 122, 14, kDarkMuted, 11.0f);
            m_hud.histWhat[i] = what.get();
            row->AddChild(std::move(what));
            card->AddChild(std::move(row));
        }
        insp->AddChild(std::move(card));
    }

    main->AddChild(std::move(insp));

    root.AddChild(std::move(main));
}

// 状态栏（hi-fi .statusbar）：圆点+状态 | 消息数 | tokens | 重绘（Composer 已收进中栏）
void HarnessApp::BuildBottom(ECDI::Widget& root) {
    auto status = std::make_unique<ECDI::Panel>();
    status->SetSize(1000, 26);
    status->SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kDarkTopBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    status->SetLayout(std::make_unique<ECDI::HorizontalLayout>(14, true, 6));

    status->AddChild(uikit::DotCell(7, kOkDark, 14, &m_hud.sbDot));

    auto stState = std::make_unique<ECDI::Label>("idle");
    m_hud.stState = stState.get();
    stState->SetTextColor(kDarkMuted);
    stState->SetStretch(1);
    stState->SetSize(200, 16);
    status->AddChild(std::move(stState));

    auto monoLine = [](const std::string& text, int width) {
        auto lbl = std::make_unique<ECDI::Label>(text);
        lbl->SetTextColor(kDarkMuted);
        ECDI::Font f = harness::MonoFont(11.0f);
        lbl->SetFont(f);
        lbl->SetSize(width, 16);
        return lbl;
    };
    m_hud.stMsgs = AddLine(*status, "0 / 0 msg", kDarkMuted, false, 120, harness::kFontMicro);
    m_hud.stTokens = AddLine(*status, "tokens —", kDarkMuted, false, 150, harness::kFontMicro);
    m_hud.stPaint = AddLine(*status, "paints —", kDarkMuted, false, 210, harness::kFontMicro);
    root.AddChild(std::move(status));
}

// ── ★★ REQ-03（2026-10-24）：把「UI 线程轮询」换成「读线程投递唤醒」────────
//
// 为什么要改：`NextWithin(line, 8)` 在**没有数据**时会把 UI 线程挂在条件变量上
// 整整 8ms，而 tick 周期是 33ms ⇒ 约 24% 的 UI 线程时间花在**等数据**上；
// 同一段阻塞期间 WM_PAINT 出不来 ⇒ 帧延迟最多 +8ms（打字/滚动的手感就丢在这）。
// 送入延迟则是 `0..33ms`(等 tick) + `0..8ms`(等队列)。
//
// ★ 关键的决定：投递的是**信号**，不是数据。行仍留在 `ChildSource` 的队列里，
//   由 UI 线程自己取。理由不是"实现简单"，而是**所有权**：
//   `Session` 是可被删除的（侧栏 × / 归档），把行搬进一个跨线程回调，
//   就得让那个回调知道目标会话是否还活着；而队列跟着 `Session` 一起死，
//   天然没有这个问题。⇒ 闭包只捕获 `this`（HarnessApp），执行时**枚举**会话。

void HarnessApp::MarkLiveEof() {
    m_sess->liveEof = true;   // core 退出（stdin EOF 或 bye 收尾）
    FailPendingOperations("core EOF / 断连");
    m_hudDirty = true;
    RefreshHud();
}

void HarnessApp::InstallWakeHandler(Session& session) {
    if (!session.child || !m_application) return;
    // ★ 值捕获 `app`：读线程不该去读 `m_application` 这个由 UI 线程写的裸指针
    //   （没有同步、语义上就是 data race）。这次读发生在**本函数**（UI 线程）、
    //   只读一次，之后这个指针由 lambda 自己持有。
    ECDI::Application* const app = m_application;
    session.child->SetWakeHandler([this, app]() -> bool {
        // ★ 本 lambda 跑在**读线程**上：除了 `PostToUi`，这里不许碰任何 UI 状态。
        try {
            return app->PostToUi([this] { DrainActiveLive(true); });
        } catch (...) {
            // `Application.h:83` 明写：入队分配失败会**抛** `bad_alloc`（框架不吞）。
            // 这里吞掉并返回 false ⇒ `ChildSource` 复位在途标志、下一行再试，
            // 期间 tick 兜底取数 ⇒ 最坏是 33ms 延迟，而不是把读线程炸掉。
            return false;
        }
    });
    if (m_options.asyncProbe) session.child->EnableLineStamps(true);
}

void HarnessApp::InstallWakeHandlers() {
    if (m_options.legacyPoll) return;   // 对照世界：不装唤醒，逐字保留 8ms 拉取
    for (auto& session : m_sessions) InstallWakeHandler(*session);
}

void HarnessApp::DrainActiveLive(bool fromWake) {
    if (!m_sess || !m_sess->child) return;

    // ★ 预算 = **行数与时间谁先到**。行数防「2000 块一次抽干」（那会冻住一整帧），
    //   时间防慢机（同样的行数在低端机上可能要几十 ms，帧预算就没了）。
    //   ★ 没取完的部分由 `RearmWake()` **立刻再投一次**——不能指望"下一行到达"
    //     顺带唤醒：数据可能早就全在队列里了，不会有下一行。
    static constexpr size_t kMaxLinesPerCallback = 256;
    static constexpr uint64_t kBudgetUs = 4000;

    const auto drainStart = std::chrono::steady_clock::now();
    size_t handled = 0;
    std::string line;
    while (handled < kMaxLinesPerCallback) {
        if (handled > 0 &&
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - drainStart)
                                      .count()) >= kBudgetUs) {
            break;
        }
        std::chrono::steady_clock::time_point stamp;
        if (!m_sess->child->NextWithinStamped(line, 0, stamp)) break;
        if (m_options.asyncProbe && stamp.time_since_epoch().count() != 0) {
            // 入队 → 现在。0 计数的 stamp = 该行没有戳（探针没开 / 戳之前入队），
            // **跳过而不是补 now()**——补 now() 会伪造一个"零延迟"读数。
            m_apLatencyUs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - stamp)
                    .count()));
        }
        ++m_sess->liveLines;
        HandleLine(line);
        ++handled;
    }

    if (m_options.asyncProbe) {
        if (fromWake) {
            m_apLinesWake += handled;
            ++m_apCallbacks;
            if (handled > m_apMaxLinesPerCallback) m_apMaxLinesPerCallback = handled;
        } else {
            m_apLinesTick += handled;
        }
    }

    // ★ 复位**所有** live 源的唤醒标志，不只是活动会话的那个：
    //   被切走的会话（冻结中）不 drain，但它的读线程可能已经 arm 过了；
    //   不复位 ⇒ 它切回活动时标志恒 true ⇒ 永不投递 ⇒ 只能靠 33ms 兜底，
    //   而这个后果**没有任何读数会报**（探针只看活动会话）。
    for (auto& session : m_sessions) {
        if (session->child) session->child->RearmWake();
    }

    if (handled > 0) {
        Relayout();
        FollowTailIfPinned();
        CacheEvalMetrics();
        RefreshHud();
        RequestRepaint();
    } else if (m_sess->child->Eof() && !m_sess->liveEof) {
        MarkLiveEof();
    }
}

// ── 回放拉取（~30Hz 单定时器驱动，规格 §6.1）───────────────────

void HarnessApp::OnReplayTick() {
    // ★★ Step 1：侧栏列表的**唯一重建点**。
    //   为什么必须在 `if (!m_sess->source) return;` **之前**：回放模式新建的空会话
    //   `source == nullptr`，本函数会在下一行直接 return —— 把重建放在后面就意味着
    //   "新建会话后侧栏永远不刷新"（列表说 1 个、模型已经 2 个）。
    //   为什么放在 tick 里而不是回调里：见 `m_sidebarDirty` 的声明注释（派发栈 UAF）。
    //   ★ timer 回调不在事件派发栈上 ⇒ 此刻 `RemoveChild` 拆树是安全的。
    if (m_sidebarDirty) {
        m_sidebarDirty = false;
        RebuildSessionList();
        // ★★ 2026-10-20：「项目」段与「会话列表」同一条链：活动会话变了
        //   ⇒ 工作区可能变了 ⇒ 目录内容要跟着刷。
        RebuildProjectList();
        RequestRepaint();
    }
    // ★★ Step 4：整树重建的**唯一执行点**（同族的派发栈 UAF 理由见
    //   `m_uiRebuildPending` 的声明注释）。放在 `if (!m_sess->source) return;`
    //   **之前**：空会话（回放模式新建的）也要能换主题。
    //   ★ 顺序上先于侧栏重建之后无所谓——两者不会同时置脏（同一 tick 内至多一个来源），
    //     但若真同时发生，整树重建会把旧列表一起丢掉，故它必须在后。
    if (m_uiRebuildPending) {
        m_uiRebuildPending = false;
        RebuildUi();
        RequestRepaint();
    }
    // ★★ Step 5：历史区在 tick 里重建（行数是扫盘来的，回调里不能扫也不能拆树）。
    if (m_historyDirty) {
        m_historyDirty = false;
        RebuildHistoryList();
        RequestRepaint();
    }
    // ★ P3.6：检查器模型列表重建（同族派发栈纪律；守卫之前 ⇒ 空会话也能刷新）。
    if (m_modelListDirty) {
        m_modelListDirty = false;
        RebuildModelList();
        RebuildSettingsProviderList();
    }
    // ★★ Step 5：打开历史会话（延迟到此：派发栈已退，拆树/抽干都安全）。
    if (!m_historyOpenPending.empty()) {
        const std::filesystem::path file = m_historyOpenPending;
        m_historyOpenPending.clear();
        OpenHistorySession(file);   // 内部会置 m_sidebarDirty + RequestRepaint
    }
    if (!m_sess->source) {
        if (m_options.workspaceProbe && !m_workspaceProbeDone) RunWorkspaceProbe();
        return;   // 源启动失败（Run 已报错退出路径）/ 0d 空会话：无可拉取
    }
    if (m_options.workspaceProbe && !m_sess->source->IsLive()) {
        RunWorkspaceProbe();  // Probe requires a live core; report unavailable instead of hanging on replay.
        return;
    }
    if (m_sess->source->IsLive()) {
        // ── ★★ REQ-03（2026-10-24）：两条通路，只有一条是本次工作的对象 ──
        //   · `--legacy-poll` = **拉**：逐字保留改动前的形状（每 tick 首行等 8ms）。
        //     它是对照世界，也是回滚开关。没有它，「推更快」这句话就没有对照物。
        //   · 默认 = **推**：读线程已经用 `Application::PostToUi` 投递了唤醒，
        //     数据由 `DrainActiveLive(true)` 在消息循环里取；tick 这边只做
        //     **非阻塞**兜底——投递可能失败（关闭中 / 唤醒失败 / `bad_alloc`），
        //     兜底保证最坏情况退化成「33ms 内取到」，而不是永远不取。
        if (m_options.asyncProbe) ++m_apTicks;
        if (m_options.legacyPoll) {
            bool gotAny = false;
            const int budgetMs = 8;
            std::string line;
            for (;;) {
                const auto waitStart = std::chrono::steady_clock::now();
                std::chrono::steady_clock::time_point stamp;
                const bool got = m_sess->child->NextWithinStamped(
                    line, gotAny ? 0 : budgetMs, stamp);
                if (got && m_options.asyncProbe && stamp.time_since_epoch().count() != 0) {
                    // ★ 对照世界也用**同一套仪器**：只改唤醒机制这一个自变量，
                    //   不能连"怎么量"都换了——那会让两组读数不可比。
                    m_apLatencyUs.push_back(static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - stamp)
                            .count()));
                }
                // ★ 只把**带超时**的那次等待计入「阻塞」：`gotAny` 之后的 0ms 调用
                //   根本不等，把它算进来会把代价记成 0（自欺）。
                if (!gotAny && m_options.asyncProbe) {
                    const uint64_t us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - waitStart)
                            .count());
                    ++m_apBlockedTicks;
                    m_apBlockedUs += us;
                    if (us > m_apMaxBlockedUs) m_apMaxBlockedUs = us;
                }
                if (!got) break;
                ++m_sess->liveLines;
                if (m_options.asyncProbe) ++m_apLinesTick;
                HandleLine(line);
                gotAny = true;
            }
            if (gotAny) {
                Relayout();
                FollowTailIfPinned();
                CacheEvalMetrics();
                RefreshHud();
                RequestRepaint();
            } else if (m_sess->child->Eof() && !m_sess->liveEof) {
                MarkLiveEof();   // core 退出（stdin EOF 或 bye 收尾）
            }
        } else {
            DrainActiveLive(false);   // 非阻塞兜底（含 EOF 检测）
        }
        if (m_options.workspaceProbe && !m_workspaceProbeDone) RunWorkspaceProbe();
        // ★★ P3.5 设置探针跨 tick 的另一半：save 之后活动会话已是 live ⇒
        //   tick 走**本分支**（replay settle 分支走不到）——stage 0 在 save 那一 tick
        //   已产出 first-run + save 两相，stage 1+ 只能在这里推进。
        //   ★ 教训同 reasoning-probe 的"第一版只放末尾"：探针的跨 tick 推进点
        //     必须铺满**所有**能让它所在的会话形态，缺一个就是永远停在那一拍。
        if (m_options.settingsProbe && !m_settingsProbeDone) RunSettingsProbe();
        return;
    }

    if (m_sess->replay->Empty() && !m_sess->waitForAnswer) {
        if (m_sess->replaySettled) {
            if (m_relayoutPulse > 0) {   // 折叠/展开动画、settle 后新增行：几何持续跟随
                --m_relayoutPulse;
                Relayout();
                FollowTailIfPinned();
                RequestRepaint();
            }
            if (m_hudDirty) {   // 已拉完：只在有新读数时刷一次 HUD，不再制造重绘
                CacheEvalMetrics();
                RefreshHud();
            }
            // ★★ 思维链折叠探针必须**在这里**推进：夹具拉完后本函数会从上面 `return`，
            //   走不到末尾那段。第一版把调用只放在末尾 ⇒ 状态机永远停在 step 0
            //   （只有 settle 那一 tick 采到 default，之后再不前进）。
            if (m_options.reasoningProbe && !m_reasoningProbeDone) {
                RunReasoningProbe();
            }
            // ★★ Step 0c 重建探针同理：夹具拉完后本函数从这里 `return`，走不到末尾。
            //   第一版只放末尾 ⇒ 摘要里连 `rebuildProbe` 键都没有（探针一步没跑）。
            if (m_options.rebuildProbe && !m_rebuildProbeDone) {
                RunRebuildProbe();
            }
            // ★★ Step 0d 会话探针：同样放在 settle 分支（回放模式下夹具拉完才跑）。
            if (m_options.sessionProbe && !m_sessionProbeDone) {
                RunSessionProbe();
            }
            // ★★ Step 1 侧栏探针：同上（同步跑完即 Exit，故必须排在其他探针之后，
            //   否则它先 Exit 会让别人一步都跑不了。顺序：reasoning → rebuild → session → sidebar）。
            if (m_options.sidebarProbe && !m_sidebarProbeDone) {
                RunSidebarProbe();
            }
            // ★★ Step 3 答案探针：排在最后（同样同步跑完即 Exit）。
            //   顺序：reasoning → rebuild → session → sidebar → answer。
            if (m_options.answerProbe && !m_answerProbeDone) {
                RunAnswerProbe();
            }
            // ★★ 2026-10-21 菜单/导出探针：同步跑完即 Exit，同上。
            //   ★ 必须排在 `RunSidebarProbe` **之后**：侧栏那一支收尾会 Exit，
            //     排在它前面的探针才有机会跑。
            if (m_options.menuProbe && !m_menuProbeDone) {
                RunMenuProbe();
            }
            // ★★ 2026-10-21 窗口拉伸探针：同步跑完即 Exit，同上。
            if (m_options.resizeProbe && !m_resizeProbeDone) {
                RunResizeProbe();
            }
            // ★★ 2026-10-21 Enter 发送探针：同步跑完即 Exit，同上。
            if (m_options.enterProbe && !m_enterProbeDone) {
                RunEnterProbe();
            }
            // ★★ 2026-10-21 输入框自动长高探针：同步跑完即 Exit，同上。
            if (m_options.growProbe && !m_growProbeDone) {
                RunGrowProbe();
            }
            // ★★ 2026-10-21 HUD 覆盖探针：同步跑完即 Exit，同上。
            if (m_options.hudProbe && !m_hudProbeDone) {
                RunHudProbe();
            }
            // ★★ P4 Phase A 跳到最新探针：同步跑完即 Exit，同上。
            if (m_options.jumpProbe && !m_jumpProbeDone) {
                RunJumpProbe();
            }
            // ★★ P4 Phase B 全局快捷键探针：同步跑完即 Exit，同上。
            if (m_options.keyProbe && !m_keyProbeDone) {
                RunKeyProbe();
            }
            // ★★ P4 Phase C 空状态探针（回放形态）：同步跑完即 Exit，同上。
            if (m_options.emptyProbe && !m_emptyProbeDone) {
                RunEmptyProbe();
            }
            // ★★ P4 Phase H 模型浮窗探针：同步跑完即 Exit，同上。
            if (m_options.modelProbe && !m_modelProbeDone) {
                RunModelProbe();
            }
            // ★★ P4 Phase G 选中复制探针：同步跑完即 Exit，同上。
            if (m_options.selectProbe && !m_selectProbeDone) {
                RunSelectProbe();
            }
            // ★★ P4 删除所选探针：同步跑完即 Exit，同上。
            if (m_options.delProvProbe && !m_delProvProbeDone) {
                RunDelProvProbe();
            }
            // ★★ P3.5 设置探针：跨 tick（save 后 hello 下一 tick 才到队列）。
            if (m_options.settingsProbe && !m_settingsProbeDone) {
                RunSettingsProbe();
            }
            // ★★ Step 4 主题/整树重建探针：排在最后。
            //   ★ 与前五个不同，它是**跨 tick** 的（toggle 相要点真实按钮，
            //     而重建被延迟到下一个 tick）⇒ 它**不能**和上面的同步探针同 tick 共存：
            //     上面任何一个一旦 Exit，本探针就再也拿不到下一个 tick。
            //     故六条互斥使用（同一进程只开一个 `--*-probe`）。
            if (m_options.themeProbe && !m_themeProbeDone) {
                RunThemeProbe();
            }
            // ★★ Step 5 历史会话探针：排在最后，同为**跨 tick**（点历史行后，
            //   打开被延迟到下一个 tick——派发栈 UAF）。
            //   顺序：reasoning → rebuild → session → sidebar → answer → theme → history。
            //   七条互斥使用。
            if (m_options.historyProbe && !m_historyProbeDone) {
                RunHistoryProbe();
            }
            return;
        }
        m_sess->replaySettled = true;
    }
    if (m_options.timed) {
        DrainTimed();
    } else {
        DrainInstant();
    }
    Relayout();
    FollowTailIfPinned();
    if (m_sess->replaySettled && m_options.scrollSweepStep > 0 && !m_sweepStarted && m_scroll) {
        // --scroll-sweep：等夹具拉完且尾随完成后，从顶部开始逐 tick 扫描到底
        m_sweepStarted = true;
        m_scroll->SetContentOffset(0, 0);
        if (m_window) {
            m_window->GetPlatformWindow().StartTimer(kSweepTimer, 33);
        }
    }
    CacheEvalMetrics();
    RefreshHud();
    // ★★ --hit-probe（REQ-04 边界）：夹具拉完 + `Relayout()` 之后**跑一次**。
    //   位置理由：此刻几何已定（按钮位置/尺寸是探针坐标的来源），
    //   且只跑一次（否则 30Hz 每 tick 都合成点击，读数被自身重复污染）。
    if (m_options.hitProbe && m_sess->replaySettled && !m_hitProbeDone) {
        m_hitProbeDone = true;
        RunHitProbe();
    }
    // ★ 思维链折叠行为取证（协议 v2 §7）：跨 tick 状态机（每次只推进一步，
    //   步间等过 200ms 折叠动画）⇒ 不能像 --hit-probe 那样一次跑完。
    if (m_options.reasoningProbe && m_sess->replaySettled && !m_reasoningProbeDone) {
        RunReasoningProbe();
    }
    // 注：`--rebuild-probe` 的推进在**上面 settle 分支里**（本函数到尾就 `return` 了，
    //   这里再写一遍是死代码；第一版正是写在这里，所以探针一步都没跑）。
    RequestRepaint();
}

void HarnessApp::OnAutoClose() {
    if (m_application) m_application->Exit();
}

// --scroll-sweep 步进：每次前进 step px；扫到底即退出（摘要由 Run() 返回后落盘）
void HarnessApp::OnSweepTick() {
    if (!m_scroll || m_sweepDone) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const int y = m_scroll->GetScrollOffsetY() + m_options.scrollSweepStep;
    if (y >= maxY) {
        m_scroll->SetContentOffset(0, maxY);
        m_sweepDone = true;
    } else {
        m_scroll->SetContentOffset(0, y);
    }
    CacheEvalMetrics();
    RefreshHud();
    RequestRepaint();
    if (m_sweepDone && m_application) {
        m_application->Exit();
    }
}

// ★★ 2026-10-23（ECDI REQ-13）：**本函数已删除**。
//
//   它曾经是本仓在没有框架能力时的下限绕行 —— `max(kMin, 请求值)` 之后再由
//   `OnWindowResized` 用 `SetBounds` 把窗口推回去，代价是拖拽过程中的一次回弹。
//   ECDI 现在有 `Window::SetMinimumSize`（`WM_GETMINMAXINFO` + `WM_WINDOWPOSCHANGING`，
//   在应用尺寸**之前**生效），建窗时登记一次即可，回弹不存在。
//
//   ★ 为什么删干净而不是留作"双保险"：两处都夹 ⇒ 任何一侧坏掉都测不出来
//     （`max()` 会把框架的失效掩盖成"我们的逻辑在工作"）。负对照的做法见
//     `RunResizeProbe` ⑥：**把框架的下限清零**，看同一个请求会不会真的落下去。

// ★★ 2026-10-21（用户要求「先把 UI 完善」第 1 项）：窗口可拉伸。
//
// 【改前是什么样】`App.cpp` 用 `Create(..., 1120, 760)` 建窗，`caption` 也是
//   `SetSize(1120, ...)` 定宽，全仓**零处**响应窗口尺寸变化。实测
//   （`.scratch/probe_resize_shots.py`，真 `SetWindowPos` 走真 WM_SIZE）：
//     · 拉到 1400x900 —— 转录行**仍是 1120 时的宽度**，右侧空出一大片白；
//     · 压到 800x560 —— 行**比视口还宽**，出现横向滚动条，文字在窗口右缘被截断。
//   两种症状同一个原因：行的宽度是 `Relayout()` 按 `ContentWidth(m_scroll)` **量**出来的，
//   而没人再调过它。ECDI 自己只同步 `RootWidget` 尺寸，它不知道我们的行是测量出来的。
//
// 【为什么不重建视图】尺寸不属于模型。`RebuildTranscriptView` 会清空行表、重放
//   整个 timeline、重算滚动位置——那是给「模型变了」用的。这里只需要重测宽度。
//   代价差得很远：重建会把每一行（含折叠体）重新测量一遍，而 `Relayout` 只碰宽度
//   真正变了的行（`lastWrapW` / `lastBodyW` 有同值守卫）。
//
// 【顺序为什么是这五步】
//   ① caption 改宽 —— 它内部按 `GetWidth()` 排三个系统钮，不改宽三个钮就停在原地；
//   ② `root.Arrange()` —— `VerticalLayout(fillCrossAxis=true)` 把新宽度下发到三个栏；
//      漏了这一步，下面的 `Relayout` 会拿**旧**的 `m_scroll->GetWidth()` 去测；
//   ③ `Relayout()` —— 按新内容宽重测每一行、重算滚动范围；
//   ④ `FollowTailIfPinned()` —— 用户在底部时保持贴底，否则一拉伸就被甩上去；
//   ⑤ `RequestRepaint()` —— 几何变了必须重画（`Arrange` 本身不 Invalidate）。
void HarnessApp::OnWindowResized(int width, int height) {
    if (!m_window) return;

    // ★★ 2026-10-23（ECDI REQ-13）：这里**不再**夹下限。
    //
    // 【改前】此处在最前面做一次 `ClampToMinimum`，把低于 880×460 的尺寸用
    //   `SetBounds` 推回下限。那是**框架没有能力**时的绕行：原生拖拽时系统不拦，
    //   只能在我们自己的 resize 回调里补救。
    // 【当时的代价（原文保留）】拖边框的过程中窗口会先缩到目标大小、再被弹回下限,
    //   表现为一次回弹。
    // 【现在】下限由框架在 `WM_GETMINMAXINFO`（ptMinTrackSize）+ `WM_WINDOWPOSCHANGING`
    //   两条路径上**在应用尺寸之前**落实 ⇒ 这个回调只会收到**已合规**的尺寸，
    //   回弹在源头上不存在。绕行删掉，不是因为"它现在无害"，而是因为**前提没了**：
    //   保留它会让"下限由谁负责"永远说不清（两处都在夹 ⇒ 谁也证明不了自己有用）。
    // 【怎么证明框架那侧真的在拦】`--resize-probe` 的 min-size 相**反向**取证：
    //   临时把下限清零，同一个 640×360 必须**真的**落成 640×360（对照世界），
    //   再把下限登记回去、重发同一请求 ⇒ 落在 >= 下限。见 `RunResizeProbe` ⑥。
    const int effW = width;
    const int effH = height;

    // ① 标题栏跟着改宽（它创建时是定宽的）
    if (m_caption) {
        const int w = std::max(320, effW);
        if (m_caption->GetWidth() != w) m_caption->SetSize(w, m_caption->GetHeight());
    }

    // ② root 重排。★ 基类已经把 root 的尺寸设好了，这里只是让布局把新宽度用起来。
    //   ★ 钳制路径下 root 可能仍是上一次的尺寸（`SetBounds` 的事件还没来），
    //     故显式对齐一次，保证"排版用的尺寸 == 生效的尺寸"。
    ECDI::Widget& root = m_window->GetRootWidget();
    if (root.GetWidth() != effW || root.GetHeight() != effH) {
        root.SetSize(effW, effH);
    }
    root.Arrange();

    // ③ 重测每一行（内容宽变了 ⇒ 折行位置变了）
    //   ★ 空会话（回放模式下新建的）`rows` 为空，`Relayout` 内部对空表安全；
    //     但仍要先确认 `m_content` 在——`BuildUi` 之前不该走到这里。
    if (!m_content || !m_sess) return;
    if (m_promptBar) {
        m_promptBar->SyncInputHeight();
    }
    Relayout();

    // ④ 贴底保持 + ⑤ 重画
    FollowTailIfPinned();
    RequestRepaint();
}

void HarnessApp::DrainInstant() {
    if (m_sess->instantDone) return;
    m_sess->instantDone = true;
    std::string line;
    // ★ 协议 §9：版本不兼容 ⇒ **停止消费**（"拒绝连接"的实质）。
    //   只在 `HandleLine` 返回后检查：`Exit()` 只让消息循环退出，**不打断本循环**，
    //   而 `--instant` 会在同一 tick 抽干整个夹具 ⇒ 若不在此处 break，
    //   "拒绝"就只是打印一行提示、随后照常把整条流处理完（实测：protocol:3 的
    //   夹具仍读到 blocks=44）——那不是拒绝，是"边抱怨边照做"。
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);   // 协议 §1.2：截断必须显式报错
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::DrainTimed() {
    if (m_sess->waitForAnswer) return;   // I3：ask 未答前 core 不再发消息
    if (m_sess->protocolIncompatible) return;   // ★ 协议 §9：版本不兼容 ⇒ 不再消费
    // ★ 0d：会话被切走（冻结）期间不拉取——它不在屏上，"到期"没有意义。
    if (m_sess->timedPaused) return;
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  m_sess->timedAnchor)
            .count();
    std::string line;
    while (m_sess->replay->NextDue(elapsedMs, line)) {
        HandleLine(line);
        if (m_sess->waitForAnswer) break;   // ask 已显示，停在 waiting（协议 §8.3）
        if (m_sess->protocolIncompatible) break;   // ★ 协议 §9：版本不兼容 ⇒ 停止消费
    }
    if (m_sess->replay->Empty() && !m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::HandleLine(const std::string& line) {
    std::string notice;
    const bool ok = m_sess->transcript.OnLine(line, notice);
    if (!notice.empty()) AddNoticeRow(notice, kErrorDark);   // I4 / I5 等显式提示
    if (ok) RouteLastMessage();
}

void HarnessApp::RouteLastMessage() {
    const json::Value& msg = m_sess->transcript.LastMessage();
    const std::string t = msg.GetString("t");

    if (t == "block_start") {
        if (const Block* block = m_sess->transcript.FindBlock(msg.GetString("id"))) {
            if (block->kind == "user" && !block->clientId.empty()) {
                const std::string clientId = block->clientId;
                for (auto it = m_pendingPromptViews.begin(); it != m_pendingPromptViews.end(); ++it) {
                    if (it->clientId == clientId) {
                        if (m_content && it->row.panel) m_content->RemoveChild(it->row.panel);
                        m_pendingPromptViews.erase(it);
                        break;
                    }
                }
                auto& pending = m_sess->pendingPrompts;
                pending.erase(std::remove_if(pending.begin(), pending.end(),
                    [&](const PendingPrompt& p) { return p.clientId == clientId; }), pending.end());
            }
            CreateBlockRow(*block);
        }
        return;
    }
    if (t == "delta") {
        const std::string id = msg.GetString("id");
        const auto row = m_sess->blockRows.find(id);
        const Block* block = m_sess->transcript.FindBlock(id);
        if (row != m_sess->blockRows.end() && block && row->second->label) {
            // ★ 未知 kind 的行有**前缀**（`[未知 kind: xxx]`）——不能整段覆盖，
            //   否则流式 delta 一到就把前缀擦掉（2026-10-13 实测：截图里 r2 的
            //   `[未知 kind: thinking]` 前缀正是这样丢的）。此处保持与
            //   `CreateBlockRow` 同一拼法。
            if (block->unknownKind) {
                row->second->label->SetText(UnknownKindText(*block));
            } else {
                row->second->label->SetText(block->text);   // 合并：只追加到文本缓冲，不逐字符刷新
            }
        }
        return;
    }
    if (t == "block_end") {
        const std::string id = msg.GetString("id");
        const Block* block = m_sess->transcript.FindBlock(id);
        const auto row = m_sess->blockRows.find(id);
        if (row == m_sess->blockRows.end() || !row->second->headerBtn) {
            return;
        }
        // ★ 协议 v2 §7：思维链块结束时标题改为「**已完成**」（用户裁决）。
        //   ★ **箭头不变**——箭头只由展开态决定，与是否结束无关。
        //     （我一度连箭头一起换成 `▶`，被截图证明违背用户明确给出的规格，已撤回。）
        //   ★ token 副标也在此刻出现（`tokens` 是 `block_end` 才有的字段）。
        //   ★ 展开态也要更新文案：用户看到的是「已完成」+ `︿`。
        //     （2026-10-18 订正：此处原文写 `▲`，但思维链标题的箭头单一真相源是
        //      `ReasoningTitle()`，用的正是用户指定的钝角 `﹀`/`︿`，不是 `▲`。）
        if (row->second->reasoning && block) {
            row->second->ended = true;
            if (row->second->headerBtn) {
                row->second->headerBtn->SetText(
                    ReasoningTitle(*block, row->second->expanded));
            }
            RequestRepaint();
            return;
        }
        // ★ REQ-02（2026-10-13）：`tool_call` 的 `summary` 此前被吞——原条件只认
        //   `tool_result`，而假 core 在 **`tool_call` 的 `block_end`** 里就发了
        //   `summary:"已发起"`（协议 §3.5 允许），于是那条摘要无处落地。
        //   修正为两类都更新标题：`tool_call` 用自身 name，`tool_result` 用 forId 引用的工具名。
        const bool isResult = (block && block->kind == "tool_result");
        const bool isCall = (block && block->kind == "tool_call");
        if (isResult) {
            // hi-fi：结果行标题 = 工具名（来自 forId 引用的 call 块）+ ok/失败摘要
            // ★ 2026-10-18：`▸`(U+25B8) 在两个主题的 face 里**没有字形**
            //   （`.scratch/symcand.py` 两轮实测：GDI 靠字体链接偷画、GL 直接空白）
            //   ⇒ 换成 `›`(U+203A)，同为右向尖括号，两主题都真有。
            std::string title = "› ";
            if (const Block* call = m_sess->transcript.FindBlock(block->forId)) {
                title += call->name.empty() ? "tool" : call->name;
            } else {
                title += "tool";
            }
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
        } else if (isCall) {
            // 工具调用：标题 = 工具名（+ 可选摘要）——此前 summary 在此被丢弃
            // ★ 2026-10-18：`▸` → `›`（字形可用性实测，见上方 isResult 分支注释）
            std::string title = "› " + (block->name.empty() ? "tool_call" : block->name);
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
        }
        return;
    }
    if (t == "ask") {
        CreateAskRow(msg);
        return;
    }
    if (t == "error") {
        const std::string clientId = msg.GetString("client_id");
        const std::string message = msg.GetString("message", "core error");
        const json::Value* fatalField = msg.Find("fatal");
        // ★ 协议 §3.8：`fatal` 缺省 = **false**。缺字段不是「致命」——§9 的宽容原则
        //   （「宽容比拒连更安全」）同样适用于这里：一个漏写字段的 core 不该被判死，
        //   真致命的 core 会自己退出，EOF 仍会兜住。
        const bool fatal = fatalField && fatalField->IsBool() && fatalField->boolean;
        const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
            m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                return p.clientId == clientId && p.status == "sending";
            });
        const bool correlated = !clientId.empty() && pending != m_sess->pendingPrompts.end();
        if (correlated) FailPrompt(clientId, message);
        if (fatal) {
            FailPendingOperations(message);
            AddNoticeRow("[fatal error] " + message, kErrorDark);
        } else if (!correlated) {
            AddNoticeRow("[error] " + message, kErrorDark);
        }
        return;
    }
    if (t == "hello") {
        m_sess->sessionId = msg.GetString("session");
        m_sess->coreVer = msg.GetString("core");
        const json::Value* protocol = msg.Find("protocol");
        m_sess->protocolVer = protocol && protocol->IsNumber()
            ? static_cast<int>(protocol->number) : 0;
        const json::Value* caps = msg.Find("caps");
        bool capsValid = caps && caps->IsArray();
        bool hasWorkspace = false;
        std::string joined;
        if (capsValid) {
            for (const auto& cap : caps->array) {
                if (!cap.IsString()) {
                    capsValid = false;
                    break;
                }
                if (!joined.empty()) joined += ", ";
                joined += cap.text;
                if (cap.text == "workspace") hasWorkspace = true;
            }
        }
        bool hasModelCap = false;
        if (capsValid) {
            for (const auto& cap : caps->array) {
                if (cap.text == "model") hasModelCap = true;
            }
        }
        m_sess->capsStr = capsValid ? joined : std::string();
        m_sess->workspaceCap = capsValid && hasWorkspace;
        m_sess->hasModelCap = capsValid && hasModelCap;
        // spawn 时 GUI 已知道初始提供商（--provider-id 与配置一致）
        if (m_sess->providerId.empty()) {
            if (const auto* def = m_appConfig.DefaultProvider()) {
                m_sess->providerId = def->id;
            }
        }
        if (m_sess->protocolVer > kProtocolMaxSupported) {
            // ★★ 协议 §9：只有 core 声明的主版本**高于**本 GUI 上限才拒连（不做降级猜测）。
            //   缺字段按 v1 宽容；**低于**上限从来不是拒绝理由（§9 无此条）。
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "[协议不兼容] core 声明 protocol %d，本 GUI 支持到 %d"
                          " —— 按 §9 拒绝连接（不做降级猜测）",
                          m_sess->protocolVer, kProtocolMaxSupported);
            AddNoticeRow(buf, kErrorDark);
            m_sess->protocolIncompatible = true;
            m_sess->disconnected = true;
            m_hudDirty = true;
            if (m_application) m_application->Exit();   // 不进入渲染路径
            return;
        }
        // ★★ 协议 v3 §3.1：caps 缺 `workspace` **不是**拒连条件，而是「GUI 不得显示
        //   工作区选择器」⇒ 隐藏整段工作区交互，但**连接与发送照常**。
        //   ★ 2026-10-20 修正：旧实现把 `!workspaceCap` 也写进上面的拒连条件，
        //     而两份 hostile 夹具的 hello 恰好是 `protocol:3` + caps 无 workspace
        //     ⇒ 它们一行都没被解析（blocks=0/violations=0），I1/I4/I5 的负对照
        //     全部空转。规范从没有「必须声明 workspace 才能连接」这一条。
        if (!m_sess->workspaceCap) {
            m_sess->workspaceStatus = "core 未声明 workspace 能力";
        }
        if (const json::Value* ctx = msg.Find("context");
            ctx && ctx->IsNumber() && ctx->number > 0) {
            m_sess->contextLimit = static_cast<long long>(ctx->number);
            // ★★ P4 H 修复（P3.6 bug #2）：ctxCapacity 的 v4 逐轮分母在此**填充后**
            //   迁移——原实现把迁移放在填充之前（迁进去的恒 0），hello.context
            //   与 usage.context 的覆盖形同虚设（百分比/环一直拿不到分母）。
            if (m_sess->ctxCapacity == 0) {
                m_sess->ctxCapacity = static_cast<int>(ctx->number);
            }
        }
        m_hudDirty = true;
        AddNoticeRow("core " + msg.GetString("core") + " · session " +
                         msg.GetString("session") + " · protocol " +
                         std::to_string(m_sess->protocolVer), kDarkMuted);
        // The v3 core starts in idle and does not emit a redundant initial state line.
        if (m_sess->coreState.empty()) m_sess->coreState = "idle";
        MaybeStartWorkspaceSync();
        return;
    }
    if (t == "state") {
        m_sess->coreState = msg.GetString("value");
        m_hudDirty = true;
        if (m_sess->coreState == "idle") {
            MaybeStartWorkspaceSync();
            SendPendingModelSwitchIfIdle();   // ★ P3.6：排队的模型切换在 idle 兑现
        }
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "workspace") {
        const std::string requestId = msg.GetString("request_id");
        const json::Value* requestField = msg.Find("request_id");
        if (!requestField || !requestField->IsString() || requestId.empty()) {
            if (!m_sess->pendingWorkspaceId.empty()) {
                const bool initialSync = m_sess->pendingWorkspaceInitialSync;
                m_sess->pendingWorkspaceId.clear();
                m_sess->pendingWorkspaceInitialSync = false;
                m_sess->workspaceStatus = "工作区确认无效：缺少 request_id";
                if (initialSync && !m_sess->workspaceSyncComplete) {
                    m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
                }
                RefreshHud();
            }
            AddNoticeRow("[workspace ack 缺少 request_id]", kErrorDark);
            return;
        }
        if (m_sess->pendingWorkspaceId.empty() || requestId != m_sess->pendingWorkspaceId) {
            AddNoticeRow("[workspace ack 不匹配] " + requestId, kErrorDark);
            return;
        }
        const bool initialSync = m_sess->pendingWorkspaceInitialSync;
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        const json::Value* ok = msg.Find("ok");
        if (!ok || !ok->IsBool()) {
            m_sess->workspaceStatus = "工作区确认无效：ok 必须是 boolean";
        } else if (ok->boolean) {
            const json::Value* path = msg.Find("path");
            if (!path || !path->IsString() || path->text.empty()) {
                m_sess->workspaceStatus = "工作区确认无效：缺少实际路径";
            } else {
                m_sess->workspacePath = path->text;
                m_sess->workspaceSyncComplete = true;
                m_sess->workspaceStatus = "已由 core 确认";
                // ★★ 2026-10-20：确认成功 = 「这个会话现在属于这条路径那个工作区」。
                //   ★ 注册只发生在这里（不在请求时）：工作区的路径定义是 **core 回显的
                //     规范化结果**，不是 GUI 自己发出去的那个字符串。在请求时注册就会
                //     把一个未经确认的路径写进注册表，而“注册表里的路径”正是侧栏
                //     告诉用户「这个会话在哪」的唯一依据——那等于自己给自己发证据。
                //   ★ `AcquireWorkspace` 按路径去重：两个会话被确认到同一目录时共享
                //     同一个工作区实体（侧栏不会出现两个同路径的标题行）。
                //   ★ 归属变了 ⇒ 侧栏的**分组结构**也变了，必须重画（不是只刷 HUD 的
                //     两行文字）。`m_sidebarDirty` 只在 tick 顶部被消费（OnReplayTick），
                //     而本分支正好就在 tick 里（HandleLine ← OnReplayTick），不在派发栈上，
                //     置脏是安全的。
                //     漏了这一行的实测症状：新会话的 ack 到达后侧栏**从不重建**，
                //     于是一个已经确认到路径的会话永远挂在“未归属工作区”下面，
                //     而顶部的工作区块却正确地显示着同一个路径——自相矛盾的界面。
                AssignSessionWorkspace(*m_sess, AcquireWorkspace(path->text));
                m_sidebarDirty = true;
            }
        } else {
            m_sess->workspaceStatus = "切换失败：" + msg.GetString("message", "core 拒绝请求");
        }
        if (initialSync && !m_sess->workspaceSyncComplete) {
            // Initial sync failure leaves the GUI gated until an explicit folder request succeeds.
            m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
        }
        m_hudDirty = true;
        RefreshHud();
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "usage") {
        m_sess->usageTurn = static_cast<int>(msg.GetNumber("turn"));
        m_sess->usageIn = static_cast<long long>(msg.GetNumber("input"));
        m_sess->usageOut = static_cast<long long>(msg.GetNumber("output"));
        // ★ v4 §3.7：可选 context = 当下分母（按轮覆盖 hello.context）。
        if (const json::Value* c = msg.Find("context");
            c && c->IsNumber() && c->number > 0) {
            m_sess->ctxCapacity = static_cast<int>(c->number);
            m_sess->contextLimit = static_cast<long long>(c->number);
        }
        m_hudDirty = true;
        return;
    }
    if (t == "model") {
        // ★ v4 §3.11：model 确认（I10 同族：id 匹配 + ok 布尔 + ok:false 带 message）
        const std::string ackId = msg.GetString("id");
        if (m_sess->pendingModelId.empty() || ackId != m_sess->pendingModelId) {
            AddNoticeRow("[model ack 不匹配] " + ackId, kErrorDark);
            return;
        }
        m_sess->pendingModelId.clear();
        const json::Value* ok = msg.Find("ok");
        if (!ok || !ok->IsBool()) {
            AddNoticeRow("[model ack 无效：ok 必须是 boolean]", kErrorDark);
            return;
        }
        if (!ok->boolean) {
            AddNoticeRow("[模型切换失败] " + msg.GetString("message", "（无原因）"),
                         kErrorDark);
        } else {
            m_sess->providerId = msg.GetString("provider", m_sess->providerId);
            m_sess->modelName = msg.GetString("model", m_sess->modelName);
            if (const json::Value* c = msg.Find("context");
                c && c->IsNumber() && c->number > 0) {
                m_sess->ctxCapacity = static_cast<int>(c->number);
                m_sess->contextLimit = static_cast<long long>(c->number);
            }
            AddNoticeRow("[已切换] " + m_sess->providerId + " / " +
                             msg.GetString("model", "?"), kDarkMuted);
            m_modelListDirty = true;
            UpdateModelChip();   // ★ P4 H：chip 常驻数据跟着切换走
        }
        m_hudDirty = true;
        return;
    }
    if (t == "bye") {
        FailPendingOperations("core 已发送 bye");
        AddNoticeRow("会话结束 (" + msg.GetString("reason") + ", code " +
                         std::to_string(static_cast<int>(msg.GetNumber("code"))) + ")",
                     kDarkMuted);
        return;
    }
    // state / usage：上移到 HUD，转录不渲染
}

// ── 行构建（规格 §5.2 渲染映射）────────────────────────────────

void HarnessApp::AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                        const std::string& blockId, size_t timelineIdx) {
    RowRec stored = rec;
    stored.panel = row.get();
    // ★★ 0c：行与时间线条目**成对**登记。下标由调用方**显式给出**而不是取
    //   `rows.size()`——重建时若某条目建不出行（块缺失即跳过），两者就会错位，
    //   而"视图状态写回模型"依赖这个下标指对地方（错位 = 把 A 的展开态写到 B 上）。
    stored.timelineIdx = timelineIdx;
    if (m_options.rowModeList) {
        stored.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
    }
    m_sess->rows.push_back(stored);
    RowRec& ref = m_sess->rows.back();
    m_content->AddChild(std::move(row));
    if (!blockId.empty()) m_sess->blockRows[blockId] = &ref;
    MaterializePendingPrompts();  // keep transient rows physically after canonical rows.
    // settle 后新增的行（发送/停止/回答反馈）没有逐 tick 的 Relayout 可蹭——
    // 排一个补重排脉冲，让定时器把它布局出来
    m_relayoutPulse = std::max(m_relayoutPulse, 2);
}

// ★★ 0c（2026-10-19）：行构造拆成**两层**。
//   `BuildBlockRow` = **纯函数**：只吃 `Block` 值 + `m_options`，产出控件树并填 `rec`；
//     不追加时间线、不发协议、不写全局状态 ⇒ **可以被重放任意多次**。
//   `CreateBlockRow` = 增量入口：建行 → 追加时间线条目 → 绑回调 → 探针捕获。
//   为何必须拆：重建视图要能"把同一个块再画一次"，而增量入口带副作用（时间线会翻倍）。
//   为何拆得开：本分支历来只读 `block` 的字段（不读会话状态），故纯化是**零成本**的。
//
//   ★ 重建保真度的一个隐含要求：**标题必须是 `block` 当前值的函数**。
//     增量路径里标题由两段代码写（`block_start` 建行时给初值、`block_end` 到达时改写），
//     重建只有一次机会 ⇒ 那两段必须收敛成同一个函数，否则重建出来的是「初值标题」
//     （工具卡会永远停在「等待结果」，而这正是历史上那个误导 bug 的形态）。
//     故 `ToolTitle()` 收口两处，`ReasoningTitle()` 早已是同款收口。
std::string ToolTitle(const Block& block, const Block* call) {
    std::string base;
    if (block.kind == "tool_call") {
        base = block.name.empty() ? "tool_call" : block.name;
    } else if (!block.ended) {
        base = "等待结果";            // 结果块还没结束：不知道是哪个工具的结论
    } else if (call && !call->name.empty()) {
        base = call->name;            // 结果块：工具名来自 forId 引用的 call 块
    } else {
        base = "tool";
    }
    if (block.ended) {
        // ★ REQ-02（2026-10-13）：`summary` 曾在此被吞——`tool_call` 的 `block_end`
        //   也会带 `summary`（协议 §3.5 允许），两类都必须落地。
        if (!block.summary.empty()) {
            base += " · " + block.summary;
        } else if (!block.ok) {
            base += " · 失败";
        }
    }
    // ★ 2026-10-18：`▸`(U+25B8) 两主题 face 都无字形 ⇒ `›`(U+203A)（同右向尖括号语义）
    return "› " + base;
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildBlockRow(const Block& block, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();

    // ★ 0c：**每一**条块行都登记自己的块 id（此前只有 reasoning/tool 两支登记）。
    //   两个后果，都不是小事：
    //   ① 重建探针靠"有没有 blockId"区分「块行」与「通知行」——chat 气泡没有 id 时
    //      会被当成通知，于是负对照只比到可折叠行（实测 5/19/126/500，而真实错位量
    //      是 0/42/218/1998）⇒ **负对照失去了判别力**，正向全等也就不算证据。
    //   ② 行不知道自己来自哪个块，任何"按块回查"的诊断都只能靠猜。
    rec.blockId = block.id;

    if (block.kind == "user") {
        rec.alignRight = true;
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kUserBorder});
        // ★★ P4 G：user 行同款只读 TextBox 化（见下方 assistant 分支注释）。
        if (m_options.selectableRows) {
            auto box = std::make_unique<ECDI::TextBox>();
            rec.label = box.get();
            box->SetText(block.text);
            box->SetReadOnly(true);
            box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            box->SetFont(harness::SansFont(harness::kFontBody));
            box->SetStyle(ECDI::TextBoxStyleOverride{
                .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
                .caretWidth = 0.0f,
                .padding = 0.0f,
                .cornerRadius = 0.0f,
                .borderWidth = 0.0f});
            panel->AddChild(std::move(box));
            return panel;
        }
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        // ★ 正文必须走主题字体工厂：此前不 SetFont ⇒ family 为空 ⇒ 落 SimSun，
        //   而侧栏/标题走雅黑 ⇒ **同一界面两种中文字形**（浅色下尤其割裂）。
        label->SetFont(harness::SansFont(harness::kFontBody));
        panel->AddChild(std::move(label));
        return panel;
    }

    if (block.kind == "assistant" || block.kind == "system" || block.kind == "error") {
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kCardBorder});
        // ★★ P4 G 尖峰（--selectable-rows）：assistant 正文行用只读 TextBox——
        //   「编辑 no-op 但方向键/选区/Ctrl+A/C 照常」（TextBox.h:50 契约）⇒ 转录
        //   可选中/复制不需要框架新能力。视觉与 Label 版同形：透明底/无边框/
        //   无 caret/同字体同色。默认关闭 ⇒ 基线路径零变化。
        if (m_options.selectableRows) {
            auto box = std::make_unique<ECDI::TextBox>();
            rec.label = box.get();
            box->SetText(block.text);
            box->SetReadOnly(true);
            box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            box->SetFont(harness::SansFont(harness::kFontBody));
            box->SetStyle(ECDI::TextBoxStyleOverride{
                .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
                .caretWidth = 0.0f,
                .padding = 0.0f,
                .cornerRadius = 0.0f,
                .borderWidth = 0.0f});
            panel->AddChild(std::move(box));
            return panel;
        }
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody));   // 同 user 分支：统一中文 face
        if (block.kind == "system") label->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
        if (block.kind == "error") label->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
        panel->AddChild(std::move(label));
        return panel;
    }

    // ★★ 思维链卡（协议 v2 §7，2026-10-14）——**默认收起**，点击标题展开，箭头翻转。
    //   ★ 为何必须有这条**显式分支**：`reasoning` 一旦进入 `IsKnownKind`，
    //     `block.unknownKind` 就是 **false** ⇒ 它会**直接掉到本函数末尾的工具卡分支**，
    //     而那个分支的 else 把标题写死成「等待结果」——**正是 2026-10-13 修掉的那个 bug
    //     换个方式复活**（思维链又一次伪装成"正在等的工具结果"）。加 kind 必须同时加分支。
    //   ★ 复用 `CollapsiblePanel`（与工具卡同机制，不新造控件）：
    //     折叠动画、内容可见性、展开基准全由框架既有能力承担。
    if (block.kind == "reasoning") {
        rec.reasoning = true;
        rec.ended = block.ended;
        rec.blockId = block.id;
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kReasoningBg, kProbeBg),
            .cornerRadius = kRadiusTool,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kReasoningBorder});

        // 标题 = 单一真相源（箭头两态）
        // ★ 0c：箭头取自 `rec.expanded`（重建时先由条目回填）——否则重建出来的行
        //   一律显示收起态箭头，而控件实际是展开的（读数与屏幕当场打架）。
        auto header = std::make_unique<ECDI::Button>(ReasoningTitle(block, rec.expanded));
        rec.headerBtn = header.get();
        header->SetTextColor(Pick(m_options.probeContrast, kReasoningInk, kProbeInk));
        {
            ECDI::Font f = harness::SansFont(12.0f);
            header->SetFont(f);
        }
        header->SetStyle(ECDI::ButtonStyleOverride{
            .background = Pick(m_options.probeContrast, kReasoningHeaderBg, kProbeBg),
            .border = kReasoningBorder,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kReasoningHover,
            .textAlignment = ECDI::TextAlignment::Left});
        panel->AddChild(std::move(header));

        auto body = std::make_unique<ECDI::CollapsiblePanel>();
        rec.body = body.get();
        rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        // 展开后是**用户主动要读**的内容 ⇒ 用常规 ink（不再弱化）
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetWordWrap(true);   // 思维链很长，必须按宽断行
        {
            ECDI::Font f = harness::SansFont(12.0f);
            label->SetFont(f);
        }
        rec.body->GetContent()->AddChild(std::move(label));
        panel->AddChild(std::move(body));
        return panel;
    }

    // ★★ 未知 `kind` 的**中性落点**（2026-10-13）。
    //   问题（实测取证，见 `.scratch/kind-probe.png` / `cot-findings.md`）：
    //     §3.3 只列六个 kind，而 I5 只管信封层的 `t` ⇒ 未知 `kind` **不被任何不变量覆盖**。
    //     此前的三分支写法让它们**静默掉进工具卡分支**，而该分支的 else 把标题写死成
    //     「等待结果」——那正是 `tool_result` 在 `block_end` 到达**前**的占位标题
    //     ⇒ **思维链块与「正在等的工具结果」视觉上无法区分**（不是难看，是**误导**）。
    //   ★ 归属 **harness 侧**（ECDI 只是忠实渲染调用方给的控件树）。
    //   ★ 处置 = **中性渲染**：弱化色 + 明写 `[未知 kind: xxx]` + 普通气泡（不折叠、
    //     不占工具卡的位置）。理由：既保住 I5 的前向兼容精神（内容仍被渲染、不被丢弃），
    //     又不让它**伪装成有语义的形态**。
    //   ★ 思维链（`reasoning` / `thinking` …）正是这条路径的主要真实来源——
    //     给它一个**一等公民的位置**需要协议 v2（`kind:"reasoning"`），
    //     在那之前，中性渲染是「不误导」的最小正确行为。
    if (block.unknownKind) {
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kWarn});   // 琥珀描边：视觉上明确「这不是普通助手文本」
        // 前缀显式标出不认识的 kind；正文仍按原样展示（不丢内容）
        std::string shown = UnknownKindText(block);
        auto label = std::make_unique<ECDI::Label>(shown);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kWarn, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody));   // 统一中文 face
        panel->AddChild(std::move(label));
        return panel;
    }

    // tool_call / tool_result：可折叠卡片（默认折叠；标题按钮 + CollapsiblePanel 展开体）
    rec.tool = true;
    rec.blockId = block.id;
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kContentBg, kProbeBg),
        .cornerRadius = kRadiusTool,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .borderColor = kCardBorder});

    std::string bodyText;
    if (block.kind == "tool_call") {
        bodyText = block.argsPretty.empty() ? "{}" : block.argsPretty;
    } else {
        bodyText = block.text.empty() ? block.summary : block.text;
    }

    const Block* call = (block.kind == "tool_result")
                            ? m_sess->transcript.FindBlock(block.forId)
                            : nullptr;
    auto header = std::make_unique<ECDI::Button>(ToolTitle(block, call));
    rec.headerBtn = header.get();
    // 结束且失败 ⇒ 标题转错误色（增量路径在 `block_end` 做，重建在这里做，同一判据）
    header->SetTextColor((block.ended && !block.ok)
                             ? kErrorDark
                             : Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    {
        ECDI::Font f = harness::SansFont(12.0f);
        header->SetFont(f);
    }
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kContentBg, kProbeBg),
        .border = kCardBorder,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover,
        .textAlignment = ECDI::TextAlignment::Left});   // Phase 30 △3：卡头贴左（hi-fi .tool>.h）
    panel->AddChild(std::move(header));
    auto body = std::make_unique<ECDI::CollapsiblePanel>();
    rec.body = body.get();
    rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
    auto label = std::make_unique<ECDI::Label>(bodyText);
    rec.label = label.get();
    label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
    label->SetWordWrap(true);   // Phase 29：工具体按 bodyW 断行（JSON/日志长串）
    {
        ECDI::Font f = harness::MonoFont(12.0f);
        label->SetFont(f);
    }
    // Phase 30 批二：CollapsiblePanel 收起态钉住内容可见性（#51④）——
    // 收起溢绘已由框架修复，应用侧 SetVisible workaround 退役。
    rec.body->GetContent()->AddChild(std::move(label));
    panel->AddChild(std::move(body));
    return panel;
}

// 可折叠行的点击绑定（**构建期与重建期共用**）。
//   ★ 0c 的关键变化：展开态**先写回模型**（`timeline[idx].expanded`），再动控件。
//     在此之前展开态只活在 `RowRec` 上 ⇒ 任何重建都会把用户的展开操作静默回退。
void HarnessApp::BindCollapsible(const Block& block, RowRec& stored) {
    if (!stored.headerBtn) return;
    const bool reasoning = stored.reasoning;
    stored.headerBtn->SetOnClick([this, &stored, reasoning] {
        if (m_options.rowModeList) return;   // 固定行高模式：展开体被后续行覆盖，v1 禁用
        ToggleRow(stored, reasoning);
    });
    (void)block;
}

// 展开/收起一行（**点击路径与探针共用的唯一入口**）。
//   ★ 为什么必须抽成函数：0c 的重建探针要构造"用户已经展开过"的状态，而它**不能**
//     合成点击——探针需要在**任意**行上造状态，包括滚动到视口外、HitTest 命不中的行；
//     若探针自己再写一遍"改标志 + 翻箭头 + 重排"，那就是**第二份实现**，测的是它自己。
void HarnessApp::ToggleRow(RowRec& stored, bool reasoning) {
    stored.expanded = !stored.expanded;
    // ★ 视图 → 模型（重建前若来不及扫全表，这一条保证单行状态不丢）
    if (stored.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[stored.timelineIdx].expanded = stored.expanded;
    }
    if (stored.body) stored.body->Toggle();
    // 箭头随展开态翻转（﹀ ⇄ ︿）；文案与 token 副标由**单一真相源**决定，
    // 故回查块（`block_end` 可能已到达 ⇒ 文案应为「已完成」+ tokens）
    if (stored.headerBtn) {
        if (const Block* b = m_sess->transcript.FindBlock(stored.blockId)) {
            if (reasoning) {
                stored.headerBtn->SetText(ReasoningTitle(*b, stored.expanded));
            } else {
                // ★ 修 2026-10-19：此前工具卡这一支传的是 `std::string()`
                //   ⇒ **点一下工具卡，标题就被清空**（卡片从此没有名字）。
                //   与"箭头两态"同一个道理：标题必须由单一真相源重算，不能置空。
                const Block* call =
                    (b->kind == "tool_result") ? m_sess->transcript.FindBlock(b->forId) : nullptr;
                stored.headerBtn->SetText(ToolTitle(*b, call));
            }
        }
    }
    m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画
    // ★★ Step 5：展开态也进 sidecar（同样是"只对历史会话"，理由同 MarkAnswered）。
    if (m_sess && m_sess->originKind == "replay-history") SaveUiState(*m_sess);
    Relayout();             // 立即重排一次（settle 后定时器路径不再经过 Relayout）
    RequestRepaint();
}

void HarnessApp::CreateBlockRow(const Block& block) {
    RowRec rec;
    auto panel = BuildBlockRow(block, rec);

    // ★★ 0c：**先**登记模型条目，**再**交给 `AddRow`——
    //   `AddRow` 用 `rows.size()` 当条目的下标，两者等长同序才成立。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Block;
    entry.id = block.id;
    entry.expanded = false;
    m_sess->timeline.push_back(entry);

    AddRow(std::move(panel), rec, block.id, m_sess->timeline.size() - 1);
    if (m_sess->rows.empty()) return;
    RowRec& stored = m_sess->rows.back();

    // ★ 在**建行时刻**抓流式态标题（`block_start` 刚到、`block_end` 还没来）。
    //   为何必须此刻抓：`--instant` 在同一 tick 抽干夹具 ⇒ 事后回读永远是
    //   「已完成」，`▼ 思考中` 那一态**根本采不到**（首版就栽在这里）。
    if (m_options.reasoningProbe && stored.reasoning && m_reasoningTitleStreaming.empty()) {
        m_reasoningTitleStreaming = stored.headerBtn->GetText();
    }
    // 只有可折叠行需要绑回调（普通气泡/未知 kind 无展开体）
    if (stored.body && stored.headerBtn) BindCollapsible(block, stored);
}

// ── ★★ 0c：模型 → 视图（重建路径）────────────────────────────────────
//
// 为什么需要"重建"而不是就地改：`Theme.h:19-20` 写明令牌是**引用别名**，UI 一旦建好就把
// 当时的颜色值拷进了各自的 PanelStyle/ButtonStyle ⇒ 之后 `SwitchTheme()` 不会重绘。
// 故换主题/换会话的唯一可靠手段是**拆掉重来**；而拆掉重来的前提是模型足够把视图原样
// 再生出来——本组函数就是那条通路，0c 的探针拿它自证（不是靠肉眼）。

// 视图 → 模型：把 `rows` 上的临场状态（展开/已答）写回 `timeline`。
//   ★ 为何要"扫全表"而不是只靠回调里那句写回：重建可能由**别的**路径触发
//     （换主题、切会话、探针），那些路径不经过点击回调。重建前统一同步一次，
//     才能保证"用户看到的"与"模型记下的"在拆树那一刻一致。
void HarnessApp::SyncRowViewState() {
    for (RowRec& r : m_sess->rows) {
        if (r.timelineIdx >= m_sess->timeline.size()) continue;
        TimelineEntry& entry = m_sess->timeline[r.timelineIdx];
        if (r.body) entry.expanded = r.expanded;
        if (r.ask) entry.answered = r.answered;
    }
}

// 把一条时间线条目落成一行（构建期与重建期**共用同一个**分派点）。
//   ★ 重建期**绝不**执行副作用：不重发 answer、不重跑自动应答、不追加通知。
void HarnessApp::MaterializeOne(size_t idx, const TimelineEntry& entry) {
    // ★ idx = 本条目在 `timeline` 中的位置，直接当作新行的 `timelineIdx`：
    //   重建期**不**追加条目、**不**发副作用，行与条目一一对应。
    (void)idx;
    switch (entry.kind) {
        case TimelineEntry::Kind::Block: {
            const Block* block = m_sess->transcript.FindBlock(entry.id);
            if (!block) return;   // 模型里指向一个不存在的块：如实跳过，不编造
            RowRec rec;
            rec.expanded = entry.expanded;   // ★ 先回填状态再建行：标题箭头据此定态
            auto panel = BuildBlockRow(*block, rec);
            AddRow(std::move(panel), rec, block->id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.expanded = entry.expanded;
            if (stored.body && stored.headerBtn) {
                BindCollapsible(*block, stored);
                // ★ 展开态要真的落到控件上（`expanded` 标志只影响行高计算，
                //   折叠体的内容可见性由控件自己管）
                if (entry.expanded) stored.body->SetExpanded(true);
            }
            return;
        }
        case TimelineEntry::Kind::Ask: {
            RowRec rec;
            auto panel = BuildAskRow(entry, rec);
            AddRow(std::move(panel), rec, entry.id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
            stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });
            // ★ 已答状态**静默回填**：只置灰、不重发 answer（否则重建 = 重放一次决策，
            //   在 live 会话里就是往 core 又答了一遍 —— 那是会改协议状态的真副作用）。
            // ★★ Step 3：走**同一条**视觉路径（`ApplyAnsweredLook`），答案取
            //   `entry.answer`。第一版这里只有两行 `SetEnabled(false)` ⇒ 重建出来的
            //   权限门与"刚答完"不同形（配色不还原），而 A==B 比的是文字与位标志，
            //   看不见配色 ⇒ 又一次"比对全等但屏幕不一样"（同 0c 的 buildTitle 缺陷）。
            if (entry.answered) {
                stored.answered = true;
                ApplyAnsweredLook(stored, entry.answer, entry.answerAt);
            }
            return;
        }
        case TimelineEntry::Kind::Notice: {
            RowRec rec;
            auto panel = BuildNoticeRow(entry.text, entry.color, rec);
            AddRow(std::move(panel), rec, std::string(), idx);
            return;
        }
    }
}

void HarnessApp::MaterializeRows() {
    for (size_t i = 0; i < m_sess->timeline.size(); ++i) {
        MaterializeOne(i, m_sess->timeline[i]);
    }
}

// Level 1 重建：拆掉转录区的全部行控件，再按模型重放一遍。
//   ★ 只碰 `m_content` 的子树 ⇒ `CountingPanel`/HUD/侧栏/检查器指针全部存活，
//     评估读数不中断（整树重建才会打死计数器，那是 Level 2，留给 0e）。
//   ★ 拆之前必须先把视图状态同步进模型，否则等于"照着一份更旧的模型重建"。
void HarnessApp::RebuildTranscriptView(const std::vector<std::string>* orderOverride) {
    SyncRowViewState();
    m_pendingPromptViews.clear();

    // ★ 重建会让内容重新排一遍 ⇒ 不刻意恢复的话视图会跳回顶部。
    //   "换主题/切会话之后回到刚才读到的地方"是**可用性**要求，不是装饰：
    //   转录一长，重排后跳顶等于让人重新找位置。
    const int keepScroll = m_scroll ? m_scroll->GetScrollOffsetY() : 0;

    // ① 先让**指针表**归零，再动控件树：否则 `Relayout()` 会顺着 `rows` 里
    //    已经指向已删控件的裸指针走（悬垂读）。顺序反了就是 UAF，不是风格问题。
    m_sess->rows.clear();
    m_sess->blockRows.clear();
    if (m_content) {
        // `RemoveChild` 返回所有权；逐个摘掉即完成子树销毁（`Widget::RemoveChild`
        // 内部走 `Window::ForgetSubtree`，焦点/捕获/悬停指针一并清掉）
        while (m_content->GetChildCount() > 0) {
            m_content->RemoveChild(m_content->GetChildAt(0));
        }
    }
    // ★★ P4 C：引导卡挂在 m_content 上，随上面的拆树一并析构——指针必须立刻
    //   归零（末尾 Relayout → RefreshEmptyGuide 会按需重建）。
    m_hud.emptyGuide = nullptr;

    // ② 按模型重放（或按调用方给的**错误**次序重放——那正是负对照要的形状）
    if (!orderOverride) {
        MaterializeRows();
    } else {
        // ★ 负对照专用：**只**照块表建块行（不建通知/问句行、不按条目回填状态）
        //   ——这正是"只拿 `Transcript::m_blocks` 重建视图"这个错误实现的样子。
        //   `timelineIdx` 给一个越界哨兵值：它**不**参与视图状态写回
        //   （负对照要复现的是错误实现，不该顺手把状态写坏）。
        const size_t kNoTimeline = static_cast<size_t>(-1);
        for (const std::string& id : *orderOverride) {
            const Block* block = m_sess->transcript.FindBlock(id);
            if (!block) continue;
            RowRec rec;
            auto panel = BuildBlockRow(*block, rec);
            AddRow(std::move(panel), rec, block->id, kNoTimeline);
            if (m_sess->rows.empty()) continue;
            RowRec& stored = m_sess->rows.back();
            if (stored.body && stored.headerBtn) BindCollapsible(*block, stored);
        }
    }
    MaterializePendingPrompts();

    // ★ 重建后必须自己收尾：重排（几何是新的）再把滚动位置放回去。
    //   让调用方记得补这一步是设计要求里最容易漏的一环，故内置。
    Relayout();
    if (m_scroll) m_scroll->SetContentOffset(0, keepScroll);
}

// 权限门行：**纯构造**（重建可重放）。问句与 id 都来自模型条目，不是来自"刚到的消息"
//   ——重建时那条消息早已不存在，若从消息取就等于"重建不出问句"。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildAskRow(const TimelineEntry& entry, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = kAskBg, .cornerRadius = kRadiusTool,
        .borderWidth = 1.0f, .borderColor = kAskBorder});
    rec.ask = true;
    rec.askId = entry.id;   // P1.5：answer 回执要带 id

    auto label = std::make_unique<ECDI::Label>("权限 " + entry.text);
    rec.label = label.get();
    label->SetTextColor(kAskQInk);
    {
        ECDI::Font f = harness::SansFont(13.0f);
        label->SetFont(f);
    }
    panel->AddChild(std::move(label));

    auto allow = std::make_unique<ECDI::Button>("允许");
    rec.allowBtn = allow.get();
    allow->SetTextColor(kWarnInk);
    {
        ECDI::Font f = harness::SansFont(12.0f);
        allow->SetFont(f);
    }
    allow->SetStyle(ECDI::ButtonStyleOverride{
        .background = kWarn,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kWarn,
        .hoverBackground = kAllowHover});
    panel->AddChild(std::move(allow));

    auto deny = std::make_unique<ECDI::Button>("拒绝");
    rec.denyBtn = deny.get();
    deny->SetTextColor(kDenyInk);
    {
        ECDI::Font f = harness::SansFont(12.0f);
        deny->SetFont(f);
    }
    deny->SetStyle(ECDI::ButtonStyleOverride{
        .background = kBtnDenyBg,
        .border = kAskBorder,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .hoverBackground = kDenyHover});
    panel->AddChild(std::move(deny));

    // ★★ P4 Phase D：已答结论行（预建、隐藏；ApplyAnsweredLook 填充并显示）。
    auto concl = std::make_unique<ECDI::Label>();
    rec.conclusion = concl.get();
    concl->SetVisible(false);
    concl->SetTextColor(kOkDark);
    concl->SetFont(harness::SansFont(12.0f));
    panel->AddChild(std::move(concl));
    return panel;
}

void HarnessApp::CreateAskRow(const json::Value& msg) {
    // ★★ 0c：问句进模型。此前它只存在于这条 json 消息里 ⇒ 任何重建都会**
    //    把权限门变成一问句都没有的空卡**（按钮还在，但不知道在问什么）。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Ask;
    entry.id = msg.GetString("id");
    entry.text = msg.GetString("question");
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildAskRow(m_sess->timeline.back(), rec);
    AddRow(std::move(panel), rec, entry.id, m_sess->timeline.size() - 1);

    RowRec& stored = m_sess->rows.back();
    stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
    stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });

    if (!m_options.autoAnswer.empty()) {
        MarkAnswered(stored, m_options.autoAnswer);   // 无人值守：立即回答（协议 §8.3）
    } else if (m_options.timed) {
        m_sess->waitForAnswer = true;   // timed 回放停在 waiting，直到用户点选
    }
}


// ── ★★ REQ-04 **边界**探针（--hit-probe，2026-10-14）────────────────────
// 目的：把「按钮禁用 ⇒ 点击不达」变成**机读证据**，且**不注入系统输入**。
//
// 为什么不能用 `--state-probe` 代替：那一条读的是 `IsEnabled()`（使能**位**）。
// REQ-04 的边界是「禁用后点击**到不到**」——中间隔着 `Widget::HitTest` 的
// `if (!IsEnabled()) return nullptr;`（`Widget.cpp:195`）。使能位对 ⇒ 命中门控
// **仍可能**不生效（两段独立代码）⇒ 必须单独取证，不能从前者推出后者。
//
// 为什么不用 `SendInput` 注入：注入会移动真实光标、抢前台（用户正在看视频时
// 被抢屏一次）。而**合成 `MouseButtonDown/UpEvent` 直接喂给 `Application::OnEvent`**
// 走的是**完全同一条**内部派发链（`OnMouseButtonDown` → `FindTargetWidget` →
// `rootWidget.HitTest` → 冒泡 → `Button::OnMouseButtonUp` → `RaiseClick`），
// 只是**跳过了 Win32 消息翻译层**（`WindowMessageHandler` 的坐标折算）。
// 本探针要证的正是**命中门控**（在 Application/Widget 层），故这层跳过不影响判据；
// 且坐标本就用 DIP、与翻译层输出同口径。
//
// 四组**受控对照**（同一个点、只改 enabled —— 单变量）：
//   ① as-is            落定后的真实状态（**正对照**：enabled 时必须命中且点击达）
//   ② off-point        同状态、点移到按钮**外侧**（**负对照**：证明坐标确实在起作用）
//   ③ forced-enabled   显式 `SetEnabled(true)`
//   ④ forced-disabled  显式 `SetEnabled(false)`（判据：命中者不再是它 ∧ 点击不达）
// ★ 没有 ①② 这两个对照，「④ 点击不达」无法区分「门控生效」与「探针根本没工作」（条 81b）。


// ── ★★ Step 0c：重建探针（--rebuild-probe）──────────────────────────────
//
// 断言：**照模型重建出来的视图 == 增量构建出来的视图**（逐行、逐字段）。
//   为什么必须机读：少一条通知、错一位次序、丢一个展开态，屏幕上看都像"差不多"。
//   故本探针把每行压成一条可比较的字符串，两侧用**同一个**量具比。
//
// 判别力（0a 的教训）：demo 夹具上「到达序」与「字典序」恰好一致 ⇒ 只用 demo
//   验不出顺序错。故本探针**自带负对照**：再按字典序重建一次，必须报出
//   cot 42 / realistic 218 / scale 1998 / demo 0 处次序错。负对照不报错 = 探针瞎了。


// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
void HarnessApp::SendPrompt(const std::string& text) {
    // ★ --hit-probe：探针合成点击时只计数、零副作用，保持旧读数。
    if (m_hitProbeActive) {
        ++m_hitProbeSendCalls;
        return;
    }
    if (m_sess->writer) {
        (void)PostPrompt(text);
        return;
    }
    AddNoticeRow("[prompt 仅记录] " + text, kDarkMuted);   // 回放：只记录
    Relayout();
    RequestRepaint();
}

std::string HarnessApp::PostPrompt(const std::string& text, bool allowBusyForProbe) {
    if (!m_sess || !m_sess->writer || m_sess->disconnected ||
        m_sess->protocolIncompatible || m_sess->protocolVer < 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        !m_sess->pendingWorkspaceId.empty()) {
        return {};
    }
    if (allowBusyForProbe) {
        if (m_sess->coreState != "running" || HasUnconfirmedPrompt()) return {};
    } else if (m_sess->coreState != "idle" || HasUnconfirmedPrompt()) {
        return {};
    }

    const std::string clientId = "p" + std::to_string(m_sess->nextClientId++);
    // ★★ m09443：发了 prompt ⇒ 临时会话转正。
    m_sess->temporary = false;
    PendingPrompt pending;
    pending.clientId = clientId;
    pending.text = text;
    m_sess->pendingPrompts.push_back(std::move(pending));

    // Materialize and paint the optimistic row before touching the reverse-channel pipe.
    MaterializePendingPrompts();
    Relayout();
    FollowTailIfPinned();
    RefreshHud();
    RequestRepaint();

    if (!m_sess->writer->Prompt(text, clientId)) {
        FailPendingOperations("prompt 写入失败，连接未确认");
    }
    return clientId;
}

bool HarnessApp::HasUnconfirmedPrompt() const {
    if (!m_sess) return false;
    return std::any_of(m_sess->pendingPrompts.begin(), m_sess->pendingPrompts.end(),
                       [](const PendingPrompt& p) { return p.status == "sending"; });
}

void HarnessApp::FailPrompt(const std::string& clientId, const std::string& reason) {
    if (!m_sess || clientId.empty()) return;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.clientId != clientId || pending.status != "sending") continue;
        pending.status = "failed";
        pending.failure = reason.empty() ? "core 未确认" : reason;
        MaterializePendingPrompts();
        Relayout();
        FollowTailIfPinned();
        RefreshHud();
        RequestRepaint();
        return;
    }
}

void HarnessApp::FailPendingOperations(const std::string& reason) {
    if (!m_sess) return;
    m_sess->disconnected = true;
    const std::string detail = reason.empty() ? "连接已断开" : reason;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.status == "sending") {
            pending.status = "failed";
            pending.failure = detail;
        }
    }
    if (!m_sess->pendingWorkspaceId.empty()) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区未确认：" + detail;
    }
    MaterializePendingPrompts();
    Relayout();
    RefreshHud();
    RequestRepaint();
}

bool HarnessApp::RequestWorkspace(const std::string& path, bool initialSync) {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer < 3 ||
        !m_sess->workspaceCap || m_sess->protocolIncompatible || m_sess->disconnected ||
        path.empty() || m_sess->coreState != "idle" ||
        !m_sess->pendingWorkspaceId.empty()) {
        return false;
    }
    if (initialSync && m_sess->workspaceSyncStarted) return false;
    if (initialSync) m_sess->workspaceSyncStarted = true;

    const std::string requestId = "w" + std::to_string(m_sess->nextWorkspaceRequestId++);
    m_sess->pendingWorkspaceId = requestId;
    m_sess->pendingWorkspaceInitialSync = initialSync;
    m_sess->workspaceStatus = "等待 core 确认…";
    RefreshHud();
    if (!m_sess->writer->Workspace(requestId, path)) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区请求发送失败";
        FailPendingOperations("workspace 写入失败，连接未确认");
        return false;
    }
    return true;
}

// ── ★★ 2026-10-20：工作区注册表（模型侧实现）────────────────────────────
// 三条不变量都在这里维护（设计注见 `App.h` 的 `struct Workspace`）：
//   ① **只追加**：`m_workspaces` 的下标即创建序，永不删除、永不重排 ⇒
//      「创建序固定」这条用户裁决不需要额外排序字段，下标本身就是证据；
//   ② **按路径去重**：同一目录的第二个会话复用同一个实体（否则侧栏会画出
//      两个同路径的标题行，而用户看到的会是"我的会话莫名其妙分成两组"）；
//   ③ **只由 core 确认过的路径创建**：`AcquireWorkspace` 的唯一调用点在 workspace
//      ack 的 `ok:true` 分支 ⇒ 注册表里不存在"GUI 自己猜的路径"。
HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

const HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::WorkspaceByPath(const std::string& path) {
    if (path.empty()) return nullptr;
    // ★ 字符串完全相等，不用 `std::filesystem::equivalent()`：入参已经是 core 回显的
    //   规范化路径（§3.9：绝对、`/` 分隔、无尾随 `/`、无 `.`/`..`）。用 equivalent
    //   会把 `D:/a` 与 `d:/A` 判成同一处——那是替 core 改写它自己的规范化结果。
    for (const auto& ws : m_workspaces) {
        if (ws && ws->path == path) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::AcquireWorkspace(const std::string& path) {
    if (path.empty()) return nullptr;
    if (Workspace* existing = WorkspaceByPath(path)) return existing;
    auto ws = std::make_unique<Workspace>();
    // ★ ID 形如 `w1`/`w2`…，与协议侧的 `request_id`（`w1`/`w2`…）**同名但不同域**：
    //   前者是 GUI 模型里的工作区实体，后者是单次请求的关联键。两者都由自增计数器
    //   产生、都从 1 开始，所以一个会话的 `pendingWorkspaceId` 常与它即将获得的
    //   `workspaceId` 字形相似——这不是缺陷（探针读的是 `Workspace::path`），
    //   但**别在代码里把两者互相当作判据**。
    ws->id = "w" + std::to_string(m_nextWorkspaceId++);
    ws->path = path;
    ws->requestedPath = path;
    m_workspaces.push_back(std::move(ws));
    return m_workspaces.back().get();
}

void HarnessApp::AssignSessionWorkspace(Session& session, Workspace* workspace) {
    session.workspaceId = workspace ? workspace->id : std::string();
}

std::string HarnessApp::InheritedWorkspacePath() const {
    if (!m_sess) return {};
    const Workspace* ws = WorkspaceById(m_sess->workspaceId);
    if (!ws) return {};
    // ★ 只读 `Workspace::path`（已确认路径），不读 `Session::workspacePath`：
    //   后者在"已发请求、等 ack"的窗口里可能还留着旧值，而在"确认失败"之后
    //   会保留失败前的值——两种情况都不是"这个会话现在在哪"的可靠答案。
    return ws->path;
}

// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：临时会话（用户裁决 m09443）
// ───────────────────────────────────────────────────────────────────────
// 从工作区标题行的「＋」按钮拉起的会话是**临时**的：它还没有执行过任何
// 任务（没发过 prompt）。一旦用户切走（`SwitchToSession` 到另一个会话），
// 临时会话就被删除——因为它的存在理由（"在这个工作区开一个空会话"）
// 已经被放弃。一旦发了 prompt（`PostPrompt` 成功），它转为正式会话。
// ═══════════════════════════════════════════════════════════════════════

void HarnessApp::NewTemporarySession(const std::string& workspaceId) {
    // 只允许 live 模式（有 core 才能"执行任务"）。
    if (m_options.corePath.empty()) return;
    const Workspace* ws = WorkspaceById(workspaceId);
    if (!ws) return;

    Session* created = CreateSessionFromOptions();
    if (!created) {
        AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }
    created->label = "会话 " + std::to_string(m_sessions.size());
    created->temporary = true;
    // 外键在创建时就设好——路径已经确认过了，工作区实体已经存在，
    // 不需要等 ack（与 `MaybeStartWorkspaceSync` 的继承口径不同：
    // 那里是"还没确认"，这里是"已经确认"）。
    created->workspaceId = workspaceId;
    created->workspacePath = ws->path;
    created->workspaceSyncComplete = true;
    created->workspaceStatus = "已由 core 确认";

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);
    Session& s = *m_sessions[idx];
    if (s.writer) s.writer->Hello("0.0.1");
    if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    m_sidebarDirty = true;
}

void HarnessApp::MaybeDeleteTemporarySession() {
    if (!m_sess || !m_sess->temporary) return;
    // ★ 只删"没发过 prompt"的临时会话：发过 prompt 的已经转正（`PostPrompt`
    //   里把 `temporary` 清掉了）。
    //   ★ 用 `m_activeIndex` 定位：此时 `m_sess` 还指着旧会话（`SwitchToSession`
    //     在改 `m_activeIndex` 之前调用本函数）。
    const size_t idx = m_activeIndex;
    if (idx >= m_sessions.size()) return;
    // 与 `DeleteSessionAt` 同一条纪律：禁删到零会话。
    if (m_sessions.size() <= 1) return;
    // 删除前不需要先切邻居——`SwitchToSession` 马上就会改 `m_activeIndex`。
    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(idx));
    // 修正活动下标（与 `DeleteSessionAt` 同逻辑）。
    if (m_activeIndex > idx) --m_activeIndex;
    if (m_activeIndex >= m_sessions.size()) m_activeIndex = m_sessions.size() - 1;
    m_sess = m_sessions[m_activeIndex].get();
    m_sidebarDirty = true;
    m_hudDirty = true;
}

// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容
// ───────────────────────────────────────────────────────────────────────
// 数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
// 回放会话没有工作区 ⇒ 此段显示「无工作区」。
// ★ 只列一级（不递归）：侧栏宽度 172px，递归树会立刻溢出；且用户要的是
//   「这个工作区里有什么」，不是文件浏览器。
// ═══════════════════════════════════════════════════════════════════════

void HarnessApp::RebuildProjectList() {
    if (!m_hud.projectList) return;
    ECDI::Widget& content = m_hud.projectList->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    // 数据源：活动会话的已确认工作区路径。
    std::string wsPath;
    if (m_sess && !m_sess->workspacePath.empty()) {
        wsPath = m_sess->workspacePath;
    }

    int y = 0;
    const int rowH = 16;
    const int gap = 2;

    if (wsPath.empty()) {
        // 回放会话 / 无工作区：诚实显示。
        auto lbl = uikit::Label("无工作区", kSidebarListW, rowH, kDarkMuted,
                                harness::kFontMicro);
        lbl->SetPosition(0, y);
        content.AddChild(std::move(lbl));
        y += rowH + gap;
    } else {
        // 列一级目录内容。
        std::error_code ec;
        std::vector<std::string> dirs;
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::path(wsPath), ec)) {
            const std::string name = entry.path().filename().generic_string();
            if (name.empty()) continue;
            if (entry.is_directory(ec)) {
                dirs.push_back(name + "/");
            } else {
                files.push_back(name);
            }
            ec.clear();
        }
        // 目录在前、文件在后，各自按名称排序（确定性）。
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        for (const auto& d : dirs) {
            auto lbl = uikit::Label("▽ " + d, kSidebarListW, rowH, kDarkInk,
                                    harness::kFontMicro, true);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
        for (const auto& f : files) {
            auto lbl = uikit::Label("· " + f, kSidebarListW, rowH, kDarkMuted,
                                    harness::kFontMicro, true);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
        if (dirs.empty() && files.empty()) {
            auto lbl = uikit::Label("（空目录）", kSidebarListW, rowH, kDarkMuted,
                                    harness::kFontMicro);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
    }

    const int totalH = y > 0 ? y - gap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * (rowH + gap) - gap);
    m_hud.projectList->SetSize(kSidebarListW, std::max(visibleH, rowH));
    m_hud.projectList->SetContentExtent(kSidebarListW, std::max(totalH, rowH));
    m_hud.projectList->SetContentOffset(0, m_hud.projectList->GetScrollOffsetY());


    if (ECDI::Widget* parent = m_hud.projectList->GetParent()) parent->Arrange();
}

void HarnessApp::MaybeStartWorkspaceSync() {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer < 3 ||
        !m_sess->workspaceCap || m_sess->workspaceSyncStarted ||
        m_sess->disconnected || m_sess->coreState != "idle") {
        return;
    }
    std::error_code ec;
    // ★★ 2026-10-20（用户裁决 m08553）：新会话**继承活动会话的工作区**。
    //   改动前这里恒用 `std::filesystem::current_path()`，即 GUI 进程的 cwd；
    //   而 `ChildSource::Start` 的 `CreateProcessW` 又不传 `lpCurrentDirectory`
    //   （子进程也继承 GUI cwd）⇒ 用户在会话 A 里把工作区切到 `D:/Project` 之后
    //   点「＋ 新建会话」，会话 B 会**静默**回到启动 GUI 的那个目录。屏幕上看不出
    //   这件事（两行会话长得一样、工作区段显示的是活动会话的值），只有对照
    //   core 的 stderr 才能发现——所以这条不是"体验优化"，是**纠错**。
    std::string initialPath = m_sess->workspaceInheritedPath;
    if (initialPath.empty()) {
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (ec) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "无法读取启动工作目录：" + ec.message();
            RefreshHud();
            return;
        }
        initialPath = PathToUtf8(cwd);
    }
    // ★ 继承路径只消费一次：若这次请求被 core 拒绝，**不**自动改用 cwd 重试。
    //   被拒不等于"路径不存在"（core 可能因自己的原因拒），而擅自换一个目录重问
    //   会让屏幕上的路径与用户以为的那个不同——那正是 §3.9 要避免的"GUI 猜"。
    m_sess->workspaceInheritedPath.clear();
    if (!RequestWorkspace(initialPath, true)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "启动工作区请求未能发送";
            RefreshHud();
        }
    }
}

void HarnessApp::MaybeSendAutoPrompt() {
    if (!m_sess || m_options.autoPrompt.empty() || m_sess->autoPromptSent ||
        m_options.workspaceProbe) {
        return;
    }
    if (!m_sess->writer) {
        m_sess->autoPromptSent = true;
        SendPrompt(m_options.autoPrompt);
        return;
    }
    if (m_sess->protocolVer < 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty() ||
        m_sess->disconnected) {
        return;
    }
    m_sess->autoPromptSent = true;
    (void)PostPrompt(m_options.autoPrompt);
}

void HarnessApp::ChooseWorkspaceFolder() {
    if (!m_sess || !m_sess->writer || !m_sess->workspaceCap ||
        m_sess->protocolVer < 3 || m_sess->disconnected ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        // ★★ P4 修复（实测反馈「直接进去选择文件夹不能选择」）：
        //   ① 首启空壳会话（无 live）此前整段**静默 return**——按钮看起来是死的。
        //     现在如实说原因（同 RequestModelSwitch 的「不是 live」风格）。
        //   ② `protocolVer != 3` 是 P3.6 升 v4 后没更新的拦路守卫——v4 core 的
        //     live 会话同样被静默拦死。workspace 能力 v3 引入、v4 保留 ⇒ 改 `< 3`。
        if (m_sess && !m_sess->writer) {
            AddNoticeRow("[选择工作区需要已连接的 live 会话——先在设置里连接提供商]",
                         kDarkMuted);
            Relayout();
            RequestRepaint();
        }
        return;
    }
    BROWSEINFOW browse{};
    browse.lpszTitle = L"选择工作区文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST item = SHBrowseForFolderW(&browse);
    if (!item) return;
    wchar_t selected[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(item, selected) != FALSE;
    CoTaskMemFree(item);
    if (!ok) {
        m_sess->workspaceStatus = "无法读取所选文件夹";
        RefreshHud();
        return;
    }
    const std::string path = Utf8FromWidePath(selected);
    if (path.empty() || !RequestWorkspace(path, false)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceStatus = "工作区请求未能发送";
            RefreshHud();
        }
    }
}

// ── ★★ 2026-10-20：中栏头 `↗`（导出）/ `···`（菜单）─────────────

// ★★ 2026-10-21：导出拆成**三段纯函数**，因为原来它是一整个「弹对话框 + 写文件」。
//   不拆的话，导出这条路径**无法被探针覆盖**：`GetSaveFileNameW` 是模态死循环，
//   合成点击一旦点上去，探针就卡在对话框里，整个 `--menu-probe` 会挂死。
//   拆开之后各自有判据：
//     · `BuildTranscriptExportText()` —— 纯函数，返回**将要写出的文本**。
//       判据：行数/内容与模型对得上、通知行真的在里面（负对照：块表里没有它们）。
//     · `WriteTranscriptExport()` —— 只负责落盘，返回 false 时给错误串。
//     · `ExportTranscript()` —— 只剩「选路径 + 调用上面两个 + 说一声」。
//   ★ 仍然**没被覆盖**的是那个对话框本身（`GetSaveFileNameW` 的调用点）。
//     探针走 `m_menuProbeExportPath` 直接给它一个路径，绕开对话框。
//     这是**诚实的缺口**，写在这里而不是假装探针覆盖了保存对话框。

// 把活动会话的转录渲染成文本。**只读**：不碰模型、不碰协议、不改 seq。
// ★ 导出源是 `timeline`（视图看到的那一份），**不是** `Transcript::m_blocks`。
//   两者不等价：`m_blocks` 只有协议块，而 `timeline` 还含通知行（`[error]` /
//   `[协议不兼容]` / I4 缺口告警）与问句行。导出后者才叫「把屏幕存下来」；
//   导出前者会把「这次运行可不可信」的全部读数默默丢掉。
std::string HarnessApp::BuildTranscriptExportText() const {
    if (!m_sess) return {};
    const std::vector<TimelineEntry>& entries = m_sess->timeline;

    std::string out;
    out.reserve(4096);
    out += "# harness 转录\n";
    out += "# 会话：" + m_sess->label + "\n";
    out += "# 来源：" + m_sess->originKind +
           (m_sess->originPath.empty() ? std::string() : (" " + PathToUtf8(m_sess->originPath))) +
           "\n";
    out += "# 契约：protocol " + std::to_string(m_sess->protocolVer) + "\n";
    out += "# 违规：" + std::to_string(m_sess->transcript.Violations()) +
           "，seq 缺口：" + std::to_string(m_sess->transcript.SeqGaps()) + "\n";
    out += "\n";
    for (const TimelineEntry& e : entries) {
        switch (e.kind) {
        case TimelineEntry::Kind::Block: {
            const Block* block = m_sess->transcript.FindBlock(e.id);
            if (!block) {
                out += "[块 已丢失] " + e.id + "\n\n";
                break;
            }
            out += "[" + block->kind + "]";
            if (!block->name.empty()) out += " " + block->name;
            out += "\n";
            if (!block->text.empty()) out += block->text + "\n";
            if (block->kind == "tool_call" && !block->argsPretty.empty()) {
                out += block->argsPretty + "\n";
            }
            if (!block->summary.empty() && block->summary != block->text) {
                out += "摘要：" + block->summary + "\n";
            }
            out += "\n";
            break;
        }
        case TimelineEntry::Kind::Ask:
            out += "[ask] " + e.text + "\n";
            if (!e.answer.empty()) out += "  答：" + e.answer + "\n";
            out += "\n";
            break;
        case TimelineEntry::Kind::Notice:
            out += e.text + "\n";
            break;
        }
    }
    return out;
}

bool HarnessApp::WriteTranscriptExport(const std::filesystem::path& path, std::string* error) {
    if (!m_sess) return false;
    const std::string out = BuildTranscriptExportText();
    // ★ 二进制模式 + 手动 `\n`：`std::ofstream` 在 Windows 文本模式下会把 `\n`
    //   翻译成 `\r\n`，那会让导出文件与仓库里 `*.jsonl` 的无 BOM+LF 纪律不一致
    //   （虽然导出物不入库，但「同一产品的文件行尾应当一致」）。
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "无法写入文件";
        return false;
    }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    const bool wrote = static_cast<bool>(f);
    f.close();
    if (!wrote && error) *error = "写入中断";
    return wrote;
}

bool HarnessApp::ExportTranscript() {
    if (!m_sess) return false;

    if (m_sess->timeline.empty()) {
        // 空会话不弹对话框：弹了也只能存个空文件，不如在转录里说一声。
        AddNoticeRow("[导出] 当前会话没有内容", kDarkMuted);
        Relayout();
        RequestRepaint();
        return false;
    }

    // 默认文件名：会话 label + 时间戳。用**本地时间到分钟**——两次导出不互相覆盖，
    // 同时文件名仍可读（用 epoch 秒就只剩一串数字了）。
    std::string baseName = m_sess->label.empty() ? std::string("transcript") : m_sess->label;
    // 文件名里不能有路径分隔符/冒号（label 可能是 fixture 路径派生出来的）。
    for (char& c : baseName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string stamp;
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char buf[32]{};
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M", &tmBuf);
        stamp = buf;
    }
    const std::string suggested = baseName + "-" + stamp + ".txt";

    // ★★ 2026-10-21：探针用**定死的目标路径**绕开保存对话框。
    //   理由见 `BuildTranscriptExportText` 头注：`GetSaveFileNameW` 是模态循环，
    //   合成点击走上去会把整个探针挂死。这不是"为了测试改产品逻辑"——
    //   探针走的是**同一个写盘函数**，绕过去的只有"路径从哪儿来"。
    //   ★ 只在 `--menu-probe` 且已经指定路径时生效（默认空 ⇒ 零行为变化）。
    std::filesystem::path target;
    if (!m_menuProbeExportPath.empty()) {
        target = m_menuProbeExportPath;
    } else {
        // 保存对话框。`OFN_OVERWRITEPROMPT` 让覆盖变成用户的选择而不是我们默默做决定。
        wchar_t fileBuf[1024]{};
        {
            const std::wstring wSuggested = WideFromUtf8Path(suggested);
            const size_t n = std::min(wSuggested.size(), static_cast<size_t>(1023));
            std::copy(wSuggested.begin(), wSuggested.begin() + static_cast<std::ptrdiff_t>(n),
                      fileBuf);
        }
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        // ★ `hwndOwner = nullptr`：ECDI 的 `PlatformWindow` **不暴露**原生窗口句柄
        //   （`grep Handle|NativeHandle Platform/PlatformWindow.h` 零命中），所以无法把
        //   对话框挂到主窗口。后果是对话框不模态于主窗口（用户可以在它开着时操作主窗口）
        //   —— 可接受：保存对话框自带自己的模态循环，不会因此错乱。
        //   ★ 不为此去 reflecion/GetActiveWindow()：那是靠推断拿句柄，拿错了会让
        //     对话框挂到别的程序的窗口上。诚实的 `nullptr` 比猜一个句柄安全。
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"\u6587\u672c\u6587\u4ef6 (*.txt)\0*.txt\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.lpstrTitle = L"\u5bfc\u51fa\u8f6c\u5f55";
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) {
            // 用户取消。★ 不写通知行：取消不是事件，往转录里塞「已取消」会把
            //   用户的视图弄脏（而且下次导出会多一行）。
            return false;
        }
        target = Utf8FromWidePath(fileBuf);
    }

    std::string error;
    if (!WriteTranscriptExport(target, &error)) {
        AddNoticeRow("[导出失败] " + error, kErrorDark);
        Relayout();
        RequestRepaint();
        return false;
    }

    // 成功也要**说一声**：保存对话框关掉后屏幕上看不出任何变化，
    //   用户只能去文件管理器里找。通知行是唯一能确认「真的导出了」的反馈。
    AddNoticeRow("[已导出] " + PathToUtf8(target), kDarkMuted);
    Relayout();
    RequestRepaint();
    return true;
}

void HarnessApp::ToggleMoreMenu() { SetMoreMenuOpen(!m_moreMenuOpen); }

void HarnessApp::SetMoreMenuOpen(bool open) {
    // ★ 幂等：同值不做事。否则每次重建都 Arrange 一次就白掉一帧。
    if (m_moreMenuOpen == open) return;
    m_moreMenuOpen = open;
    if (m_hud.moreMenu) {
        m_hud.moreMenu->SetVisible(open);
        // ★ 必须重排**父容器**（`counting`）：布局跳过隐藏子（C-VIS-1），
        //   但「跳过」只在 Arrange 时发生。不 Arrange 的话菜单条消失后
        //   那个 34px 的槽位会留着——转录区凭空缩进一块。
        if (ECDI::Widget* parent = m_hud.moreMenu->GetParent()) parent->Arrange();
        Relayout();
        RequestRepaint();
    }
}

// ── ★★ P3.5：设置面板 / 无参启动 / 凭据注入（规格 §8.5 D4–D7）────────

void HarnessApp::ApplySettingsVisibility() {
    // 唯一的投影路径：BuildUi 首帧与 RebuildUi 重建都走这里——
    // 「投影只存在于一条路径」是 2026-10-21 起始幻影（34px 空洞）的教训。
    if (!m_hud.settingsPanel) return;
    m_hud.settingsPanel->SetVisible(m_settingsVisible);
    if (m_hud.settingsMask) m_hud.settingsMask->SetVisible(m_settingsVisible);
    // ★★ P4 F（行为变化点）：不再隐藏 scroll/composer——设置是浮在视口上的
    //   模态层，流式输出在半透明遮罩后继续可见/继续滚动（用户验收过原型）。
    //   --settings-probe 的「腾槽」判据已同步迁移为「scroll 必须仍可见」。
    // 三件套互斥（menu/settings）：开设置合菜单——菜单还是槽位控件，两者同开
    //   会一个占槽一个浮层，视觉关系说不清。
    if (m_settingsVisible && m_moreMenuOpen) SetMoreMenuOpen(false);
    LayoutSettingsOverlay();
}

// ★★ P4 F：遮罩 + 居中 sheet 的几何。★ 修复（实测反馈）：**遮罩盖全窗**
//   （mask+sheet 挂 root 末尾，画过 caption/counting 全部；此前挂 m_scroll
//   只盖转录视口）——sheet 尺寸改按 root 算。浮层无父布局 ⇒ SetSize 后必须
//   自己 Arrange（ArrangeInternal 递归驱动整棵子树布局——字段区由此摆位），
//   **再 UpdateContentExtent**（字段包围盒 → content extent；漏了它 extent=0，
//   字段区不显示——首版实测反馈的根因）。
//   ★ root.Arrange()（HarnessRootLayout）**跳过浮层子**——浮层几何完全归
//   本函数管，布局与浮层互不干扰。
void HarnessApp::LayoutSettingsOverlay() {
    if (!m_hud.settingsPanel) return;
    ECDI::Widget& root = m_window->GetRootWidget();
    const int rw = root.GetWidth();
    const int rh = root.GetHeight();
    if (m_hud.settingsMask) {
        m_hud.settingsMask->SetSize(rw, rh);
        m_hud.settingsMask->SetPosition(0, 0);
    }
    const int w = std::clamp(rw - 80, 280, 620);
    const int h = std::clamp(rh - 80, 240, 560);
    m_hud.settingsPanel->SetSize(w, h);
    m_hud.settingsPanel->SetPosition((rw - w) / 2, (rh - h) / 2);
    m_hud.settingsPanel->Arrange();
    if (m_hud.settingsSv) {
        m_hud.settingsSv->UpdateContentExtent();   // 字段包围盒 → 滚动范围
    }
}

void HarnessApp::SetSettingsVisible(bool visible) {
    if (m_settingsVisible == visible &&
        (!m_hud.settingsPanel || m_hud.settingsPanel->IsVisible() == visible)) {
        return;   // 幂等（同 SetSidebarVisible）
    }
    m_settingsVisible = visible;   // ① 模型侧先落
    ApplySettingsVisibility();     // ② 再投影
    Relayout();
    RequestRepaint();
}

bool HarnessApp::ResolveCoreExecutable(std::filesystem::path& out) const {
    namespace fs = std::filesystem;
    // ① 配置覆盖（保存时把解析结果写回配置 ⇒ 后续启动不再依赖 cwd）
    if (!m_appConfig.corePath.empty()) {
        if (fs::exists(m_appConfig.corePath)) {
            out = m_appConfig.corePath;
            return true;
        }
        return false;
    }
    // ②③ 从 cwd 与 exe 目录**向上找祖先**：core/bin/ecdi-core.exe。
    //   ★ 为什么必须向上走（2026-10-24 用户实测抓到）：双击 exe 的默认 cwd =
    //     exe 所在目录（gui/build/Release），仓库根布局的 core/bin/ 在**三级之上**
    //     ⇒ 只查 cwd 和 exeDir 两个固定点，双击场景永远找不到 ⇒ 自动连接失败、
    //     首启面板里保存也失败（同一函数）⇒ "会话没有输入框"的死胡同。
    //     祖先上溯（≤6 级）同时覆盖：仓库根跑 / Release 目录双击 / 安装布局平铺。
    auto findUpward = [](const fs::path& start) -> std::filesystem::path {
        fs::path dir = fs::absolute(start);
        for (int i = 0; i < 6 && !dir.empty(); ++i) {
            const fs::path cand = dir / "core" / "bin" / "ecdi-core.exe";
            std::error_code ec;
            if (fs::exists(cand, ec) && !fs::is_directory(cand, ec)) {
                return fs::weakly_canonical(cand, ec);
            }
            const fs::path flat = dir / "ecdi-core.exe";
            if (fs::exists(flat, ec) && !fs::is_directory(flat, ec)) {
                return fs::weakly_canonical(flat, ec);
            }
            const fs::path parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
        return {};
    };
    if (std::filesystem::path found = findUpward("."); !found.empty()) {
        out = found;
        return true;
    }
    wchar_t exeDirBuf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exeDirBuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        if (std::filesystem::path found = findUpward(fs::path(exeDirBuf).parent_path());
            !found.empty()) {
            out = found;
            return true;
        }
    }
    return false;
}

bool HarnessApp::TryAutoConnectFromConfig() {
    std::string error;
    if (!m_appConfig.Load(error)) {
        // 文件不存在 = 干净首启；存在但坏 = 要说清楚（非敏感）
        ECDI::Logger::Log(ECDI::LogLevel::Warning, ECDI::UTF8ToWide("AppConfig: " + error));
        return false;
    }
    CacheProviderKeys();
    // 默认提供商 = defaultProviderId（v2）/ 迁移后的唯一提供商（v1）。
    const ProviderConfig* def = m_appConfig.DefaultProvider();
    if (!def) return false;
    auto keyIt = m_providerKeys.find(def->id);
    if (keyIt == m_providerKeys.end()) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("no credential for default provider " + def->id));
        return false;
    }
    std::filesystem::path coreExe;
    if (!ResolveCoreExecutable(coreExe)) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ecdi-core.exe not found (config/cwd/exe ancestors)"));
        return false;
    }
    // 把解析结果回写配置（下次启动不依赖 cwd）；失败不致命——本次仍可连。
    m_appConfig.corePath = coreExe.generic_string();
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning, ECDI::UTF8ToWide("AppConfig save: " + saveError));
    }
    m_coreApiKey = keyIt->second;
    m_options.corePath = coreExe;
    m_options.coreArgs = "--base-url=" + def->baseUrl +
                         " --model=" + def->model +
                         " --provider-id=" + def->id;
    if (def->context > 0) {
        m_options.coreArgs += " --context=" + std::to_string(def->context);
    }
    if (def->maxOutput > 0) {
        m_options.coreArgs += " --max-output=" + std::to_string(def->maxOutput);
    }
    m_options.fixtureName = def->model + " (live)";
    return true;
}

void HarnessApp::OnSettingsSave() {
    if (!m_hud.settingsBase || !m_hud.settingsKey || !m_hud.settingsModel ||
        !m_hud.settingsHint) {
        return;
    }
    const auto setHint = [this](const std::string& text, const ECDI::Color& color) {
        m_hud.settingsHint->SetText(text);
        m_hud.settingsHint->SetTextColor(color);
        RequestRepaint();
    };
    const std::string baseUrl = m_hud.settingsBase->GetText();
    const std::string key = m_hud.settingsKey->GetText();
    const std::string model = m_hud.settingsModel->GetText();
    const std::string contextText = m_hud.settingsContext ? m_hud.settingsContext->GetText() : "";
    const std::string maxOutText = m_hud.settingsMaxOut ? m_hud.settingsMaxOut->GetText() : "";
    if (baseUrl.empty() || model.empty()) {
        setHint("BaseURL 和模型名不能为空", kErrorDark);
        return;
    }
    int context = 0;
    if (!contextText.empty()) context = std::atoi(contextText.c_str());
    int maxOutput = 0;
    if (!maxOutText.empty()) maxOutput = std::atoi(maxOutText.c_str());
    const std::string providerId = AppConfig::ProviderIdFromBaseUrl(baseUrl);
    const std::string target = AppConfig::CredentialTargetFor(providerId);

    // ① 凭据：key 非空 → 覆盖写；key 为空且已有 → 保持不变（轮换语义）。
    if (!key.empty()) {
        std::string credError;
        if (!creds::Write(ECDI::UTF8ToWide(target), key, credError)) {
            setHint("凭据写入失败：" + credError, kErrorDark);
            return;
        }
    } else if (!creds::Exists(ECDI::UTF8ToWide(target))) {
        setHint("API Key 为空（凭据库中也没有该提供商的记录）", kErrorDark);
        return;
    }

    // ② 解析 core 可执行文件（结果写进配置 ⇒ 下次无参启动不依赖 cwd）。
    std::filesystem::path coreExe;
    if (!ResolveCoreExecutable(coreExe)) {
        setHint("找不到 ecdi-core.exe（先构建：go build -o core/bin/ecdi-core.exe ./cmd/ecdi-core）",
                kErrorDark);
        return;
    }

    // ③ 写配置（v2 多提供商 Upsert；结构上无密钥字段——AppConfig.h 防泄漏注释）。
    ProviderConfig pc;
    pc.id = providerId;
    pc.baseUrl = baseUrl;
    pc.model = model;
    pc.context = context;
    pc.maxOutput = maxOutput;
    pc.credentialTarget = target;
    m_appConfig.Upsert(pc);
    m_appConfig.defaultProviderId = providerId;
    if (!m_coreApiKey.empty() && m_providerKeys.find(providerId) == m_providerKeys.end()) {
        // 旧配置迁移路径：m_coreApiKey 属于原默认提供商时不得张冠李戴
    }
    if (!key.empty()) m_providerKeys[providerId] = key;
    m_appConfig.corePath = coreExe.generic_string();
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        setHint("配置写入失败：" + saveError, kErrorDark);
        return;
    }
    m_editingProviderId = providerId;

    // ④ 连接：无 live 会话 ⇒ 新建（env 注入走统一路径）；已有支持热切换的
    //    live 会话 ⇒ 发 model 切换请求（I11：排队语义由 RequestModelSwitch 处理）。
    const bool hasLive = m_sess && m_sess->originKind == "live" && m_sess->writer;
    if (hasLive && m_sess->hasModelCap) {
        auto keyIt = m_providerKeys.find(providerId);
        if (keyIt != m_providerKeys.end()) {
            m_coreApiKey = keyIt->second;   // 新 provider 成为默认 ⇒ ECDI_CORE_KEY 跟随
        }
        setHint("已保存；正在切换到 " + providerId + " / " + model, kDarkMuted);
        SetSettingsVisible(false);
        RequestModelSwitch(providerId);
        return;
    }
    if (hasLive && !m_sess->hasModelCap) {
        setHint("已保存；当前 core 不支持热切换（cap 未声明），重启会话后生效", kDarkMuted);
        SetSettingsVisible(false);
        return;
    }
    {
        auto keyIt = m_providerKeys.find(providerId);
        if (keyIt != m_providerKeys.end()) m_coreApiKey = keyIt->second;
        m_options.corePath = coreExe;
        m_options.coreArgs = "--base-url=" + baseUrl + " --model=" + model +
                             " --provider-id=" + providerId;
        if (context > 0) m_options.coreArgs += " --context=" + std::to_string(context);
        if (maxOutput > 0) m_options.coreArgs += " --max-output=" + std::to_string(maxOutput);
        Session* live = CreateSessionFromOptions();
        if (!live) {
            setHint("core 启动失败（见 core-stderr 日志）", kErrorDark);
            return;
        }
        SwitchToSessionByPtr(live);
    }
    setHint("已保存并连接：" + providerId + " / " + model, kDarkMuted);
    SetSettingsVisible(false);
    m_modelListDirty = true;
}

// ── ★★ P3.6（协议 v4）：模型热切换 ───────────────────────────────

std::string HarnessApp::EnvSanitizeProviderId(const std::string& id) {
    // 与 core 的 sanitizeProviderID 同一条规则（非字母数字 → '_'，大写）。
    // 两份实现是必须的（环境变量名不能带 '.'），规则只此一条，注释互指。
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9');
        out.push_back(ok ? static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c) : '_');
    }
    return out;
}

void HarnessApp::CacheProviderKeys() {
    // 凭据库 → m_providerKeys（已有缓存的不覆盖——设置面板刚写入的值更新）。
    for (const auto& pc : m_appConfig.providers) {
        if (m_providerKeys.count(pc.id)) continue;
        std::string key;
        std::string err;
        if (creds::Read(ECDI::UTF8ToWide(pc.credentialTarget), key, err)) {
            m_providerKeys[pc.id] = key;
        }
    }
}

void HarnessApp::RebuildModelList() {
    if (!m_hud.modelList) return;
    while (m_hud.modelList->GetChildCount() > 0) {
        m_hud.modelList->RemoveChild(m_hud.modelList->GetChildAt(0));
    }
    // ★★ P4 H：右栏改**只读卡**（当前 提供商/模型/容量/输出上限）——切换入口
    //   收敛到顶栏 chip 浮窗（P3.6 的逐行切换列表退役；两处入口必然漂移）。
    const Session* s = m_sess;
    auto addRow = [this](const std::string& label, const std::string& value) {
        auto row = std::make_unique<ECDI::Panel>();
        row->SetSize(200, 18);
        auto l = std::make_unique<ECDI::Label>(label);
        l->SetTextColor(kDarkMuted);
        l->SetFont(harness::SansFont(harness::kFontMicro));
        l->SetSize(70, 16);
        l->SetPosition(0, 1);
        row->AddChild(std::move(l));
        auto v = std::make_unique<ECDI::Label>(value);
        v->SetTextColor(kDarkInk);
        v->SetFont(harness::SansFont(harness::kFontMicro));
        v->SetSize(130, 16);
        v->SetPosition(72, 1);
        row->AddChild(std::move(v));
        m_hud.modelList->AddChild(std::move(row));
    };
    addRow("提供商", s && !s->providerId.empty() ? s->providerId : "—");
    addRow("模型", s && !s->modelName.empty() ? s->modelName : "—");
    if (s && s->ctxCapacity > 0) {
        addRow("容量", std::to_string(s->ctxCapacity) + " tok");
    } else {
        addRow("容量", "—");
    }
    const ProviderConfig* pc =
        s ? m_appConfig.Find(s->providerId) : nullptr;
    addRow("输出上限",
           pc && pc->maxOutput > 0 ? std::to_string(pc->maxOutput) : "—");
    if (ECDI::Widget* parent = m_hud.modelList->GetParent()) parent->Arrange();
    RequestRepaint();
}

// ── ★★ P4 Phase H（2026-10-25）：顶栏 chip + 模型下拉浮窗 ──────────────

void HarnessApp::SetModelPopupOpen(bool open) {
    if (m_modelPopupOpen == open &&
        (!m_hud.modelPopup || m_hud.modelPopup->IsVisible() == open)) {
        return;   // 幂等（同 SetSidebarVisible）
    }
    m_modelPopupOpen = open;
    if (!m_hud.modelPopup) return;
    m_hud.modelPopup->SetVisible(open);
    if (open) {
        if (m_moreMenuOpen) SetMoreMenuOpen(false);   // 互斥：两处下拉不同开
        if (m_hud.modelSearch) m_hud.modelSearch->SetText("");
        RebuildModelPopupList();
        // 顶部下拉、右上角对齐（视口坐标系）。
        m_hud.modelPopup->SetPosition(
            std::max(0, m_scroll->GetWidth() - m_hud.modelPopup->GetWidth() - 12), 0);
    }
}

void HarnessApp::RebuildModelPopupList() {
    if (!m_hud.modelPopupList) return;
    while (m_hud.modelPopupList->GetChildCount() > 0) {
        m_hud.modelPopupList->RemoveChild(m_hud.modelPopupList->GetChildAt(0));
    }
    const std::string q = m_hud.modelSearch ? m_hud.modelSearch->GetText() : "";
    // 同名检测：跨提供商相同 model 名 ⇒ 标黄 + 「同名×N」（同名跨家切换容易
    // 误点，这是唯一的防呆读数）。
    std::map<std::string, int> modelCounts;
    for (const auto& pc : m_appConfig.providers) ++modelCounts[pc.model];
    const std::string active = m_sess ? m_sess->providerId : std::string();
    int y = 0;
    int rows = 0;
    for (const auto& pc : m_appConfig.providers) {
        const bool idHit = q.empty() ||
                           pc.id.find(q) != std::string::npos ||
                           pc.model.find(q) != std::string::npos;
        if (!idHit) continue;
        auto group = std::make_unique<ECDI::Label>(
            (pc.id == active ? "● " : "") + pc.id);
        group->SetTextColor(kDarkMuted);
        group->SetFont(harness::SansFont(harness::kFontMicro));
        group->SetSize(320, 16);
        group->SetPosition(0, y);
        y += 18;
        m_hud.modelPopupList->AddChild(std::move(group));
        const int dup = modelCounts[pc.model];
        std::string text = pc.model;
        if (pc.context > 0) {
            text += " · " + std::to_string(pc.context / 1000) + "K";
        }
        if (dup > 1) text += "  同名×" + std::to_string(dup);
        auto row = std::make_unique<ECDI::Button>(text);
        row->SetTextColor(dup > 1 ? kWarn : kDarkInk);   // 同名标黄
        row->SetFont(harness::SansFont(harness::kFontSmall));
        row->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kToolHeaderHover});
        row->SetSize(320, 24);
        row->SetPosition(0, y);
        y += 28;
        const std::string pid = pc.id;
        row->SetOnClick([this, pid] {
            SetModelPopupOpen(false);
            RequestModelSwitch(pid);
        });
        m_hud.modelPopupList->AddChild(std::move(row));
        ++rows;
    }
    if (rows == 0) {
        auto l = std::make_unique<ECDI::Label>("（无匹配——用设置添加提供商）");
        l->SetTextColor(kDarkMuted);
        l->SetFont(harness::SansFont(harness::kFontSmall));
        l->SetSize(320, 20);
        l->SetPosition(0, y);
        m_hud.modelPopupList->AddChild(std::move(l));
    }
    RequestRepaint();
}

void HarnessApp::UpdateModelChip() {
    if (!m_hud.modelChip || !m_sess) return;
    std::string text = m_sess->providerId.empty()
                           ? "—"
                           : (m_sess->providerId + " › " +
                              (m_sess->modelName.empty() ? "?" : m_sess->modelName));
    // ★ 用量常驻（v4 分母 ctxCapacity——P3.6 只写不读的接线在这里补上）：
    //   分子与右栏百分比同口径（usageIn）。
    if (m_sess->ctxCapacity > 0 && m_sess->usageIn >= 0) {
        const long long pct =
            (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity;
        text += " · " + std::to_string(pct) + "%";
    }
    text += " ▾";
    m_hud.modelChip->SetText(text);
}

void HarnessApp::RebuildSettingsProviderList() {
    if (!m_hud.settingsProviderList) return;
    while (m_hud.settingsProviderList->GetChildCount() > 0) {
        m_hud.settingsProviderList->RemoveChild(m_hud.settingsProviderList->GetChildAt(0));
    }
    const std::string liveId = m_sess ? m_sess->providerId : std::string();
    for (const auto& pc : m_appConfig.providers) {
        std::string marker = "○ ";
        if (pc.id == liveId) marker = "● ";   // ★ P4：用守卫式 liveId（原裸解引用 m_sess）
        if (pc.id == m_editingProviderId) marker += "[编辑中] ";
        auto row = std::make_unique<ECDI::Button>(
            marker + pc.id + " · " + pc.model);
        row->SetTextColor(kDarkInk);
        row->SetFont(harness::SansFont(harness::kFontSmall));
        row->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kToolHeaderHover});
        row->SetSize(500, 24);
        const std::string pid = pc.id;
        row->SetOnClick([this, pid] {
            m_editingProviderId = pid;
            if (const ProviderConfig* sel = m_appConfig.Find(pid)) {
                if (m_hud.settingsBase) {
                    m_hud.settingsBase->SetText(sel->baseUrl);
                    m_hud.settingsModel->SetText(sel->model);
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(
                            sel->context > 0 ? std::to_string(sel->context) : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(
                            sel->maxOutput > 0 ? std::to_string(sel->maxOutput) : "");
                    }
                    if (m_hud.settingsKey) m_hud.settingsKey->SetText("");
                    if (m_hud.settingsHint) {
                        m_hud.settingsHint->SetText(
                            "编辑 " + pid + "——key 留空保持已存凭据不变");
                    }
                }
            }
            RebuildSettingsProviderList();
        });
        m_hud.settingsProviderList->AddChild(std::move(row));
    }
    if (ECDI::Widget* parent = m_hud.settingsProviderList->GetParent()) {
        parent->Arrange();
    }
}

void HarnessApp::RequestModelSwitch(const std::string& providerId) {
    if (!m_sess || !m_sess->writer) {
        AddNoticeRow("[当前会话不是 live——先连接提供商]", kDarkMuted);
        return;
    }
    if (!m_sess->hasModelCap) {
        AddNoticeRow("[当前 core 未声明 model 能力，不能热切换]", kDarkMuted);
        return;
    }
    if (m_sess->providerId == providerId && m_sess->pendingModelId.empty()) {
        AddNoticeRow("[已是当前模型] " + providerId, kDarkMuted);
        return;
    }
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        // ★ I11 排队语义：running/waiting 时**不发**——等上一个模型完整输出完。
        m_sess->queuedModelProvider = providerId;
        AddNoticeRow("[模型切换已排队：" + providerId + "（本轮结束后生效）]", kDarkMuted);
        return;
    }
    SendModelSwitch(providerId);
}

void HarnessApp::SendModelSwitch(const std::string& providerId) {
    const ProviderConfig* pc = m_appConfig.Find(providerId);
    if (!pc) {
        AddNoticeRow("[切换目标不在配置中] " + providerId, kErrorDark);
        return;
    }
    const std::string id = "m" + std::to_string(m_sess->writer->LastSeqSent() + 1);
    m_sess->pendingModelId = id;
    if (!m_sess->writer->Model(id, providerId, pc->context, pc->maxOutput)) {
        m_sess->pendingModelId.clear();
        AddNoticeRow("[切换请求发送失败]", kErrorDark);
        return;
    }
    AddNoticeRow("[切换请求已发送] " + providerId + " / " + pc->model, kDarkMuted);
}

void HarnessApp::SendPendingModelSwitchIfIdle() {
    if (!m_sess || !m_sess->writer || m_sess->queuedModelProvider.empty()) return;
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) return;
    const std::string provider = m_sess->queuedModelProvider;
    m_sess->queuedModelProvider.clear();
    SendModelSwitch(provider);
}

void HarnessApp::MaterializePendingPrompts() {
    if (!m_content || !m_sess) return;
    // Transient rows are intentionally rebuilt rather than inserted into canonical row/timeline state.
    for (const PendingPromptView& view : m_pendingPromptViews) {
        if (view.row.panel) m_content->RemoveChild(view.row.panel);
    }
    m_pendingPromptViews.clear();

    for (const PendingPrompt& pending : m_sess->pendingPrompts) {
        auto panel = std::make_unique<ECDI::Panel>();
        RowRec row;
        row.alignRight = true;
        row.label = nullptr;
        std::string display;
        // ★★ 协议 §7（原文）：待确认气泡「与已确认 `user` 气泡**同位置同形状**，只多一个
        //   弱化后缀 `· 发送中`」；失败态「后缀改 `· 未确认`，描边转为错误色」。
        //   ★ 2026-10-20 修正：此前是**换行**追加「未确认 · 等待 core 接受」/「未确认：<原因>」，
        //     那会改变气泡高度与形状（违反「同形状」），文案也不是规范里的那两个后缀；
        //     listlayout 又自创了 `[未确认] ` / `[失败] ` 前缀。现在一律只用同行后缀。
        display = pending.text;
        display += pending.status == "sending" ? " · 发送中" : " · 未确认";
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            // ★ 协议 §7：失败态「描边转为错误色」；发送中仍用 user 气泡的常规描边
            //   （“同形状”也包括边框，只有颜色在失败时换）。
            .borderColor = pending.status == "sending" ? kUserBorder : kErrorDark});
        auto label = std::make_unique<ECDI::Label>(display);
        row.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody));
        panel->AddChild(std::move(label));
        row.panel = panel.get();
        if (m_options.rowModeList) row.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
        m_content->AddChild(std::move(panel));
        PendingPromptView view;
        view.clientId = pending.clientId;
        view.row = row;
        m_pendingPromptViews.push_back(std::move(view));
    }
    // ★★ P4 C：pending 视图增删后刷新空状态引导（AddRow 尾部也经此处 ⇒
    //   所有「行到达」路径一并覆盖——任何行/pending 出现即拆引导卡）。
    RefreshEmptyGuide();
}


// ★★ P4 Phase D：本地时刻 "HH:MM"（answerAt 用——审计信息，分钟粒度够用）。
static std::string NowHm() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    if (localtime_s(&tmv, &now) != 0) return std::string();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}

void HarnessApp::MarkAnswered(RowRec& rec, const std::string& choice) {
    if (rec.answered) return;
    rec.answered = true;
    // ★ 0c：已答进模型（重建时回填成"置灰"而不是"可再答一次"）
    // ★★ Step 3：**答的是什么**也进模型。0c 只存了 `answered`（bool），于是任何
    //   重建之后"当时允许还是拒绝"就丢了——两个钮都置灰，读者无从判断。
    //   权限门是**授权决定**，不是一个"已处理"标记：丢掉结论等于把历史抹成空白。
    if (rec.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[rec.timelineIdx].answered = true;
        m_sess->timeline[rec.timelineIdx].answer = choice;
        m_sess->timeline[rec.timelineIdx].answerAt = NowHm();
    }
    rec.answer = choice;
    // ★ 视觉走**同一条**路径（见 `ApplyAnsweredLook` 的注释：首答与重建必须同形，
    //   否则"刚答完"和"重建后"长得不同，而 A==B 看不见配色差异）。
    ApplyAnsweredLook(rec, choice,
                      rec.timelineIdx < m_sess->timeline.size()
                          ? m_sess->timeline[rec.timelineIdx].answerAt
                          : std::string());
    m_sess->waitForAnswer = false;
    if (m_sess->writer) {
        // live：真发 answer（协议 §4：需 id + choice；I3 的解除信号）
        if (!rec.askId.empty()) {
            m_sess->writer->Answer(rec.askId, choice);
        }
    }
    m_sess->askHist.emplace_back(choice, rec.label ? rec.label->GetText() : std::string());
    if (m_sess->askHist.size() > 16) m_sess->askHist.pop_front();
    // ★★ Step 5：把结论落到 sidecar（**只有历史打开的会话**才写——启动时 `--replay`
    //   的会话是跑批基线，写它等于让"再跑一次"与"第一次跑"不同）。
    if (m_sess->originKind == "replay-history") SaveUiState(*m_sess);
    m_hudDirty = true;
    AddNoticeRow("[answer] " + choice, kDarkMuted);
    RequestRepaint();
}

// ★★ Step 3：已答权限门的视觉。**首答与重建共用**（这是本函数存在的唯一理由）。
//
// ★★ P4 Phase D（2026-10-25）改版：两钮**隐藏** + 行内结论标记
//   （`● 已允许 HH:MM` 绿 / `● 已拒绝 HH:MM` 红）。
//   · 原「选中者留色、另一个灰化」只把结论编码在配色里（审计事实读不出来），
//     且 ECDI Button 不画 disabled 态——两个死按钮留在行里占位却不可点。
//   · 结论标记是文本 ⇒ 探针可从 `conclusion->GetText()` **独立于模型**读回
//     （原手法读两钮文字色，钮一隐藏即失效——本版判据反而更直接）。
//   · `when` 为空 = 旧 sidecar 重建（无时刻记录）——如实省略时刻段，不编造。
//   · 未答/重建未答：conclusion 隐藏、两钮可见可点（幂等）。
void HarnessApp::ApplyAnsweredLook(RowRec& rec, const std::string& choice,
                                   const std::string& when) {
    if (rec.allowBtn) {
        rec.allowBtn->SetEnabled(false);   // 双保险：不可点（命中早退）且不可见
        rec.allowBtn->SetVisible(false);
    }
    if (rec.denyBtn) {
        rec.denyBtn->SetEnabled(false);
        rec.denyBtn->SetVisible(false);
    }
    if (!rec.conclusion) return;
    // 记下视图侧结论（`SnapshotRows` 只读 RowRec，不回查模型——见字段注释）
    rec.answer = choice;
    const bool allowPicked = (choice == "allow");
    const bool denyPicked = (choice == "deny");
    const std::string whenSeg = when.empty() ? std::string() : (" " + when);
    if (allowPicked) {
        rec.conclusion->SetTextColor(kOkDark);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8" + whenSeg);
    } else if (denyPicked) {
        rec.conclusion->SetTextColor(kErrorDark);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE6\x8B\x92\xE7\xBB\x9D" + whenSeg);
    } else {
        // 未知 choice（协议外的值）：如实呈现「答了但答案不认识」，不编造正常态。
        rec.conclusion->SetTextColor(kDarkMuted);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE5\x9B\x9E\xE7\xAD\x94\xEF\xBC\x88\xE6\x9C\xAA\xE7\x9F\xA5\xE7\xBB\x93\xE8\xAE\xBA\xEF\xBC\x89" + whenSeg);
    }
    rec.conclusion->SetVisible(true);
}

// 通知行：**纯构造**（重建可重放）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildNoticeRow(const std::string& text,
                                                        const ECDI::Color& color, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    auto label = std::make_unique<ECDI::Label>(text);
    rec.label = label.get();
    label->SetTextColor(color);
    label->SetFont(harness::SansFont(harness::kFontSmall));   // 统一中文 face（原先落 SimSun）
    panel->AddChild(std::move(label));
    return panel;
}

// ── ★★ P4 Phase C（2026-10-25）：空状态引导 ──────────────────────────
// 修的死胡同：首启（firstRun）关掉设置面板后，转录区一行都没有、屏幕全空。
// 空转录 = 「引导卡」的展示条件：一句话 + 出口（live 会话给建议指令、无 core
// 给「打开设置」）。任何行（含 notice）/pending 气泡到达即拆——回放/Ctrl+N 的
// 空会话带 notice，天然不触发（方案 §5 的「回放/harness 场景不弹」由此成立）。
bool HarnessApp::TranscriptIsEmpty() const {
    return m_sess && m_sess->rows.empty() && m_pendingPromptViews.empty();
}

void HarnessApp::RefreshEmptyGuide() {
    if (!m_content || !m_sess) return;
    if (!TranscriptIsEmpty()) {
        if (m_hud.emptyGuide) {
            m_content->RemoveChild(m_hud.emptyGuide);   // 返回所有权，丢弃即析构
            m_hud.emptyGuide = nullptr;
            // ★ 不 RequestRepaint：拆除发生在「行/pending 到达」的 tick 里，
            //   调用方（relayoutPulse / Relayout 链）必然带着重绘——再请求一次
            //   会让 invalidations 漂 +1（demo 实测 8→10，违反 §0.3 读数纪律）。
        }
        return;
    }
    if (!m_hud.emptyGuide) {
        auto panel = std::make_unique<ECDI::Panel>();
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = kPanel2,
            .cornerRadius = harness::kRadiusCard,
            .borderWidth = 1.0f,
            .borderColor = kLine});
        panel->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 18));
        auto title = std::make_unique<ECDI::Label>("开始新对话");
        title->SetTextColor(kDarkInk);
        title->SetFont(harness::SansFont(harness::kFontTitle));
        title->SetSize(400, 20);
        panel->AddChild(std::move(title));
        auto mkBtn = [](const std::string& label) {
            auto b = std::make_unique<ECDI::Button>(label);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = harness::kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            return b;
        };
        // 出口分两形态：live 会话给建议指令（点击 = SendPrompt，pending 气泡
        // 一出现本卡即拆）；无 core（firstRun 空壳 / 回放空会话）给「打开设置」。
        if (m_sess->source && m_sess->source->IsLive()) {
            auto sub = std::make_unique<ECDI::Label>("输入消息，或从下面的建议开始：");
            sub->SetTextColor(kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontSmall));
            sub->SetSize(400, 16);
            panel->AddChild(std::move(sub));
            auto row = std::make_unique<ECDI::Panel>();
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, false, 0));
            const char* prompts[] = {"介绍这个工作区里有什么", "帮我梳理最近的改动"};
            for (const char* p : prompts) {
                auto b = mkBtn(p);
                b->SetSize(170, 30);
                std::string text = p;
                b->SetOnClick([this, text] { SendPrompt(text); });
                row->AddChild(std::move(b));
            }
            row->SetSize(356, 30);
            panel->AddChild(std::move(row));
        } else {
            auto sub = std::make_unique<ECDI::Label>(
                "还没有连接任何提供商——先在设置里配好 BaseURL 和 API Key。");
            sub->SetTextColor(kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontSmall));
            sub->SetSize(400, 16);
            panel->AddChild(std::move(sub));
            auto b = mkBtn("打开设置");
            b->SetSize(110, 30);
            b->SetOnClick([this] { SetSettingsVisible(true); });
            panel->AddChild(std::move(b));
        }
        panel->SetPosition(kCanvasPadX, 0);
        m_hud.emptyGuide = panel.get();
        m_content->AddChild(std::move(panel));
        // ★ 不 RequestRepaint：建卡的路径（首帧 BuildUi / 关设置的
        //   ApplySettingsVisibility→SetVisible 内建 Invalidate / tick 重排链）
        //   都自带整窗重绘——额外请求会污染 invalidations（demo 实测 +1）。
    }
    // 宽度跟随（resize/侧栏切换/设置开合）：空转录时 Relayout 的行循环零迭代，
    // extent 收敛到 0——本卡不进 extent，由这里自己摆。
    const int w = std::max(200, ContentWidth(m_scroll) - 2 * kCanvasPadX);
    m_hud.emptyGuide->SetSize(w, 128);
}

void HarnessApp::AddNoticeRow(const std::string& text, const ECDI::Color& color) {
    // ★★ 0c：通知进模型。此前它**只**是控件 —— 而通知恰好承载全部协议健康信息
    //   （I4/I5 缺口、截断报错、协议 §9 拒连、hello/bye 摘要、发送/回答回执）。
    //   ⇒ 任何重建都会把"这个会话被截断过""协议版本不兼容被拒"这类信息**静默抹掉**，
    //     屏幕看起来只是"少了几行灰字"，而丢掉的是**这次运行是否可信**的判据。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Notice;
    entry.text = text;
    entry.color = color;
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildNoticeRow(text, color, rec);
    AddRow(std::move(panel), rec, std::string(), m_sess->timeline.size() - 1);
}

// ── 布局与重绘（规格 §5.3 / §6.2）──────────────────────────────

void HarnessApp::Relayout() {
    const int contentW = ContentWidth(m_scroll);

    if (m_options.rowModeList) {
        // listlayout：行高固定、不测量；Arrange 由 ListLayout 完成
        for (RowRec& r : m_sess->rows) {
            if (r.panel->GetHeight() != kListRowHeight) {
                r.panel->SetSize(contentW, kListRowHeight);
            }
        }
        for (PendingPromptView& view : m_pendingPromptViews) {
            if (view.row.panel && view.row.panel->GetHeight() != kListRowHeight) {
                view.row.panel->SetSize(contentW, kListRowHeight);
            }
        }
        m_content->Arrange();
        m_scroll->UpdateContentExtent();
        return;
    }

    // autosize：每行按内容测量（文本测量成本正是本模式要暴露的指标 ④）
    // ★ 行可用宽 = 内容宽 − 左右画布留白；行本身从 kCanvasPadX 起排。
    const int innerW = std::max(120, contentW - 2 * kCanvasPadX);
    int y = 0;
    for (RowRec& r : m_sess->rows) {
        int rowH = 0;
        // ★ 协议 v2：思维链卡与工具卡**同一套几何**（通栏 + 折叠体），故共用本分支
        if (r.tool || r.reasoning) {
            const int bodyW = innerW - 2 * kRowPadX;
            const int wrapW = std::max(40, bodyW - 20);
            int bodyH = 0;
            if (r.label) {
                // Phase 29 wrap：工具体按 bodyW 宽断行；★ 宽度不变不 SetSize——
                // 无条件 SetSize 会打穿 C29-10 wrap 布局缓存（指纹含宽度），
                // 逐帧重建 = 4.8s 测量成本事故（2026-10-06 实测）
                if (r.lastWrapW != wrapW) {
                    r.label->SetSize(wrapW, r.label->GetHeight());
                    r.lastWrapW = wrapW;
                }
                // ★★ A-2 已复核**否决**（2026-10-13 晚）：此处**保持**无条件 `AutoSize()`。
                //   我曾判定「折叠卡的长文本被每帧重测」并改成「按需重测」，
                //   **实测推翻了它**——把 `chars` 分解为 `chars = a(一次性) + b(每帧)·frames`
                //   （`.scratch/probe_a2_decompose.py`，同长度下多档 `--auto-close` 拟合）：
                //     body=4000 ⇒ a = 123,081 · **b = 504**
                //     body=40   ⇒ a =     138 · **b = 502**
                //   ⇒ **每帧成本与文本长度无关**（b 之比 **1.00×**）；长文本的代价是
                //     **一次性**的布局建立（ECDI 的 C29-10 五元组指纹缓存**按文档生效**）。
                //   ⇒ 先前那个「8.80×」是**同时长下两个不同长度**的对比，差值全在 a，
                //     被我误读成了逐帧浪费 ⇒ **原判收回**（条 30/40）。
                //   ⇒ 「按需重测」那版实测**零收益**（读数完全一致），故**不保留**——
                //     留一个不解决问题的状态变量只会带来「失效纪律」的腐化风险。
                //   ★ 一次性成本是**必需的**：`CollapsiblePanel` 的收起态 `SetSize`
                //     **定义展开基准**（其头注释 76-78 行），`bodyH` 必须有值。
                r.label->AutoSize();
                bodyH = r.label->GetHeight() + 8;
            }
            r.bodyH = bodyH;
            if (r.headerBtn) {
                r.headerBtn->SetSize(innerW - 2 * kRowPadX, kHeaderH);
                r.headerBtn->SetPosition(kRowPadX, 2);
            }
            if (r.body) {
                // 只在基准尺寸变化时下发：收起态 SetSize 定义展开基准，
                // 展开态 SetSize 会直接钉住呈现值——动画期间重入会把动画打死
                if (r.lastBodyW != bodyW || r.lastBodyH != bodyH) {
                    r.lastBodyW = bodyW;
                    r.lastBodyH = bodyH;
                    r.body->SetSize(bodyW, bodyH);
                }
                r.body->SetPosition(kRowPadX, 2 + kHeaderH + 2);
            }
            rowH = 2 + kHeaderH + 2 + (r.expanded ? bodyH : 0);
        } else {
            const int maxBubbleW = std::max(120, innerW * 78 / 100);
            const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
            if (r.label) {
                if (r.ask) {
                    r.label->AutoSize();   // 权限问句保持单行
                    if (r.allowBtn) {
                        r.allowBtn->SetSize(70, 28);
                        r.allowBtn->SetPosition(innerW - 160, 5);
                    }
                    if (r.denyBtn) {
                        r.denyBtn->SetSize(70, 28);
                        r.denyBtn->SetPosition(innerW - 82, 5);
                    }
                    // ★★ P4 D：已答结论行占两钮原来的位置（右对齐、垂直居中）。
                    if (r.conclusion && r.conclusion->IsVisible()) {
                        r.conclusion->AutoSize();
                        const int cw = r.conclusion->GetWidth();
                        r.conclusion->SetPosition(innerW - cw - 14, 10);
                    }
                } else {
                    // 聊天气泡（Phase 29 wrap）：单行放得下 → 气泡收缩；
                    // 超上限 → 按上限宽断行（高度 = 行数×行高，布局驱动）
                    if (!r.wrapped) {
                        r.label->AutoSize();   // 单行 extent
                    }
                    // ★ REQ-01（2026-10-13）：含**显式 `\n`** 的文本也必须走 wrap 路径。
                    //   非 wrap ⇒ `TextWidget` 走 Phase 28 的「单条 DrawText、布局引擎零接触」
                    //   路径（`TextWidget.h:62-63`）⇒ **`\n` 不被消费**，整段被拼成一行，
                    //   再由宽度重新断行（断点与原意不符）。ECDI 侧本身正确（`TextWidget.cpp:235-248`
                    //   显式切硬段、段数 = `\n` 数 + 1），此前误判为 ECDI 缺陷，实为本行只看了宽度。
                    const bool hasNewline =
                        r.label->GetText().find('\n') != std::string::npos;
                    const bool needWrap =
                        r.wrapped || hasNewline || r.label->GetWidth() > maxTextW;
                    if (needWrap != r.wrapped) {
                        r.wrapped = needWrap;
                        r.label->SetWordWrap(needWrap);
                    }
                    if (r.wrapped) {
                        if (r.lastWrapW != maxTextW) {
                            r.label->SetSize(maxTextW, r.label->GetHeight());
                            r.lastWrapW = maxTextW;
                        }
                        r.label->AutoSize();   // 稳定期命中 wrap 布局缓存，不逐帧重建
                    }
                }
                r.label->SetPosition(kRowPadX, 8);
            }
            rowH = std::max(r.label ? r.label->GetHeight() + 16 : 0, r.ask ? 38 : 0);
        }
        if (r.tool || r.reasoning || r.ask) {
            // 工具卡 / 思维链卡 / 权限门：通栏（hi-fi align-self: stretch）
            r.panel->SetSize(innerW, rowH);
            r.panel->SetPosition(kCanvasPadX, y);
        } else {
            // 聊天气泡（hi-fi .msg max-width:78%）：user 右对齐、assistant 左对齐
            const int maxBubbleW = std::max(120, innerW * 78 / 100);
            int bubbleW = innerW;
            if (r.label) {
                bubbleW = r.wrapped
                              ? maxBubbleW
                              : std::min(maxBubbleW,
                                         r.label->GetWidth() + 2 * kRowPadX + 2);
            }
            r.panel->SetSize(bubbleW, rowH);
            r.panel->SetPosition(r.alignRight ? kCanvasPadX + innerW - bubbleW
                                              : kCanvasPadX,
                                 y);
        }
        y += rowH + kRowGap;
    }
    for (PendingPromptView& view : m_pendingPromptViews) {
        RowRec& r = view.row;
        if (!r.panel) continue;
        const int maxBubbleW = std::max(120, innerW * 78 / 100);
        const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
        if (r.label) {
            if (!r.wrapped) r.label->AutoSize();
            const bool hasNewline = r.label->GetText().find('\n') != std::string::npos;
            const bool needWrap = r.wrapped || hasNewline || r.label->GetWidth() > maxTextW;
            if (needWrap != r.wrapped) {
                r.wrapped = needWrap;
                r.label->SetWordWrap(needWrap);
            }
            if (r.wrapped) {
                if (r.lastWrapW != maxTextW) {
                    r.label->SetSize(maxTextW, r.label->GetHeight());
                    r.lastWrapW = maxTextW;
                }
                r.label->AutoSize();
            }
            r.label->SetPosition(kRowPadX, 8);
        }
        const int rowH = r.label ? r.label->GetHeight() + 16 : 0;
        const int bubbleW = r.label
            ? (r.wrapped ? maxBubbleW : std::min(maxBubbleW, r.label->GetWidth() + 2 * kRowPadX + 2))
            : maxBubbleW;
        r.panel->SetSize(bubbleW, rowH);
        r.panel->SetPosition(kCanvasPadX + innerW - bubbleW, y);
        y += rowH + kRowGap;
    }
    m_scroll->SetContentExtent(contentW, std::max(0, y - kRowGap));
    // ★★ P4 Phase A：内容范围变了 ⇒ maxOffset/滚动条可见性都可能变——
    //   「跳到最新」的几何与显隐跟着刷新一次（Relayout 覆盖 resize/侧栏/菜单/
    //   设置切换等全部几何路径）。
    RefreshJumpButton();
    // ★★ P4 Phase C：空状态引导的宽度跟随（resize/侧栏切换后重摆）。
    RefreshEmptyGuide();
    // ★★ P4 Phase F：设置浮层几何跟随（视口尺寸变了，遮罩/sheet 重摆）。
    LayoutSettingsOverlay();
}

void HarnessApp::FollowTailIfPinned() {
    if (!m_scroll) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const bool pinned = m_scroll->GetScrollOffsetY() >= maxY - kPinThreshold;
    if (pinned) m_scroll->SetContentOffset(0, maxY);
    // ★★ P4 Phase A：填行/重排后顺手刷新「跳到最新」——本函数的调用点天然
    //   覆盖所有内容增长路径（tick 填行 / Relayout / 切会话重建），无需另铺。
    RefreshJumpButton();
}

// ★★ P4 Phase A（2026-10-25）：刷新「跳到最新」按钮的几何与显隐（唯一刷新口）。
//   ★ 调用时点：滚轮/导航键后（HarnessApplication override）、填行后
//     （FollowTailIfPinned 尾部）、重排后（Relayout 尾部）、tick 脏检查
//     （DirtyRefreshJumpButton——拖滚动条滑块没有事件回调，靠定时器节拍兜底）。
//   ★ 全部是只读查询 + 几何 setter + 同值守卫的 SetVisible（M9：同值不
//     Invalidate）⇒ 高频调用零重绘代价，不打扰 CountingPanel 读数。
void HarnessApp::RefreshJumpButton() {
    if (!m_hud.jumpLatest || !m_scroll) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const int offset = m_scroll->GetScrollOffsetY();
    m_jumpLastOffsetY = offset;   // 脏检查基准同步更新
    // 几何：视口右下角、贴滚动条一侧（原型 F 条）。条厚不猜——垂直条可见时按
    // 其实际宽度让位（kSidebarListW 注释里的 BarFootprint=14 是侧栏的观测值，
    // 这里直接读控件，条隐藏/显示切换时几何自然跟随）。
    int barW = 0;
    if (ECDI::ScrollBar* bar = m_scroll->GetVerticalScrollBar()) {
        if (bar->IsVisible()) barW = bar->GetWidth() + 6;
    }
    m_hud.jumpLatest->SetSize(kJumpBtnW, kJumpBtnH);
    m_hud.jumpLatest->SetPosition(m_scroll->GetWidth() - barW - kJumpBtnW - 16,
                                  m_scroll->GetHeight() - kJumpBtnH - 10);
    const bool pinned = offset >= maxY - kPinThreshold;
    // 显隐 = 离开贴底带 ∧ 有可滚内容 ∧ 转录可见（设置面板占槽时 scroll 不可见）。
    m_hud.jumpLatest->SetVisible(!pinned && maxY > 0 && m_scroll->IsVisible());
}

// ★★ P4 Phase A：tick 脏检查——offset 与缓存不等才真正刷新（禁止无条件刷新/
//   Invalidate，CountingPanel 读数纪律）。
void HarnessApp::DirtyRefreshJumpButton() {
    if (!m_scroll || !m_hud.jumpLatest) return;
    if (m_scroll->GetScrollOffsetY() != m_jumpLastOffsetY) RefreshJumpButton();
}

// ── ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层 ─────────────────────
// ECDI 无 Tooltip 原语（源码仓核实：TrayIcon szTip 是唯一同名物）——harness 拼：
// TipButton（UiKit）回调 enter/leave，这里管单例浮层 + 400ms 延迟定时器。
// ★ 定位纪律：锚点坐标一律 GetAbsolutePosition（不手算父链，App.cpp:4274 同款）；
//   顶栏/中栏头按钮在视口上方 ⇒ y 钳到 0（浮层挂 m_scroll，负 y 会被视口裁剪）。
void HarnessApp::ShowTooltipFor(const ECDI::Widget* anchor, const std::string& text) {
    if (!anchor || !m_window) return;
    m_tooltipAnchor = anchor;
    m_tooltipPending = text;
    m_window->GetPlatformWindow().StartTimer(kTooltipTimer, kTooltipDelayMs);
}

void HarnessApp::HideTooltip() {
    if (m_window) m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    m_tooltipAnchor = nullptr;
    m_tooltipPending.clear();
    if (m_hud.tooltip) m_hud.tooltip->SetVisible(false);
}

void HarnessApp::OnTooltipFire() {
    if (!m_hud.tooltip || !m_hud.tooltipText || !m_tooltipAnchor || !m_scroll) {
        return;
    }
    if (m_window) m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    // 定时器期间锚点可能已脱树（整树重建）——IsInTree 没有公开口，用宽度/父链
    // 兜底：GetParent 为空的根除外……直接按绝对坐标定位，锚点已析构的情况由
    // HideTooltip（leave 先于析构）与重建路径（BuildUi 后浮层隐藏）兜住。
    const ECDI::Point a = m_tooltipAnchor->GetAbsolutePosition();
    const ECDI::Point s = m_scroll->GetAbsolutePosition();
    m_hud.tooltipText->SetText(m_tooltipPending);
    const int w = std::clamp<int>(static_cast<int>(m_tooltipPending.size()) * 7 + 20,
                                  48, m_scroll->GetWidth());
    const int h = 26;
    int x = static_cast<int>(a.x) - static_cast<int>(s.x);
    int y = static_cast<int>(a.y) + m_tooltipAnchor->GetHeight() + 6 -
            static_cast<int>(s.y);
    x = std::clamp(x, 0, std::max(0, m_scroll->GetWidth() - w));
    y = std::clamp(y, 0, std::max(0, m_scroll->GetHeight() - h));
    m_hud.tooltipText->SetSize(w - 16, 16);
    m_hud.tooltip->SetSize(w, h);
    m_hud.tooltip->SetPosition(x, y);
    m_hud.tooltip->SetVisible(true);
}

void HarnessApp::RequestRepaint() {
    ++m_invalidations;
    if (m_counting) m_counting->Invalidate();
}

void HarnessApp::CacheEvalMetrics() {
    if (!m_counting) return;
    m_evalPaints = m_counting->PaintCount();
    const std::vector<uint64_t>& frames = m_counting->FrameDurationsUs();
    m_evalFrames = frames.size();
    m_evalMaxUs = 0;
    m_evalSumUs = 0;
    for (const uint64_t us : frames) {
        m_evalMaxUs = std::max(m_evalMaxUs, us);
        m_evalSumUs += us;
    }
    m_evalLastUs = frames.empty() ? 0 : frames.back();

    if (m_probe) {
        m_evalProbeFrames = m_probe->FrameCount();
        m_evalProbeCommands = m_probe->TotalCommands();
        m_evalProbeMax = m_probe->MaxFrameCommands();
        m_evalProbeAvg = m_probe->AvgFrameCommands();
        m_evalMaxExecUs = m_probe->MaxExecuteUs();
        m_evalAvgExecUs = m_probe->AvgExecuteUs();
    }
    if (m_measureProbe) {
        m_evalMeasureCalls = m_measureProbe->MeasureCalls();
        m_evalMeasureTotalUs = m_measureProbe->TotalUs();
        m_evalMeasureMaxUs = m_measureProbe->MaxUs();
    }
}

// ★★ 2026-10-21：HUD 覆盖自检（理由见 `App.h` 里 `HudTouch` 的声明注释）。
//
//   这一类缺陷的形状：**读了但从不写**。`RefreshHud` 里写着
//   `if (m_hud.X) { m_hud.X->SetText(...) }`，字面上像"有这个控件就更新它"，
//   而如果建树时从来没赋值，那个 `if` 就永远是假分支——代码看着对，
//   屏幕上是空的，**所有既有读数都看不见它**。
//   已发现三个实例：`backendChip`（屏幕上从来没有）、
//   `sessSession` / `sessCore`（Step 1 之后不再建）。前两个已删、
//   `backendChip` 已补上真实实现——但"修三个实例"不是修这一类，所以这里把它
//   变成一条会自己变红的判据。
//
//   ★ 开销：默认 `m_hudAudit == false` ⇒ `HudTouch` 只做一次 bool 判断就返回，
//     不分配、不记录。只有探针打开它之后才登记。
void HarnessApp::HudTouch(const char* name) {
    if (!m_hudAudit) return;
    m_hudTouched.emplace_back(name);
}

std::vector<std::string> HarnessApp::HudUnbuilt() const {
    // ★ "这个字段建过没有"直接看**指针本身**，不另维护"已建"表——
    //   指针非空 ⇔ 建树时赋过值，这是唯一可靠且不会漂移的判据。
    auto nonNull = [this](const std::string& n) -> bool {
        const Hud& h = m_hud;
        if (n == "title") return h.title != nullptr;
        if (n == "rowChip") return h.rowChip != nullptr;
        if (n == "backendChip") return h.backendChip != nullptr;
        if (n == "sessChip") return h.sessChip != nullptr;
        if (n == "statusDot") return h.statusDot != nullptr;
        if (n == "sbDot") return h.sbDot != nullptr;
        if (n == "stateChip") return h.stateChip != nullptr;
        if (n == "sessProgress") return h.sessProgress != nullptr;
        if (n == "convTitle") return h.convTitle != nullptr;
        if (n == "convSub") return h.convSub != nullptr;
        if (n == "workspacePath") return h.workspacePath != nullptr;
        if (n == "workspaceStatus") return h.workspaceStatus != nullptr;
        if (n == "workspacePick") return h.workspacePick != nullptr;
        if (n == "historyList") return h.historyList != nullptr;
        if (n == "historyCount") return h.historyCount != nullptr;
        if (n == "projectList") return h.projectList != nullptr;
        if (n == "sessList") return h.sessList != nullptr;
        if (n == "sessCount") return h.sessCount != nullptr;
        if (n == "healthSeq") return h.healthSeq != nullptr;
        if (n == "healthViol") return h.healthViol != nullptr;
        if (n == "healthKind") return h.healthKind != nullptr;
        if (n == "evalPaints") return h.evalPaints != nullptr;
        if (n == "probeCmds") return h.probeCmds != nullptr;
        if (n == "probeExec") return h.probeExec != nullptr;
        if (n == "usageInV") return h.usageInV != nullptr;
        if (n == "usageOutV") return h.usageOutV != nullptr;
        if (n == "ctxPct") return h.ctxPct != nullptr;
        if (n == "histChip") return h.histChip[0] != nullptr;
        if (n == "histWhat") return h.histWhat[0] != nullptr;
        if (n == "inspector") return h.inspector != nullptr;
        if (n == "inspectorToggle") return h.inspectorToggle != nullptr;
        if (n == "exportToggle") return h.exportToggle != nullptr;
        if (n == "moreToggle") return h.moreToggle != nullptr;
        if (n == "moreMenu") return h.moreMenu != nullptr;
        // ★ 不在表里 ⇒ 按**未建**处理（fail-closed：宁可报红，不可静默通过）。
        //   这条很关键：将来新增一个 Hud 字段却忘了加进这张表，判据会立刻红，
        //   而不是默默漏检——"表会漏项"是这套机制唯一的真实弱点，所以要让它
        //   **漏了就吵**。
        return false;
    };
    std::vector<std::string> out;
    std::vector<std::string> seen;
    for (const std::string& n : m_hudTouched) {
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        if (!nonNull(n)) out.push_back(n);
    }
    return out;
}

// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。
void HarnessApp::RefreshHud() {
    m_hudDirty = false;
    if (!m_hud.title) return;

    m_hud.title->SetText("harness");
    if (HudTouch("rowChip"), m_hud.rowChip) {
        m_hud.rowChip->SetText(m_options.rowModeList ? "listlayout" : "autosize");
    }
    if (HudTouch("sessChip"), m_hud.sessChip) {
        m_hud.sessChip->SetText(m_sess->sessionId.empty() ? "s-____" : m_sess->sessionId);
    }
    if (HudTouch("backendChip"), m_hud.backendChip) {
        // ★★ 2026-10-21：**不再拼 `▽`**。后端在 `Create` 时定死，运行期没有切换
        //   路径（全仓唯一赋值处是 GL 就绪检查之后）⇒ 箭头会承诺一个不存在的菜单。
        //   原写法 `m_backendName + " ▽"` 是"看着能点、其实不能"的标本。
        m_hud.backendChip->SetText(m_backendName);
    }
    // 中栏会话头（2026-10-18，原型 C）：副标随会话 id / core 版本到达而更新。
    // ★ 2026-10-18 订正：此前只拼 `协议 v2 · 回放模式 · <sessionId>`，漏掉
    //   `hello.core_version` 与 `protocol`（截图上副标是
    //   `core 0.1.0 · session s-0001 · protocol 1` —— 与规格 §9 的
    //   `version/protocol` 呈现位不符，看着像没接上 hello）。
    //   ★ 只更新**会变的部分**（模式与工作区是启动即定的），标题保持建头时的值。
    // ★ 0d：标题改成反映**活动会话**（多会话后，标题不该恒是首会话的名字）。
    if (HudTouch("convTitle"), m_hud.convTitle) {
        m_hud.convTitle->SetText(m_sess->label.empty() ? ConversationTitle() : m_sess->label);
    }
    if (HudTouch("convSub"), m_hud.convSub) {
        std::string sub = "core " + (m_sess->coreVer.empty() ? std::string("—") : m_sess->coreVer);
        if (!m_sess->sessionId.empty()) sub += " · " + m_sess->sessionId;
        sub += " · protocol " + std::to_string(m_sess->protocolVer);
        // ★ 0d：会话计数——让用户看见"现在有几个会话、我在第几个"。
        sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
               std::to_string(m_sessions.size());
        m_hud.convSub->SetText(sub);
    }
    const std::string stateText =
        m_sess->coreState.empty() ? std::string("idle") : m_sess->coreState;
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
    const bool live = m_sess->writer != nullptr;
    // ★★ 2026-10-20：`liveReady` **不再**包含 `workspaceCap`——协议 §3.1 只要求
    //   「未声明 workspace 的 core，GUI 不得显示工作区选择器」，而不是「不能对话」。
    //   旧写法把 workspace 能力当成 live 就绪的前提，导致一份合法的不支持工作区的
    //   core 连一句 prompt 都发不出去（发送钮永久置灰）。
    const bool liveReady = live && m_sess->protocolVer == 3 &&
                           !m_sess->protocolIncompatible && !m_sess->disconnected &&
                           !m_sess->liveEof;
    // 只有「core 声明了 workspace 且初始同步未完成」才是阻塞条件；无此能力时直接放行。
    const bool workspaceReady = !m_sess->workspaceCap || m_sess->workspaceSyncComplete;
    const bool sendEnabled = live
        ? (liveReady && workspaceReady && m_sess->coreState == "idle" &&
           m_sess->pendingWorkspaceId.empty() && !HasUnconfirmedPrompt())
        : !active;
    const bool stopEnabled = live ? (liveReady && active) : active;
    if (m_promptBar) m_promptBar->SetButtonStates(sendEnabled, stopEnabled);
    const bool pickerEnabled = m_sess->workspaceCap && liveReady &&
                               m_sess->coreState == "idle" &&
                               m_sess->pendingWorkspaceId.empty();
    // ★★ 协议 §3.1：对一份**真实的 core**，它未声明 workspace ⇒ GUI「不得显示工作区
    //   选择器」。⇒ 隐藏路径与选择钮，只留一行诚实的状态说明（“core 未声明 workspace 能力”）。
    //   ★ 只在 live 下藏：回放/空会话根本没有 core，沿用既有外观（零回归风险）。
    //   ★ 用控件自己的 IsVisible() 当“上次已应用值”，不新增模型字段——重建后新建的
    //     控件默认可见，这个判据会自己纠回来；换成成员记录反而会在重建后陈旧。
    const bool hideWorkspace = live && !m_sess->workspaceCap;
    bool workspaceVisChanged = false;
    auto applyWorkspaceVis = [&](ECDI::Widget* w) {
        if (!w) return;
        if (w->IsVisible() == hideWorkspace) {
            w->SetVisible(!hideWorkspace);
            workspaceVisChanged = true;
        }
    };
    applyWorkspaceVis(m_hud.workspacePath);
    applyWorkspaceVis(m_hud.workspacePick);
    applyWorkspaceVis(m_hud.projectList);
    if (workspaceVisChanged) {
        // VerticalLayout 只在 Arrange 时给隐藏子不占槽（C-VIS-1）⇒ 不重排就会留个空洞。
        if (ECDI::Widget* p = m_hud.workspaceStatus ? m_hud.workspaceStatus->GetParent()
                                                    : nullptr) {
            p->Arrange();
        }
    }
    if (HudTouch("workspacePath"), m_hud.workspacePath) {
        m_hud.workspacePath->SetText(m_sess->workspacePath.empty()
            ? "core 实际工作区：尚未确认" : "core 实际工作区：" + m_sess->workspacePath);
    }
    if (HudTouch("workspaceStatus"), m_hud.workspaceStatus) m_hud.workspaceStatus->SetText(m_sess->workspaceStatus);
    if (HudTouch("workspacePick"), m_hud.workspacePick) {
        m_hud.workspacePick->SetEnabled(pickerEnabled);
        m_hud.workspacePick->SetTextColor(pickerEnabled ? kDarkInk : kDarkMuted);
    }
    // ★★ REQ-04 直接回读探针（--state-probe，2026-10-14）
    //   · `SetEnabled` 不内建 Invalidate（Widget.h:185-187）⇒ 使能翻转**没有**绘制侧读数
    //     ⇒ 用**只读回读**替代：把按钮的真实 IsEnabled() 与 GetText() 取出来记档。
    //   · 只在「与上次不同」时记一行 ⇒ 摘要是一份状态翻转轨迹，可逐行核对协议 §6。
    //   · 放在 SetStateDrivenEnabled **之后**——读到的才是本次驱动后的结果。
    if (m_options.stateProbe && m_promptBar) {
        ECDI::Button* send = m_promptBar->sendButton();
        ECDI::Button* stop = m_promptBar->stopButton();
        if (send && stop) {
            const bool sendEnabled = send->IsEnabled();
            const bool stopEnabled = stop->IsEnabled();
            const std::string sendTitle = send->GetText();
            const bool changed = !m_stateProbeLastValid ||
                                 sendEnabled != m_stateProbeLastSend ||
                                 stopEnabled != m_stateProbeLastStop;
            if (changed) {
                StateProbeRec rec;
                rec.coreState = m_sess->coreState.empty() ? "idle" : m_sess->coreState;
                rec.sendEnabled = sendEnabled;
                rec.stopEnabled = stopEnabled;
                rec.sendTitle = sendTitle;
                m_stateProbeLog.push_back(std::move(rec));
                m_stateProbeLastSend = sendEnabled;
                m_stateProbeLastStop = stopEnabled;
                m_stateProbeLastValid = true;
            }
        }
    }
    if (HudTouch("stateChip"), m_hud.stateChip) {
        m_hud.stateChip->SetText(stateText);
        m_hud.stateChip->SetTextColor(kDarkMuted);
    }
    if (HudTouch("statusDot"), m_hud.statusDot) {
        m_hud.statusDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 4.0f});
    }
    if (HudTouch("sbDot"), m_hud.sbDot) {
        m_hud.sbDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 3.5f});
    }

    // ★★ Step 1：侧栏会话的"活动会话"读数**搬到了列表行里**（每行自带 id/label/选中态）。
    //   `sessSession` / `sessCore` 两个槽位因此**不再赋值**。
    //   ★★ 2026-10-21：这两个槽位与它们的 `if` 分支**已删除**。原注释写的是
    //     "字段仍留在 Hud 里（删字段是独立清理），故这里的两处 if 永远不成立
    //     （死代码但无害）"——"无害"这个判断错了：留着它们会让**下一个读代码的人
    //     以为侧栏有一行会话 id/meta**，而那一行早就不存在了。更重要的是，
    //     它们是"**读了但永远不写**"这一类缺陷的样板，而这类缺陷刚好可以机检
    //     （见 `--hud-probe`）。修一个样板、留一个样板是自相矛盾的。
    if (HudTouch("sessCount"), m_hud.sessCount) {
        // 计数与列表同源：都从 m_sessions.size() 出发 ⇒ 这一行是列表的**文字对照**。
        // 两者不一致（列表少画/多画了一行）时，人和探针都读得出来。
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%zu 个会话", m_sessions.size());
        m_hud.sessCount->SetText(buf);
    }
    if (HudTouch("sessProgress"), m_hud.sessProgress) {
        char buf[64];
        if (m_sess->replay) {
            std::snprintf(buf, sizeof(buf), "%zu / %zu 条", m_sess->replay->Consumed(),
                          m_sess->replay->Total());
        } else if (m_sess->source) {
            std::snprintf(buf, sizeof(buf), "%zu 条（live）", m_sess->liveLines);
        } else {
            // ★ 无 source 且无 replay = 回放模式下新建的空会话（0d 起就有这种会话）。
            //   上一版把它归进 else 支，于是侧栏写着「0 条（live）」——**谎报来源**。
            //   这个错在 0d 就存在，但那时侧栏只画活动会话、用户几乎见不到空会话；
            //   Step 1 把 N 个会话都摆到屏上之后，它变成每点一次「新建」就出现一次。
            std::snprintf(buf, sizeof(buf), "%s", "空会话 · 无 core");
        }
        m_hud.sessProgress->SetText(buf);
    }
    if (m_replayBar) {
        if (m_sess->replay) {
            const size_t total = m_sess->replay->Total();
            m_replayBar->SetProgress(total > 0
                                         ? static_cast<float>(m_sess->replay->Consumed()) /
                                               static_cast<float>(total)
                                         : 0.0f);
        } else {
            m_replayBar->SetProgress(0.0f);   // live：总量未知，进度条不表达
        }
    }

    if (HudTouch("healthSeq"), m_hud.healthSeq) {
        const size_t gaps = m_sess->transcript.SeqGaps();
        if (gaps == 0) {
            m_hud.healthSeq->SetText("seq 连续 · 0 缺口");
            m_hud.healthSeq->SetTextColor(kDarkInk);
        } else {
            m_hud.healthSeq->SetText("seq 缺口 " + std::to_string(gaps));
            m_hud.healthSeq->SetTextColor(kErrorDark);
        }
    }
    if (HudTouch("healthViol"), m_hud.healthViol) {
        m_hud.healthViol->SetText("违规 " + std::to_string(m_sess->transcript.Violations()) +
                                  " · 未知 " + std::to_string(m_sess->transcript.UnknownTypes()));
    }
    // ★ 未知 kind 单列一行（2026-10-13）：它与「未知 t」管辖**不同的判别符**，
    //   合并会同时导致①语义误读（把「协议空白」读成「I5 已覆盖」）
    //   ②文本裁切（并进上行实测被裁，见 BuildInspector 注释）。
    if (HudTouch("healthKind"), m_hud.healthKind) {
        const size_t uk = m_sess->transcript.UnknownKinds();
        if (uk == 0) {
            m_hud.healthKind->SetText("未知 kind · 0");
            m_hud.healthKind->SetTextColor(kDarkMuted);
        } else {
            // 协议 §3.3 没有这些值 ⇒ 用琥珀色（与 seq 缺口的红、正常的灰区分）
            // ★ 2026-10-18：`⚠`(U+26A0) 两主题 face 都无字形 ⇒ `△`(U+25B3) 空心三角
            //   （本身就是警示语义，且 `▲` 同族已在用），配琥珀色。
            m_hud.healthKind->SetText("未知 kind · " + std::to_string(uk) + " △");
            m_hud.healthKind->SetTextColor(kWarn);
        }
    }
    if (HudTouch("evalPaints"), m_hud.evalPaints) {
        m_hud.evalPaints->SetText(std::to_string(m_evalPaints) + " 次 · 请求 " +
                                  std::to_string(m_invalidations));
    }
    if (HudTouch("probeCmds"), m_hud.probeCmds) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeCmds->SetText("avg " + std::to_string(m_evalProbeAvg));
        } else {
            m_hud.probeCmds->SetText("—");
        }
    }
    if (HudTouch("probeExec"), m_hud.probeExec) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeExec->SetText("avg " +
                                     std::to_string(static_cast<unsigned long long>(m_evalAvgExecUs)) +
                                     " us");
        } else {
            m_hud.probeExec->SetText("—");
        }
    }
    if (HudTouch("usageInV"), m_hud.usageInV) {
        m_hud.usageInV->SetText(m_sess->usageIn < 0 ? "—"
                                              : std::to_string(m_sess->usageIn));
    }
    if (HudTouch("usageOutV"), m_hud.usageOutV) {
        m_hud.usageOutV->SetText(m_sess->usageOut < 0 ? "—"
                                                : std::to_string(m_sess->usageOut));
    }
    // ★★ 上下文占用百分比（协议 v2 §3.1，2026-10-14）——**用户要求**。
    //   ★ 分母 = `hello.context`（**唯一来源**）；**不再**用 `max(input, output)`。
    //     旧写法是**拿输入跟输出比**（两个不同量纲的东西相除）⇒ 读数无意义，
    //     现已被"真正的分母"取代。这是本行**修掉的一个既有缺陷**，不只是新增。
    //   ★ 分子 = `usage.input`（= 本轮送进模型的上下文规模）。**不含 output**：
    //     output 是本轮新生成的，尚未回到下一轮的上下文里。
    //   ★ 三者任一未知 ⇒ 显示 `—` 且条归零（**不猜**；分母未知时百分比无定义）。
    // ★★ P4 H 修复（P3.6 遗留接线）：分母改 **ctxCapacity**（v4 逐轮覆盖：
    // hello.context ⇒ usage.context ⇒ model ack 逐级写它）——此前只写不读，
    // 百分比/环一直用 v2 的 contextLimit，usage.context 覆盖从未生效。
    // ★ P4 H：顶栏模型 chip 的常驻数据同源刷新。
    UpdateModelChip();
    const bool ctxKnown = (m_sess->ctxCapacity > 0 && m_sess->usageIn >= 0);
    if (HudTouch("ctxPct"), m_hud.ctxPct) {
        if (!ctxKnown) {
            m_hud.ctxPct->SetText("—");
            m_hud.ctxPct->SetTextColor(kDarkMuted);
        } else {
            // 四舍五入到整数百分比；占用可能 >100%（core 低估容量）⇒ 如实显示，不钳
            const long long pct =
                (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity;
            m_hud.ctxPct->SetText(std::to_string(pct) + "%");
            // 配色：<70% 常规 · 70–90% 琥珀（该收尾了）· >90% 红（快溢出）
            m_hud.ctxPct->SetTextColor(pct >= 90 ? kErrorDark
                                                 : (pct >= 70 ? kWarn : kOkDark));
        }
    }
    if (m_ctxRing) {
        const float frac = ctxKnown
                               ? static_cast<float>(m_sess->usageIn) /
                                     static_cast<float>(m_sess->ctxCapacity)
                               : 0.0f;
        // 环本身钳到 [0,1]（SetProgress 契约）；文本仍如实显示 >100%
        m_ctxRing->SetProgress(frac > 1.0f ? 1.0f : frac);
        // ★ 环色与右侧数字**同源**（同一组阈值），避免「环是绿的、字是红的」自相矛盾。
        //   阈值沿用既有口径：<70 常规绿 · 70–90 琥珀 · >90 红；分母未知 ⇒ 回退绿（环归零）。
        const long long pctForColor =
            ctxKnown ? (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity : 0;
        const int level = pctForColor >= 90 ? 2 : (pctForColor >= 70 ? 1 : 0);
        // ★★ 只在**档位翻转**时 SetStyle + 补重绘——不能每 tick 都做：
        //   `RingProgressBar::SetStyle` 的头**未**声明「覆盖后 Invalidate」
        //   （对比 `Panel.h:39` 明确写了），实现又在 lib 里 ⇒ 不能假定它内建重绘；
        //   但每 tick 补一次 `RequestRepaint()` 会把 `invalidations`/`paints`
        //   凭空刷高（cot instant 20s ≈ 600 ticks ⇒ 16 变 ~616），而这两个数正是
        //   本项目的**评估读数**（与 p32 基线逐位对比）⇒ 那是「自己的仪器污染实验」
        //   （条 57③）。档位翻转全场至多 2 次 ⇒ 污染可忽略且可解释。
        if (level != m_ctxRingLevel) {
            m_ctxRingLevel = level;
            m_ctxRing->SetStyle(ECDI::RingProgressBarStyleOverride{
                .fillColor = level == 2 ? kErrorDark
                                        : (level == 1 ? kWarn : harness::kOkDark)});
            RequestRepaint();   // 颜色变了 ⇒ 确保这一帧被重画
        }
    }
    if (m_usageBarIn && m_usageBarOut) {
        // ★ 输入/输出两根条改为**以上下文容量为共同分母**（原来拿二者峰值互比，
        //   两个不同量纲相除 ⇒ 无意义）。容量未知时退化为二者峰值互比，
        //   仅作"相对大小"提示——**不**声称是占用率。
        const long long denom = (m_sess->contextLimit > 0)
                                    ? m_sess->contextLimit
                                    : std::max(m_sess->usageIn, m_sess->usageOut);
        const float inFrac = (m_sess->usageIn >= 0 && denom > 0)
                                 ? static_cast<float>(m_sess->usageIn) / static_cast<float>(denom)
                                 : 0.0f;
        const float outFrac = (m_sess->usageOut >= 0 && denom > 0)
                                  ? static_cast<float>(m_sess->usageOut) / static_cast<float>(denom)
                                  : 0.0f;
        m_usageBarIn->SetProgress(inFrac > 1.0f ? 1.0f : inFrac);
        m_usageBarOut->SetProgress(outFrac > 1.0f ? 1.0f : outFrac);
    }
    // 权限历史：无记录时给一行占位（否则是一张 70px 高的空盒子——`v8-light-insp.png` 可见）。
    // ★ 为何不 `SetVisible(false)` 把卡片藏掉：`Widget::SetVisible` **故意不内建 Invalidate**
    //   （`Widget.h:160-170` 明写「静默不刷新」），harness 还得自己补重绘；而「协议允许问、
    //   但本次一次都没问」本身是**有信息量的评估读数**（ask 门是 I3 的可见载体），
    //   藏掉不如显式说「暂无」。复用现有 3 行控件 ⇒ 零新增控件、零布局改动。
    const bool noHist = m_sess->askHist.empty();
    for (size_t i = 0; i < 3; ++i) {
        if (m_hud.histChip[i] && m_hud.histWhat[i]) {
            const size_t n = m_sess->askHist.size();
            if (i < n) {
                const auto& [choice, what] = m_sess->askHist[n - 1 - i];
                m_hud.histChip[i]->SetText(choice);
                m_hud.histChip[i]->SetTextColor(choice == "allow" ? kOkDark : kErrorDark);
                m_hud.histWhat[i]->SetText(what);
            } else {
                m_hud.histChip[i]->SetText(noHist && i == 0 ? "—" : "");
                m_hud.histChip[i]->SetTextColor(kDarkMuted);
                m_hud.histWhat[i]->SetText(noHist && i == 0 ? "暂无权限请求" : "");
            }
        }
    }

    if (m_hud.stState) {
        m_hud.stState->SetText(stateText + " · protocol " +
                               std::to_string(m_sess->protocolVer));
    }
    if (m_hud.stMsgs) {
        if (m_sess->replay) {
            m_hud.stMsgs->SetText(std::to_string(m_sess->replay->Consumed()) + " / " +
                                  std::to_string(m_sess->replay->Total()) + " msg");
        } else {
            m_hud.stMsgs->SetText(std::to_string(m_sess->liveLines) + " msg（live）");
        }
    }
    if (m_hud.stTokens) {
        if (m_sess->usageIn < 0) {
            m_hud.stTokens->SetText("tokens —");
        } else {
            m_hud.stTokens->SetText("tokens " + std::to_string(m_sess->usageIn) + " / " +
                                    std::to_string(m_sess->usageOut));
        }
    }
    if (m_hud.stPaint) {
        m_hud.stPaint->SetText("paints " + std::to_string(m_evalPaints) + " · 上次 " +
                               std::to_string(m_evalLastUs) + " us");
    }
}

// ── 退出摘要（规格 §6.3）───────────────────────────────────────

