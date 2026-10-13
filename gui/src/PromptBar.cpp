#include "PromptBar.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"

#include "Theme.h"

#include <memory>

PromptBar::PromptBar() {
    SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kInspBg,
        .borderWidth = 1.0f,
        .borderColor = harness::kLine});

    auto input = std::make_unique<ECDI::TextBox>();
    m_input = input.get();
    m_input->SetSingleLine(true);
    // Phase 30 caretColor：深底输入框可用（光标 = accent），hi-fi 时代的"浅井"退役。
    m_input->SetStyle(ECDI::TextBoxStyleOverride{
        .background = harness::kInputBg,
        .caretColor = harness::kInputCaret,
        .padding = 6.0f,
        .cornerRadius = harness::kRadiusInput,
        .borderWidth = 1.0f,
        .borderColor = harness::kInputBorder});
    AddChild(std::move(input));

    auto hint = std::make_unique<ECDI::Label>("Enter 发送 · Shift+Enter 换行");
    m_hint = hint.get();
    m_hint->SetTextColor(harness::kDarkMuted);
    {
        ECDI::Font f; f.size = 10.5f;
        m_hint->SetFont(f);
    }
    AddChild(std::move(hint));

    auto send = std::make_unique<ECDI::Button>("发送");
    m_send = send.get();
    m_send->SetTextColor(harness::kBtnPrimaryInk);
    {
        ECDI::Font f; f.size = 13.0f;
        m_send->SetFont(f);
    }
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
    {
        ECDI::Font f; f.size = 13.0f;
        m_stop->SetFont(f);
    }
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
    // ① 行为：真正的使能（`HitTest` 会按 `IsEnabled` 过滤 ⇒ 禁用后点击不达）
    if (m_send) m_send->SetEnabled(enabled);
    if (m_stop) m_stop->SetEnabled(!enabled);
    // ② 视觉：显式灰化（框架不做 disabled 绘制）
    ApplySendStyle(enabled);
    ApplyStopStyle(!enabled);
    // ③ 重绘（SetStyle 内部已 Invalidate，此处不重复请求）
}

void PromptBar::LayoutChildren() {
    if (!m_input) return;
    const int buttonW = 64;
    const int controlH = 32;
    const int padX = 12;
    m_input->SetSize(GetWidth() - 2 * padX - 2 * buttonW - 20, controlH);
    m_input->SetPosition(padX, 6);
    if (m_hint) {
        m_hint->SetPosition(padX + 2, 6 + controlH + 2);
    }
    if (m_send) {
        m_send->SetSize(buttonW, 38);
        m_send->SetPosition(GetWidth() - padX - buttonW, 9);
    }
    if (m_stop) {
        m_stop->SetSize(buttonW, 38);
        m_stop->SetPosition(GetWidth() - padX - 2 * buttonW - 10, 9);
    }
}