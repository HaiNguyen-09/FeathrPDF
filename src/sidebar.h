#pragma once

#include <windows.h>

class NavigationSidebar {
public:
    static bool RegisterClass(HINSTANCE hInstance);
    static HWND Create(HWND hWndParent, HINSTANCE hInstance);
    static void InvalidateThumbnails(HWND hSidebar);
    static void InvalidatePageThumbnail(HWND hSidebar, int pageIdx);
    static void PruneThumbnails(HWND hSidebar);
    static void ScrollToPage(HWND hSidebar, int pageIdx);
    static void ClearCache();
};
