#include "core/EventMonitor.h"

namespace imetip {
namespace {

EventMonitor* g_monitor = nullptr;

constexpr DWORD kShiftTapMaxMs = 400; // 轻点 Shift 切换中英文的判定窗口

bool IsShiftKey(DWORD vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

bool IsModifierKey(DWORD vk) {
    return IsShiftKey(vk) || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_MENU ||
           vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

} // namespace

bool EventMonitor::Start(HINSTANCE instance, HWND notifyWindow, UINT message) {
    instance_ = instance;
    notifyWindow_ = notifyWindow;
    message_ = message;
    g_monitor = this;

    enabled_ = true;
    InstallHooks();
    return WinEventHooksInstalled();
}

void EventMonitor::Stop() {
    UninstallHooks();
    enabled_ = false;
    g_monitor = nullptr;
}

void EventMonitor::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (enabled) {
        InstallHooks();
    } else {
        UninstallHooks();
        shiftDown_ = false;
        shiftUsed_ = false;
    }
}

void EventMonitor::InstallHooks() {
    if (!instance_) return;

    if (!foregroundHook_) {
        foregroundHook_ = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc,
                                           0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    if (!focusHook_) {
        focusHook_ = ::SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, WinEventProc, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    if (!mouseHook_) {
        mouseHook_ = ::SetWindowsHookExW(WH_MOUSE_LL, MouseProc, instance_, 0);
    }
    if (!keyboardHook_) {
        keyboardHook_ = ::SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance_, 0);
    }
}

void EventMonitor::UninstallHooks() {
    if (foregroundHook_) {
        ::UnhookWinEvent(foregroundHook_);
        foregroundHook_ = nullptr;
    }
    if (focusHook_) {
        ::UnhookWinEvent(focusHook_);
        focusHook_ = nullptr;
    }
    if (mouseHook_) {
        ::UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_) {
        ::UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
}

void EventMonitor::Notify(InputTrigger trigger) {
    if (!enabled_ || !notifyWindow_) return;
    ::PostMessageW(notifyWindow_, message_, static_cast<WPARAM>(trigger), 0);
}

void CALLBACK EventMonitor::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                         DWORD) {
    EventMonitor* monitor = g_monitor;
    if (!monitor || !monitor->enabled_) return;
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(idChild);

    if (event == EVENT_SYSTEM_FOREGROUND) {
        monitor->Notify(InputTrigger::Foreground);
    } else if (event == EVENT_OBJECT_FOCUS) {
        if (idObject == OBJID_CLIENT || idObject == OBJID_WINDOW) {
            monitor->Notify(InputTrigger::Focus);
        }
    }
}

LRESULT CALLBACK EventMonitor::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    EventMonitor* monitor = g_monitor;
    if (code == HC_ACTION && monitor && monitor->enabled_) {
        // 只关心点击动作本身，不记录坐标、不记录任何内容。
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            monitor->Notify(InputTrigger::Click);
        }
    }
    UNREFERENCED_PARAMETER(lParam);
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK EventMonitor::KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    EventMonitor* monitor = g_monitor;
    if (code == HC_ACTION && monitor && monitor->enabled_) {
        const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (keyboard && (keyboard->flags & LLKHF_INJECTED) == 0) {
            const DWORD vk = keyboard->vkCode;
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (isDown && IsShiftKey(vk)) {
                monitor->shiftDown_ = true;
                monitor->shiftUsed_ = false;
                monitor->shiftDownTick_ = ::GetTickCount();
            } else if (isDown && monitor->shiftDown_ && !IsModifierKey(vk)) {
                monitor->shiftUsed_ = true;
            }

            if (isDown && vk == VK_CAPITAL) {
                monitor->Notify(InputTrigger::Key);
            } else if (isUp && IsShiftKey(vk)) {
                // 轻点 Shift 且期间没有输入其它字符：视为输入法中英文切换。
                const bool quickTap = !monitor->shiftUsed_ &&
                                      (::GetTickCount() - monitor->shiftDownTick_) <= kShiftTapMaxMs;
                monitor->shiftDown_ = false;
                if (quickTap) {
                    monitor->Notify(InputTrigger::Key);
                }
            } else if (isUp && vk == VK_SPACE) {
                if ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                    monitor->Notify(InputTrigger::Key);
                }
            }
        }
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace imetip
