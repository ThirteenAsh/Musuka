#include "DesktopWindow.h"

#include "App.h"
#include "ImageUtil.h"
#include "Util.h"
#include "WinUtil.h"

#include <shellapi.h>
#include <windowsx.h>

namespace musuka {

namespace {

constexpr int ID_CONTEXT_OPEN = 2001;
constexpr int ID_CONTEXT_OPEN_LOCATION = 2002;
constexpr int ID_CONTEXT_RETURN_SETTINGS = 2003;
constexpr int ID_CONTEXT_EXIT = 2004;
constexpr BYTE kAlphaThreshold = 20;

bool ShellExecuteChecked(HWND owner,
                         const wchar_t* operation,
                         const std::wstring& file,
                         const std::wstring& parameters = {}) {
    HINSTANCE result = ShellExecuteW(owner,
                                     operation,
                                     file.c_str(),
                                     parameters.empty() ? nullptr : parameters.c_str(),
                                     nullptr,
                                     SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ShowError(owner, L"打开失败。");
        return false;
    }
    return true;
}

std::wstring ExplorerSelectParameter(const std::wstring& path) {
    return L"/select,\"" + path + L"\"";
}

} // namespace

DesktopWindow::DesktopWindow(App* app) : app_(app) {}

DesktopWindow::~DesktopWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool DesktopWindow::Create() {
    RegisterClass();

    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW,
                            L"MusukaDesktopWindow",
                            L"musuka desktop",
                            WS_POPUP | WS_VISIBLE,
                            0,
                            0,
                            width,
                            height,
                            nullptr,
                            nullptr,
                            app_->Instance(),
                            this);
    if (!hwnd_) {
        return false;
    }

    LoadAssets();
    AutoArrangeMissingPositions();
    RecalculateRects();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetForegroundWindow(hwnd_);
    return true;
}

void DesktopWindow::Hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void DesktopWindow::RegisterClass() {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = DesktopWindow::WindowProc;
    wc.hInstance = app_->Instance();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MusukaDesktopWindow";
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);
    registered = true;
}

