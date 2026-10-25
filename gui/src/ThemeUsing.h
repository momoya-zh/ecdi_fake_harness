#pragma once
// ============================================================
// 主题令牌短名（`harness::kDarkInk` → `kDarkInk`）
// ------------------------------------------------------------
// 为什么要有这个头：`Theme.h` 里的令牌全在 `namespace harness` 下，而
//   App.cpp 历来在文件级写 50 条 `using harness::xxx;` 让调用点保持短名。
//   2026-10-25 拆分 App.cpp 后，探针与各 UI 单元各成一个 TU，若每个文件
//   复制一遍这 50 行 ⇒ 加一个令牌要改 6 处（迟早漂移）。故收口到这里，
//   谁用谁 include（App.cpp 亦改为 include 本头，行为不变）。
//   ★ 这是**头文件级**的全局短名注入：只给 harness 自己的 .cpp 用，
//     不要被其他工程消费（Theme.h 本身仍然是唯一的令牌来源）。
// ============================================================
#include "Theme.h"

using harness::kDarkInk;
using harness::kDarkMuted;
using harness::kDarkAccent;
using harness::kAccentInk;
using harness::kOkDark;
using harness::kRunDark;
using harness::kErrorDark;
using harness::kWarn;
using harness::kWarnInk;
using harness::kSidebarBg;
using harness::kContentBg;
using harness::kInspBg;
using harness::kPanel2;
using harness::kLine;
using harness::kUserBg;
using harness::kUserBorder;
using harness::kUserInk;
using harness::kToolBg;
using harness::kToolBorder;
using harness::kToolHeaderBg;
using harness::kToolHeaderHover;
using harness::kToolHeaderPressed;
using harness::kToolHeaderBorder;
using harness::kReasoningBg;
using harness::kReasoningBorder;
using harness::kReasoningHeaderBg;
using harness::kReasoningHover;
using harness::kReasoningInk;
using harness::kAskBg;
using harness::kAskBorder;
using harness::kAskQInk;
using harness::kDenyInk;
using harness::kDenyHover;
using harness::kAllowHover;
using harness::kCardBg;
using harness::kCardBorder;
using harness::kBtnPrimary;
using harness::kBtnPrimaryHover;
using harness::kBtnPrimaryPressed;
using harness::kBtnPrimaryInk;
using harness::kBtnNeutralBg;
using harness::kBtnNeutralBorder;
using harness::kBtnDenyBg;
using harness::kTrackBg;
using harness::kRingTrack;
using harness::kDarkPillBg;
using harness::kRadiusCard;
using harness::kRadiusTool;
using harness::kRadiusCard2;
using harness::kRadiusBtn;
using harness::kRadiusPill;
using harness::kMonoFamily;
