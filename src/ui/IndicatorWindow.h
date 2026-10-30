#pragma once

#include "ui/GdiplusHeaders.h"
#include "ui/IndicatorLayout.h"
#include "ui/IndicatorRenderer.h"

namespace imetip {

// 光标旁的悬浮指示窗：分层、透明、置顶、点击穿透、不抢焦点。
class IndicatorWindow {
public:
    bool Create(HINSTANCE instance);
    void Destroy();

    // 绘制并显示，同时按光标位置完成定位与边界翻转（默认与光标同一水平线）。
    void Present(const IndicatorModel& model, const RECT& caret, const RECT& workArea, int offsetX, UINT dpiValue);
    void Hide(bool animate);

    bool IsVisible() const { return visible_; }
    HWND Handle() const { return window_; }

private:
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void EnsureSurface(int width, int height);
    void ReleaseSurface();
    void Render(float alpha);
    void PushToScreen(int x, int y);

    HWND window_ = nullptr;
    HBITMAP surface_ = nullptr;
    HDC surfaceDc_ = nullptr;
    HGDIOBJ oldSurface_ = nullptr;
    void* surfaceBits_ = nullptr;
    int surfaceWidth_ = 0;
    int surfaceHeight_ = 0;

    IndicatorModel model_{};
    IndicatorPlacement placement_{};
    IndicatorStyle style_{};
    bool visible_ = false;
    int fadeStep_ = 0;
};

} // namespace imetip
