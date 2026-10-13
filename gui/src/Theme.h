#pragma once
// harness 视觉令牌（2026-10-06 v6：按用户高保真模型 harness-hifi-variant-a.html 逐值对齐）。
// 唯一颜色真相源：App.cpp 与 PromptBar.cpp 都从这里取色，禁止就地写 RGBA。
// 对应关系（hi-fi CSS 变量 → 本文件令牌）：
//   --app-bg    #16181d → kContentBg   （转录画布 / 窗口兜底）
//   --app-panel #1e2128 → kDarkTopBg/kSidebarBg/kInspBg/kCardBg
//   --app-panel2#23262f → kPanel2      （chip/卡片/工具卡头/输入框底）
//   --app-line  #2e333e → kCardBorder 等描边
//   --app-user  #263449 → kUserBg     （气泡 + 描边 #3a506e）
//   --app-tool  #20242c → kToolBg
//   --app-ask   #3a2f1d → kAskBg      （描边 #8a6d3b）
//   accent #5b9dff · ok #3fbf7f · warn #e0a33c · err #e0564a

#include "ECDI/Core/Color.h"

namespace harness {

// ── 框架面（hi-fi --app-panel）────────────────────────────────
inline const ECDI::Color kDarkTopBg  = ECDI::Color::FromRGBA8(30, 33, 40, 255);    // 顶栏/状态栏
inline const ECDI::Color kSidebarBg  = ECDI::Color::FromRGBA8(30, 33, 40, 255);    // 左栏
inline const ECDI::Color kInspBg     = ECDI::Color::FromRGBA8(30, 33, 40, 255);    // 右栏/Composer
inline const ECDI::Color kContentBg  = ECDI::Color::FromRGBA8(22, 24, 29, 255);    // 转录画布 --app-bg
inline const ECDI::Color kPanel2     = ECDI::Color::FromRGBA8(35, 38, 47, 255);    // --app-panel2
inline const ECDI::Color kLine       = ECDI::Color::FromRGBA8(46, 51, 62, 255);    // --app-line

// ── 文字 ──────────────────────────────────────────────────────
inline const ECDI::Color kDarkInk    = ECDI::Color::FromRGBA8(230, 233, 239, 255); // --app-fg
inline const ECDI::Color kDarkMuted  = ECDI::Color::FromRGBA8(154, 162, 177, 255); // --app-dim
inline const ECDI::Color kDarkAccent = ECDI::Color::FromRGBA8(91, 157, 255, 255);  // --accent
inline const ECDI::Color kAccentInk  = ECDI::Color::FromRGBA8(207, 225, 255, 255); // accent 面上的字 #cfe1ff

inline const ECDI::Color kOkDark     = ECDI::Color::FromRGBA8(63, 191, 127, 255);  // --ok
inline const ECDI::Color kRunDark    = ECDI::Color::FromRGBA8(91, 157, 255, 255);
inline const ECDI::Color kErrorDark  = ECDI::Color::FromRGBA8(224, 86, 74, 255);   // --err
inline const ECDI::Color kWarn       = ECDI::Color::FromRGBA8(224, 163, 60, 255);  // --warn
inline const ECDI::Color kWarnInk    = ECDI::Color::FromRGBA8(34, 26, 12, 255);    // 琥珀钮上的字 #221a0c

// ── 转录卡片 ──────────────────────────────────────────────────
inline const ECDI::Color kCardBg       = ECDI::Color::FromRGBA8(30, 33, 40, 255);  // assistant 气泡 --app-panel
inline const ECDI::Color kCardBorder   = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kUserBg       = ECDI::Color::FromRGBA8(38, 52, 73, 255);  // user 气泡 --app-user
inline const ECDI::Color kUserBorder   = ECDI::Color::FromRGBA8(58, 80, 110, 255); // #3a506e
inline const ECDI::Color kUserInk      = ECDI::Color::FromRGBA8(232, 238, 247, 255); // #e8eef7
inline const ECDI::Color kToolBg       = ECDI::Color::FromRGBA8(32, 36, 44, 255);  // --app-tool
inline const ECDI::Color kToolBorder   = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kToolHeaderBg      = ECDI::Color::FromRGBA8(38, 42, 52, 255);  // hover ≈ rgba(255,255,255,.04)
inline const ECDI::Color kToolHeaderHover   = ECDI::Color::FromRGBA8(42, 46, 56, 255);
inline const ECDI::Color kToolHeaderPressed = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kToolHeaderBorder  = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kAskBg        = ECDI::Color::FromRGBA8(58, 47, 29, 255);  // --app-ask
inline const ECDI::Color kAskBorder    = ECDI::Color::FromRGBA8(138, 109, 59, 255); // --app-ask-line
inline const ECDI::Color kAskQInk      = ECDI::Color::FromRGBA8(245, 232, 208, 255); // .gate .q #f5e8d0
inline const ECDI::Color kDenyInk      = ECDI::Color::FromRGBA8(232, 213, 181, 255); // .btn.deny #e8d5b5
inline const ECDI::Color kDenyHover    = ECDI::Color::FromRGBA8(83, 64, 57, 255);    // deny hover ≈ err 15% 混入

// ── 按钮 ──────────────────────────────────────────────────────
inline const ECDI::Color kBtnPrimary        = ECDI::Color::FromRGBA8(91, 157, 255, 255);  // .btn.send --accent
inline const ECDI::Color kBtnPrimaryHover   = ECDI::Color::FromRGBA8(124, 177, 255, 255); // #7cb1ff
inline const ECDI::Color kBtnPrimaryPressed = ECDI::Color::FromRGBA8(74, 138, 240, 255);
inline const ECDI::Color kBtnPrimaryInk     = ECDI::Color::FromRGBA8(11, 18, 32, 255);    // #0b1220
inline const ECDI::Color kBtnNeutralBg      = ECDI::Color::FromRGBA8(35, 38, 47, 255);    // panel2（≈透明底）
inline const ECDI::Color kBtnNeutralBorder  = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kBtnDenyBg         = ECDI::Color::FromRGBA8(58, 47, 29, 255);    // = kAskBg（视觉透明）

// ★ REQ-04 禁用态（2026-10-13）：ECDI **有意不做** disabled 绘制（YAGNI——
//   `modelprobe-p1-preliminary-design.md:130`「disabled 视觉灰化：demo 层处理」）
//   ⇒「变灰」由应用侧自备；此处给灰底灰字，沿用 modelprobe 的 busy 态先例。
inline const ECDI::Color kBtnDisabledBg   = ECDI::Color::FromRGBA8(38, 41, 50, 255);   // 略深于 panel2
inline const ECDI::Color kBtnDisabledInk  = ECDI::Color::FromRGBA8(96, 102, 114, 255);  // 明显低于常态对比
inline const ECDI::Color kBtnDisabledLine = ECDI::Color::FromRGBA8(52, 57, 68, 255);

// ── 进度条 / 小控件 ───────────────────────────────────────────
inline const ECDI::Color kTrackBg     = ECDI::Color::FromRGBA8(35, 38, 47, 255);   // bar 轨道 = panel2
inline const ECDI::Color kDarkPillBg  = ECDI::Color::FromRGBA8(35, 38, 47, 255);   // chip 底 = panel2

// Composer 输入框（Phase 30 caretColor 落地——深底不再隐形光标，浅井退役）
inline const ECDI::Color kInputBg      = ECDI::Color::FromRGBA8(35, 38, 47, 255);
inline const ECDI::Color kInputBorder  = ECDI::Color::FromRGBA8(46, 51, 62, 255);
inline const ECDI::Color kInputCaret   = ECDI::Color::FromRGBA8(91, 157, 255, 255);  // 光标 = accent

inline constexpr float kRadiusCard  = 12.0f;   // 消息气泡
inline constexpr float kRadiusTool  = 10.0f;   // 工具卡 / 权限门
inline constexpr float kRadiusCard2 = 8.0f;    // 检查器小卡
inline constexpr float kRadiusBtn   = 7.0f;    // 按钮
inline constexpr float kRadiusInput = 9.0f;    // 输入框
inline constexpr float kRadiusPill  = 10.0f;   // chip（≈全圆）

inline const char* kMonoFamily = "Consolas";   // hi-fi --app-mono（Cascadia 不保证在，退 Consolas）

} // namespace harness