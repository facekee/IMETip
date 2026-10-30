#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace imetip {

// 系统托盘图标（Shell_NotifyIcon）。退出时必须调用 Remove，避免残留幽灵图标。
class TrayIcon {
public:
    bool Add(HWND owner, UINT callbackMessage, HICON icon);
    void Remove();

    void SetIcon(HICON icon);
    void SetTooltip(const std::wstring& text);
    void ShowBalloon(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO);

    bool Added() const { return added_; }

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

} // namespace imetip
