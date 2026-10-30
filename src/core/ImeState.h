#pragma once

#include <windows.h>

namespace imetip {

struct ImeSnapshot {
    bool valid = false;   // 是否成功读取到转换模式；false 时上层应沿用上次状态
    bool chinese = false; // true=中文（本地语言模式），false=英文
};

// 通过目标窗口的 IME 转换模式判断中英文，使用语言模式掩码，不硬编码具体输入法返回值。
ImeSnapshot QueryImeState(DWORD threadId, HWND focusWindow);

// 判断键盘布局是否属于中文（仅中文输入法参与中/英判定，其他语言按英文处理）。
bool IsChineseLayout(HKL layout);

} // namespace imetip
