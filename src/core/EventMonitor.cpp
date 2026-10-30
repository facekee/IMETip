#include "core/EventMonitor.h"

namespace imetip {
namespace {

EventMonitor* g_monitor = nullptr;

bool IsShiftKey(DWORD vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

// 翻页键属于明确的“滚动”意图；方向键可能只是移动光标，不做处理。
bool IsScrollKey(DWORD vk) {
    return vk == VK_PRIOR || vk == VK_NEXT;
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
    if (!scrollHook_) {
        // 窗口开始滚动（滚轮、拖动滚动条、代码滚动）都会触发，用于立即隐藏指示窗
        scrollHook_ = ::SetWinEventHook(EVENT_SYSTEM_SCROLLINGSTART, EVENT_SYSTEM_SCROLLINGSTART, nullptr,
                                        WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
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
    if (scrollHook_) {
        ::UnhookWinEvent(scrollHook_);
        scrollHook_ = nullptr;
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
    } else if (event == EVENT_SYSTEM_SCROLLINGSTART) {
        monitor->Notify(InputTrigger::Scroll);
    } else if (event == EVENT_OBJECT_FOCUS) {
        if (idObject == OBJID_CLIENT || idObject == OBJID_WINDOW) {
            monitor->Notify(InputTrigger::Focus);
        }
    }
}

LRESULT CALLBACK EventMonitor::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    EventMonitor* monitor = g_monitor;
    if (code == HC_ACTION && monitor && monitor->enabled_) {
        // 只关心动作类型，不记录坐标、不记录任何内容。
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            monitor->Notify(InputTrigger::Click);
        } else if (wParam == WM_MOUSEWHEEL) {
            monitor->Notify(InputTrigger::Scroll);
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

            if (isDown && vk == VK_CAPITAL) {
                monitor->Notify(InputTrigger::Key);
            } else if (isUp && IsShiftKey(vk)) {
                // Shift 是常见的输入法中英文切换键：抬起即复查一次状态。
                // 这里只用于触发复查，不参与大小写判断（大小写只看 Caps Lock）。
                monitor->Notify(InputTrigger::Key);
            } else if (isUp && vk == VK_SPACE) {
                if ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                    monitor->Notify(InputTrigger::Key);
                }
            } else if (isDown && IsScrollKey(vk)) {
                monitor->Notify(InputTrigger::Scroll);
            }
        }
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace imetip
