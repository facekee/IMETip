#pragma once

#include <windows.h>

#include <cstdint>

#include "platform/UiaProbe.h"

namespace imetip {

struct InputSnapshot {
    bool inputActive = false;
    bool stateKnown = false; // IME 状态是否读取成功
    bool chinese = false;
    bool capsLock = false;
    bool viaUia = false;
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

    InputSnapshot Probe(HWND ownWindow);

    bool LastKnownChinese() const { return lastChinese_; }

private:
    UiaProbe uia_;
    bool lastChinese_ = true;
};

} // namespace imetip
