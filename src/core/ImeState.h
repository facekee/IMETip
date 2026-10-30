#pragma once

#include <windows.h>

namespace imetip {

struct ImeSnapshot {
    bool valid = false;       // 是否成功读取到转换模式；false 时上层应沿用上次状态
    bool chinese = false;     // true=中文（本地语言模式），false=英文
    bool viaFallback = false; // 是否通过输入法窗口回退路径读取
};

// 通过目标窗口的 IME 转换模式判断中英文，使用语言模式掩码，不硬编码具体输入法返回值。
// focusWindow / caretWindow 用于尝试获取 IMM 上下文；
// 现代 TSF 应用（Chrome、新版记事本、微信、WPS 等）通常没有 IMM 上下文，
// 此时回退为询问该线程的默认 IME 窗口（WM_IME_CONTROL + IMC_GETCONVERSIONMODE）。
ImeSnapshot QueryImeState(DWORD threadId, HWND focusWindow, HWND caretWindow);

// 判断键盘布局是否属于中文（仅中文输入法参与中/英判定，其他语言按英文处理）。
bool IsChineseLayout(HKL layout);

} // namespace imetip
