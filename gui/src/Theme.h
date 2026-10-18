#pragma once
// harness 视觉令牌 —— 唯一颜色真相源（App.cpp / PromptBar.cpp / Transcript.cpp 都从这里取色，
// 禁止就地写 RGBA）。
//
// ── 2026-10-18 v7：从「编译期常量」改为「运行期可切换的两套令牌」─────────────
// 背景：用户拿 DSH GUI 的浅色/深色两套主题做对照，指出浅色版才是他想要的方向；
//       `docs/prototype/harness-hifi-variant-c.html` 已按浅色做出来。
//       要在真 GUI 上验证浅色能不能落地，令牌必须是运行期数据，不能是 constexpr。
//
// ★ 设计要点（为什么不改成 `theme().ink` 那种访问器）：
//   全仓有 ~200 处按名字引用令牌（`kSidebarBg` / `kDarkInk` …）。若改成成员访问，
//   每一处都要动，改动面大且容易漏。这里改成**引用别名**：
//       struct Theme { ... };  inline Theme g_theme = MakeDark();
//       inline const ECDI::Color& kSidebarBg = g_theme.sidebarBg;
//   引用绑定的地址不变，`SwitchTheme()` 只改 `g_theme` 的内容 ⇒ **调用点零改动**。
//   ★ 静态初始化顺序：同一 TU 内按声明顺序初始化，`g_theme` 声明在所有别名之前 ⇒
//     别名绑定时 `g_theme` 已就绪（`inline` 变量全程序唯一，不会重复初始化）。
//
// ★ 只允许在 **构建 UI 之前** 调 `SwitchTheme()`。令牌是引用，UI 一旦建好就
//   把当时的颜色值拷进了各自的 `PanelStyle` / `ButtonStyle`，之后再切不会重绘。

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"

namespace harness {

// ── 令牌集合 ──────────────────────────────────────────────────
// 字段名 = 旧常量名去掉 `k` 前缀并改驼峰（`kDarkTopBg` → `darkTopBg`）。
struct Theme {
    // 框架面
    ECDI::Color darkTopBg;    // 顶栏/状态栏
    ECDI::Color sidebarBg;    // 左栏
    ECDI::Color inspBg;       // 右栏/Composer
    ECDI::Color contentBg;    // 转录画布（窗口兜底）
    ECDI::Color panel2;       // chip / 卡片 / 工具卡头 / 输入框底
    ECDI::Color line;         // 描边

    // 文字
    ECDI::Color darkInk;      // 正文
    ECDI::Color darkMuted;    // 次要
    ECDI::Color darkAccent;   // 强调（蓝）
    ECDI::Color accentInk;    // accent 面上的字

    // 状态
    ECDI::Color okDark;
    ECDI::Color runDark;
    ECDI::Color errorDark;
    ECDI::Color warn;
    ECDI::Color warnInk;

    // 转录卡片
    ECDI::Color cardBg;
    ECDI::Color cardBorder;
    ECDI::Color userBg;
    ECDI::Color userBorder;
    ECDI::Color userInk;
    ECDI::Color toolBg;
    ECDI::Color toolBorder;
    ECDI::Color toolHeaderBg;
    ECDI::Color toolHeaderHover;
    ECDI::Color toolHeaderPressed;
    ECDI::Color toolHeaderBorder;

    // 思维链卡（协议 v2 §7）——设计意图：次要内容，整体比工具卡更弱
    ECDI::Color reasoningBg;
    ECDI::Color reasoningBorder;
    ECDI::Color reasoningHeaderBg;
    ECDI::Color reasoningHover;
    ECDI::Color reasoningInk;

    // 权限门（ask）
    ECDI::Color askBg;
    ECDI::Color askBorder;
    ECDI::Color askQInk;
    ECDI::Color denyInk;
    ECDI::Color denyHover;
    ECDI::Color allowHover;   // 「允许」钮 hover（原先就地写死 240,183,92，浅色下白字不可读）

    // 按钮
    ECDI::Color btnPrimary;
    ECDI::Color btnPrimaryHover;
    ECDI::Color btnPrimaryPressed;
    ECDI::Color btnPrimaryInk;
    ECDI::Color btnNeutralBg;
    ECDI::Color btnNeutralBorder;
    ECDI::Color btnDenyBg;
    ECDI::Color btnDisabledBg;
    ECDI::Color btnDisabledInk;
    ECDI::Color btnDisabledLine;

