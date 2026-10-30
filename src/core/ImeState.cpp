#include "core/ImeState.h"

#include <imm.h>

// IMC_GETCONVERSIONMODE 在公开 SDK 的 imm.h 中不提供，值定义见 IMM 文档。
#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif

namespace imetip {
namespace {

bool TryReadConversion(HWND window, DWORD& conversion) {
    if (!window) return false;
    const HIMC context = ::ImmGetContext(window);
    if (!context) return false;

    DWORD sentence = 0;
    const BOOL ok = ::ImmGetConversionStatus(context, &conversion, &sentence);
    ::ImmReleaseContext(window, context);
    return ok != FALSE;
}

// 没有 IMM 上下文的 TSF 应用：询问该线程的默认 IME 窗口。
// 该调用是跨进程 SendMessage，必须带超时，避免无响应应用拖住 UI 线程。
bool TryReadFromImeWindow(HWND window, DWORD& conversion) {
    if (!window) return false;
    const HWND imeWindow = ::ImmGetDefaultIMEWnd(window);
    if (!imeWindow) return false;

    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeoutW(imeWindow, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0,
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 30, &result);
    if (sent == 0) return false;

    conversion = static_cast<DWORD>(result & 0xFFFF);
    return true;
}

} // namespace

bool IsChineseLayout(HKL layout) {
    const LANGID language = LOWORD(reinterpret_cast<UINT_PTR>(layout));
    return PRIMARYLANGID(language) == LANG_CHINESE;
}

ImeSnapshot QueryImeState(DWORD threadId, HWND focusWindow, HWND caretWindow) {
    ImeSnapshot snapshot;

    const HKL layout = ::GetKeyboardLayout(threadId);
    if (!IsChineseLayout(layout)) {
        // 非中文键盘布局：确定为英文态，无需查询 IME。
        snapshot.valid = true;
        snapshot.chinese = false;
        return snapshot;
    }

    // 依次尝试焦点窗口、光标窗口、前台窗口的 IMM 上下文。
    const HWND candidates[] = {focusWindow, caretWindow, ::GetForegroundWindow()};
    DWORD conversion = 0;
    for (HWND window : candidates) {
        if (TryReadConversion(window, conversion)) {
            snapshot.valid = true;
            // IME_CMODE_NATIVE 表示本地语言模式（中文），与具体输入法私有返回值无关。
            snapshot.chinese = (conversion & IME_CMODE_NATIVE) != 0;
            return snapshot;
        }
    }

    // TSF 应用没有 IMM 上下文：回退到默认 IME 窗口。
    const HWND imeHost = focusWindow ? focusWindow : ::GetForegroundWindow();
    if (TryReadFromImeWindow(imeHost, conversion)) {
        snapshot.valid = true;
        snapshot.viaFallback = true;
        snapshot.chinese = (conversion & IME_CMODE_NATIVE) != 0;
        return snapshot;
    }

    // 全部失败：保持 valid=false，由上层沿用上次有效状态。
    return snapshot;
}

} // namespace imetip
