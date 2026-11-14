// ============================================================
// 开发者视图 ↔ 用户模式分流探针（--mode-probe）
// ------------------------------------------------------------
// 闭合 P4 Phase J 已知硬缺口（用户模式零探针覆盖）：
//   在无参启动时产品默认以「用户模式」运行，隐藏所有评估专属控件、
//   底栏、夹具历史区，收起检视栏，并将按钮与状态换为自然中文。
//
// 判据设计（跨 tick 状态机）：
//   ① baseline-dev:
//      回放夹具拉完后初始处于开发者模式（m_devMode == true）。
//      断言开发者专属控件全部可见：
//        - 顶栏四个评估胶囊（sessChipPill, modeChipPill, rowChipPill, backendChipPill）
//        - 26px 底栏（statusBar）
//        - 侧栏回放项（sessProgress, replayBar, historyHeader, historyList, historyCount）
//        - 检视栏「渲染探针」卡（inspProbeCard）
//        - 检视栏（m_inspectorVisible == true）
//        - 中栏头三按钮（exportToggle, inspectorToggle, moreToggle）宽均为 28px
//        - 更多菜单中项（devModeMenuBtn）文本为 "简洁视图"
//      随后合成 Ctrl+Shift+D 快捷键（验证真实事件派发与拦截）。
//
//   ② switch-user:
//      等待 400ms 让布局与 Dirty 标记在 tick 中更新完毕。
//      断言用户模式所有特征：
//        - m_devMode == false
//        - 顶栏四个评估胶囊全部 IsVisible() == false
//        - 26px 底栏 IsVisible() == false
//        - 侧栏回放项全部 IsVisible() == false
//        - 检视栏「渲染探针」卡 IsVisible() == false
//        - 检视栏收起（m_inspectorVisible == false）
//        - 中栏头三按钮文本变为 "导出" / "详情" / "更多"，宽均为 44px
//        - 更多菜单中项文本变为 "开发者视图"
//      随后再次合成 Ctrl+Shift+D 快捷键切回开发者模式。
//
//   ③ dev-restored:
//      断言切回后状态与 baseline-dev 完全一致（证明切换完全可逆，无单向残留）。
//
//   ④ negative:
//      负对照相：临时切换到用户模式，故意反向断言 statusBar 与 sessChipPill 为可见。
//      反向断言必然失败（matched == false），证明探针具备真正的区分力（teeth），
//      绝非恒绿断言。在回归门中登记为 designed-red。
//      最后恢复为开发者模式。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/ScrollView.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace {

bool IsWidgetVisible(ECDI::Widget* w) {
    return w != nullptr && w->IsVisible();
}

std::string GetWidgetText(ECDI::Button* b) {
    return b ? b->GetText() : std::string();
}

int GetWidgetWidth(ECDI::Widget* w) {
    return w ? w->GetWidth() : 0;
}

} // namespace

