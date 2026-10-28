#include "PromptBar.h"

#include "ECDI/Core/Color.h"
// ★★ 2026-10-21：Enter 拦截需要用完整的 `KeyDownEvent` / `KeyCode`。
//   `Widget.h` 里只有 `class KeyDownEvent;` 前置声明（够写引用形参），
//   但这里要读 `GetKeyCode()` / `IsShiftDown()` ⇒ 必须是完整类型。
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
// ★★ 2026-10-21（REQ-12 收敛后）：`ECDI/Window/Window.h` 已**不再需要** ——
//   它当初只为一个转接函数（`ComposerBox::LineHeightNow` 调
//   `Window::GetTextMeasurer()`）而引入；那个函数与手抄的行高规则一起拆掉了。
//   多行 preferred 现在由 `TextBox::MultilinePreferred` 在框架内部算完。
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"
#include "ECDI/Window/Window.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>
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
// ★★ 2026-10-21：**Enter 发送 / Shift+Enter 换行**（用户「UI 完善」第 5 项）。
//
// 【改前是什么样】`LayoutChildren` 里那句注释是诚实的：*"Keep the editor
//   single-line until Enter/newline behavior is implemented explicitly"*。
//   于是聊天框**只能点按钮发**——提示行写着「点击发送提交」，那是对缺陷的描述，
//   不是设计。聊天界面里按 Enter 没反应，是"看起来能聊天、其实不能"的典型。
//
// 【为什么在 TextBox 子类里拦，而不是在 App 层收 KeyDown】
//   ECDI 的键盘是**单收件人**语义（`Application.cpp` 的注释：不冒泡），按键
//   直接派给焦点控件。要拦 Enter 就必须站在那个收件人位置上——也就是输入框
//   自己的 `OnKeyDown`。在 App 层挂 handler 拿不到这个事件。
//
// 【IME 安全吗】安全，而且是**结构上**安全（查源码确认，不是推断）：
//   · `TextBox::OnCharInput` 丢弃控制字符（`cp < 0x20`）⇒ Enter 的 WM_CHAR
//     通道永远进不了文本；
//   · 中文输入法"上屏"走 `TextBox::CommitComposition`，它只做
//     `ReplaceTextRange` + 清组合态，**全程不派发 KeyDownEvent** ⇒ 输入法
//     选词时按的 Enter 不会走到这里。
//   ★ `m_isComposing` 是 TextBox 私有且无访问器，这里**拿不到**它——但上面
//     两条已经说明不需要：本 override 只在"真有 KeyDown(Enter)"时被调用。
class ComposerBox : public ECDI::TextBox {
public:
    ComposerBox() {
        SetShowFocusRect(false);   // Widget.h:274；焦点改由 borderColor 表达
        // ★★ 2026-10-21：**显式钉死字体**。此前本类从未调过 `SetFont`，
        //   于是输入框拿的是 ECDI 默认 `Font{14.0, family=""}`（`Core/Font.h`）
        //   = **SimSun 14**，而旁边 10.5px 的提示行与两个按钮都是 `SansFont()`。
        //   FONT-UNIFIED（同日）刚刚统一了主题字体，输入框是唯一被漏掉的那个。
        //   ★ 这不只是观感：自动长高的行高正是由字体算出来的（见 `PromptBar::
        //     DesiredInputHeight`）——字体未钉死时，长高量测的是一个与用户所见不同的 face。
        SetFont(harness::SansFont(harness::kFontBody));
        // ★★ 2026-10-27：文字色从未 Set ⇒ ECDI TextStyle 缺省黑；叠加下方 ApplyBorder
        //   的 kPanel2 深灰底后黑字不可读。输入框文字与 Label 同族，吃 kDarkInk
        //   （深色≈白字，浅色仍是深墨）。SetTextColor 内部走 TextStyleOverride，
        //   与 ApplyBorder 的 TextBoxStyleOverride 是两个互不干扰的覆盖通路。
        SetTextColor(harness::kDarkInk);
        ApplyBorder(false);
    }

    // 按键上报目标（非拥有）。`PromptBar` 构造时注入自己。
    void SetEnterSink(PromptBar* sink) { m_enterSink = sink; }

