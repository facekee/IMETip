#pragma once

#include <windows.h>

#include <cstdint>

#include "platform/UiaProbe.h"

namespace imetip {

struct InputSnapshot {
    bool inputActive = false;
    bool stateKnown = false;  // IME 状态是否由系统接口真实读取到
    bool chinese = false;
    bool capsLock = false;
    bool viaUia = false;
    bool imeInferred = false;    // IME 状态为本地推断（系统接口读不到时按 Shift/Ctrl+Space 翻转）
    bool uiaTextAvailable = false; // 焦点元素是否提供 UIA 文本接口（决定能否用缓存光标兜底）
    RECT caret{};
    HWND focusWindow = nullptr;
    DWORD threadId = 0;
    std::uintptr_t sessionKey = 0; // 用于区分输入态会话
};

// 输入态探测：标准光标（GetGUIThreadInfo）优先，UI Automation 文本光标回退。
class InputProbe {
public:
    bool Start();
    void Stop();

    // slowUia: 首次点击后的复查使用更长等待，给 UIA 首次加载提供程序留出时间。
    InputSnapshot Probe(HWND ownWindow, bool slowUia = false);

    // 系统接口读不到 IME 状态时（TSF 应用），按 Shift / Ctrl+Space 的切换动作翻转本地状态。
    void ToggleImeState();

    // 启动时用上次退出前的状态初始化推断值。
    void SetInitialChinese(bool chinese) { believedChinese_ = chinese; }

    bool LastKnownChinese() const { return believedChinese_; }

private:
    UiaProbe uia_;
    // 最近一次可信的中文/英文状态：真实读取优先，读不到时由 ToggleImeState 维护。
    bool believedChinese_ = true;
};

} // namespace imetip
