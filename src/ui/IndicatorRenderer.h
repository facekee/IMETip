#pragma once

#include "ui/GdiplusHeaders.h"

namespace imetip {

// 指示窗显示内容：中英文模式 + Caps Lock（大写时才出现「A」）。
struct IndicatorModel {
    bool chinese = true;
    bool capsLock = false;
};

// 与 DPI 相关的物理像素尺寸。
struct IndicatorStyle {
    int block = 32;
    int gap = 5;
    int radius = 8;
    int shadow = 5;
};

// 把指示窗内容绘制到位图上；位图尺寸应等于 ComputePlacement 得到的窗口尺寸。
// alpha 用于淡出（0~1），绘制结果始终为预乘 alpha。
void RenderIndicator(Gdiplus::Bitmap& target, const IndicatorModel& model, const IndicatorStyle& style, float alpha);

} // namespace imetip
