#pragma once

#include <windows.h>

namespace imetip {

struct IndicatorMetrics {
    int block = 32;  // 单个色块边长（物理像素）
    int gap = 5;     // 色块间距
    int offsetX = 8; // 相对光标右边缘的水平偏移
    int offsetY = 8; // 相对光标上边缘的垂直偏移
    int shadow = 5;  // 阴影内边距
    int count = 1;   // 色块数量：1（仅中/英）或 2（中/英 + A）
};

struct IndicatorPlacement {
    RECT window{};      // 含阴影的窗口矩形（物理像素）
    bool flippedX = false;
    bool flippedY = false;
};

// 计算指示窗位置：优先放在光标右上方，越界时翻转到左上 / 右下并在工作区内收敛。
// 纯整数运算，便于单元测试。
inline IndicatorPlacement ComputePlacement(const RECT& caret, const RECT& work, const IndicatorMetrics& metrics) {
    IndicatorPlacement result;

    const int count = metrics.count < 1 ? 1 : metrics.count;
    const int innerWidth = count * metrics.block + (count - 1) * metrics.gap;
    const int innerHeight = metrics.block;
    const int windowWidth = innerWidth + metrics.shadow * 2;
    const int windowHeight = innerHeight + metrics.shadow * 2;

    const int caretLeft = caret.left;
    const int caretTop = caret.top;
    const int caretRight = caret.right > caret.left ? caret.right : caret.left + 1;
    const int caretBottom = caret.bottom > caret.top ? caret.bottom : caret.top + 1;

    // 内容块左上角（不含阴影）
    int innerX = caretRight + metrics.offsetX;
    int innerY = caretTop - metrics.offsetY - innerHeight;

    if (innerY < work.top) {
        // 上方空间不足：翻转到光标下方
        innerY = caretBottom + metrics.offsetY;
        result.flippedY = true;
    }
    if (innerY + innerHeight > work.bottom) {
        innerY = work.bottom - innerHeight;
    }
    if (innerY < work.top) {
        innerY = work.top;
    }

    if (innerX + innerWidth > work.right) {
        // 右侧空间不足：翻转到光标左侧
        innerX = caretLeft - metrics.offsetX - innerWidth;
        result.flippedX = true;
    }
    if (innerX < work.left) {
        innerX = work.left;
    }
    if (innerX + innerWidth > work.right) {
        innerX = work.right - innerWidth;
    }

    result.window.left = innerX - metrics.shadow;
    result.window.top = innerY - metrics.shadow;
    result.window.right = result.window.left + windowWidth;
    result.window.bottom = result.window.top + windowHeight;

    // 阴影外扩后仍保证窗口整体位于工作区内
    if (result.window.left < work.left) {
        const int shift = work.left - result.window.left;
        result.window.left += shift;
        result.window.right += shift;
    }
    if (result.window.top < work.top) {
        const int shift = work.top - result.window.top;
        result.window.top += shift;
        result.window.bottom += shift;
    }
    if (result.window.right > work.right) {
        const int shift = result.window.right - work.right;
        result.window.left -= shift;
        result.window.right -= shift;
    }
    if (result.window.bottom > work.bottom) {
        const int shift = result.window.bottom - work.bottom;
        result.window.top -= shift;
        result.window.bottom -= shift;
    }
    return result;
}

} // namespace imetip