    // ── ★★ 2026-10-21（最终形态）：内容高**不再需要本类的任何辅助函数** ──
    //
    //   历史：K4 挂账期间这里手抄过 `\n` 计数、`LineHeight(font)`、`padding×2`
    //   三条规则，并且要 `GetWindow()->GetTextMeasurer()` 才拿得到测量器。
    //   ECDI 采纳 REQ-12 的 ⓐ 订正语义后，`TextBox::GetPreferredSize()` 自己
    //   给出 `{当前宽, ceil(行数 × 行高 + padding×2)}`，于是：
    //     · 那三条规则**还给框架**；
    //     · `ContentHeight()` 与 `LineHeightNow()` 两个转接函数**一并删掉**
    //       （它们只剩"调一次虚函数"的作用，留着会被下一个读者当成真相源）；
    //     · 本文件不再手抄内容测量规则；`Window.h` 仅用于读取 root 高度的 UI 上限。
    //   ★ 这正是当初登记 REQ 的意义：抄的规则要还得回去，连给绕过用的脚手架一起拆。
    //   ★ 顺带消失的三处隐患：`max(0,·)` 陷阱、取整方向、`ceil` 归属。
    //   ★ 产品侧唯一剩下的判断是 clamp 到 [下限, MaxInputHeight()]——那是 UI 策略，
    //     不是内容测量，本来就该住在 `PromptBar` 里（见 `.cpp` 的 `DesiredInputHeight`）。

protected:
    void OnFocusGained() override {
        ECDI::TextBox::OnFocusGained();
        ApplyBorder(true);
    }
    void OnFocusLost() override {
        ECDI::TextBox::OnFocusLost();
        ApplyBorder(false);
    }

    // ★★ 2026-10-21：编辑导致的文本变化 → 立即上报（自动长高的**键盘路径**）。
    //   ★ 为什么 override `OnTextChanged` 而不是用 `SetOnTextChanged` 回调：
    //     后者是**覆盖式**的单一槽位（`TextBox.h:96` "覆盖式：传空 = 解除注册"），
    //     `App` 将来若要挂自己的监听会被本类**静默顶掉**。虚方法没有这个问题。
    //   ★ 只覆盖**编辑操作**（`RaiseTextChanged` 的六个调用点：插入/删除/粘贴/IME 提交/Undo）。
    //     `SetText` **不走**这里（D7 契约，`TextBox.cpp:121-141`）——那三个程序侧入口
    //     由 `PromptBar` 显式调 `SyncInputHeight()`，见其注释。
    void OnTextChanged(const std::string& text) override {
        ECDI::TextBox::OnTextChanged(text);
        if (m_enterSink) m_enterSink->SyncInputHeight();
    }

    void OnKeyDown(const ECDI::KeyDownEvent& event) override {
        if (IsReadOnly()) {
            return;
        }
        // ★ 只拦 Enter（含小键盘 Enter），其余按键**原样**交给基类。
        //   绝不能"先自己处理再调基类"——那会把方向键/Backspace 等编辑键
        //   吃成两次。
        if (event.GetKeyCode() == ECDI::KeyCode::Enter ||
            event.GetKeyCode() == ECDI::KeyCode::NumpadEnter) {
            if (event.IsShiftDown()) {
                // Shift+Enter = 换行。★ 必须走基类：多行插入由
                //   `TextBox::OnKeyDown` 的 `InsertText("\n")` 负责，它同时维护
                //   行缓存 / 滚动 / 光标可见；自己塞 `\n` 会绕过这些。
                ECDI::TextBox::OnKeyDown(event);
                return;
            }
            // 无 Shift ⇒ 发送。★ **消费**这个按键（不调基类）：多行模式下基类
            //   会插入一个换行，发送完框里就多一个空行。
            if (m_enterSink) m_enterSink->NotifyEnterPressed();
            return;
        }
        ECDI::TextBox::OnKeyDown(event);
    }

private:
    void ApplyBorder(bool focused) {
        SetStyle(ECDI::TextBoxStyleOverride{
            // ★ 2026-10-27：kInputBg 是「白底输入」令牌——深色主题下纯白一块，
            //   白字落不上去。与设置面板/浮窗搜索框同款：kPanel2 作底，浅色下
            //   它仍是亮面（241,243,247），观感不回退。
            .background = harness::kPanel2,
            .caretColor = harness::kInputCaret,
            .padding = 6.0f,
            .cornerRadius = harness::kRadiusInput,
            .borderWidth = 1.0f,
            .borderColor = focused ? harness::kDarkAccent : harness::kInputBorder,
            // ★ REQ-14（ECDI Phase 37 verticalAlign）：单行内容垂直居中。
            //   Composer 29 = lineH 17 + padding 6×2，Top 与 Center 读数相同
            //   （--valign-probe 的 composer 相会记录 delta=0）——这里选 Center
            //   是把「恰好居中」钉成「契约居中」：将来 padding 或行高漂移
            //   （DPI/字体），行箱仍按几何中心落，不再依赖 29=17+6×2 的巧合。
            .verticalAlign = ECDI::VerticalAlignment::Center});
    }

