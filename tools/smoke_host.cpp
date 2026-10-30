// 手动验证宿主：启动一个带多行编辑框的窗口并聚焦，
// 用于观察 IMETip 指示窗是否出现在光标右上方。8 秒后自动退出。
//
// 用法：先运行 IMETip.exe，再运行 imetip-smoke-host.exe

#include <windows.h>

namespace {

constexpr int kEditId = 1001;
constexpr wchar_t kClassName[] = L"IMETip.SmokeHost";

HWND g_edit = nullptr;

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 20, 20, 540,
                                   220, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), nullptr,
                                   nullptr);
        if (g_edit) {
            ::SetWindowTextW(g_edit, L"点这里输入，观察光标右上方是否出现「中 / 英」提示。");
            ::SendMessageW(g_edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        }
        return 0;
    }
    case WM_SETFOCUS:
        if (g_edit) ::SetFocus(g_edit);
        return 0;
    case WM_TIMER:
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

void ForceForeground(HWND window) {
    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);
    if (::GetForegroundWindow() == window) {
        if (g_edit) ::SetFocus(g_edit);
        return;
    }

    // 从后台进程启动时系统可能拒绝设置前台窗口，附加输入队列后再试一次。
    const DWORD foregroundThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
    const DWORD currentThread = ::GetCurrentThreadId();
    ::AttachThreadInput(currentThread, foregroundThread, TRUE);
    ::BringWindowToTop(window);
    ::SetForegroundWindow(window);
    if (g_edit) ::SetFocus(g_edit);
    ::AttachThreadInput(currentThread, foregroundThread, FALSE);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kClassName;
    if (!::RegisterClassExW(&windowClass)) return 1;

    HWND window = ::CreateWindowExW(0, kClassName, L"IMETip 手动验证宿主", WS_OVERLAPPEDWINDOW, 120, 120, 620, 320,
                                    nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ForceForeground(window);
    ::SetTimer(window, 1, 8000, nullptr);

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return 0;
}
