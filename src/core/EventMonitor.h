#pragma once

#include <windows.h>

namespace imetip {

enum class InputTrigger {
    Foreground, // 前台窗口切换
    Focus,      // 焦点变化
    Click,      // 鼠标点击
    Key,        // 按键（仅用于输入态内的状态复查）
    Scroll,     // 滚轮 / 翻页 / 窗口开始滚动
};

// 事件源：WinEvent 钩子（前台切换 / 焦点 / 开始滚动）+ 低级鼠标 / 键盘钩子。
// 不做任何定时轮询，所有检查均由系统事件触发，最终以窗口消息通知宿主。
class EventMonitor {
public:
    bool Start(HINSTANCE instance, HWND notifyWindow, UINT message);
    void Stop();
    void SetEnabled(bool enabled);

    bool Enabled() const { return enabled_; }
    bool WinEventHooksInstalled() const { return foregroundHook_ != nullptr || focusHook_ != nullptr; }
    bool MouseHookInstalled() const { return mouseHook_ != nullptr; }
    bool KeyboardHookInstalled() const { return keyboardHook_ != nullptr; }

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD threadId, DWORD time);
    static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);

    void Notify(InputTrigger trigger);
    void InstallHooks();
    void UninstallHooks();

    HINSTANCE instance_ = nullptr;
    HWND notifyWindow_ = nullptr;
    UINT message_ = 0;
    bool enabled_ = false;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;
    HWINEVENTHOOK scrollHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
};

} // namespace imetip
