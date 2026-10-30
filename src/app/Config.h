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
    bool imeChinese = true; // 上次退出时的中英文状态（TSF 应用读不到状态时用于初始化推断值）
    bool stickyAnchor = true; // 窗口不提供光标时，沿用同窗口上次检测到的光标位置（微信等）

    static Config Load();
    void Save() const;

    static std::wstring Directory();
    static std::wstring FilePath();
};

} // namespace imetip
