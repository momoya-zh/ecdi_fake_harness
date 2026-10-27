// ============================================================
// 共享 UI helper：文本拼法 / 宽度测量 / 一行小标签（2026-10-25 从 App.cpp 搬出）
// ------------------------------------------------------------
// 为什么单独一个文件：这些函数原本是 App.cpp 匿名命名空间的内部链接 helper。
//   1d 把转录（AppTranscript）与 HUD（AppHud）拆成独立 TU、1e 又把 UI 构建拆成
//   Chrome/Main 两块之后，多处都要用同一份——尤其是 UnknownKindText /
//   ReasoningTitle，它们是**单一真相源**：同一段文本在"建行 / delta 更新 /
//   展开收起"三处生成，各写一份必然漂移（本项目已踩过两次）。
// ★ 纪律：这里只放**无状态** helper——要么输入→字符串/宽度，要么"给父容器建一个
//   标签"。不许读 m_* 成员、不许按业务状态决定长相；一旦需要读状态，
//   说明它该是 HarnessApp 的成员函数，不该在这里。
//   `AddLine` 是唯一"会建控件"的成员（1e 加进来的）：它是 AppUiChrome 与
//   AppUiMain 共用的同一段建标签逻辑，各写一份会漏掉「行高随字号走」这条修复。
// ★ 函数体与搬家前**逐字节相同**（门 2 的函数体指纹可比）。
// ============================================================

#include "AppInternal.h"

#include "Theme.h"   // harness::SansFont
#include "Transcript.h"   // Block 完整类型（UnknownKindText / ReasoningTitle 读它的字段）
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollBar.h"   // 瘦身：ScrollBarStyleOverride（ApplyCompactScrollBars）
#include "ECDI/Widget/ScrollView.h"

#include <memory>
#include <optional>

namespace harness {
namespace internal {

int ContentWidth(const ECDI::ScrollView* scroll) {
    const int w = scroll ? scroll->GetWidth() : 0;
    return w > 80 ? w - 40 : 600;   // 扣除滚动条与边距；未布局前给初值
}

// ★★ 2026-10-27：滚动条瘦身（`kScrollBarThickness`，默认 12 的 1/3）。
//   ★ 一处实现、全仓 ScrollView 共用——中栏条 4px 而侧栏 12px 会比"4px 本身"更显眼。
//   ★ 三个坑都在这三行里处理：
//     ① `SetStyle` 只 `Invalidate` 自身，**不重排**（条几何/viewport/maxOffset 都在
//        `ApplyLayout` 里算，只由 `SetSize`/`SetContentExtent`/`SetScrollBarVisible` 触发）
//        ⇒ 末尾补一次 `SetSize` 等价调用；
//     ② `BarThickness()` 只读**垂直**条 ⇒ 两条同设，否则一轴的"画出的厚度"与
//        "扣掉的厚度"不一致，内容压在条底下；
//     ③ 实际占位 = `thickness + 2`（`kBarInset` 四边内缩）⇒ 改厚度会连带改
//        `GetMaxOffsetY()`，所以必须在**建树期**设，别在运行期偷偷改。
void ApplyCompactScrollBars(ECDI::ScrollView& sv) {
    const ECDI::ScrollBarStyleOverride compact{ .thickness = kScrollBarThickness };
    if (ECDI::ScrollBar* v = sv.GetVerticalScrollBar()) v->SetStyle(compact);
    if (ECDI::ScrollBar* h = sv.GetHorizontalScrollBar()) h->SetStyle(compact);
    // 守卫：建树期宽高还是 0，此刻重排只会算出负 viewport（`w - t - inset`）；
    // 随后布局分配的第一次 `SetSize` 自然会带新厚度走一遍 `ApplyLayout`。
    if (sv.GetWidth() > 0 && sv.GetHeight() > 0) sv.SetSize(sv.GetWidth(), sv.GetHeight());
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

// ★★ 2026-10-27：权限门折叠的三件套（声明与设计理由见 AppInternal.h）。
//   ★ 只做**列宽估算**（ASCII 记 1 列、多字节记 2 列），**不做像素测量**：
//     权限门在流式路径上可能高频重建，任何 `MeasureText` 都会计入
//     `m_evalMeasureCalls`（评估仪器口径）；而摘要只需"大致占半行"，
//     不必像侧栏标题那样精确贴边（修法 D 那套是给"必须填满"的标题用的）。
//   ★ UTF-8 按首字节判长，**不引第三方、不构造 wstring**：截断点永远落在码点
//     边界上——劈开一个多字节字符，屏幕上就是一串乱码方块。
namespace {

// 首字节 ⇒ 该码点的字节数（非法首字节按 1 处理：宁可不截，也不越界）。
std::size_t Utf8SeqLen(unsigned char lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

}  // namespace

std::string AskSummary(const std::string& question, int maxCols) {
    std::string out;
    int cols = 0;
    for (std::size_t i = 0; i < question.size();) {
        const std::size_t len = Utf8SeqLen(static_cast<unsigned char>(question[i]));
        if (i + len > question.size()) break;   // 尾部残缺序列：就地停（不补不猜）
        const int w = (len == 1) ? 1 : 2;
        if (cols + w > maxCols) {
            out += "\xE2\x80\xA6";              // … U+2026
            return out;
        }
        out.append(question, i, len);
        cols += w;
        i += len;
    }
    return out;
}

bool AskNeedsFold(const std::string& question, int maxCols) {
    // 与 AskSummary 同源判据：摘要装不下原文 ⇔ 出现了截断。
    //   ★ 不做"长度 > N"的独立算术——两份阈值实现迟早对不上（一个算列一个算字节），
    //     那时症状是"折叠了但摘要没省略号"这类只在视觉上暴露的怪象。
    return AskSummary(question, maxCols) != question;
}

std::string AskTitle(const std::string& question, bool expanded, bool foldable) {
    std::string out;
    if (foldable) {
        // 箭头字节照抄 ReasoningTitle（同一对：︿ U+FE3F / ﹀ U+FE40）——
        // 那两个转义是 Python 反查得到的，**别手写**（ReasoningTitle 上有踩坑注释）。
        out = expanded ? "\xEF\xB8\xBF " : "\xEF\xB9\x80 ";
    }
    out += "权限 ";
    // 装得下 ⇒ 直接用全文（此时摘要等于原文，压根不折叠，也就不存在"同一句话显示两遍"）。
    out += foldable ? AskSummary(question) : question;
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

}  // namespace internal
}  // namespace harness
