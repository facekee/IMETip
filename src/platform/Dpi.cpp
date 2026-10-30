#include "platform/Dpi.h"

#include <shellscalingapi.h>

namespace imetip::dpi {

void EnablePerMonitorAwareness() {
    // 清单已声明 PerMonitorV2；这里只做无清单场景的兜底。
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }
    ::SetProcessDPIAware();
}

UINT ForMonitor(HMONITOR monitor) {
    if (!monitor) {
        monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
        return dpiX;
    }

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info)) {
        HDC dc = ::CreateDCW(L"DISPLAY", info.szDevice, nullptr, nullptr);
        if (dc) {
            const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
            ::DeleteDC(dc);
            if (dpi > 0) return static_cast<UINT>(dpi);
        }
    }
    return 96;
}

int Scale(int value, UINT dpiValue) {
    if (dpiValue == 0) dpiValue = 96;
    return ::MulDiv(value, static_cast<int>(dpiValue), 96);
}

} // namespace imetip::dpi
