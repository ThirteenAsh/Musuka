#include "ImageUtil.h"

#include "Util.h"

#include <algorithm>
#include <cmath>
#include <shlobj.h>
#include <vector>

namespace musuka {

namespace {

int GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) {
        return -1;
    }

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(count, size, encoders);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, format) == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

HICON LoadStockIcon(SHSTOCKICONID id, bool large) {
    SHSTOCKICONINFO info{};
    info.cbSize = sizeof(info);
    const UINT flags = SHGSI_ICON | (large ? SHGSI_LARGEICON : SHGSI_SMALLICON);
    if (SUCCEEDED(SHGetStockIconInfo(id, flags, &info))) {
        return info.hIcon;
    }
    return nullptr;
}

} // namespace

bool SaveHIconAsPng(HICON icon, const std::wstring& path) {
    if (!icon) {
        return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromHICON(icon));
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    CLSID pngClsid{};
    if (GetEncoderClsid(L"image/png", &pngClsid) < 0) {
        return false;
    }
    return bitmap->Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
}

HICON LoadShellIconForObject(const DesktopObject& object, bool large) {
    if (object.type == DesktopObjectType::ThisPC) {
        HICON icon = LoadStockIcon(SIID_DESKTOPPC, large);
        if (icon) {
            return icon;
        }
    }
    if (object.type == DesktopObjectType::RecycleBin) {
        HICON icon = LoadStockIcon(SIID_RECYCLER, large);
        if (icon) {
            return icon;
        }
    }
    if (object.type == DesktopObjectType::Folder) {
        HICON icon = LoadStockIcon(SIID_FOLDER, large);
        if (icon) {
            return icon;
        }
    }

    const std::wstring shellPath = (object.type == DesktopObjectType::ThisPC ||
                                   object.type == DesktopObjectType::RecycleBin)
        ? OpenShellIdForObject(object)
        : object.path;

    SHFILEINFOW info{};
    const UINT flags = SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (SHGetFileInfoW(shellPath.c_str(), 0, &info, sizeof(info), flags) != 0 && info.hIcon) {
        return info.hIcon;
    }
    return LoadStockIcon(SIID_APPLICATION, large);
}

HBITMAP CreateThumbnailBitmap(const std::wstring& imagePath, int width, int height) {
    auto bitmap = LoadBitmapFromPath(ToAbsoluteAppPath(imagePath));
    if (!bitmap) {
        return nullptr;
    }

    Gdiplus::Bitmap canvas(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    DrawImageContain(graphics, bitmap.get(), Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));

    HBITMAP handle = nullptr;
    canvas.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &handle);
    return handle;
}

bool ImageCanBeLoaded(const std::wstring& imagePath) {
    auto bitmap = LoadBitmapFromPath(imagePath);
    return bitmap != nullptr;
}

std::unique_ptr<Gdiplus::Bitmap> LoadBitmapFromPath(const std::wstring& imagePath) {
    if (imagePath.empty() || !FileExists(imagePath)) {
        return nullptr;
    }
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(imagePath.c_str(), FALSE);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok ||
        bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
        return nullptr;
    }
    return bitmap;
}

bool BitmapHasAlpha(Gdiplus::Bitmap* bitmap) {
    if (!bitmap) {
        return false;
    }
    const UINT flags = bitmap->GetFlags();
    if ((flags & Gdiplus::ImageFlagsHasAlpha) != 0) {
        return true;
    }
    const PixelFormat format = bitmap->GetPixelFormat();
    return (format & PixelFormatAlpha) != 0 || (format & PixelFormatPAlpha) != 0;
}

Gdiplus::RectF CalculateContainRect(Gdiplus::Image* image,
                                    float x,
                                    float y,
                                    float maxWidth,
                                    float maxHeight) {
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) {
        return Gdiplus::RectF(x, y, maxWidth, maxHeight);
    }
    const float imageWidth = static_cast<float>(image->GetWidth());
    const float imageHeight = static_cast<float>(image->GetHeight());
    const float scale = std::min(maxWidth / imageWidth, maxHeight / imageHeight);
    const float width = imageWidth * scale;
    const float height = imageHeight * scale;
    return Gdiplus::RectF(x + (maxWidth - width) * 0.5f,
                          y + (maxHeight - height) * 0.5f,
                          width,
                          height);
}

void DrawImageContain(Gdiplus::Graphics& graphics,
                      Gdiplus::Image* image,
                      const Gdiplus::RectF& bounds) {
    if (!image) {
        return;
    }
    const Gdiplus::RectF rect = CalculateContainRect(image, bounds.X, bounds.Y, bounds.Width, bounds.Height);
    graphics.DrawImage(image, rect);
}

void DrawImageCover(Gdiplus::Graphics& graphics,
                    Gdiplus::Image* image,
                    const Gdiplus::RectF& bounds) {
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) {
        return;
    }
    const float imageWidth = static_cast<float>(image->GetWidth());
    const float imageHeight = static_cast<float>(image->GetHeight());
    const float scale = std::max(bounds.Width / imageWidth, bounds.Height / imageHeight);
    const float width = imageWidth * scale;
    const float height = imageHeight * scale;
    const float x = bounds.X + (bounds.Width - width) * 0.5f;
    const float y = bounds.Y + (bounds.Height - height) * 0.5f;
    graphics.DrawImage(image, Gdiplus::RectF(x, y, width, height));
}

bool AlphaHitTest(Gdiplus::Bitmap* bitmap,
                  const Gdiplus::RectF& drawRect,
                  int screenX,
                  int screenY,
                  BYTE threshold) {
    if (!bitmap) {
        return false;
    }
    if (screenX < drawRect.X || screenY < drawRect.Y ||
        screenX >= drawRect.X + drawRect.Width ||
        screenY >= drawRect.Y + drawRect.Height) {
        return false;
    }
    if (!BitmapHasAlpha(bitmap)) {
        return true;
    }

    const double localX = (static_cast<double>(screenX) - drawRect.X) / drawRect.Width;
    const double localY = (static_cast<double>(screenY) - drawRect.Y) / drawRect.Height;
    const UINT px = static_cast<UINT>(std::clamp(localX, 0.0, 0.999999) * bitmap->GetWidth());
    const UINT py = static_cast<UINT>(std::clamp(localY, 0.0, 0.999999) * bitmap->GetHeight());

    Gdiplus::Color color;
    if (bitmap->GetPixel(px, py, &color) != Gdiplus::Ok) {
        return true;
    }
    return color.GetAlpha() > threshold;
}

} // namespace musuka