void HarnessApp::RunModeProbe() {
    if (!m_sess || !m_window) {
        m_probeState.modeProbeDone = true;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_probeState.modeProbeStep > 0 && now < m_probeState.modeProbeNextAt) {
        return;
    }
    const auto settle = std::chrono::milliseconds(400);

    auto snap = [&](const char* phase) -> ModeProbeRec {
        ModeProbeRec rec;
        rec.phase = phase;
        rec.devMode = m_devMode;
        rec.inspectorVisible = m_inspectorVisible;

        rec.sessChipVisible = IsWidgetVisible(m_hud.sessChipPill);
        rec.modeChipVisible = IsWidgetVisible(m_hud.modeChipPill);
        rec.rowChipVisible = IsWidgetVisible(m_hud.rowChipPill);
        rec.backendChipVisible = IsWidgetVisible(m_hud.backendChipPill);

        rec.statusBarVisible = IsWidgetVisible(m_hud.statusBar);

        rec.sessProgressVisible = IsWidgetVisible(m_hud.sessProgress);
        rec.replayBarVisible = IsWidgetVisible(m_replayBar);
        rec.historyHeaderVisible = IsWidgetVisible(m_hud.historyHeader);
        rec.historyListVisible = IsWidgetVisible(m_hud.historyList);
        rec.historyCountVisible = IsWidgetVisible(m_hud.historyCount);

        rec.inspProbeCardVisible = IsWidgetVisible(m_hud.inspProbeCard);

        rec.exportText = GetWidgetText(m_hud.exportToggle);
        rec.exportWidth = GetWidgetWidth(m_hud.exportToggle);
        rec.inspectorText = GetWidgetText(m_hud.inspectorToggle);
        rec.inspectorWidth = GetWidgetWidth(m_hud.inspectorToggle);
        rec.moreText = GetWidgetText(m_hud.moreToggle);
        rec.moreWidth = GetWidgetWidth(m_hud.moreToggle);

        rec.devMenuText = GetWidgetText(m_hud.devModeMenuBtn);
        return rec;
    };

    switch (m_probeState.modeProbeStep) {
        case 0: {
            // 0. baseline-dev: 初始处于开发者模式，所有评估控件/胶囊/底栏/历史区全部可见
            ModeProbeRec rec = snap("baseline-dev");
            bool ok = true;
            std::string err;

            if (!rec.devMode) { ok = false; err += "devMode!=true; "; }
            if (!rec.sessChipVisible || !rec.modeChipVisible || !rec.rowChipVisible || !rec.backendChipVisible) {
                ok = false; err += "topChipsNotVisible; ";
            }
            if (!rec.statusBarVisible) { ok = false; err += "statusBarNotVisible; "; }
            if (!rec.sessProgressVisible || !rec.replayBarVisible || !rec.historyHeaderVisible || !rec.historyListVisible || !rec.historyCountVisible) {
                ok = false; err += "sidebarHistoryNotVisible; ";
            }
            if (!rec.inspProbeCardVisible) { ok = false; err += "inspProbeCardNotVisible; "; }
            if (!rec.inspectorVisible) { ok = false; err += "inspectorVisible!=true; "; }
            if (rec.exportWidth != 28 || rec.inspectorWidth != 28 || rec.moreWidth != 28) {
                ok = false; err += "btnWidth!=28; ";
            }
            if (rec.devMenuText != "简洁视图") {
                ok = false; err += "devMenuText!=\"简洁视图\"; ";
            }

            rec.matched = ok;
            rec.note = err;
            m_probeState.modeProbeLog.push_back(std::move(rec));

            // 合成 Ctrl+Shift+D 切换至用户模式
            SynthesizeKey(ECDI::KeyCode::D, /*shift=*/true, ECDI::KeyModifier::Ctrl | ECDI::KeyModifier::Shift);
            m_probeState.modeProbeStep = 1;
            m_probeState.modeProbeNextAt = now + settle;
            return;
        }
        case 1: {
            // 1. switch-user: 切换至用户模式，评估专属控件全数隐藏
            ModeProbeRec rec = snap("switch-user");
            bool ok = true;
            std::string err;

            if (rec.devMode) { ok = false; err += "devMode!=false; "; }
            if (rec.sessChipVisible || rec.modeChipVisible || rec.rowChipVisible || rec.backendChipVisible) {
                ok = false; err += "topChipsStillVisible; ";
            }
            if (rec.statusBarVisible) { ok = false; err += "statusBarStillVisible; "; }
            if (rec.sessProgressVisible || rec.replayBarVisible || rec.historyHeaderVisible || rec.historyListVisible || rec.historyCountVisible) {
                ok = false; err += "sidebarHistoryStillVisible; ";
            }
            if (rec.inspProbeCardVisible) { ok = false; err += "inspProbeCardStillVisible; "; }
            if (rec.inspectorVisible) { ok = false; err += "inspectorVisible!=false; "; }
            if (rec.exportWidth != 44 || rec.inspectorWidth != 44 || rec.moreWidth != 44) {
                ok = false; err += "btnWidth!=44; ";
            }
            if (rec.exportText != "导出" || rec.inspectorText != "详情" || rec.moreText != "更多") {
                ok = false; err += "btnTextWrong; ";
            }
            if (rec.devMenuText != "开发者视图") {
                ok = false; err += "devMenuText!=\"开发者视图\"; ";
            }

            rec.matched = ok;
            rec.note = err;
            m_probeState.modeProbeLog.push_back(std::move(rec));

            // 再次合成 Ctrl+Shift+D 切回开发者模式
            SynthesizeKey(ECDI::KeyCode::D, /*shift=*/true, ECDI::KeyModifier::Ctrl | ECDI::KeyModifier::Shift);
            m_probeState.modeProbeStep = 2;
            m_probeState.modeProbeNextAt = now + settle;
            return;
        }
        case 2: {
            // 2. dev-restored: 恢复开发者模式，完全恢复至基线
            ModeProbeRec rec = snap("dev-restored");
            bool ok = true;
            std::string err;

            if (!rec.devMode) { ok = false; err += "devMode!=true; "; }
            if (!rec.sessChipVisible || !rec.modeChipVisible || !rec.rowChipVisible || !rec.backendChipVisible) {
                ok = false; err += "topChipsNotRestored; ";
            }
            if (!rec.statusBarVisible) { ok = false; err += "statusBarNotRestored; "; }
            if (!rec.sessProgressVisible || !rec.replayBarVisible || !rec.historyHeaderVisible || !rec.historyListVisible || !rec.historyCountVisible) {
                ok = false; err += "sidebarHistoryNotRestored; ";
            }
            if (!rec.inspProbeCardVisible) { ok = false; err += "inspProbeCardNotRestored; "; }
            if (!rec.inspectorVisible) { ok = false; err += "inspectorVisible!=true; "; }
            if (rec.exportWidth != 28 || rec.inspectorWidth != 28 || rec.moreWidth != 28) {
                ok = false; err += "btnWidth!=28; ";
            }
            if (rec.devMenuText != "简洁视图") {
                ok = false; err += "devMenuText!=\"简洁视图\"; ";
            }

            rec.matched = ok;
            rec.note = err;
            m_probeState.modeProbeLog.push_back(std::move(rec));

            m_probeState.modeProbeStep = 3;
            m_probeState.modeProbeNextAt = now + std::chrono::milliseconds(50);
            return;
        }
        case 3: {
            // 3. negative: 负对照——故意在用户模式下反向断言开发者专有控件为可见
            SetDevMode(false);
            ModeProbeRec rec = snap("negative");
            // 反向断言：断言 statusBar 与 sessChip 必须为可见。因为用户模式下已隐藏，故断言必假
            const bool invertedAssert = rec.statusBarVisible && rec.sessChipVisible;
            rec.matched = invertedAssert; // 必为 false
            if (!rec.matched) {
                rec.note = "ok(expected divergence): statusBar and sessChip are hidden in user mode";
            } else {
                rec.note = "FAIL: statusBar or sessChip was unexpectedly visible in user mode";
            }
            m_probeState.modeProbeLog.push_back(std::move(rec));

            // 转入第 4 相：验证缺口 5 闭合（检视栏偏好跨模式独立记忆）
            m_probeState.modeProbeStep = 4;
            m_probeState.modeProbeNextAt = now + std::chrono::milliseconds(50);
            return;
        }
        case 4: {
            // 4. inspector-memory: 闭合缺口 5 机器判据
            // (a) 在用户模式下手动打开检视栏（点击“详情”）
            SetDevMode(false);
            if (!m_inspectorVisible) {
                ToggleInspector(); // 展开用户模式检视栏
            }
            const bool userToggledOpen = m_inspectorVisible;

            // (b) 切到开发者模式，并手动收起检视栏
            SetDevMode(true);
            if (m_inspectorVisible) {
                ToggleInspector(); // 收起开发者模式检视栏
            }
            const bool devToggledClosed = !m_inspectorVisible;

            // (c) 切回用户模式，断言检视栏依然是展开的（未被开发者模式的收起或默认值抹杀）
            SetDevMode(false);
            const bool userRestoredOpen = m_inspectorVisible;

            // (d) 切回开发者模式，断言检视栏依然是收起的（未被默认展开抹杀）
            SetDevMode(true);
            const bool devRestoredClosed = !m_inspectorVisible;

            ModeProbeRec rec = snap("inspector-memory");
            bool ok = userToggledOpen && devToggledClosed && userRestoredOpen && devRestoredClosed;
            rec.matched = ok;
            if (!ok) {
                rec.note = "FAIL: inspector visibility memory failed across mode switches";
            } else {
                rec.note = "ok: dev/user inspector preferences remembered independently";
            }
            m_probeState.modeProbeLog.push_back(std::move(rec));

            // 复原默认偏好并验证检视栏竖向可滚、零横向溢出
            m_devInspectorVisible = true;
            m_userInspectorVisible = false;
            m_inspectorVisible = true;
            ApplyInspectorVisibility();
            SetDevMode(true);
            if (m_hud.inspector) {
                if (ECDI::Widget* p = m_hud.inspector->GetParent()) p->Arrange();
            }
            Relayout();

            {
                ModeProbeRec srec = snap("inspector-scroll");
                bool sok = false;
                std::string snote;
                if (!m_hud.inspectorSv) {
                    snote = "FAIL: inspectorSv is null";
                } else {
                    const int maxY = m_hud.inspectorSv->GetMaxOffsetY();
                    const int maxX = m_hud.inspectorSv->GetMaxOffsetX();
                    m_hud.inspectorSv->SetContentOffset(0, maxY);
                    const int scrolledY = m_hud.inspectorSv->GetScrollOffsetY();
                    m_hud.inspectorSv->SetContentOffset(0, 0);
                    sok = (maxY > 0 && maxX == 0 && scrolledY == maxY);
                    snote = "maxY=" + std::to_string(maxY) + " maxX=" + std::to_string(maxX) +
                            " scrolledY=" + std::to_string(scrolledY);
                }
                srec.matched = sok;
                srec.note = snote;
                m_probeState.modeProbeLog.push_back(std::move(srec));
            }

            m_probeState.modeProbeDone = true;
            if (m_application) {
                m_application->Exit();
            }
            return;
        }
        default:
            m_probeState.modeProbeDone = true;
            if (m_application) {
                m_application->Exit();
            }
            return;
    }
}

void HarnessApp::AppendModeProbeSummary(std::string& json) {
    json += "  \"modeProbe\": [";
    for (size_t i = 0; i < m_probeState.modeProbeLog.size(); ++i) {
        const ModeProbeRec& r = m_probeState.modeProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"devMode\": %s, \"inspector\": %s, "
                      "\"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(),
                      r.devMode ? "true" : "false",
                      r.inspectorVisible ? "true" : "false",
                      r.matched ? "true" : "false",
                      r.note.c_str());
        json += b;
    }
    json += "],\n";
}
