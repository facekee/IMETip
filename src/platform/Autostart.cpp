#include "platform/Autostart.h"

#include <windows.h>

namespace imetip::autostart {
namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"IMETip";

} // namespace

std::wstring ExecutablePath() {
    wchar_t buffer[MAX_PATH * 4]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
    if (length == 0 || length >= ARRAYSIZE(buffer)) return {};
    return std::wstring(buffer, length);
}

bool IsEnabled() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t data[1024]{};
    DWORD size = sizeof(data) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status = ::RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(data), &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;

    const std::wstring expected = L"\"" + ExecutablePath() + L"\"";
    return _wcsnicmp(data, expected.c_str(), expected.size()) == 0;
}

bool SetEnabled(bool enabled) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = L"\"" + ExecutablePath() + L"\" --silent";
        status = ::RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                  static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = ::RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace imetip::autostart
