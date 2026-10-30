#include "core/ImeState.h"

#include <imm.h>

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
    // TSF 应用通常三者都拿不到上下文，返回 valid=false 交给上层推断。
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

    return snapshot;
}

} // namespace imetip
