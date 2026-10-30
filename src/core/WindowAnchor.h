#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace imetip {

// 无光标窗口的输入框位置记忆。
//
// 少数程序（如微信的 Qt 输入框）完全不向系统暴露光标：GetGUIThreadInfo 没有 caret，
// UI Automation 也没有文本接口。这类窗口一旦拿到过一次真实光标位置，就把「相对窗口
// 左上角的偏移」记下来，之后按窗口类名复用，避免每次都要用户双击输入框。
struct WindowAnchor {
    std::wstring windowClass;
    int offsetX = 0; // 相对窗口左上角（物理像素）
    int offsetY = 0;
    int width = 0;  // 学习时的窗口尺寸，用于识别布局变化
    int height = 0;
};

// 窗口尺寸变化在 15%（至少 24px）以内视为同一布局。
inline bool IsAnchorSizeCompatible(const WindowAnchor& anchor, int width, int height) {
    if (anchor.width <= 0 || anchor.height <= 0 || width <= 0 || height <= 0) return false;
    const int toleranceX = (std::max)(24, anchor.width * 15 / 100);
    const int toleranceY = (std::max)(24, anchor.height * 15 / 100);
    return std::abs(width - anchor.width) <= toleranceX && std::abs(height - anchor.height) <= toleranceY;
}

inline const WindowAnchor* FindAnchor(const std::vector<WindowAnchor>& anchors, const std::wstring& windowClass,
                                      int width, int height) {
    if (windowClass.empty()) return nullptr;
    for (const WindowAnchor& anchor : anchors) {
        if (_wcsicmp(anchor.windowClass.c_str(), windowClass.c_str()) == 0 &&
            IsAnchorSizeCompatible(anchor, width, height)) {
            return &anchor;
        }
    }
    return nullptr;
}

// 写入或更新一个窗口类的记忆位置；最多保留 12 条，超出时丢弃最早的一条。
inline void UpsertAnchor(std::vector<WindowAnchor>& anchors, const WindowAnchor& anchor) {
    if (anchor.windowClass.empty()) return;
    for (WindowAnchor& existing : anchors) {
        if (_wcsicmp(existing.windowClass.c_str(), anchor.windowClass.c_str()) == 0) {
            existing = anchor;
            return;
        }
    }
    constexpr size_t kMaxAnchors = 12;
    if (anchors.size() >= kMaxAnchors) {
        anchors.erase(anchors.begin());
    }
    anchors.push_back(anchor);
}

// 点击是否意味着“进入了某个输入框”。参数均为相对窗口左上角的坐标。
//
// 只有两种情况才算：点在输入框附近（右下方向），或点在窗口左侧的列表 / 会话栏
// ——后者是聊天类程序切换会话的位置。点消息气泡、图片等区域不算，
// 避免在只是翻阅消息时反复弹出提示。
inline bool IsAnchorActivationPoint(int anchorX, int anchorY, int x, int y) {
    constexpr int kNearInputMarginX = 40;  // 输入框左侧留白
    constexpr int kNearInputMarginY = 80;  // 输入框上方留白
    constexpr int kListWidth = 120;        // 会话列表的宽度上限差异

    if (x >= anchorX - kNearInputMarginX && y >= anchorY - kNearInputMarginY) return true;
    if (x <= anchorX - kListWidth) return true;
    return false;
}

} // namespace imetip
