#pragma once

#include <string>

namespace imetip::autostart {

// 开机自启通过 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 实现，无需管理员权限。
bool IsEnabled();
bool SetEnabled(bool enabled);

std::wstring ExecutablePath();

} // namespace imetip::autostart
