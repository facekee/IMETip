#pragma once

#include "ui/GdiplusHeaders.h"

#include <initializer_list>
#include <memory>

namespace imetip::gfx {

inline void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float d = radius * 2.0f;
    if (d <= 0.5f || d >= rect.Width || d >= rect.Height) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

inline std::unique_ptr<Gdiplus::FontFamily> PickFamily(std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* name : names) {
        auto family = std::make_unique<Gdiplus::FontFamily>(name);
        if (family->GetLastStatus() == Gdiplus::Ok) {
            return family;
        }
    }
    return nullptr;
}

} // namespace imetip::gfx
