#pragma once

#include <string>

namespace imetip::log {

// 日志默认关闭，仅 --log 启动参数或配置文件 log=1 时开启。
void SetEnabled(bool enabled);
bool Enabled();

// 设置日志文件路径（默认 %LOCALAPPDATA%\IMETip\imetip.log）。
void SetLogFile(const std::wstring& path);

// 写入一条日志，同时输出到调试器和日志文件。不记录任何输入内容。
void Write(const wchar_t* format, ...);

} // namespace imetip::log
