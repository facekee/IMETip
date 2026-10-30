#include "core/InputProbe.h"

#include "core/ImeState.h"

namespace imetip {
namespace {

// 这些窗口即使带有光标也不算输入态（桌面、任务栏、列表等）。
bool IsNonInputWindowClass(HWND window) {
    wchar_t className[128]{};
    if (!window || ::GetClassNameW(window, className, ARRAYSIZE(className)) == 0) {
        return false;
    }
    static const wchar_t* kBlocked[] = {
        L"Progman",      L"WorkerW",        L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
        L"NotifyIconOverflowWindow", L"SysListView32", L"SysTreeView32", L"ToolbarWindow32",
    };
    for (const wchar_t* name : kBlocked) {
        if (_wcsicmp(className, name) == 0) return true;
    }
    return false;
}

// 只读的标准 Edit 控件会显示光标但不接受输入，需要排除。
bool IsReadOnlyEditControl(HWND window) {
    if (!window) return false;
    wchar_t className[128]{};
    if (::GetClassNameW(window, className, ARRAYSIZE(className)) == 0) {
        return false;
    }
    if (_wcsicmp(className, L"Edit") != 0) {
        return false;
    }
    const LONG_PTR style = ::GetWindowLongPtrW(window, GWL_STYLE);
    return (style & ES_READONLY) != 0;
}

} // namespace

bool InputProbe::Start() {
    return uia_.Start();
}

void InputProbe::Stop() {
    uia_.Stop();
}

InputSnapshot InputProbe::Probe(HWND ownWindow) {
    InputSnapshot snapshot;

    const HWND foreground = ::GetForegroundWindow();
    if (!foreground || foreground == ownWindow) {
        return snapshot;
    }

    DWORD processId = 0;
    const DWORD threadId = ::GetWindowThreadProcessId(foreground, &processId);
    if (threadId == 0 || processId == ::GetCurrentProcessId()) {
        return snapshot;
    }

    snapshot.threadId = threadId;
    snapshot.focusWindow = foreground;
    snapshot.capsLock = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    bool haveCaret = false;
    RECT caret{};
    HWND focusWindow = foreground;

    if (!IsNonInputWindowClass(foreground)) {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (::GetGUIThreadInfo(threadId, &info) && info.hwndCaret) {
            HWND target = info.hwndFocus ? info.hwndFocus : foreground;
            const bool caretRectUsable = info.rcCaret.bottom > info.rcCaret.top;
            if (caretRectUsable && !IsReadOnlyEditControl(target) && !IsNonInputWindowClass(target)) {
                POINT topLeft{info.rcCaret.left, info.rcCaret.top};
                POINT bottomRight{info.rcCaret.right, info.rcCaret.bottom};
                if (::ClientToScreen(info.hwndCaret, &topLeft) && ::ClientToScreen(info.hwndCaret, &bottomRight)) {
                    caret = RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
                    focusWindow = target;
                    haveCaret = true;
                }
            }
        }
    }

    if (!haveCaret) {
        // 浏览器 / 终端等不使用标准光标的场景，走 UIA 文本光标回退（带超时）。
        const UiaCaretInfo uiaInfo = uia_.Query(70);
        if (uiaInfo.ok && uiaInfo.editable) {
            caret = uiaInfo.caret;
            haveCaret = true;
            snapshot.viaUia = true;
        }
    }

    if (!haveCaret) {
        return snapshot;
    }

    if (caret.right <= caret.left) caret.right = caret.left + 1;
    if (caret.bottom <= caret.top) caret.bottom = caret.top + 1;

    snapshot.inputActive = true;
    snapshot.caret = caret;
    snapshot.focusWindow = focusWindow;
    snapshot.sessionKey = reinterpret_cast<std::uintptr_t>(focusWindow);

    const ImeSnapshot ime = QueryImeState(threadId, focusWindow);
    snapshot.stateKnown = ime.valid;
    if (ime.valid) {
        lastChinese_ = ime.chinese;
        snapshot.chinese = ime.chinese;
    } else {
        // 查询失败时沿用上次有效状态，不弹窗、不崩溃。
        snapshot.chinese = lastChinese_;
    }
    return snapshot;
}

} // namespace imetip
