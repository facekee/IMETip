#pragma once

#include <windows.h>

namespace imetip {

struct ImeSnapshot {
    bool valid = false;   // 是否成功读取到转换模式；false 时上层用本地维护的状态
    bool chinese = false; // true=中文（本地语言模式），false=英文
};

// 通过目标窗口的 IME 转换模式判断中英文，使用语言模式掩码，不硬编码具体输入法返回值。
// 现代 TSF 应用（Chrome / 微信 / 新版记事本 / WPS 等）没有 IMM 上下文，这里会返回 valid=false，
// 由上层按 Shift / Ctrl+Space 的切换动作维护本地状态（见 InputProbe::ToggleImeState）。
ImeSnapshot QueryImeState(DWORD threadId, HWND focusWindow, HWND caretWindow);

// 判断键盘布局是否属于中文（仅中文输入法参与中/英判定，其他语言按英文处理）。
bool IsChineseLayout(HKL layout);

} // namespace imetip
