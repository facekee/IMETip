#include "ui/IndicatorRenderer.h"

#include "ui/GdiplusUtil.h"

#include <algorithm>

using namespace Gdiplus;

namespace imetip {
namespace {

constexpr ARGB kTextChinese = 0xFFFFECEC; // 浅红字
constexpr ARGB kTextEnglish = 0xFFE8F1FF; // 浅蓝字
constexpr ARGB kTextCaps = 0xFFEEECFF;    // 浅蓝紫字

void ApplyAlpha(Bitmap& bitmap, float alpha) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) return;

    const int scale = static_cast<int>(alpha * 255.0f + 0.5f);
    if (scale >= 255) return;

    Rect rect(0, 0, width, height);
    BitmapData data{};
    if (bitmap.LockBits(&rect, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Ok) {
        return;
    }

    for (int y = 0; y < height; ++y) {
        auto* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (int x = 0; x < width; ++x) {
            BYTE* pixel = row + x * 4; // BGRA，预乘 alpha
            pixel[0] = static_cast<BYTE>(pixel[0] * scale / 255);
            pixel[1] = static_cast<BYTE>(pixel[1] * scale / 255);
            pixel[2] = static_cast<BYTE>(pixel[2] * scale / 255);
            pixel[3] = static_cast<BYTE>(pixel[3] * scale / 255);
        }
    }
    bitmap.UnlockBits(&data);
}

void DrawShadow(Graphics& graphics, float x, float y, float width, float height, float radius) {
    // 阴影只绘制在整体轮廓之外：轮廓内（含色块间隙）不铺阴影，避免间隙变成一条灰带。
    GraphicsPath hole;
    gfx::AddRoundRect(hole, RectF(x, y, width, height), radius);

    const GraphicsState state = graphics.Save();
    graphics.SetClip(&hole, CombineModeExclude);

    for (int layer = 3; layer >= 1; --layer) {
        const float grow = static_cast<float>(layer) * 1.3f;
        GraphicsPath path;
        gfx::AddRoundRect(path, RectF(x - grow, y - grow + 2.0f, width + grow * 2.0f, height + grow * 2.0f),
                          radius + grow);
        SolidBrush brush(Color(9, 0, 0, 0));
        graphics.FillPath(&brush, &path);
    }

    GraphicsPath base;
    gfx::AddRoundRect(base, RectF(x - 1.0f, y - 0.5f, width + 2.0f, height + 2.0f), radius + 1.0f);
    SolidBrush baseBrush(Color(26, 0, 0, 0));
    graphics.FillPath(&baseBrush, &base);

    graphics.Restore(state);
}

void DrawBlock(Graphics& graphics, float x, float y, float size, float radius, bool capsBlock, bool chinese) {
    GraphicsPath path;
    gfx::AddRoundRect(path, RectF(x, y, size, size), radius);

    Color top;
    Color bottom;
    Color textColor;
    if (capsBlock) {
        // 大写：蓝底浅蓝字（比英文块更深的蓝，便于与「英」区分）
        top = Color(255, 92, 106, 228);
        bottom = Color(255, 62, 74, 188);
        textColor = Color(kTextCaps);
    } else if (chinese) {
        // 中文：红底浅红字
        top = Color(255, 214, 69, 75);
        bottom = Color(255, 182, 40, 46);
        textColor = Color(kTextChinese);
    } else {
        // 英文：蓝底浅蓝字
        top = Color(255, 58, 116, 230);
        bottom = Color(255, 38, 86, 190);
        textColor = Color(kTextEnglish);
    }

    LinearGradientBrush fill(PointF(x, y), PointF(x, y + size), top, bottom);
    graphics.FillPath(&fill, &path);

    GraphicsPath inner;
    gfx::AddRoundRect(inner, RectF(x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f), std::max(1.0f, radius - 1.0f));
    Pen border(Color(48, 255, 255, 255), 1.0f);
    graphics.DrawPath(&border, &inner);

    const wchar_t* glyph = capsBlock ? L"A" : (chinese ? L"\u4E2D" : L"\u82F1");
    auto family = capsBlock ? gfx::PickFamily({L"Segoe UI", L"Arial", L"Microsoft YaHei UI"})
                            : gfx::PickFamily({L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimHei", L"Segoe UI"});
    if (!family) return;

    const float emSize = size * (capsBlock ? 0.58f : 0.55f);
    Font font(family.get(), emSize, capsBlock ? FontStyleBold : FontStyleRegular, UnitPixel);
    if (font.GetLastStatus() != Ok) return;

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textBrush(textColor);
    const RectF textRect(x, y - size * 0.02f, size, size);
    graphics.DrawString(glyph, -1, &font, textRect, &format, &textBrush);
}

} // namespace

void RenderIndicator(Bitmap& target, const IndicatorModel& model, const IndicatorStyle& style, float alpha) {
    Graphics graphics(&target);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Color(0, 0, 0, 0));

    const int count = model.capsLock ? 2 : 1;
    const float block = static_cast<float>(style.block);
    const float gap = static_cast<float>(style.gap);
    const float inset = static_cast<float>(style.shadow);
    const float totalWidth = count * block + (count - 1) * gap;

    DrawShadow(graphics, inset, inset, totalWidth, block, static_cast<float>(style.radius));
    for (int i = 0; i < count; ++i) {
        DrawBlock(graphics, inset + i * (block + gap), inset, block, static_cast<float>(style.radius), i == 1,
                  model.chinese);
    }

    if (alpha < 0.999f) {
        ApplyAlpha(target, alpha);
    }
}

} // namespace imetip
