#pragma once

#include <windows.h>

namespace imetip {

// UIA 返回的光标矩形是否可信：必须落在焦点元素的矩形内。
// Chromium / Electron 在折叠选区（纯光标）时可能返回完全无关元素的矩形，
// 例如 QQ 的 ProseMirror 编辑器会把工具栏上某个 21x21 的方块当成光标，
// 直接用会让提示窗跑到离光标很远的地方。
inline bool IsCaretRectTrustworthy(const RECT& caret, const RECT& element) {
    if (element.right <= element.left || element.bottom <= element.top) return false;
    if (caret.right <= caret.left || caret.bottom <= caret.top) return false;
    return caret.right > element.left && caret.left < element.right && caret.bottom > element.top &&
           caret.top < element.bottom;
}

// 元素矩形是否像一个输入框（排除整篇文档 / 整个窗口）。
inline bool IsEditorLikeRect(const RECT& element, int windowWidth, int windowHeight) {
    const int width = element.right - element.left;
    const int height = element.bottom - element.top;
    if (width < 40 || height < 16) return false;
    if (windowWidth <= 0 || windowHeight <= 0) return false;
    const long long area = 1LL * width * height;
    const long long windowArea = 1LL * windowWidth * windowHeight;
    return area * 100 <= windowArea * 35;
}

} // namespace imetip
