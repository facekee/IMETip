#include "app/Config.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>

namespace imetip {
namespace {

int Clamp(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

// "dx,dy,w,h"
bool ParseAnchorValue(const std::wstring& text, WindowAnchor& anchor) {
    int dx = 0, dy = 0, w = 0, h = 0;
    if (::swscanf_s(text.c_str(), L"%d,%d,%d,%d", &dx, &dy, &w, &h) != 4) return false;
    if (w <= 0 || h <= 0) return false;
    anchor.offsetX = dx;
    anchor.offsetY = dy;
    anchor.width = w;
    anchor.height = h;
    return true;
}

void LoadAnchors(const wchar_t* path, std::vector<WindowAnchor>& anchors) {
    std::vector<wchar_t> buffer(4096, L'\0');
    const DWORD copied = ::GetPrivateProfileSectionW(L"anchors", buffer.data(), static_cast<DWORD>(buffer.size()), path);
    if (copied == 0) return;

    for (const wchar_t* line = buffer.data(); *line; line += wcslen(line) + 1) {
        const wchar_t* equal = wcschr(line, L'=');
        if (!equal) continue;
        WindowAnchor anchor;
        anchor.windowClass.assign(line, static_cast<size_t>(equal - line));
        if (anchor.windowClass.empty()) continue;
        if (!ParseAnchorValue(equal + 1, anchor)) continue;
        anchors.push_back(anchor);
    }
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
    config.debounceMs = ::GetPrivateProfileIntW(L"indicator", L"debounceMs", config.debounceMs, path);
    config.fadeOut = ::GetPrivateProfileIntW(L"indicator", L"fadeOut", config.fadeOut ? 1 : 0, path) != 0;
    config.startPaused = ::GetPrivateProfileIntW(L"startup", L"startPaused", 0, path) != 0;
    config.logEnabled = ::GetPrivateProfileIntW(L"diagnostics", L"log", 0, path) != 0;
    config.imeChinese = ::GetPrivateProfileIntW(L"indicator", L"imeChinese", 1, path) != 0;
    config.anchorEnabled = ::GetPrivateProfileIntW(L"indicator", L"anchorMode", 1, path) != 0;
    LoadAnchors(path, config.anchors);

    config.hideDelayMs = Clamp(config.hideDelayMs, 1000, 30000);
    config.offsetX = Clamp(config.offsetX, 0, 64);
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
    // 旧版本的垂直偏移已废弃（布局改为与光标同一水平线），顺手删掉残留键
    ::WritePrivateProfileStringW(L"indicator", L"offsetY", nullptr, path);
    ::swprintf_s(buffer, L"%d", debounceMs);
    ::WritePrivateProfileStringW(L"indicator", L"debounceMs", buffer, path);
    ::WritePrivateProfileStringW(L"indicator", L"fadeOut", fadeOut ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"indicator", L"imeChinese", imeChinese ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"indicator", L"anchorMode", anchorEnabled ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"startup", L"startPaused", startPaused ? L"1" : L"0", path);
    ::WritePrivateProfileStringW(L"diagnostics", L"log", logEnabled ? L"1" : L"0", path);

    // 位置记忆整段重写，避免残留已失效的窗口类。
    ::WritePrivateProfileStringW(L"anchors", nullptr, nullptr, path);
    for (const WindowAnchor& anchor : anchors) {
        wchar_t value[64]{};
        ::swprintf_s(value, L"%d,%d,%d,%d", anchor.offsetX, anchor.offsetY, anchor.width, anchor.height);
        ::WritePrivateProfileStringW(L"anchors", anchor.windowClass.c_str(), value, path);
    }
}

} // namespace imetip
