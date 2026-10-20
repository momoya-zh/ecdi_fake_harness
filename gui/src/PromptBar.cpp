#include "PromptBar.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"

#include "Theme.h"

#include <memory>

namespace {

// ── 输入框：把 ECDI 默认的「点线焦点框」换成「实线描边随焦点变色」──────
// 为什么要换（实测取证，`.scratch/shot-b/v8-composer-zoom.png`）：
//   ECDI `TextBox::OnPaint` 有**两套**边框（`TextBox.cpp:1229-1265`）：
//     ① `borderWidth/borderColor` ⇒ 实心圆角描边环（我们设了 1px 浅灰）；
//     ② `HasFocus() && ShowFocusRect()` ⇒ `DrawFocusRect` **点线框**，颜色取
//        `TextBoxStyle.border`（默认品牌蓝）。
//   本项目的输入框**启动即获焦**（`App.cpp:325` `win.SetFocusedWidget(m_promptBar->input())`，
//   验收 A2「打开即可打字」），⇒ ②**永远**盖在①上 ⇒ 常态就是一圈蓝色虚线。
//   variant-c 的 `.composer` 是实线 1px 描边，两者形态冲突。
// 换法：关掉②，改让①的颜色表达焦点——
//   失焦 `kInputBorder`（浅灰）/ 获焦 `kDarkAccent`（品牌蓝）。
// ★ 为何不是直接 `SetShowFocusRect(false)` 了事：那样焦点**完全不可见**，
//   是拿可访问性换观感。实线描边换色既消掉虚线，焦点指示还比点线框**对比更高**。
class ComposerBox : public ECDI::TextBox {
public:
    ComposerBox() {
        SetShowFocusRect(false);   // Widget.h:274；焦点改由 borderColor 表达
        ApplyBorder(false);
    }

protected:
    void OnFocusGained() override {
        ECDI::TextBox::OnFocusGained();
        ApplyBorder(true);
    }
    void OnFocusLost() override {
        ECDI::TextBox::OnFocusLost();
        ApplyBorder(false);
    }

private:
    void ApplyBorder(bool focused) {
        SetStyle(ECDI::TextBoxStyleOverride{
            .background = harness::kInputBg,
            .caretColor = harness::kInputCaret,
            .padding = 6.0f,
            .cornerRadius = harness::kRadiusInput,
            .borderWidth = 1.0f,
            .borderColor = focused ? harness::kDarkAccent : harness::kInputBorder});
    }
};

}  // namespace

PromptBar::PromptBar() {
    SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kInspBg,
        .borderWidth = 1.0f,
        .borderColor = harness::kLine});

    auto input = std::make_unique<ComposerBox>();
    m_input = input.get();
    m_input->SetSingleLine(true);
    // Phase 30 caretColor：深底输入框可用（光标 = accent），hi-fi 时代的"浅井"退役。
    // ★ 样式（含焦点描边色）已由 ComposerBox 自管，此处不再 SetStyle。
    AddChild(std::move(input));

    auto hint = std::make_unique<ECDI::Label>("点击发送提交");
    m_hint = hint.get();
    m_hint->SetTextColor(harness::kDarkMuted);
    m_hint->SetFont(harness::SansFont(10.5f));
    AddChild(std::move(hint));

    auto send = std::make_unique<ECDI::Button>("发送");
    m_send = send.get();
    m_send->SetTextColor(harness::kBtnPrimaryInk);
    m_send->SetFont(harness::SansFont(13.0f));
    m_send->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kBtnPrimary,
        .cornerRadius = 8.0f,
        .pressedBackground = harness::kBtnPrimaryPressed,
        .hoverBackground = harness::kBtnPrimaryHover});
    m_send->SetOnClick([this] {
        if (m_onSend) m_onSend(m_input ? m_input->GetText() : std::string());
    });
    AddChild(std::move(send));

    auto stop = std::make_unique<ECDI::Button>("停止");
    m_stop = stop.get();
    m_stop->SetTextColor(harness::kDarkMuted);
    m_stop->SetFont(harness::SansFont(13.0f));
    m_stop->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kInspBg,
        .border = harness::kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .hoverBackground = harness::kPanel2});
    m_stop->SetOnClick([this] {
        if (m_onStop) m_onStop(std::string());
    });
    AddChild(std::move(stop));
}

void PromptBar::SetSize(int w, int h) {
    ECDI::Panel::SetSize(w, h);
    LayoutChildren();
}

// ── ★ REQ-04：state 驱动的使能与灰化 ─────────────────────────────
// 协议 §6：「发送 / 停止按钮**只由 `state` 驱动**（不要自己推测）」。
// ECDI 侧能力早已具备（`Widget::SetEnabled` 自 Phase 3；`HitTest` 已门控），
// 此前缺的只是本类不暴露按钮 + 未做灰化 ⇒ 判据 A5 结构上不可达。

void PromptBar::ApplySendStyle(bool enabled) {
    if (!m_send) return;
    m_send->SetTextColor(enabled ? harness::kBtnPrimaryInk : harness::kBtnDisabledInk);
    m_send->SetStyle(ECDI::ButtonStyleOverride{
        .background = enabled ? harness::kBtnPrimary : harness::kBtnDisabledBg,
        .cornerRadius = 8.0f,
        .pressedBackground = enabled ? harness::kBtnPrimaryPressed : harness::kBtnDisabledBg,
        .hoverBackground = enabled ? harness::kBtnPrimaryHover : harness::kBtnDisabledBg});
}

void PromptBar::ApplyStopStyle(bool enabled) {
    if (!m_stop) return;
    // 停止按钮的「启用」= 可中断（running）；禁用时灰化
    m_stop->SetTextColor(enabled ? harness::kDarkInk : harness::kBtnDisabledInk);
    m_stop->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kInspBg,
        .border = enabled ? harness::kLine : harness::kBtnDisabledLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .hoverBackground = enabled ? harness::kPanel2 : harness::kInspBg});
}

void PromptBar::SetStateDrivenEnabled(bool enabled) {
    SetButtonStates(enabled, !enabled);
}

void PromptBar::SetButtonStates(bool sendEnabled, bool stopEnabled) {
    // 行为与视觉分开驱动：workspace pending 禁用 Send，但不得启用 Stop。
    if (m_send) m_send->SetEnabled(sendEnabled);
    if (m_stop) m_stop->SetEnabled(stopEnabled);
    ApplySendStyle(sendEnabled);
    ApplyStopStyle(stopEnabled);
}

void PromptBar::LayoutChildren() {
    if (!m_input) return;
    const int buttonW = 64;
    const int padX = 12;
    // Give the composer a clear writing surface and a separate action row, like variant C.
    // Keep the editor single-line until Enter/newline behavior is implemented explicitly.
    m_input->SetSize(GetWidth() - 2 * padX, 44);
    m_input->SetPosition(padX, 8);
    if (m_hint) {
        m_hint->SetPosition(padX + 2, 66);
    }
    if (m_send) {
        m_send->SetSize(buttonW, 28);
        m_send->SetPosition(GetWidth() - padX - buttonW, 58);
    }
    if (m_stop) {
        m_stop->SetSize(buttonW, 28);
        m_stop->SetPosition(GetWidth() - padX - 2 * buttonW - 10, 58);
    }
}