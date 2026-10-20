#pragma once
// 底部输入行（hi-fi .composer：输入框 + 提示行 + 发送/停止；已收进中栏）。
// v1 仅 UI：点击只记录并请求重绘，不产生 gui→core 消息。
// ★ ECDI TextBox 多行/滚动已随 Phase 8.5.2 落地——v1 Composer 形态仍取单行
//   （聊天惯例：回车发送），升级多行是 harness 自身的待办。
// ★ 输入框深底时自画光标不可见（ECDI TextBox.cpp:1338 光标硬编码黑）——
//   caretColor 样式化落地前输入框保持浅底"浅井"。
// 子控件所有权归 Widget 树（AddChild 转移），成员只存非拥有指针。

#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/TextBox.h"

#include <functional>

class PromptBar : public ECDI::Panel {
public:
    using Action = std::function<void(const std::string&)>;

    PromptBar();

    void SetOnSend(Action action) { m_onSend = std::move(action); }
    void SetOnStop(Action action) { m_onStop = std::move(action); }

    ECDI::TextBox* input() const { return m_input; }

    // ★ REQ-04（2026-10-13）：暴露两个按钮，供 App 按 core 的 `state` 驱动使能。
    //   协议 §6 是硬要求：「发送 / 停止按钮**只由 `state` 驱动**（不要自己推测）」。
    //   此前 `m_send` / `m_stop` 私有且无访问器 ⇒ `App` 拿不到 `Button*`，
    //   判据 A5「state 驱动使能」**结构上不可达**（只登记、未兑现）。
    //   ★ ECDI 侧能力早已具备：`Widget::SetEnabled` 自 Phase 3 即在
    //   （`Widget.h:150-152`），且 `HitTest` 已按 `IsEnabled` 门控（`Widget.cpp:114-128`）
    //   ⇒ **纯 harness 侧缺口**，不需要 ECDI 任何改动。
    //   ★ 返回非拥有指针（所有权仍在 Widget 树），调用方不得 delete。
    ECDI::Button* sendButton() const { return m_send; }
    ECDI::Button* stopButton() const { return m_stop; }

    // 宽度变化时重排子控件（输入框铺满剩余宽，按钮固定）
    void SetSize(int w, int h) override;

    // ★ REQ-04：按 core 的 `state` 驱动两个按钮（协议 §6 的唯一真相源）。
    //   `enabled` 语义 = 「当前处于可发送的 idle 态」：
    //     · true  ⇒ 发送可点、停止置灰
    //     · false ⇒ 反之（running / waiting 都不可再发；waiting = ask 未答）
    //   ★ 同时做**视觉**降级：ECDI **有意不做** disabled 绘制（YAGNI——
    //   `modelprobe-p1-preliminary-design.md:130`「disabled 视觉灰化：demo 层处理」）,
    //   ⇒ 「变灰」是**调用方责任**，本方法显式替换禁用态配色。
    void SetStateDrivenEnabled(bool enabled);
    // v3 workspace handshake can disable Send without falsely enabling Stop.
    void SetButtonStates(bool sendEnabled, bool stopEnabled);

private:
    void LayoutChildren();

    // 禁用/启用态配色切换（灰化由应用侧自备——框架不提供 disabled 绘制）
    void ApplySendStyle(bool enabled);
    void ApplyStopStyle(bool enabled);

    ECDI::TextBox* m_input = nullptr;   // 非拥有——树所有
    ECDI::Label* m_hint = nullptr;      // 非拥有——"Enter 发送"提示行
    ECDI::Button* m_send = nullptr;     // 非拥有
    ECDI::Button* m_stop = nullptr;     // 非拥有
    Action m_onSend;
    Action m_onStop;
};