    // 进度条 / 小控件 / 输入框
    ECDI::Color trackBg;
    ECDI::Color ringTrack;    // 环形进度条的轨道色（★ 不能复用 line：浅色下 line 压在白卡上会消失）
    ECDI::Color darkPillBg;
    ECDI::Color inputBg;
    ECDI::Color inputBorder;
    ECDI::Color inputCaret;

    // ★ 中文正文族名。空串 = ECDI 默认 face（实测两链都是 SimSun）。
    //   见 `docs/variant-c-ecdi-mapping.md` §3.1：浅色下换成雅黑观感更接近原型，
    //   但行高会 +17~21%，由 `LineHeight()` 动态算高度的调用点吸收。
    const char* sansFamily;
};

// ── 深色（v6，2026-10-06 按 harness-hifi-variant-a.html 逐值对齐）──────────
// 这是**既有交付外观**，一个字节都不改——浅色是新增的，深色是回归基线。
inline Theme MakeDark() {
    Theme t{};
    // 框架面
    t.darkTopBg  = ECDI::Color::FromRGBA8(30, 33, 40, 255);
    t.sidebarBg  = ECDI::Color::FromRGBA8(30, 33, 40, 255);
    t.inspBg     = ECDI::Color::FromRGBA8(30, 33, 40, 255);
    t.contentBg  = ECDI::Color::FromRGBA8(22, 24, 29, 255);
    t.panel2     = ECDI::Color::FromRGBA8(35, 38, 47, 255);
    t.line       = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    // 文字
    t.darkInk    = ECDI::Color::FromRGBA8(230, 233, 239, 255);
    t.darkMuted  = ECDI::Color::FromRGBA8(154, 162, 177, 255);
    t.darkAccent = ECDI::Color::FromRGBA8(91, 157, 255, 255);
    t.accentInk  = ECDI::Color::FromRGBA8(207, 225, 255, 255);
    // 状态
    t.okDark     = ECDI::Color::FromRGBA8(63, 191, 127, 255);
    t.runDark    = ECDI::Color::FromRGBA8(91, 157, 255, 255);
    t.errorDark  = ECDI::Color::FromRGBA8(224, 86, 74, 255);
    t.warn       = ECDI::Color::FromRGBA8(224, 163, 60, 255);
    t.warnInk    = ECDI::Color::FromRGBA8(34, 26, 12, 255);
    // 卡片
    t.cardBg       = ECDI::Color::FromRGBA8(30, 33, 40, 255);
    t.cardBorder   = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    t.userBg       = ECDI::Color::FromRGBA8(38, 52, 73, 255);
    t.userBorder   = ECDI::Color::FromRGBA8(58, 80, 110, 255);
    t.userInk      = ECDI::Color::FromRGBA8(232, 238, 247, 255);
    t.toolBg       = ECDI::Color::FromRGBA8(32, 36, 44, 255);
    t.toolBorder   = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    t.toolHeaderBg      = ECDI::Color::FromRGBA8(38, 42, 52, 255);
    t.toolHeaderHover   = ECDI::Color::FromRGBA8(42, 46, 56, 255);
    t.toolHeaderPressed = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    t.toolHeaderBorder  = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    // 思维链
    t.reasoningBg       = ECDI::Color::FromRGBA8(28, 31, 38, 255);
    t.reasoningBorder   = ECDI::Color::FromRGBA8(40, 45, 55, 255);
    t.reasoningHeaderBg = ECDI::Color::FromRGBA8(33, 37, 45, 255);
    t.reasoningHover    = ECDI::Color::FromRGBA8(38, 43, 52, 255);
    t.reasoningInk      = ECDI::Color::FromRGBA8(154, 162, 177, 255);
    // ask
    t.askBg     = ECDI::Color::FromRGBA8(58, 47, 29, 255);
    t.askBorder = ECDI::Color::FromRGBA8(138, 109, 59, 255);
    t.askQInk   = ECDI::Color::FromRGBA8(245, 232, 208, 255);
    t.denyInk   = ECDI::Color::FromRGBA8(232, 213, 181, 255);
    t.denyHover = ECDI::Color::FromRGBA8(83, 64, 57, 255);
    t.allowHover = ECDI::Color::FromRGBA8(240, 183, 92, 255);   // 既有值，原样保留
    // 按钮
    t.btnPrimary        = ECDI::Color::FromRGBA8(91, 157, 255, 255);
    t.btnPrimaryHover   = ECDI::Color::FromRGBA8(124, 177, 255, 255);
    t.btnPrimaryPressed = ECDI::Color::FromRGBA8(74, 138, 240, 255);
    t.btnPrimaryInk     = ECDI::Color::FromRGBA8(11, 18, 32, 255);
    t.btnNeutralBg      = ECDI::Color::FromRGBA8(35, 38, 47, 255);
    t.btnNeutralBorder  = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    t.btnDenyBg         = ECDI::Color::FromRGBA8(58, 47, 29, 255);
    t.btnDisabledBg     = ECDI::Color::FromRGBA8(38, 41, 50, 255);
    t.btnDisabledInk    = ECDI::Color::FromRGBA8(96, 102, 114, 255);
    t.btnDisabledLine   = ECDI::Color::FromRGBA8(52, 57, 68, 255);
    // 进度条 / 输入框
    t.trackBg     = ECDI::Color::FromRGBA8(35, 38, 47, 255);
    t.ringTrack   = ECDI::Color::FromRGBA8(46, 51, 62, 255);   // = line，既有值原样保留
    t.darkPillBg  = ECDI::Color::FromRGBA8(35, 38, 47, 255);
    t.inputBg     = ECDI::Color::FromRGBA8(35, 38, 47, 255);
    t.inputBorder = ECDI::Color::FromRGBA8(46, 51, 62, 255);
    t.inputCaret  = ECDI::Color::FromRGBA8(91, 157, 255, 255);
    t.sansFamily  = "";   // 保持既有渲染（SimSun），深色是回归基线，不动字形
    return t;
}

// ── 浅色（v7，2026-10-18 按 harness-hifi-variant-c.html 的 :root 映射）──────
// 映射依据见 `docs/variant-c-ecdi-mapping.md`；括号内是原型的 CSS 变量。
// ★ 对比度按用户口径调整过（他明确说浅色不必按深色标准苛求），但正文仍压到
//   `#23262e`/白 = **15.13:1**，次要 `#6b7078`/白 = **4.98:1**——
//   比原型的 8.20 / 2.68 高一档，不牺牲柔和观感。
inline Theme MakeLight() {
    Theme t{};
    // 框架面（--panel #f8f9fb / --surface #fff / --line #e7e9ee）
    t.darkTopBg  = ECDI::Color::FromRGBA8(248, 249, 251, 255);
    t.sidebarBg  = ECDI::Color::FromRGBA8(248, 249, 251, 255);
    t.inspBg     = ECDI::Color::FromRGBA8(248, 249, 251, 255);
    t.contentBg  = ECDI::Color::FromRGBA8(255, 255, 255, 255);
    t.panel2     = ECDI::Color::FromRGBA8(241, 243, 247, 255);
    t.line       = ECDI::Color::FromRGBA8(231, 233, 238, 255);
    // 文字（--ink #23262e / --muted #6b7078 / --blue #596de8）
    t.darkInk    = ECDI::Color::FromRGBA8(35, 38, 46, 255);
    t.darkMuted  = ECDI::Color::FromRGBA8(107, 112, 120, 255);
    t.darkAccent = ECDI::Color::FromRGBA8(89, 109, 232, 255);
    t.accentInk  = ECDI::Color::FromRGBA8(76, 96, 215, 255);
    // 状态（--green #28966b；error 取深一档保证白底可读）
    t.okDark     = ECDI::Color::FromRGBA8(36, 125, 89, 255);
    t.runDark    = ECDI::Color::FromRGBA8(89, 109, 232, 255);
    t.errorDark  = ECDI::Color::FromRGBA8(180, 66, 58, 255);
    t.warn       = ECDI::Color::FromRGBA8(138, 100, 32, 255);   // --amber-strong
    t.warnInk    = ECDI::Color::FromRGBA8(255, 255, 255, 255);  // 琥珀实心钮上的白字
    // 卡片
    // ★★ 2026-10-18 修：原值 `cardBg = 255,255,255` 与 `contentBg` **同值** ⇒ 助手气泡
    //   只剩 1px 描边，视觉上退化成「裸文本」（放大截图 `z3-v7-light-bubble.png` 可见）。
    //   深色从来不存在这个问题（`kCardBg (30,33,40)` vs `kContentBg (22,24,29)` = 1.10）。
    //   ⇒ 浅色改用**反向 ramp**：深色是「越重要越亮」，浅色是「越重要越暗」。
    //     以白画布为基准的 L* 偏移对齐深色（`reasoning` 3.49 → `card` 4.47 →
    //     `tool` 5.88 → `toolHeader` 8.82），即 1.8 / 2.4 / 3.5 / 5.7。
    //   ★ 为何不把 cardBg 压到 `#f1f3f7`（L* 4.2，数值上更接近深色）：那是右栏卡片的
    //     底色，助手气泡在正文区若用同值会与侧栏/右栏抢层级——正文区必须最亮。
    t.cardBg       = ECDI::Color::FromRGBA8(247, 248, 251, 255);   // #f7f8fb，dL* -2.4
    t.cardBorder   = ECDI::Color::FromRGBA8(226, 229, 236, 255);   // #e2e5ec（原 #e7e9ee 压白卡上太淡）
    t.userBg       = ECDI::Color::FromRGBA8(238, 240, 255, 255);   // --blue-soft
    t.userBorder   = ECDI::Color::FromRGBA8(216, 221, 250, 255);
    t.userInk      = ECDI::Color::FromRGBA8(35, 38, 46, 255);
    t.toolBg       = ECDI::Color::FromRGBA8(243, 245, 249, 255);   // #f3f5f9，dL* -3.5
    t.toolBorder   = ECDI::Color::FromRGBA8(224, 228, 236, 255);
    t.toolHeaderBg      = ECDI::Color::FromRGBA8(235, 239, 245, 255);   // #ebeff5，dL* -5.7
    t.toolHeaderHover   = ECDI::Color::FromRGBA8(228, 233, 241, 255);
    t.toolHeaderPressed = ECDI::Color::FromRGBA8(220, 226, 236, 255);
    t.toolHeaderBorder  = ECDI::Color::FromRGBA8(224, 228, 236, 255);
    // 思维链（比工具卡更弱 ⇒ 更接近白：dL* -1.8）
    // ★ 原值 `#fbfbfc` 只有 dL* -1.3，且**比助手卡还暗**（层级颠倒）。
    t.reasoningBg       = ECDI::Color::FromRGBA8(249, 250, 252, 255);   // #f9fafc
    t.reasoningBorder   = ECDI::Color::FromRGBA8(226, 230, 238, 255);   // #e2e6ee
    t.reasoningHeaderBg = ECDI::Color::FromRGBA8(242, 244, 248, 255);   // #f2f4f8，dL* -3.9
    t.reasoningHover    = ECDI::Color::FromRGBA8(235, 238, 244, 255);
    t.reasoningInk      = ECDI::Color::FromRGBA8(107, 112, 120, 255);
    // ask（--amber-bg #fdf7ea / --amber-line #ecdcb6 / --amber-ink #7a5a1e）
    t.askBg     = ECDI::Color::FromRGBA8(253, 247, 234, 255);
    t.askBorder = ECDI::Color::FromRGBA8(236, 220, 182, 255);
    t.askQInk   = ECDI::Color::FromRGBA8(74, 60, 28, 255);
    t.denyInk   = ECDI::Color::FromRGBA8(122, 90, 30, 255);
    t.denyHover = ECDI::Color::FromRGBA8(246, 236, 216, 255);
    t.allowHover = ECDI::Color::FromRGBA8(116, 84, 26, 255);   // 比常态**深**一档：白字要一直读得清
    // 按钮
    t.btnPrimary        = ECDI::Color::FromRGBA8(76, 96, 215, 255);   // #4c60d7，白字 5.30:1
    t.btnPrimaryHover   = ECDI::Color::FromRGBA8(91, 110, 224, 255);
    t.btnPrimaryPressed = ECDI::Color::FromRGBA8(63, 82, 196, 255);
    t.btnPrimaryInk     = ECDI::Color::FromRGBA8(255, 255, 255, 255);
    t.btnNeutralBg      = ECDI::Color::FromRGBA8(241, 243, 247, 255);
    t.btnNeutralBorder  = ECDI::Color::FromRGBA8(231, 233, 238, 255);
    t.btnDenyBg         = ECDI::Color::FromRGBA8(253, 247, 234, 255);
    t.btnDisabledBg     = ECDI::Color::FromRGBA8(241, 243, 247, 255);
    t.btnDisabledInk    = ECDI::Color::FromRGBA8(168, 173, 184, 255);
    t.btnDisabledLine   = ECDI::Color::FromRGBA8(231, 233, 238, 255);
    // 进度条 / 输入框
    t.trackBg     = ECDI::Color::FromRGBA8(238, 240, 244, 255);
    // ★★ 2026-10-18 修：环形进度条轨道原先复用 `kLine`（`App.cpp:667` 的注释
    //   「不用 kTrackBg（与卡片同值 ⇒ 看不见整圆）」），但 `kLine #e7e9ee` 压在白卡
    //   `kPanel2` 上只有 **1.09:1** ⇒ 浅色下整圈轨道消失（`v7-light-insp.png` 可见）。
    //   深色同一对是 1.19:1，勉强可见。浅色需独立更深的轨道色：1.24:1。
    t.ringTrack   = ECDI::Color::FromRGBA8(216, 220, 230, 255);   // #d8dce6
    t.darkPillBg  = ECDI::Color::FromRGBA8(241, 243, 247, 255);
    t.inputBg     = ECDI::Color::FromRGBA8(255, 255, 255, 255);
    t.inputBorder = ECDI::Color::FromRGBA8(224, 227, 233, 255);
    t.inputCaret  = ECDI::Color::FromRGBA8(76, 96, 215, 255);
    // ★ 浅色下中文换成雅黑（原型 --font 里 "Microsoft YaHei" 就在第二顺位）。
    //   两链都能解析（`docs/variant-c-ecdi-mapping.md` §2.3）。
    t.sansFamily  = "Microsoft YaHei";
    return t;
}

// ── 当前生效的令牌（全程序唯一）────────────────────────────────
inline Theme g_theme = MakeDark();
inline bool g_themeLight = false;

/// 切换主题。★ 必须在 `BuildUi()` 之前调用（见文件头说明）。
inline void SwitchTheme(bool light) {
    g_theme = light ? MakeLight() : MakeDark();
    g_themeLight = light;
}

// ── 引用别名（调用点零改动）────────────────────────────────────
inline const ECDI::Color& kDarkTopBg  = g_theme.darkTopBg;
inline const ECDI::Color& kSidebarBg  = g_theme.sidebarBg;
inline const ECDI::Color& kInspBg     = g_theme.inspBg;
inline const ECDI::Color& kContentBg  = g_theme.contentBg;
inline const ECDI::Color& kPanel2     = g_theme.panel2;
inline const ECDI::Color& kLine       = g_theme.line;

inline const ECDI::Color& kDarkInk    = g_theme.darkInk;
inline const ECDI::Color& kDarkMuted  = g_theme.darkMuted;
inline const ECDI::Color& kDarkAccent = g_theme.darkAccent;
inline const ECDI::Color& kAccentInk  = g_theme.accentInk;

inline const ECDI::Color& kOkDark     = g_theme.okDark;
inline const ECDI::Color& kRunDark    = g_theme.runDark;
inline const ECDI::Color& kErrorDark  = g_theme.errorDark;
inline const ECDI::Color& kWarn       = g_theme.warn;
inline const ECDI::Color& kWarnInk    = g_theme.warnInk;

inline const ECDI::Color& kCardBg       = g_theme.cardBg;
inline const ECDI::Color& kCardBorder   = g_theme.cardBorder;
inline const ECDI::Color& kUserBg       = g_theme.userBg;
inline const ECDI::Color& kUserBorder   = g_theme.userBorder;
inline const ECDI::Color& kUserInk      = g_theme.userInk;
inline const ECDI::Color& kToolBg       = g_theme.toolBg;
inline const ECDI::Color& kToolBorder   = g_theme.toolBorder;
inline const ECDI::Color& kToolHeaderBg      = g_theme.toolHeaderBg;
inline const ECDI::Color& kToolHeaderHover   = g_theme.toolHeaderHover;
inline const ECDI::Color& kToolHeaderPressed = g_theme.toolHeaderPressed;
inline const ECDI::Color& kToolHeaderBorder  = g_theme.toolHeaderBorder;

inline const ECDI::Color& kReasoningBg       = g_theme.reasoningBg;
inline const ECDI::Color& kReasoningBorder   = g_theme.reasoningBorder;
inline const ECDI::Color& kReasoningHeaderBg = g_theme.reasoningHeaderBg;
inline const ECDI::Color& kReasoningHover    = g_theme.reasoningHover;
inline const ECDI::Color& kReasoningInk      = g_theme.reasoningInk;

inline const ECDI::Color& kAskBg        = g_theme.askBg;
inline const ECDI::Color& kAskBorder    = g_theme.askBorder;
inline const ECDI::Color& kAskQInk      = g_theme.askQInk;
inline const ECDI::Color& kDenyInk      = g_theme.denyInk;
inline const ECDI::Color& kDenyHover    = g_theme.denyHover;
inline const ECDI::Color& kAllowHover   = g_theme.allowHover;

inline const ECDI::Color& kBtnPrimary        = g_theme.btnPrimary;
inline const ECDI::Color& kBtnPrimaryHover   = g_theme.btnPrimaryHover;
inline const ECDI::Color& kBtnPrimaryPressed = g_theme.btnPrimaryPressed;
inline const ECDI::Color& kBtnPrimaryInk     = g_theme.btnPrimaryInk;
inline const ECDI::Color& kBtnNeutralBg      = g_theme.btnNeutralBg;
inline const ECDI::Color& kBtnNeutralBorder  = g_theme.btnNeutralBorder;
inline const ECDI::Color& kBtnDenyBg         = g_theme.btnDenyBg;
inline const ECDI::Color& kBtnDisabledBg     = g_theme.btnDisabledBg;
inline const ECDI::Color& kBtnDisabledInk    = g_theme.btnDisabledInk;
inline const ECDI::Color& kBtnDisabledLine   = g_theme.btnDisabledLine;

inline const ECDI::Color& kTrackBg     = g_theme.trackBg;
inline const ECDI::Color& kRingTrack   = g_theme.ringTrack;
inline const ECDI::Color& kDarkPillBg  = g_theme.darkPillBg;
inline const ECDI::Color& kInputBg     = g_theme.inputBg;
inline const ECDI::Color& kInputBorder = g_theme.inputBorder;
inline const ECDI::Color& kInputCaret  = g_theme.inputCaret;

// ── 圆角（与主题无关，仍是编译期常量）────────────────────────
inline constexpr float kRadiusCard  = 12.0f;   // 消息气泡
inline constexpr float kRadiusTool  = 10.0f;   // 工具卡 / 权限门
inline constexpr float kRadiusCard2 = 8.0f;    // 检查器小卡
inline constexpr float kRadiusBtn   = 7.0f;    // 按钮
inline constexpr float kRadiusInput = 9.0f;    // 输入框
inline constexpr float kRadiusPill  = 10.0f;   // chip（≈全圆）

inline const char* kMonoFamily = "Consolas";   // hi-fi --app-mono（Cascadia 不保证在，退 Consolas）

// ── 字体工厂（2026-10-18）──────────────────────────────────────
// 全仓构造 `ECDI::Font` 都该走这里，理由：
//   · **中文正文族名要能随主题变**（深色保持 SimSun 回归基线，浅色换雅黑）；
//   · 漏写 `family` 的子控件会各自拿到"空 = SimSun"，**同一界面里两种中文字形**——
//     这是 2026-10-18 之前实际存在的情况（只有 Consolas 被显式指定过）。
// ★ 等宽（代码/工具体）**不受主题影响**，恒 Consolas。
inline ECDI::Font SansFont(float size) {
    ECDI::Font f;
    f.size = size;
    f.family = g_theme.sansFamily;
    return f;
}

inline ECDI::Font MonoFont(float size) {
    ECDI::Font f;
    f.size = size;
    f.family = kMonoFamily;
    return f;
}

} // namespace harness
