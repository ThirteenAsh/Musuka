#pragma once

#include "Models.h"

#include <windows.h>
#include <gdiplus.h>

#include <memory>
#include <vector>

namespace musuka {

class App;

class DesktopWindow {
public:
    explicit DesktopWindow(App* app);
    ~DesktopWindow();

    bool Create();
    void Hide();

private:
    struct RenderItem {
        int objectIndex = -1;
        std::unique_ptr<Gdiplus::Bitmap> bitmap;
        Gdiplus::RectF rect;
        bool hasAlpha = false;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void RegisterClass();
    void LoadAssets();
    void AutoArrangeMissingPositions();
    void RecalculateRects();
    void Paint();
    void DrawBackground(Gdiplus::Graphics& graphics, const RECT& rc);
    int HitTest(int x, int y) const;
    void OpenObject(int objectIndex);
    void OpenContainingLocation(int objectIndex);
    void ShowContextMenu(int x, int y);
    void SaveConfigQuietly();

    App* app_ = nullptr;
    HWND hwnd_ = nullptr;
    std::vector<RenderItem> items_;
    std::unique_ptr<Gdiplus::Bitmap> wallpaper_;
    int selectedObjectIndex_ = -1;
    int draggingItem_ = -1;
    POINT dragOffset_{};
    bool movedDuringDrag_ = false;
};

} // namespace musuka