    PromptBar* m_enterSink = nullptr;   // 非拥有
};

}  // namespace

PromptBar::PromptBar() {
    SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kInspBg,
        .borderWidth = 1.0f,
        .borderColor = harness::kLine});

    auto input = std::make_unique<ComposerBox>();
    m_input = input.get();
    input->SetEnterSink(this);          // ★ Enter 上报回 PromptBar（唯一发送入口）
    // ★★ 2026-10-21：改为**多行**（Shift+Enter 才能换行）。
    //   改前是 `SetSingleLine(true)`：Enter 被基类当 no-op 吞掉，Shift+Enter
    //   也一样（单行模式下 `InsertText("\n")` 根本不执行）⇒ 换行这条路是死的。
    //   ★ 多行的代价：`GetPreferredSize` 对含 `\n` 的文本返回**当前尺寸**
    //     （ECDI §3.4 明确「多行高度 v1 手工 SetSize，不参与 AutoSize」）
    //     ⇒ 高度由 `LayoutChildren` 固定给 44px，内容超出时 TextBox 自己滚动
    //     （`EnsureCaretVisible` + 内部滚动条）。这是**已知边界**，不是缺陷。
    m_input->SetSingleLine(false);
    // ★★ 2026-10-26（首帧压扁的**正解**，订正同日更早的一版）：
    //   症状是真的——`TextBox` 默认高 20，而 `LayoutChildren` 把提示行/按钮的 Y
    //   全部**由输入框高推导**（`hintY = 8 + inputH + 6`），⇒ 没量过之前它们悬在 y=34，
    //   输入框下半截空出 30px。
    //   ★ 但**修法不能是手抄一个常数**。此前那版写 `SetSize(400, 36)`，36 是估的：
    //     框架实测单行 = **29**（`TextBox::GetPreferredSize` 单行分支 =
    //     行高 17 + padding 6×2，`--grow-probe` 的 baseline 相读数
    //     `inputH=29 / barH=81 / hintY=43` 可复核），于是 36 比真值高 7px，
    //     第一次键入 / 第一次 resize 触发 `SyncInputHeight()` 时会**当场缩回 29**
    //     ——那就是"首帧对齐"改成"首次按键跳一下"，缺陷只是搬了个时刻。
    //     （本仓纪律 14：凡是「常数 × 逐单位度量 ⇒ 像素」的预算都必须实测，不许手抄。）
    //   ⇒ 常数删掉，高度交给**唯一入口** `SyncInputHeight()`：调用点在
    //     `AppUiChrome.cpp` 的 `BuildUi` 收尾（`root.Arrange()` 之后——宽度要到那一刻
    //     才有值），首帧即由测量给出，且首建/换主题重建两条路共用同一处。
    //   ★ 构造期不能调：那会儿还没有窗口/测量器，`GetPreferredSize()` 会退回当前尺寸
    //     （= 默认 20），量了等于没量。这就是常数曾经存在的唯一理由。
    // Phase 30 caretColor：深底输入框可用（光标 = accent），hi-fi 时代的"浅井"退役。
    // ★ 样式（含焦点描边色）已由 ComposerBox 自管，此处不再 SetStyle。
    AddChild(std::move(input));

    // ★ 2026-10-21：提示行如实写出手册（改前写「点击发送提交」，只描述了当时
    //   唯一的发送方式，等于把缺陷当说明）。
    auto hint = std::make_unique<ECDI::Label>("Enter 发送 · Shift+Enter 换行");
    m_hint = hint.get();
    m_hint->SetTextColor(harness::kDarkMuted);
    m_hint->SetFont(harness::SansFont(10.5f));
    // ★ 必须给尺寸：`Label` 默认 0×0 且不参与布局（`PromptBar` 是裸 `Panel`，
    //   没有 Layout）⇒ 不给就是**可见但零尺寸**的静默消失
    //   （正是 `uikit::WarnZeroSized` 在巡检的那类缺陷）。
    m_hint->SetSize(340, 16);
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
    m_send->SetOnClick([this] { Submit(); });
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

    auto cont = std::make_unique<ECDI::Button>("继续");
    m_continue = cont.get();
    m_continue->SetTextColor(harness::kBtnPrimaryInk);
    m_continue->SetFont(harness::SansFont(13.0f));
    m_continue->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kBtnPrimary,
        .cornerRadius = 8.0f,
        .pressedBackground = harness::kBtnPrimaryPressed,
        .hoverBackground = harness::kBtnPrimaryHover});
    m_continue->SetOnClick([this] {
        if (m_onContinue) m_onContinue();
    });
    m_continue->SetVisible(false);
    AddChild(std::move(cont));
}

