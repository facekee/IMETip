// 生成 README / 文档用的指示窗样式预览图。
// 用法： imetip-preview [输出路径]    默认 docs/preview.png

#include "ui/IndicatorRenderer.h"

#include <objbase.h>
#include <objidl.h>

#include <cstdio>
#include <vector>

using namespace Gdiplus;

namespace {

int GetEncoderClsid(const wchar_t* mimeType, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    GetImageEncodersSize(&count, &size);
    if (size == 0) return -1;
    std::vector<BYTE> buffer(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    GetImageEncoders(count, size, codecs);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DrawCaption(Graphics& graphics, const wchar_t* text, const RectF& rect) {
    FontFamily family(L"Microsoft YaHei UI");
    Font font(&family, 14.0f, FontStyleRegular, UnitPixel);
    SolidBrush brush(Color(255, 90, 99, 112));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentNear);
    graphics.DrawString(text, -1, &font, rect, &format, &brush);
}

void DrawSample(Graphics& graphics, float left, float top, const imetip::IndicatorModel& model, int blocks) {
    const imetip::IndicatorStyle style{96, 15, 24, 15};
    const int width = blocks * style.block + (blocks - 1) * style.gap + style.shadow * 2;
    const int height = style.block + style.shadow * 2;

    Bitmap sample(width, height, PixelFormat32bppPARGB);
    imetip::RenderIndicator(sample, model, style, 1.0f);
    graphics.DrawImage(&sample, RectF(left, top, static_cast<float>(width), static_cast<float>(height)));
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* output = (argc > 1) ? argv[1] : L"docs/preview.png";

    GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &startupInput, nullptr) != Ok) {
        std::fwprintf(stderr, L"GDI+ 初始化失败\n");
        return 1;
    }

    const int canvasWidth = 780;
    const int canvasHeight = 264;
    CLSID clsid{};

    // GDI+ 对象必须在 GdiplusShutdown 之前析构
    {
        Bitmap canvas(canvasWidth, canvasHeight, PixelFormat32bppARGB);

        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        graphics.Clear(Color(255, 247, 248, 250));

        {
            FontFamily family(L"Microsoft YaHei UI");
            Font title(&family, 22.0f, FontStyleBold, UnitPixel);
            SolidBrush titleBrush(Color(255, 31, 36, 48));
            graphics.DrawString(L"IMETip 指示窗样式", -1, &title, PointF(40.0f, 28.0f), &titleBrush);

            Font subtitle(&family, 13.0f, FontStyleRegular, UnitPixel);
            SolidBrush subtitleBrush(Color(255, 107, 114, 128));
            graphics.DrawString(L"仅在可编辑输入框获得焦点时显示于光标右上方，5 秒后自动隐藏", -1, &subtitle,
                                PointF(40.0f, 62.0f), &subtitleBrush);
        }

        const float sampleTop = 104.0f;
        const float captionTop = 212.0f;
        DrawSample(graphics, 40.0f, sampleTop, imetip::IndicatorModel{true, false}, 1);
        DrawCaption(graphics, L"中文 · 小写", RectF(24.0f, captionTop, 158.0f, 24.0f));

        DrawSample(graphics, 232.0f, sampleTop, imetip::IndicatorModel{false, false}, 1);
        DrawCaption(graphics, L"英文 · 小写", RectF(216.0f, captionTop, 158.0f, 24.0f));

        DrawSample(graphics, 424.0f, sampleTop, imetip::IndicatorModel{false, true}, 2);
        DrawCaption(graphics, L"英文 · Caps Lock 开启时追加 A", RectF(408.0f, captionTop, 300.0f, 24.0f));

        if (GetEncoderClsid(L"image/png", &clsid) < 0) {
            std::fwprintf(stderr, L"找不到 PNG 编码器\n");
            GdiplusShutdown(token);
            return 1;
        }
        if (canvas.Save(output, &clsid, nullptr) != Ok) {
            std::fwprintf(stderr, L"写入 %s 失败\n", output);
            GdiplusShutdown(token);
            return 1;
        }
    }

    GdiplusShutdown(token);
    std::wprintf(L"已写入 %s\n", output);
    return 0;
}
