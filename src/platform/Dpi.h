#pragma once

#include <windows.h>

namespace imetip::dpi {

// 进程级 DPI 感知兜底。清单已声明 PerMonitorV2，此处仅在清单失效时生效。
void EnablePerMonitorAwareness();

// 返回显示器有效 DPI（96 为 100%）。
UINT ForMonitor(HMONITOR monitor);

// 按 96 DPI 逻辑像素换算为物理像素。
int Scale(int value, UINT dpiValue);

// 指示窗使用的 96 DPI 逻辑尺寸。
constexpr int kBlockSize = 32;
// 两个色块之间的间距：留够空间让各自的阴影分开，看起来是两个独立图标而不是连成一条。
constexpr int kBlockGap = 10;
constexpr int kBlockRadius = 8;
constexpr int kShadowPadding = 5;

} // namespace imetip::dpi
