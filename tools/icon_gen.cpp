// 图标生成工具：把 AppIcon 的绘制结果打包成多尺寸 .ico。
// 用法： imetip-icon [输出路径]    默认 res/IMETip.ico
//
// 该工具只依赖 GDI+，与运行时托盘图标共用同一套绘制代码，保证视觉一致。

#include "ui/AppIcon.h"

#include <objbase.h>
#include <objidl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int GetEncoderClsid(const wchar_t* mimeType, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return -1;

    std::vector<BYTE> buffer(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count, size, codecs);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EncodePng(Gdiplus::Bitmap& bitmap, std::vector<BYTE>& output) {
    CLSID clsid{};
    if (GetEncoderClsid(L"image/png", &clsid) < 0) return false;

    IStream* stream = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;

    bool ok = bitmap.Save(stream, &clsid, nullptr) == Gdiplus::Ok;
    if (ok) {
        STATSTG stat{};
        if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) {
            ok = false;
        } else {
            output.resize(static_cast<size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero{};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            ULONG read = 0;
            stream->Read(output.data(), static_cast<ULONG>(output.size()), &read);
            output.resize(read);
        }
    }
    stream->Release();
    return ok;
}

bool EncodeDib(Gdiplus::Bitmap& bitmap, std::vector<BYTE>& output) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = width;
    header.biHeight = height * 2;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;

    output.assign(sizeof(header), 0);
    std::memcpy(output.data(), &header, sizeof(header));

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = height - 1; y >= 0; --y) {
        const auto* row = reinterpret_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        const size_t offset = output.size();
        output.resize(offset + rowBytes);
        std::memcpy(output.data() + offset, row, rowBytes);
    }

    bitmap.UnlockBits(&data);

    // AND 掩码全 0（透明信息由 alpha 通道提供）
    const int maskStride = ((width + 31) / 32) * 4;
    const size_t maskOffset = output.size();
    output.resize(maskOffset + static_cast<size_t>(maskStride) * height, 0);
    return true;
}

void AppendU16(std::vector<BYTE>& out, unsigned value) {
    out.push_back(static_cast<BYTE>(value & 0xFF));
    out.push_back(static_cast<BYTE>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<BYTE>& out, unsigned value) {
    out.push_back(static_cast<BYTE>(value & 0xFF));
    out.push_back(static_cast<BYTE>((value >> 8) & 0xFF));
    out.push_back(static_cast<BYTE>((value >> 16) & 0xFF));
    out.push_back(static_cast<BYTE>((value >> 24) & 0xFF));
}

struct ImageEntry {
    int size = 0;
    std::vector<BYTE> data;
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* output = (argc > 1) ? argv[1] : L"res/IMETip.ico";

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) != Gdiplus::Ok) {
        std::fwprintf(stderr, L"GDI+ 初始化失败\n");
        return 1;
    }

    const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
    std::vector<ImageEntry> images;
    images.reserve(std::size(sizes));

    for (int size : sizes) {
        Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
        imetip::icon::RenderAppIcon(bitmap, false);

        ImageEntry entry;
        entry.size = size;
        const bool ok = (size >= 256) ? EncodePng(bitmap, entry.data) : EncodeDib(bitmap, entry.data);
        if (!ok) {
            std::fwprintf(stderr, L"生成 %dpx 图像失败\n", size);
            Gdiplus::GdiplusShutdown(token);
            return 1;
        }
        std::wprintf(L"  size %3d: %zu bytes\n", size, entry.data.size());
        images.push_back(std::move(entry));
    }

    std::vector<BYTE> ico;
    AppendU16(ico, 0);
    AppendU16(ico, 1);
    AppendU16(ico, static_cast<unsigned>(images.size()));

    unsigned offset = 6 + 16 * static_cast<unsigned>(images.size());
    for (const ImageEntry& image : images) {
        const BYTE dimension = static_cast<BYTE>(image.size >= 256 ? 0 : image.size);
        ico.push_back(dimension);
        ico.push_back(dimension);
        ico.push_back(0);
        ico.push_back(0);
        AppendU16(ico, 1);
        AppendU16(ico, 32);
        AppendU32(ico, static_cast<unsigned>(image.data.size()));
        AppendU32(ico, offset);
        offset += static_cast<unsigned>(image.data.size());
    }
    for (const ImageEntry& image : images) {
        ico.insert(ico.end(), image.data.begin(), image.data.end());
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, output, L"wb") != 0 || !file) {
        std::fwprintf(stderr, L"无法写入 %s\n", output);
        Gdiplus::GdiplusShutdown(token);
        return 1;
    }
    std::fwrite(ico.data(), 1, ico.size(), file);
    std::fclose(file);

    std::wprintf(L"已写入 %s（%zu 字节）\n", output, ico.size());
    Gdiplus::GdiplusShutdown(token);
    return 0;
}
