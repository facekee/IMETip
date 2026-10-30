#pragma once

#include <cstdint>

namespace imetip {

// 指示窗指示的状态快照：中英文模式 + Caps Lock。
struct IndicatorState {
    bool chinese = true;
    bool capsLock = false;

    bool operator==(const IndicatorState& other) const {
        return chinese == other.chinese && capsLock == other.capsLock;
    }
    bool operator!=(const IndicatorState& other) const {
        return !(*this == other);
    }
};

enum class IndicatorAction {
    None,    // 什么都不做
    Show,    // 显示指示窗（含新会话首次显示、隐藏后状态变化重新弹出）
    Refresh, // 指示窗已显示，仅刷新内容并重置计时
    Hide,    // 隐藏指示窗
};

struct SessionDecision {
    IndicatorAction action = IndicatorAction::None;
    bool restartHideTimer = false;
};

// 输入态会话状态机（对应 PRD 第 7 节）。
// 纯逻辑实现，不依赖 Win32，便于单元测试。
class SessionLogic {
public:
    // 收到一次输入态探测结果。
    // inputActive: 当前是否处于输入态；state: 当前状态；focusKey: 焦点控件标识（用于判定新会话）。
    SessionDecision OnProbe(bool inputActive, const IndicatorState& state, std::uintptr_t focusKey) {
        if (!inputActive) {
            return EndSession();
        }

        if (!sessionActive_) {
            sessionActive_ = true;
            visible_ = true;
            state_ = state;
            focusKey_ = focusKey;
            return {IndicatorAction::Show, true};
        }

        if (focusKey != focusKey_) {
            // 焦点移到另一个输入控件：视为新的输入态会话，按规则重新显示一次。
            focusKey_ = focusKey;
            state_ = state;
            visible_ = true;
            return {IndicatorAction::Show, true};
        }

        if (state != state_) {
            const bool wasVisible = visible_;
            state_ = state;
            visible_ = true;
            // 显示中则原地刷新；已隐藏则重新弹出（PRD FR-04 第 4 条）。
            return {wasVisible ? IndicatorAction::Refresh : IndicatorAction::Show, true};
        }

        // 同一会话内状态未变化：显示中不重置计时，已隐藏不重复弹出。
        return {};
    }

    // 5 秒显示计时到期。
    SessionDecision OnHideTimeout() {
        if (!visible_) return {};
        visible_ = false;
        return {IndicatorAction::Hide, false};
    }

    // 输入态结束（焦点离开 / 前台切换 / 光标消失 / 暂停）。
    SessionDecision EndSession() {
        const bool wasVisible = visible_;
        const bool wasActive = sessionActive_;
        sessionActive_ = false;
        visible_ = false;
        focusKey_ = 0;
        if (wasActive && wasVisible) {
            return {IndicatorAction::Hide, false};
        }
        return {};
    }

    void Reset() {
        sessionActive_ = false;
        visible_ = false;
        state_ = IndicatorState{};
        focusKey_ = 0;
    }

    bool SessionActive() const { return sessionActive_; }
    bool Visible() const { return visible_; }
    const IndicatorState& State() const { return state_; }
    std::uintptr_t FocusKey() const { return focusKey_; }

private:
    bool sessionActive_ = false;
    bool visible_ = false;
    IndicatorState state_{};
    std::uintptr_t focusKey_ = 0;
};

} // namespace imetip
