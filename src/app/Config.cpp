#include "app/Config.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

namespace imetip {
namespace {

int Clamp(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

} // namespace

std::wstring Config::Directory() {
    PWSTR raw = nullptr;
    std::wstring dir = L".";
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        dir.assign(raw);
        ::CoTaskMemFree(raw);
        dir += L"\\IMETip";
        ::CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

std::wstring Config::FilePath() {
    return Directory() + L"\\config.ini";
}

Config Config::Load() {
    Config config;
    const std::wstring file = FilePath();
    const wchar_t* path = file.c_str();

    config.hideDelayMs = ::GetPrivateProfileIntW(L"indicator", L"hideDelayMs", config.hideDelayMs, path);
    config.offsetX = ::GetPrivateProfileIntW(L"indicator", L"offsetX", config.offsetX, path);
    config.offsetY = ::GetPrivateProfileIntW(L"indicator", L"offsetY", config.offsetY, path);
    config.debounceMs = ::GetPrivateProfileIntW(L"indicator", L"debounceMs", config.debounceMs, path);
    config.fadeOut = ::GetPrivateProfileIntW(L"indicator", L"fadeOut", config.fadeOut ? 1 : 0, path) != 0;
    config.startPaused = ::GetPrivateProfileIntW(L"startup", L"startPaused", 0, path) != 0;
    config.logEnabled = ::GetPrivateProfileIntW(L"diagnostics", L"log", 0, path) != 0;

    config.hideDelayMs = Clamp(config.hideDelayMs, 1000, 30000);
    config.offsetX = Clamp(config.offsetX, 0, 64);
    config.offsetY = Clamp(config.offsetY, 0, 64);
    config.debounceMs = Clamp(config.debounceMs, 20, 200);
    return config;
}

void Config::Save() const {
    const std::wstring file = FilePath();
    const wchar_t* path = file.c_str();
    wchar_t buffer[32]{};

    ::swprintf_s(buffer, L"%d", hideDelayMs);
    ::WritePrivateProfileStringW(L"indicator", L"hideDelayMs", buffer, path);
    ::swprintf_s(buffer, L"%d", offsetX);
    ::WritePrivateProfileStringW(L"indicator", L"offsetX", buffer, path);
    ::swprintf_s(buffer, L"%d", offsetY);
    ::WritePrivateProfileStringW(L"indicator", L"offsetY", buffer, path);
    ::swprintf_s(buffer, L"%d", debounceMs);
    ::WritePrivateProfileStringW(L"indicator", L"debounceMs", buffer, path);
    ::WritePrivateProfileStringW(L"indicator", L"fadeOut", fadeOut ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"startup", L"startPaused", startPaused ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"diagnostics", L"log", logEnabled ? L"1" : L"0", path);
}

} // namespace imetip
