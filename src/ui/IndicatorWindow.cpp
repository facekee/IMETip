#include "ui/IndicatorWindow.h"

#include "platform/Dpi.h"

namespace imetip {
namespace {

constexpr wchar_t kClassName[] = L"IMETip.IndicatorWindow";
constexpr UINT_PTR kFadeTimer = 1;
constexpr int kFadeSteps = 12; // 12 * 16ms ≈ 200ms 淡出

} // namespace

bool IndicatorWindow::Create(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &IndicatorWindow::WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    if (!::RegisterClassExW(&windowClass) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                kClassName, L"IMETip", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    return window_ != nullptr;
}

void IndicatorWindow::Destroy() {
    if (window_) {
        HWND window = window_;
        window_ = nullptr;
        ::KillTimer(window, kFadeTimer);
        ::DestroyWindow(window);
    }
    ReleaseSurface();
    visible_ = false;
}

void IndicatorWindow::Present(const IndicatorModel& model, const RECT& caret, const RECT& workArea, int offsetX,
                              int offsetY, UINT dpiValue) {
    if (!window_) return;

    ::KillTimer(window_, kFadeTimer);
    fadeStep_ = 0;

    style_.block = dpi::Scale(dpi::kBlockSize, dpiValue);
    style_.gap = dpi::Scale(dpi::kBlockGap, dpiValue);
    style_.radius = dpi::Scale(dpi::kBlockRadius, dpiValue);
    style_.shadow = dpi::Scale(dpi::kShadowPadding, dpiValue);

    IndicatorMetrics metrics;
    metrics.block = style_.block;
    metrics.gap = style_.gap;
    metrics.offsetX = dpi::Scale(offsetX, dpiValue);
    metrics.offsetY = dpi::Scale(offsetY, dpiValue);
    metrics.shadow = style_.shadow;
    metrics.count = model.capsLock ? 2 : 1;

    placement_ = ComputePlacement(caret, workArea, metrics);
    const int width = placement_.window.right - placement_.window.left;
    const int height = placement_.window.bottom - placement_.window.top;
    if (width <= 0 || height <= 0) return;

    model_ = model;
    EnsureSurface(width, height);
    if (!surface_) return;

    Render(1.0f);
    PushToScreen(placement_.window.left, placement_.window.top);
    visible_ = true;
}

void IndicatorWindow::Hide(bool animate) {
    if (!window_) return;
    ::KillTimer(window_, kFadeTimer);
    if (!visible_) return;

    if (animate) {
        fadeStep_ = 0;
        ::SetTimer(window_, kFadeTimer, 16, nullptr);
        return;
    }

    ::ShowWindow(window_, SW_HIDE);
    visible_ = false;
}

void IndicatorWindow::EnsureSurface(int width, int height) {
    if (surface_ && width == surfaceWidth_ && height == surfaceHeight_) {
        return;
    }
    ReleaseSurface();

    HDC screen = ::GetDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // 自上而下
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    surface_ = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &surfaceBits_, nullptr, 0);
    surfaceDc_ = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);

    if (surface_ && surfaceDc_) {
        oldSurface_ = ::SelectObject(surfaceDc_, surface_);
        surfaceWidth_ = width;
        surfaceHeight_ = height;
    } else {
        ReleaseSurface();
    }
}

void IndicatorWindow::ReleaseSurface() {
    if (surfaceDc_) {
        if (oldSurface_) ::SelectObject(surfaceDc_, oldSurface_);
        ::DeleteDC(surfaceDc_);
    }
    if (surface_) ::DeleteObject(surface_);
    surfaceDc_ = nullptr;
    surface_ = nullptr;
    oldSurface_ = nullptr;
    surfaceBits_ = nullptr;
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;
}

void IndicatorWindow::Render(float alpha) {
    if (!surface_ || !surfaceBits_ || surfaceWidth_ <= 0) return;

    Gdiplus::Bitmap bitmap(surfaceWidth_, surfaceHeight_, surfaceWidth_ * 4, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(surfaceBits_));
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return;

    RenderIndicator(bitmap, model_, style_, alpha);
}

void IndicatorWindow::PushToScreen(int x, int y) {
    HDC screen = ::GetDC(nullptr);
    POINT destination{x, y};
    POINT source{0, 0};
    SIZE size{surfaceWidth_, surfaceHeight_};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    ::UpdateLayeredWindow(window_, screen, &destination, &size, surfaceDc_, &source, 0, &blend, ULW_ALPHA);
    ::ReleaseDC(nullptr, screen);

    ::ShowWindow(window_, SW_SHOWNOACTIVATE);
}

LRESULT CALLBACK IndicatorWindow::WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    IndicatorWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<IndicatorWindow*>(create->lpCreateParams);
        self->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<IndicatorWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (self) {
        switch (message) {
        case WM_TIMER:
            if (wParam == kFadeTimer) {
                self->fadeStep_++;
                const float alpha = 1.0f - static_cast<float>(self->fadeStep_) / static_cast<float>(kFadeSteps);
                if (alpha <= 0.0f) {
                    ::KillTimer(window, kFadeTimer);
                    ::ShowWindow(window, SW_HIDE);
                    self->visible_ = false;
                } else {
                    self->Render(alpha);
                    self->PushToScreen(self->placement_.window.left, self->placement_.window.top);
                }
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_DESTROY:
            self->window_ = nullptr;
            break;
        default:
            break;
        }
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

} // namespace imetip
