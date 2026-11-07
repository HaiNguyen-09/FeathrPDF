#pragma once

#include <windows.h>
#include <d2d1.h>

class ContinuousCanvas {
public:
    static bool RegisterClass(HINSTANCE hInstance);
    static HWND Create(HWND hWndParent, HINSTANCE hInstance);
    static void UpdateScroll(HWND hCanvas);
    static void ScrollToPage(HWND hCanvas, int pageIdx);
    static void ScrollToPageTop(HWND hCanvas, int pageIdx);
    static void ScrollToPageBottom(HWND hCanvas, int pageIdx);
    static void ScrollByDelta(HWND hCanvas, int deltaX, int deltaY);
    static void ResetScroll();
    static void ApplyZoomWithAnchor(HWND hCanvas, float oldZoom, float newZoom, int anchorX = -1, int anchorY = -1);
    static int GetScrollY();
    static int GetScrollX();
    static void SetScroll(HWND hCanvas, int scrollX, int scrollY);
    static ID2D1HwndRenderTarget* GetRenderTarget();
};
