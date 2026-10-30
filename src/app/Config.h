#pragma once

#include <string>

namespace imetip {

// 用户配置，持久化在 %LOCALAPPDATA%\IMETip\config.ini。
// 全部为本地配置，不含任何输入内容。
struct Config {
    int hideDelayMs = 5000; // 指示窗显示时长
    int offsetX = 8;        // 光标右上方水平偏移（96 DPI 逻辑像素）
    int offsetY = 8;        // 光标右上方垂直偏移（96 DPI 逻辑像素）
    int debounceMs = 50;    // 事件合并去抖窗口
    bool fadeOut = false;   // 隐藏时是否使用 200ms 淡出
    bool startPaused = false;
    bool logEnabled = false;

    static Config Load();
    void Save() const;

    static std::wstring Directory();
    static std::wstring FilePath();
};

} // namespace imetip
