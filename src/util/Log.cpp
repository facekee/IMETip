#include "util/Log.h"

#include <windows.h>
#include <strsafe.h>

#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <vector>

namespace imetip::log {
namespace {

bool g_enabled = false;
std::wstring g_path;
std::mutex g_mutex;

std::string ToUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), need, nullptr, nullptr);
    out.resize(static_cast<size_t>(need - 1));
    return out;
}

void AppendToFile(const wchar_t* line) {
    if (g_path.empty()) return;
    const HANDLE file = ::CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t stamp[64]{};
    ::StringCchPrintfW(stamp, ARRAYSIZE(stamp), L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ", st.wYear, st.wMonth,
                       st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    const std::string utf8 = ToUtf8(stamp) + ToUtf8(line) + "\r\n";
    DWORD written = 0;
    ::WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::CloseHandle(file);
}

} // namespace

void SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool Enabled() {
    return g_enabled;
}

void SetLogFile(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
}

void Write(const wchar_t* format, ...) {
    if (!g_enabled) return;

    wchar_t buffer[1024]{};
    va_list args;
    va_start(args, format);
    ::StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);

    ::OutputDebugStringW(buffer);
    ::OutputDebugStringW(L"\n");

    std::lock_guard<std::mutex> lock(g_mutex);
    AppendToFile(buffer);
}

} // namespace imetip::log
