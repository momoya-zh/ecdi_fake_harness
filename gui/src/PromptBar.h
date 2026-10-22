#pragma once
// 底部输入行（hi-fi .composer：输入框 + 提示行 + 发送/停止；已收进中栏）。
// v1 仅 UI：点击只记录并请求重绘，不产生 gui→core 消息。
// ★ ECDI TextBox 多行/滚动已随 Phase 8.5.2 落地——v1 Composer 形态仍取单行
//   （聊天惯例：回车发送）。
//   ★★ 2026-10-21 兑现：**Enter 发送 / Shift+Enter 换行**已落地（见 `ComposerBox`）。
//     此前 `LayoutChildren` 里那句注释是诚实的：*"Keep the editor single-line until
//     Enter/newline behavior is implemented explicitly"* —— 现在它被实现了，
//     注释也一并改掉，不留"说没做、其实做了"的旧话。
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

    // ★★ 2026-10-21：**Enter 发送的单一入口**（按钮点击与按键最终都走这里）。
    //   ★ 为什么不把 `m_onSend` 直接交给 `ComposerBox`：那会让"按 Enter"与
    //     "点发送"成为两条各自生长的路径，而 `App` 侧 `SetOnSend` 里那次
    //     "清空输入框"的副作用就会只挂在其中一条上（另一条忘清 ⇒ 发出去的文字
    //     还留在框里）。收敛成一个函数，副作用只有一份。
    //   ★ 空输入**不发送**（见 `.cpp` 的实现与注释）：聊天界面里对空框按 Enter
    //     不该产生一条空消息。
    void Submit();
    // 供 `ComposerBox` 回报"用户按了 Enter"（它不拥有发送语义，只上报）。
    void NotifyEnterPressed() { Submit(); }

    // ★★ 2026-10-21（REQ-12 闭合后）：**多行自动长高**——内容高问框架，本类只管 clamp。
    //
    //   【曾经的缺陷】ECDI 对多行 `TextBox` 的 `GetPreferredSize` 返回**当前尺寸**
    //   （旧 `TextBox.cpp:166-174`「多行高度 v1 手工 SetSize，不参与 AutoSize」，
    //   挂账 K4 / `phase9.8-...-detailed-design.md:110`）⇒ `AutoSize()` 结构性 no-op。
    //   而同一条虚函数在 `TextWidget` 的 wrap 分支返回 `{GetWidth(), 行数×行高}`
    //   （`TextWidget.cpp:428-436`，需求稿 D29-B）⇒ 气泡能长高、输入框不能。
    //   **一个虚函数对同一个问题给了两个答案**，这是缺陷，不是两类控件的分野。
    //
    //   【处理过程】本仓先做组合实现（手抄「`\n` 数 + 1」「`LineHeight(font)」
    //   「padding×2」三条规则）让功能可用，同时按铁律把它登记为
    //   `docs/ecdi-capability-requests.md` 的 REQ-12。ECDI 采纳了 ⓐ 方案
    //   （订正 `GetPreferredSize` 语义，而非新增 `GetContentHeight()`），
    //   当日发布新库；本仓随即收敛：**三条规则还给框架**，
    //   `ContentHeight()` / `LineHeightNow()` 两个绕过用的脚手架一并删除。
    //
    //   【现在这个函数只做两件事】
    //     ① 问框架要内容高：`m_input->GetPreferredSize().height`
    //        （多行 = `ceil(行数 × 行高 + padding×2)`，取整已在框架内完成）
    //     ② clamp 到 [单行时的真实高度, `kInputMaxH`]——这是 UI 策略，不是内容测量
    //
    //   【为什么不用 `GetMaxScrollOffset() + GetTextAreaHeight()`】那正是
    //   `SyncScrollBar` 算 `contentExtent` 的式子（`TextBox.cpp:541`），它带 `max(0, ...)`
    //   ⇒ **不溢出时等于视口高**（拿它当内容高会导致"只会变高、永远缩不回去"）。
    //   ★ 这条陷阱是本设计里最容易踩的一个，故 `--grow-probe` 的 `shrink` 相专门盯它，
    //     负对照 `M5-monotonic-never-shrinks` 注入的正是这个错误形态。
    [[nodiscard]] int DesiredInputHeight();

    // 把 `DesiredInputHeight()` 应用到位（含 `PromptBar` 自身高度 + 父布局重排）。
    //   ★ 返回 true = 高度真的变了（调用方可据此决定是否请求重绘）。
    bool SyncInputHeight();

    // 输入框当前高度（探针读数用——避免探针自己再算一遍而与产品路径漂移）。
    [[nodiscard]] int inputHeight() const { return m_input ? m_input->GetHeight() : 0; }

    // 高成长度的上限（客户区 DIP）。★ public：探针要拿它当判据，不能各写一份。
    //   · 下限**不是常量**——它等于"单行时的 preferred 高"，直接从框架问出来
    //     （探针读 `--grow-probe` 的 `baseline.inputH`，实测 29）。改前这里曾有
    //     一个 `kInputMinH`，收敛时随"自己算下限"一起删掉了：手写常量会与字体
    //     绑死，而字体是可换的（FONT-UNIFIED 已经把行高从 +17% 拉到 +21%）。
    //   · 最大 = 6 行（超出的部分交给 `TextBox` 自己的内部滚动条——那条能力是既有的、
    //     且 `SyncScrollBar` 会按需显示，见 `scrollbar-placement-and-appearance.md`）
    static constexpr int kInputMaxH = 132;

private:
    void LayoutChildren();

    // 禁用/启用态配色切换（灰化由应用侧自备——框架不提供 disabled 绘制）
    void ApplySendStyle(bool enabled);
    void ApplyStopStyle(bool enabled);

    ECDI::TextBox* m_input = nullptr;   // 非拥有——树所有
    ECDI::Label* m_hint = nullptr;      // 非拥有——"Enter 发送 / Shift+Enter 换行"
    ECDI::Button* m_send = nullptr;     // 非拥有
    ECDI::Button* m_stop = nullptr;     // 非拥有
    Action m_onSend;
    Action m_onStop;
};