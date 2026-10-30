#include "core/InputProbe.h"

#include "core/ImeState.h"
#include "util/Log.h"

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

InputSnapshot InputProbe::Probe(HWND ownWindow, bool slowUia) {
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
    HWND caretWindow = nullptr;

    if (!IsNonInputWindowClass(foreground)) {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (::GetGUIThreadInfo(threadId, &info)) {
            // 焦点可能落在同进程的另一个 GUI 线程上（内嵌编辑器等），
            // 此时前台线程没有光标，需要再查焦点窗口所在线程。
            if (!info.hwndCaret && info.hwndFocus) {
                DWORD focusProcessId = 0;
                const DWORD focusThreadId = ::GetWindowThreadProcessId(info.hwndFocus, &focusProcessId);
                if (focusThreadId != 0 && focusThreadId != threadId && focusProcessId == processId) {
                    GUITHREADINFO focusInfo{};
                    focusInfo.cbSize = sizeof(focusInfo);
                    if (::GetGUIThreadInfo(focusThreadId, &focusInfo) && focusInfo.hwndCaret) {
                        info = focusInfo;
                    }
                }
            }

            if (info.hwndCaret) {
                HWND target = info.hwndFocus ? info.hwndFocus : foreground;
                const bool caretRectUsable = info.rcCaret.bottom > info.rcCaret.top;
                if (caretRectUsable && !IsReadOnlyEditControl(target) && !IsNonInputWindowClass(target)) {
                    POINT topLeft{info.rcCaret.left, info.rcCaret.top};
                    POINT bottomRight{info.rcCaret.right, info.rcCaret.bottom};
                    if (::ClientToScreen(info.hwndCaret, &topLeft) &&
                        ::ClientToScreen(info.hwndCaret, &bottomRight)) {
                        caret = RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
                        focusWindow = target;
                        caretWindow = info.hwndCaret;
                        haveCaret = true;
                    }
                }
            }
        }
    }

    UiaCaretInfo uiaInfo{};
    if (!haveCaret) {
        // 浏览器 / 终端等不使用标准光标的场景，走 UIA 文本光标回退（带超时）。
        // 首次查询需要加载目标应用的 UIA 提供程序，慢一些，复查时给更长的等待。
        uiaInfo = uia_.Query(slowUia ? 260 : 70);
        snapshot.uiaTextAvailable = uiaInfo.ok;
        if (uiaInfo.ok && uiaInfo.editable) {
            caret = uiaInfo.caret;
            haveCaret = true;
            snapshot.viaUia = true;
        }
    }

    if (!haveCaret) {
        if (log::Enabled()) {
            // 只记录窗口类名与控件类型，便于定位不支持的应用，不记录任何输入内容。
            wchar_t className[128]{};
            ::GetClassNameW(foreground, className, ARRAYSIZE(className));
            log::Write(L"未检测到光标：foreground=%s uiaOk=%d uiaType=%ld", className, uiaInfo.ok ? 1 : 0,
                       uiaInfo.controlType);
        }
        return snapshot;
    }

    if (caret.right <= caret.left) caret.right = caret.left + 1;
    if (caret.bottom <= caret.top) caret.bottom = caret.top + 1;

    snapshot.inputActive = true;
    snapshot.caret = caret;
    snapshot.focusWindow = focusWindow;
    snapshot.sessionKey = reinterpret_cast<std::uintptr_t>(focusWindow);

    const ImeSnapshot ime = QueryImeState(threadId, focusWindow, caretWindow);
    snapshot.stateKnown = ime.valid;
    if (ime.valid) {
        believedChinese_ = ime.chinese;
        snapshot.chinese = ime.chinese;
    } else {
        // 系统接口读不到（TSF 应用）：使用本地维护的状态，不弹窗、不崩溃。
        snapshot.imeInferred = true;
        snapshot.chinese = believedChinese_;
    }
    return snapshot;
}

void InputProbe::ToggleImeState() {
    believedChinese_ = !believedChinese_;
    log::Write(L"按切换键推断 IME 状态：%s", believedChinese_ ? L"中" : L"英");
}

} // namespace imetip
