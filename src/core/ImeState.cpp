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

ImeSnapshot QueryImeState(DWORD threadId, HWND focusWindow) {
    ImeSnapshot snapshot;

    const HKL layout = ::GetKeyboardLayout(threadId);
    if (!IsChineseLayout(layout)) {
        // 非中文键盘布局：确定为英文态，无需查询 IME。
        snapshot.valid = true;
        snapshot.chinese = false;
        return snapshot;
    }

    // 优先查询焦点窗口，失败时退回前台窗口。
    DWORD conversion = 0;
    if (!TryReadConversion(focusWindow, conversion) && !TryReadConversion(::GetForegroundWindow(), conversion)) {
        // 查询失败：保持 valid=false，由上层沿用上次有效状态。
        return snapshot;
    }

    snapshot.valid = true;
    // IME_CMODE_NATIVE 表示本地语言模式（中文），与具体输入法私有返回值无关。
    snapshot.chinese = (conversion & IME_CMODE_NATIVE) != 0;
    return snapshot;
}

} // namespace imetip