LRESULT CALLBACK DesktopWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    DesktopWindow* self = reinterpret_cast<DesktopWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<DesktopWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT DesktopWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        RecalculateRects();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int itemIndex = HitTest(x, y);
        if (itemIndex >= 0) {
            draggingItem_ = itemIndex;
            selectedObjectIndex_ = items_[static_cast<size_t>(itemIndex)].objectIndex;
            DesktopObject& object = app_->Config().objects[static_cast<size_t>(selectedObjectIndex_)];
            dragOffset_.x = x - object.x;
            dragOffset_.y = y - object.y;
            movedDuringDrag_ = false;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            selectedObjectIndex_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (draggingItem_ >= 0 && (wParam & MK_LBUTTON) != 0) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            RenderItem& item = items_[static_cast<size_t>(draggingItem_)];
            DesktopObject& object = app_->Config().objects[static_cast<size_t>(item.objectIndex)];
            const int newX = x - dragOffset_.x;
            const int newY = y - dragOffset_.y;
            if (object.x != newX || object.y != newY) {
                object.x = newX;
                object.y = newY;
                movedDuringDrag_ = true;
                RecalculateRects();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (draggingItem_ >= 0) {
            ReleaseCapture();
            if (movedDuringDrag_) {
                SaveConfigQuietly();
            }
            draggingItem_ = -1;
            movedDuringDrag_ = false;
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        const int itemIndex = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (itemIndex >= 0) {
            selectedObjectIndex_ = items_[static_cast<size_t>(itemIndex)].objectIndex;
            OpenObject(selectedObjectIndex_);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int itemIndex = HitTest(x, y);
        if (itemIndex >= 0) {
            selectedObjectIndex_ = items_[static_cast<size_t>(itemIndex)].objectIndex;
        } else {
            selectedObjectIndex_ = -1;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        POINT point{x, y};
        ClientToScreen(hwnd_, &point);
        ShowContextMenu(point.x, point.y);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_CONTEXT_OPEN:
            if (selectedObjectIndex_ >= 0) {
                OpenObject(selectedObjectIndex_);
            }
            break;
        case ID_CONTEXT_OPEN_LOCATION:
            if (selectedObjectIndex_ >= 0) {
                OpenContainingLocation(selectedObjectIndex_);
            }
            break;
        case ID_CONTEXT_RETURN_SETTINGS:
            SaveConfigQuietly();
            app_->ReturnToSettings();
            break;
        case ID_CONTEXT_EXIT:
            app_->Exit();
            break;
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            SaveConfigQuietly();
            app_->ReturnToSettings();
            return 0;
        }
        break;
    case WM_CLOSE:
        SaveConfigQuietly();
        app_->Exit();
        return 0;
    case WM_NCDESTROY:
    {
        HWND oldHwnd = hwnd_;
        hwnd_ = nullptr;
        return DefWindowProcW(oldHwnd, message, wParam, lParam);
    }
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DesktopWindow::LoadAssets() {
    items_.clear();
    wallpaper_.reset();

    AppConfig& config = app_->Config();
    if (config.backgroundSource == BackgroundSource::SystemWallpaper && !config.systemWallpaperPath.empty()) {
        wallpaper_ = LoadBitmapFromPath(config.systemWallpaperPath);
    }

    for (int i = 0; i < static_cast<int>(config.objects.size()); ++i) {
        DesktopObject& object = config.objects[static_cast<size_t>(i)];
        if (!object.includeInDesktop || object.candidates.empty()) {
            continue;
        }
        if (object.selectedCandidate < 0 ||
            object.selectedCandidate >= static_cast<int>(object.candidates.size())) {
            object.selectedCandidate = 0;
        }
        std::unique_ptr<Gdiplus::Bitmap> bitmap =
            LoadBitmapFromPath(ToAbsoluteAppPath(object.candidates[static_cast<size_t>(object.selectedCandidate)].internalPath));
        if (!bitmap) {
            for (const auto& candidate : object.candidates) {
                bitmap = LoadBitmapFromPath(ToAbsoluteAppPath(candidate.internalPath));
                if (bitmap) {
                    break;
                }
            }
        }
        if (!bitmap) {
            continue;
        }

        RenderItem item;
        item.objectIndex = i;
        item.hasAlpha = BitmapHasAlpha(bitmap.get());
        item.bitmap = std::move(bitmap);
        items_.push_back(std::move(item));
    }
}

void DesktopWindow::AutoArrangeMissingPositions() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int startX = 40;
    const int startY = 40;
    const int stepX = kDesktopIconMaxSize + 34;
    const int stepY = kDesktopIconMaxSize + 34;
    int x = startX;
    int y = startY;
    bool changed = false;

    for (auto& item : items_) {
        DesktopObject& object = app_->Config().objects[static_cast<size_t>(item.objectIndex)];
        if (object.x < 0 || object.y < 0) {
            object.x = x;
            object.y = y;
            changed = true;
            y += stepY;
            if (y + kDesktopIconMaxSize > rc.bottom - 20) {
                y = startY;
                x += stepX;
            }
        }
    }
    if (changed) {
        SaveConfigQuietly();
    }
}

void DesktopWindow::RecalculateRects() {
    for (auto& item : items_) {
        DesktopObject& object = app_->Config().objects[static_cast<size_t>(item.objectIndex)];
        item.rect = CalculateContainRect(item.bitmap.get(),
                                         static_cast<float>(object.x),
                                         static_cast<float>(object.y),
                                         static_cast<float>(kDesktopIconMaxSize),
                                         static_cast<float>(kDesktopIconMaxSize));
    }
}

void DesktopWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    HDC memory = CreateCompatibleDC(dc);
    HBITMAP buffer = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
    HGDIOBJ oldBitmap = SelectObject(memory, buffer);

    Gdiplus::Graphics graphics(memory);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    DrawBackground(graphics, rc);

    for (const auto& item : items_) {
        graphics.DrawImage(item.bitmap.get(), item.rect);
        if (item.objectIndex == selectedObjectIndex_) {
            Gdiplus::Pen pen(Gdiplus::Color(210, 40, 120, 230), 2.0f);
            graphics.DrawRectangle(&pen, item.rect);
        }
    }
    graphics.Flush();

    BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(buffer);
    DeleteDC(memory);
    EndPaint(hwnd_, &ps);
}

void DesktopWindow::DrawBackground(Gdiplus::Graphics& graphics, const RECT& rc) {
    const Gdiplus::RectF bounds(0.0f,
                                0.0f,
                                static_cast<float>(rc.right - rc.left),
                                static_cast<float>(rc.bottom - rc.top));
    AppConfig& config = app_->Config();
    if (config.backgroundSource == BackgroundSource::SystemWallpaper && wallpaper_) {
        DrawImageCover(graphics, wallpaper_.get(), bounds);
        return;
    }

    Gdiplus::Color color(255,
                         GetRValue(config.solidColor),
                         GetGValue(config.solidColor),
                         GetBValue(config.solidColor));
    graphics.Clear(color);
}

int DesktopWindow::HitTest(int x, int y) const {
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
        const RenderItem& item = items_[static_cast<size_t>(i)];
        if (AlphaHitTest(item.bitmap.get(), item.rect, x, y, kAlphaThreshold)) {
            return i;
        }
    }
    return -1;
}

void DesktopWindow::OpenObject(int objectIndex) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(app_->Config().objects.size())) {
        return;
    }
    const DesktopObject& object = app_->Config().objects[static_cast<size_t>(objectIndex)];
    if (!object.includeInDesktop) {
        return;
    }

    if (object.type == DesktopObjectType::ThisPC || object.type == DesktopObjectType::RecycleBin) {
        ShellExecuteChecked(hwnd_, L"open", OpenShellIdForObject(object));
        return;
    }
    ShellExecuteChecked(hwnd_, L"open", object.path);
}

void DesktopWindow::OpenContainingLocation(int objectIndex) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(app_->Config().objects.size())) {
        return;
    }
    const DesktopObject& object = app_->Config().objects[static_cast<size_t>(objectIndex)];
    if (object.type == DesktopObjectType::ThisPC || object.type == DesktopObjectType::RecycleBin) {
        OpenObject(objectIndex);
        return;
    }
    ShellExecuteChecked(hwnd_, L"open", L"explorer.exe", ExplorerSelectParameter(object.path));
}

void DesktopWindow::ShowContextMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    const bool hasSelection = selectedObjectIndex_ >= 0;
    AppendMenuW(menu, MF_STRING | (hasSelection ? MF_ENABLED : MF_GRAYED), ID_CONTEXT_OPEN, L"打开");
    AppendMenuW(menu, MF_STRING | (hasSelection ? MF_ENABLED : MF_GRAYED), ID_CONTEXT_OPEN_LOCATION, L"打开所在位置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_CONTEXT_RETURN_SETTINGS, L"返回 settings");
    AppendMenuW(menu, MF_STRING, ID_CONTEXT_EXIT, L"退出 musuka");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void DesktopWindow::SaveConfigQuietly() {
    std::wstring error;
    app_->Store().Save(app_->Config(), error);
}

} // namespace musuka