void PromptBar::SetSize(int w, int h) {
    ECDI::Panel::SetSize(w, h);
    // ★ 高度由 `SyncInputHeight` 主导：`SetSize` 只认宽度。
    //   ★★ 否则会形成一个**自激励循环**：`SyncInputHeight` → `SetSize` →
    //     `LayoutChildren` → 若这里再来一次"按内容改高"，两者会互相触发
    //     （每帧一次 `parent->Arrange()`，`TextBox::SetSize` 里的 `Invalidate`
    //      再加上 `VerticalLayout` 的重复分配 = 帧率塌方，且读数上看不出原因）。
    //   故此处只按**当前**输入框高重排子控件，不再改高。
    LayoutChildren();
}

// ★★ 2026-10-21：**发送的唯一入口**（点按钮 / 按 Enter 都走这里）。
//
//   ★ 空输入**不发送**：聊天界面里对空框按 Enter 不该产生一条空消息，而
//     "空消息"在协议里是一块 `texk:""` 的 user 块——core 会如实回显一个空气泡，
//     屏幕上凭空多一行。这条与"取消不是事件"同族：**没有内容是用户没做这件事**。
//   ★ 空白（只有空格/换行）也算空：用户敲了 Shift+Enter 再回删，留下的是
//     "\n"，那不是一条消息。判据用"存在非空白字符"，而不是 `!text.empty()`。
//   ★ 清空输入框**不在这里做**：`App` 的 `SetOnSend` 回调负责（与点按钮同一份
//     副作用）。在这里再清一次会形成两处写入，将来改一处就漂移。
void PromptBar::Submit() {
    if (m_isReadOnly) return;
    if (!m_input) return;
    const std::string text = m_input->GetText();
    bool hasContent = false;
    for (unsigned char c : text) {
        // 只把空白当"无内容"：空格 / 制表 / 回车 / 换行。
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            hasContent = true;
            break;
        }
    }
    if (!hasContent) return;
    if (m_onSend) m_onSend(text);
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

void PromptBar::SetReadOnlyMode(bool readOnly) {
    m_isReadOnly = readOnly;
    if (m_continue) m_continue->SetVisible(readOnly);
    if (m_send) m_send->SetVisible(!readOnly);
    if (m_stop) m_stop->SetVisible(!readOnly);
    if (m_input) {
        m_input->SetReadOnly(readOnly);
        m_input->SetEnabled(!readOnly);
    }
}

void PromptBar::SetHint(const std::string& text, const ECDI::Color& color) {
    if (!m_hint) return;
    if (m_hint->GetText() != text) {
        m_hint->SetText(text);
    }
    m_hint->SetTextColor(color);
}

int PromptBar::MaxInputHeight() {
    if (ECDI::Window* win = GetWindow()) {
        const int rootH = win->GetRootWidget().GetHeight();
        if (rootH > 0) {
            // 700px 高度下恰为 132（既有基准）；窗口拉伸时按比例扩展/收缩。
            const float scaled = static_cast<float>(rootH) * (kInputMaxH / 700.0f);
            return std::clamp(static_cast<int>(std::round(scaled)), 46, 260);
        }
    }
    return kInputMaxH;
}

