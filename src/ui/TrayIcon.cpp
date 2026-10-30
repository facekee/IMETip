#include "ui/TrayIcon.h"

#include <strsafe.h>

namespace imetip {

bool TrayIcon::Add(HWND owner, UINT callbackMessage, HICON icon) {
    // 允许重复调用：资源管理器重启后需要重新 NIM_ADD。
    data_ = NOTIFYICONDATAW{};
    data_.cbSize = sizeof(NOTIFYICONDATAW);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = icon;
    data_.uVersion = NOTIFYICON_VERSION_4;
    ::StringCchCopyW(data_.szTip, ARRAYSIZE(data_.szTip), L"IMETip");

    if (!::Shell_NotifyIconW(NIM_ADD, &data_)) {
        return false;
    }
    ::Shell_NotifyIconW(NIM_SETVERSION, &data_);
    added_ = true;
    return true;
}

void TrayIcon::Remove() {
    if (!added_) return;
    ::Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayIcon::SetIcon(HICON icon) {
    if (!added_ || !icon) return;
    data_.hIcon = icon;
    data_.uFlags = NIF_ICON;
    ::Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::SetTooltip(const std::wstring& text) {
    if (!added_) return;
    ::StringCchCopyW(data_.szTip, ARRAYSIZE(data_.szTip), text.c_str());
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    ::Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& text, DWORD flags) {
    if (!added_) return;

    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = flags;
    ::StringCchCopyW(data_.szInfoTitle, ARRAYSIZE(data_.szInfoTitle), title.c_str());
    ::StringCchCopyW(data_.szInfo, ARRAYSIZE(data_.szInfo), text.c_str());
    ::Shell_NotifyIconW(NIM_MODIFY, &data_);

    data_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

} // namespace imetip
