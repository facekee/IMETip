#include "test_util.h"

#include "core/SessionLogic.h"

void RunSessionLogicTests() {
    using namespace imetip;

    const IndicatorState chinese{true, false};
    const IndicatorState english{false, false};
    const IndicatorState englishCaps{false, true};

    {
        SessionLogic session;

        // 首次进入输入态：显示一次并启动计时
        SessionDecision decision = session.OnProbe(true, chinese, 0x1000);
        CHECK(decision.action == IndicatorAction::Show);
        CHECK(decision.restartHideTimer);
        CHECK(session.SessionActive());
        CHECK(session.Visible());

        // 同会话、同状态：不重复弹出、不重置计时
        decision = session.OnProbe(true, chinese, 0x1000);
        CHECK(decision.action == IndicatorAction::None);
        CHECK(!decision.restartHideTimer);

        // 5 秒到：隐藏，但会话仍然有效
        decision = session.OnHideTimeout();
        CHECK(decision.action == IndicatorAction::Hide);
        CHECK(!session.Visible());
        CHECK(session.SessionActive());

        // 隐藏后状态未变化：不重复弹出（FR-04 第 5 条）
        decision = session.OnProbe(true, chinese, 0x1000);
        CHECK(decision.action == IndicatorAction::None);

        // 隐藏后中英文变化：重新弹出并重置计时
        decision = session.OnProbe(true, english, 0x1000);
        CHECK(decision.action == IndicatorAction::Show);
        CHECK(decision.restartHideTimer);

        // 显示中状态变化：原地刷新 + 重置计时
        decision = session.OnProbe(true, englishCaps, 0x1000);
        CHECK(decision.action == IndicatorAction::Refresh);
        CHECK(decision.restartHideTimer);

        // 大写切小写：移除「A」并继续显示
        decision = session.OnProbe(true, english, 0x1000);
        CHECK(decision.action == IndicatorAction::Refresh);
        CHECK(decision.restartHideTimer);

        // 焦点离开：立即隐藏并结束会话
        decision = session.OnProbe(false, english, 0);
        CHECK(decision.action == IndicatorAction::Hide);
        CHECK(!session.SessionActive());
        CHECK(!session.Visible());

        // 重新进入输入态：作为新会话再次显示
        decision = session.OnProbe(true, english, 0x1000);
        CHECK(decision.action == IndicatorAction::Show);
        CHECK(decision.restartHideTimer);
    }

    {
        // 同一窗口内焦点在不同输入控件之间移动：视为新的输入态会话
        SessionLogic session;
        CHECK(session.OnProbe(true, chinese, 0x1000).action == IndicatorAction::Show);
        const SessionDecision decision = session.OnProbe(true, chinese, 0x2000);
        CHECK(decision.action == IndicatorAction::Show);
        CHECK(decision.restartHideTimer);
    }

    {
        // 从未进入输入态时，超时与结束都为无动作
        SessionLogic session;
        CHECK(session.OnHideTimeout().action == IndicatorAction::None);
        CHECK(session.EndSession().action == IndicatorAction::None);
        CHECK(!session.SessionActive());
    }

    {
        // 会话已结束再次结束：无动作
        SessionLogic session;
        session.OnProbe(true, chinese, 0x1000);
        CHECK(session.EndSession().action == IndicatorAction::Hide);
        CHECK(session.EndSession().action == IndicatorAction::None);
    }

    {
        // 窗口滚动导致的外部隐藏：会话保留，状态变化后仍可重新弹出
        SessionLogic session;
        session.OnProbe(true, chinese, 0x1000);
        CHECK(session.OnExternalHide().action == IndicatorAction::Hide);
        CHECK(!session.Visible());
        CHECK(session.SessionActive());
        CHECK(session.OnExternalHide().action == IndicatorAction::None);
        CHECK(session.OnProbe(true, chinese, 0x1000).action == IndicatorAction::None);
        CHECK(session.OnProbe(true, english, 0x1000).action == IndicatorAction::Show);
    }

    {
        // 暂停时 Reset 后不残留会话
        SessionLogic session;
        session.OnProbe(true, chinese, 0x1000);
        session.Reset();
        CHECK(!session.SessionActive());
        CHECK(!session.Visible());
        CHECK(session.OnProbe(true, chinese, 0x1000).action == IndicatorAction::Show);
    }
}