// ── ★★ 2026-10-21（收口）：多行自动长高 ──────────────────────
//
// 【改前的形状】ECDI 对**多行** `TextBox` 的 `GetPreferredSize` 返回当前尺寸
// （K4 挂账）⇒ `AutoSize()` 结构性 no-op，而同一框架的 wrap 分支返回
// `{GetWidth(), 行数×行高}` ⇒ 气泡能长高、输入框不能。**一个虚函数两个答案**。
// 本仓先做组合实现绕过（手抄两条规则），并把它登记成 REQ-12。
//
// 【现在】ECDI 采纳 ⓐ 订正了语义 ⇒ 本类不再手算，只调 `GetPreferredSize()`。
// 取整（`ceil`）、`max(0,·)` 陷阱、`ceil` 归属这些**全部回到框架内部**，
// 调用方只剩"clamp 到 [单行高, 上限]"这一件真正属于 UI 策略的事。
// ★ `--grow-probe` 同时验证内容高度和窗口高度变化下的 clamp 策略；
//   内容测量仍由框架负责，本类只应用当前窗口的 UI 上限。
int PromptBar::DesiredInputHeight() {
    const int maxH = MaxInputHeight();
    if (!m_input) return maxH;

    // ① 期望 = 框架给出的内容高（多行 = ceil(行数 × 行高 + padding×2)；
    //    单行 = 内容测量 + padding×2；无窗口/无测量器 ⇒ 当前尺寸）。
    //    ★ 不再需要 `dynamic_cast<ComposerBox*>`：`GetPreferredSize()` 是 `Widget` 的虚函数。
    int want = static_cast<int>(std::ceil(m_input->GetPreferredSize().height));

    // ② 下限 = 单行时的真实高度。**直接从框架问**，不在这里重算行高与 padding：
    //    把输入框临时当成"没有换行"是做不到的（那要改文本），所以用一条
    //    更稳的等价判据——`preferred.height` 在**单行**文本下就是这个值。
    //    ★ 这里保留一个最小兜底（>0），防"测量器缺失 ⇒ 返回当前尺寸 ⇒ 当前恰为 0"
    //      的退化路径把框压成 0 高。
    if (want <= 0) want = m_input->GetHeight();

    // ③ 上限：超出交给 `TextBox` 自己的内部滚动条（既有能力，`SyncScrollBar` 按需显示）
    if (want > maxH) want = maxH;
    if (want <= 0) want = 1;
    return want;
}

bool PromptBar::SyncInputHeight() {
    if (!m_input) return false;
    const int want = DesiredInputHeight();
    if (want == m_input->GetHeight()) return false;   // ★ 同值守卫：防每帧重排（本次改动的自激励风险）

    // 高度由本函数写：`LayoutChildren` 不再碰输入框的高（否则两处各写一份会漂移）。
    m_input->SetSize(m_input->GetWidth(), want);
    // 自身高 = 输入框高 + 上边距 8 + 按钮行 28 + 间隙 8 + 下边距 8
    SetSize(GetWidth(), want + 8 + 28 + 8 + 8);
    LayoutChildren();

    // ★★ 必须重排父节点：`counting` 是 `VerticalLayout`、`scroll` 是 `stretch=1`，
    //   只改自己的尺寸**不会**让转录视口收缩 —— `VerticalLayout::Arrange` 才是读者
    //   （`VerticalLayout.cpp:53` 用父高减去 fixedTotal 分配剩余空间）。
    //   ★ 顺序：先自己摆好子控件，再让父布局按新高度重新分配。
    if (ECDI::Widget* parent = GetParent()) {
        parent->Arrange();
    }
    Invalidate();
    return true;
}

void PromptBar::LayoutChildren() {
    if (!m_input) return;
    const int buttonW = 64;
    const int padX = 12;
    // 输入框在上（高由 `SyncInputHeight` 写）、提示行与按钮行在下——**全部由输入框高推导**，
    // 不再有 44 / 66 / 58 三个硬编码。此前它们是硬编码的：输入框一旦变高，
    // 提示行与按钮会被输入框压住（一次静默的重叠，没有任何读数会报）。
    const int inputH = m_input->GetHeight();
    const int hintY = 8 + inputH + 6;
    const int buttonY = 8 + inputH + 6;

    m_input->SetSize(GetWidth() - 2 * padX, inputH);
    m_input->SetPosition(padX, 8);
    if (m_hint) {
        m_hint->SetPosition(padX + 2, hintY);
    }
    if (m_send) {
        m_send->SetSize(buttonW, 28);
        m_send->SetPosition(GetWidth() - padX - buttonW, buttonY);
    }
    if (m_continue) {
        m_continue->SetSize(buttonW, 28);
        m_continue->SetPosition(GetWidth() - padX - buttonW, buttonY);
    }
    if (m_stop) {
        m_stop->SetSize(buttonW, 28);
        m_stop->SetPosition(GetWidth() - padX - 2 * buttonW - 10, buttonY);
    }
}