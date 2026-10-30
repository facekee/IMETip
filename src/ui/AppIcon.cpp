#include "ui/AppIcon.h"

#include "ui/GdiplusUtil.h"

namespace imetip::icon {
namespace {

using namespace Gdiplus;
using imetip::gfx::AddRoundRect;
using imetip::gfx::PickFamily;

} // namespace

void RenderAppIcon(Bitmap& target, bool paused) {
    const int size = static_cast<int>(target.GetWidth());
    if (size <= 0) return;

    Graphics graphics(&target);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Color(0, 0, 0, 0));

    const float s = static_cast<float>(size);
    const float pad = s * 0.04f;
    const RectF rect(pad, pad, s - pad * 2.0f, s - pad * 2.0f);
    const float radius = s * 0.235f;

    GraphicsPath body;
    AddRoundRect(body, rect, radius);

    const Color top = paused ? Color(255, 124, 132, 146) : Color(255, 214, 69, 75);
    const Color bottom = paused ? Color(255, 82, 90, 104) : Color(255, 184, 40, 46);
    LinearGradientBrush fill(PointF(rect.X, rect.Y), PointF(rect.X, rect.GetBottom()), top, bottom);
    graphics.FillPath(&fill, &body);

    // 顶部高光
    GraphicsPath highlight;
    const RectF highlightRect(rect.X + rect.Width * 0.10f, rect.Y + rect.Height * 0.07f, rect.Width * 0.80f,
                              rect.Height * 0.32f);
    AddRoundRect(highlight, highlightRect, radius * 0.55f);
    SolidBrush highlightBrush(Color(38, 255, 255, 255));
    graphics.FillPath(&highlightBrush, &highlight);

    if (paused) {
        // 暂停标记：两根白色圆角竖条
        const float barWidth = s * 0.11f;
        const float barHeight = s * 0.36f;
        const float gap = s * 0.09f;
        const float topY = (s - barHeight) * 0.5f;
        const float leftX = (s - (barWidth * 2.0f + gap)) * 0.5f;
        SolidBrush barBrush(Color(255, 250, 250, 252));
        GraphicsPath barLeft;
        AddRoundRect(barLeft, RectF(leftX, topY, barWidth, barHeight), barWidth * 0.35f);
        GraphicsPath barRight;
        AddRoundRect(barRight, RectF(leftX + barWidth + gap, topY, barWidth, barHeight), barWidth * 0.35f);
        graphics.FillPath(&barBrush, &barLeft);
        graphics.FillPath(&barBrush, &barRight);
        return;
    }

    auto family = PickFamily({L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimHei", L"Segoe UI"});
    if (!family) return;

    const float emSize = s * 0.60f;
    Font font(family.get(), emSize, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 255, 246, 246));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    const RectF textRect(0.0f, -s * 0.02f, s, s);
    graphics.DrawString(L"\u4E2D", -1, &font, textRect, &format, &textBrush);
}

HICON CreateAppIcon(int pixelSize, bool paused) {
    if (pixelSize < 8) pixelSize = 8;

    Bitmap bitmap(pixelSize, pixelSize, PixelFormat32bppARGB);
    RenderAppIcon(bitmap, paused);

    HICON handle = nullptr;
    if (bitmap.GetHICON(&handle) != Ok) {
        return nullptr;
    }
    return handle;
}

} // namespace imetip::icon
