#pragma once

#include "ui/GdiplusHeaders.h"

namespace imetip::icon {

// 把应用图标绘制到给定 32bpp ARGB 位图上（尺寸由位图决定）。
void RenderAppIcon(Gdiplus::Bitmap& target, bool paused);

// 生成托盘图标：启用态为红底「中」，暂停态为灰底暂停标记。
HICON CreateAppIcon(int pixelSize, bool paused);

} // namespace imetip::icon
