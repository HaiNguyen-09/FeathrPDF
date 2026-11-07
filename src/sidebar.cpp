#include "sidebar.h"
#include "app.h"
#include <vector>
#include <map>
#include <cmath>
#include <dwmapi.h>
#include <uxtheme.h>

static const int ITEM_WIDTH = 140;
static const int ITEM_THUMB_HEIGHT = 198;
static const int ITEM_HEIGHT = 236;
static const int ITEM_PADDING = 10;

static int g_scrollY = 0;
static double g_sideWheelAccumY = 0.0;
static HFONT g_hSidebarFont = NULL;
static HFONT g_hSidebarBoldFont = NULL;
static std::map<int, HBITMAP> g_thumbCache;

static int g_dragSrc = -1;
static int g_dragDest = -1;
static bool g_isDragging = false;
static bool g_isDragInside = false;
static bool g_pendingSingleSelect = false;
static POINT g_dragStartPt = { 0, 0 };

static const UINT_PTR TIMER_ID_AUTOSCROLL = 2001;
static UINT_PTR g_autoScrollTimer = 0;
static int g_autoScrollDelta = 0;

static void StopAutoScroll(HWND hWnd) {
    if (g_autoScrollTimer != 0) {
        KillTimer(hWnd, g_autoScrollTimer);
        g_autoScrollTimer = 0;
        g_autoScrollDelta = 0;
    }
}

static void StartAutoScroll(HWND hWnd, int delta) {
    g_autoScrollDelta = delta;
    if (g_autoScrollTimer == 0) {
        g_autoScrollTimer = SetTimer(hWnd, TIMER_ID_AUTOSCROLL, 30, NULL);
    }
}

struct MovePosDialogState {
    int totalPages;
    int targetPage;
    bool confirmed;
    HWND hEdit;
};

static LRESULT CALLBACK MovePosDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MovePosDialogState* state = (MovePosDialogState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        state = (MovePosDialogState*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)state);

        if (PDFApp::Instance().IsDarkMode()) {
            BOOL useDark = TRUE;
            if (FAILED(DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark)))) {
                DwmSetWindowAttribute(hWnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &useDark, sizeof(useDark));
            }
            COLORREF captionColor = RGB(24, 24, 27);
            COLORREF captionText = RGB(244, 244, 245);
            DwmSetWindowAttribute(hWnd, 35, &captionColor, sizeof(captionColor));
            DwmSetWindowAttribute(hWnd, 36, &captionText, sizeof(captionText));
            SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }

        HFONT hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        std::wstring prompt = L"Enter the target page number (1 - " + std::to_wstring(state->totalPages) + L"):";
        HWND hLbl = CreateWindowW(L"STATIC", prompt.c_str(), WS_CHILD | WS_VISIBLE,
            20, 20, 280, 22, hWnd, NULL, cs->hInstance, NULL);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        state->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(state->targetPage).c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
            20, 48, 280, 26, hWnd, (HMENU)101, cs->hInstance, NULL);
        SendMessageW(state->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(state->hEdit, EM_SETSEL, 0, -1);

        HWND hBtnOk = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            105, 88, 90, 28, hWnd, (HMENU)IDOK, cs->hInstance, NULL);
        SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            205, 88, 90, 28, hWnd, (HMENU)IDCANCEL, cs->hInstance, NULL);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(state->hEdit);
        return 0;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        bool isDark = PDFApp::Instance().IsDarkMode();
        COLORREF bgCol = isDark ? RGB(24, 24, 27) : GetSysColor(COLOR_BTNFACE);
        COLORREF textCol = isDark ? RGB(244, 244, 245) : RGB(24, 24, 27);
        SetTextColor(hdcStatic, textCol);
        SetBkColor(hdcStatic, bgCol);
        static HBRUSH hBrDark = CreateSolidBrush(RGB(24, 24, 27));
        return (LRESULT)(isDark ? hBrDark : (HBRUSH)(COLOR_BTNFACE + 1));
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        bool isDark = PDFApp::Instance().IsDarkMode();
        COLORREF bgCol = isDark ? RGB(39, 39, 42) : RGB(255, 255, 255);
        COLORREF textCol = isDark ? RGB(244, 244, 245) : RGB(24, 24, 27);
        SetTextColor(hdcEdit, textCol);
        SetBkColor(hdcEdit, bgCol);
        static HBRUSH hBrDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrLight = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hBrDark : hBrLight);
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            WCHAR buf[32] = { 0 };
            GetWindowTextW(state->hEdit, buf, 32);
            int val = _wtoi(buf);
            if (val >= 1 && val <= state->totalPages) {
                state->targetPage = val;
                state->confirmed = true;
                DestroyWindow(hWnd);
            } else {
                std::wstring err = L"Invalid page number! Please enter a number between 1 and " + std::to_wstring(state->totalPages);
                MessageBoxW(hWnd, err.c_str(), L"Error", MB_OK | MB_ICONWARNING);
                SetFocus(state->hEdit);
                SendMessageW(state->hEdit, EM_SETSEL, 0, -1);
            }
        } else if (id == IDCANCEL) {
            state->confirmed = false;
            DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_CLOSE:
        state->confirmed = false;
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool ShowMoveToPositionDialog(HWND hParent, int totalPages, int currentFirstPage, int& outTargetPage) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = MovePosDlgProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"FeathrPDFMovePosDlgClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    MovePosDialogState state;
    state.totalPages = totalPages;
    state.targetPage = currentFirstPage;
    state.confirmed = false;
    state.hEdit = NULL;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int dlgW = 340;
    int dlgH = 168;
    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"FeathrPDFMovePosDlgClass",
        L"Move to Position",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH, hParent, NULL, hInst, &state);

    if (!hDlg) return false;

    EnableWindow(hParent, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
                continue;
            } else if (msg.wParam == VK_ESCAPE) {
                SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
                continue;
            }
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetFocus(hParent);

    if (state.confirmed) {
        outTargetPage = state.targetPage;
        return true;
    }
    return false;
}

static void UpdateSidebarScroll(HWND hWnd) {
    PDFApp& app = PDFApp::Instance();
    int totalPages = app.GetPageCount();
    RECT rc;
    GetClientRect(hWnd, &rc);
    int clientH = rc.bottom - rc.top;

    int totalH = totalPages * ITEM_HEIGHT + 20;

    SCROLLINFO si = { sizeof(SCROLLINFO) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalH;
    si.nPage = clientH;
    si.nPos = g_scrollY;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

    if (g_scrollY + clientH > totalH) {
        g_scrollY = std::max(0, totalH - clientH);
        SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
    }
}

void NavigationSidebar::ScrollToPage(HWND hSidebar, int pageIdx) {
    if (!hSidebar) return;
    PDFApp& app = PDFApp::Instance();
    int totalPages = app.GetPageCount();
    if (pageIdx < 0 || pageIdx >= totalPages) return;

    RECT rc;
    GetClientRect(hSidebar, &rc);
    int clientH = rc.bottom - rc.top;
    if (clientH <= 0) return;

    int totalH = totalPages * ITEM_HEIGHT + 20;
    int maxScrollY = std::max(0, totalH - clientH);

    int itemTop = pageIdx * ITEM_HEIGHT + 10;
    int itemBottom = itemTop + ITEM_HEIGHT - 10;

    int newScrollY = g_scrollY;
    if (itemTop < g_scrollY) {
        // Thumbnail is above visible viewport: scroll up just enough so it's comfortably visible
        newScrollY = std::max(0, itemTop - 10);
    } else if (itemBottom > g_scrollY + clientH) {
        // Thumbnail is below visible viewport: scroll down just enough so it's comfortably visible
        newScrollY = std::min(maxScrollY, itemBottom + 10 - clientH);
    }

    if (newScrollY != g_scrollY) {
        g_scrollY = newScrollY;
        UpdateSidebarScroll(hSidebar);
        InvalidateRect(hSidebar, NULL, FALSE);
    }
}

void NavigationSidebar::ClearCache() {
    for (auto& pair : g_thumbCache) {
        if (pair.second) {
            DeleteObject(pair.second);
        }
    }
    g_thumbCache.clear();
}

void NavigationSidebar::InvalidatePageThumbnail(HWND hSidebar, int pageIdx) {
    auto it = g_thumbCache.find(pageIdx);
    if (it != g_thumbCache.end()) {
        if (it->second) {
            DeleteObject(it->second);
        }
        g_thumbCache.erase(it);
    }
    if (hSidebar) {
        InvalidateRect(hSidebar, NULL, TRUE);
        UpdateWindow(hSidebar);
    }
}

void NavigationSidebar::PruneThumbnails(HWND hSidebar) {
    if (!hSidebar) return;
    RECT rc;
    GetClientRect(hSidebar, &rc);
    int clientH = rc.bottom - rc.top;
    int firstVisible = std::max(0, (g_scrollY - 10) / ITEM_HEIGHT);
    int lastVisible = (g_scrollY + clientH + 10) / ITEM_HEIGHT;

    for (auto it = g_thumbCache.begin(); it != g_thumbCache.end(); ) {
        if (it->first < firstVisible - 2 || it->first > lastVisible + 2) {
            if (it->second) DeleteObject(it->second);
            it = g_thumbCache.erase(it);
        } else {
            ++it;
        }
    }
}

void NavigationSidebar::InvalidateThumbnails(HWND hSidebar) {
    ClearCache();
    if (hSidebar) {
        UpdateSidebarScroll(hSidebar);
        InvalidateRect(hSidebar, NULL, TRUE);
        UpdateWindow(hSidebar);
    }
}

static LRESULT CALLBACK SidebarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PDFApp& app = PDFApp::Instance();

    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // Prevent background erasing flicker on Intel Iris Xe

    case WM_CREATE: {
        if (!g_hSidebarFont) {
            g_hSidebarFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hSidebarBoldFont = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
        return 0;
    }

    case WM_SIZE: {
        UpdateSidebarScroll(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_VSCROLL: {
        int action = LOWORD(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientH = rc.bottom - rc.top;
        int totalH = app.GetPageCount() * ITEM_HEIGHT + 20;

        switch (action) {
        case SB_LINEUP: g_scrollY -= 30; break;
        case SB_LINEDOWN: g_scrollY += 30; break;
        case SB_PAGEUP: g_scrollY -= clientH; break;
        case SB_PAGEDOWN: g_scrollY += clientH; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            g_scrollY = HIWORD(wParam);
            break;
        }

        g_scrollY = std::max(0, std::min(g_scrollY, std::max(0, totalH - clientH)));
        SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_sideWheelAccumY += (double)delta;
        double pixelDelta = g_sideWheelAccumY * (80.0 / 120.0);
        if (std::abs(pixelDelta) >= 1.0) {
            int moveY = (int)pixelDelta;
            g_sideWheelAccumY -= ((double)moveY * (120.0 / 80.0));

            RECT rc;
            GetClientRect(hWnd, &rc);
            int clientH = rc.bottom - rc.top;
            int totalH = app.GetPageCount() * ITEM_HEIGHT + 20;
            int maxScrollY = std::max(0, totalH - clientH);

            int targetY = g_scrollY - moveY;
            if (targetY <= 0 || targetY >= maxScrollY) {
                g_sideWheelAccumY = 0.0;
            }
            g_scrollY = std::max(0, std::min(targetY, maxScrollY));
            SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_ID_AUTOSCROLL) {
            if (!g_isDragging || !g_isDragInside || g_autoScrollDelta == 0) {
                StopAutoScroll(hWnd);
                return 0;
            }

            RECT rc;
            GetClientRect(hWnd, &rc);
            int clientH = rc.bottom - rc.top;
            int totalH = app.GetPageCount() * ITEM_HEIGHT + 20;
            int maxScrollY = std::max(0, totalH - clientH);

            int prevScrollY = g_scrollY;
            g_scrollY = std::max(0, std::min(maxScrollY, g_scrollY + g_autoScrollDelta));

            if (g_scrollY != prevScrollY) {
                SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);

                // Update g_dragDest with new scroll offset in real-time
                POINT curPt;
                GetCursorPos(&curPt);
                ScreenToClient(hWnd, &curPt);

                int hoverY = curPt.y + g_scrollY;
                if (hoverY < 0) hoverY = 0;
                int hoverIdx = hoverY / ITEM_HEIGHT;
                int relY = hoverY % ITEM_HEIGHT;
                int totalPages = app.GetPageCount();
                g_dragDest = (relY > ITEM_HEIGHT / 2) ? hoverIdx + 1 : hoverIdx;
                g_dragDest = std::max(0, std::min(totalPages, g_dragDest));

                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_SETCURSOR: {
        if (g_isDragging) {
            if (g_isDragInside) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            } else {
                SetCursor(LoadCursor(NULL, IDC_NO));
            }
            return TRUE;
        }
        break;
    }

    case WM_CAPTURECHANGED: {
        if (g_isDragging && (HWND)lParam != hWnd) {
            StopAutoScroll(hWnd);
            g_isDragging = false;
            g_isDragInside = false;
            g_dragSrc = -1;
            g_dragDest = -1;
            g_pendingSingleSelect = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_DESTROY: {
        StopAutoScroll(hWnd);
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE && g_isDragging) {
            ReleaseCapture();
            StopAutoScroll(hWnd);
            g_isDragging = false;
            g_isDragInside = false;
            g_dragSrc = -1;
            g_dragDest = -1;
            g_pendingSingleSelect = false;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (wParam == VK_DELETE) {
            app.DeleteSelectedPages();
            return 0;
        } else if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            app.SelectAllPages();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        } else if (wParam == VK_UP && (GetKeyState(VK_MENU) & 0x8000)) { // Alt + Up
            app.MoveSelectedPagesUp();
            return 0;
        } else if (wParam == VK_DOWN && (GetKeyState(VK_MENU) & 0x8000)) { // Alt + Down
            app.MoveSelectedPagesDown();
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int clickedY = y + g_scrollY;
        int clickedIdx = clickedY / ITEM_HEIGHT;
        int totalPages = app.GetPageCount();

        if (clickedIdx >= 0 && clickedIdx < totalPages) {
            g_dragSrc = clickedIdx;
            g_dragStartPt = { x, y };

            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            if (isShift) {
                app.SelectPageRange(clickedIdx);
                g_pendingSingleSelect = false;
                app.GoToPage(clickedIdx + 1);
            } else if (isCtrl) {
                app.SelectPageToggle(clickedIdx);
                g_pendingSingleSelect = false;
                app.GoToPage(clickedIdx + 1);
            } else {
                // If clicked page is already part of the current selection,
                // keep the selection intact in case the user wants to drag the group!
                if (app.IsPageSelected(clickedIdx)) {
                    g_pendingSingleSelect = true;
                } else {
                    app.SelectPageSingle(clickedIdx);
                    g_pendingSingleSelect = false;
                    app.GoToPage(clickedIdx + 1);
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int totalPages = app.GetPageCount();

        if (wParam & MK_LBUTTON) {
            if (!g_isDragging && g_dragSrc >= 0) {
                int dx = x - g_dragStartPt.x;
                int dy = y - g_dragStartPt.y;
                if (dx * dx + dy * dy > 25) {
                    g_isDragging = true;
                    g_isDragInside = true;
                    g_pendingSingleSelect = false;
                    SetCapture(hWnd);
                }
            }

            if (g_isDragging) {
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                POINT pt = { x, y };

                if (PtInRect(&rcClient, pt)) {
                    g_isDragInside = true;
                    SetCursor(LoadCursor(NULL, IDC_ARROW));

                    int hoverY = y + g_scrollY;
                    if (hoverY < 0) hoverY = 0;
                    int hoverIdx = hoverY / ITEM_HEIGHT;
                    int relY = hoverY % ITEM_HEIGHT;
                    g_dragDest = (relY > ITEM_HEIGHT / 2) ? hoverIdx + 1 : hoverIdx;
                    g_dragDest = std::max(0, std::min(totalPages, g_dragDest));

                    // Edge Auto-scroll trigger (35px margin)
                    const int SCROLL_MARGIN = 35;
                    const int SCROLL_STEP = 10; // 8 - 12 px per 30ms tick

                    if (y < SCROLL_MARGIN) {
                        StartAutoScroll(hWnd, -SCROLL_STEP);
                    } else if (y > rcClient.bottom - SCROLL_MARGIN) {
                        StartAutoScroll(hWnd, SCROLL_STEP);
                    } else {
                        StopAutoScroll(hWnd);
                    }
                } else {
                    // Cursor is outside Sidebar (e.g. over Ribbon, Toolbar, outside window)
                    g_isDragInside = false;
                    SetCursor(LoadCursor(NULL, IDC_NO));
                    StopAutoScroll(hWnd);
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_isDragging) {
            ReleaseCapture();
            StopAutoScroll(hWnd);

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

            // Drop Boundary Guard: Only apply drop if released INSIDE Sidebar
            if (PtInRect(&rcClient, pt) && g_isDragInside) {
                if (g_dragDest >= 0) {
                    app.MoveSelectedPages(g_dragDest);
                }
            } else {
                // Cancel drag: do not move pages, do not change selection or page order
            }

            g_isDragging = false;
            g_isDragInside = false;
            g_dragSrc = -1;
            g_dragDest = -1;
            g_pendingSingleSelect = false;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (g_pendingSingleSelect && g_dragSrc >= 0) {
            // User clicked an already selected page but didn't drag it: clear other selections
            app.SelectPageSingle(g_dragSrc);
            app.GoToPage(g_dragSrc + 1);
            g_pendingSingleSelect = false;
            g_dragSrc = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int clickedY = y + g_scrollY;
        int clickedIdx = clickedY / ITEM_HEIGHT;
        int totalPages = app.GetPageCount();

        if (clickedIdx >= 0 && clickedIdx < totalPages) {
            if (!app.IsPageSelected(clickedIdx)) {
                app.SelectPageSingle(clickedIdx);
                app.GoToPage(clickedIdx + 1);
                InvalidateRect(hWnd, NULL, FALSE);
            }

            HMENU hMenu = CreatePopupMenu();
            int selCount = app.GetSelectedCount();
            std::wstring moveUpText = (selCount > 1) ? 
                (L"⬆️ Move " + std::to_wstring(selCount) + L" pages up") :
                L"⬆️ Move up";
            std::wstring moveDownText = (selCount > 1) ? 
                (L"⬇️ Move " + std::to_wstring(selCount) + L" pages down") :
                L"⬇️ Move down";

            AppendMenuW(hMenu, MF_STRING, 1006, moveUpText.c_str());
            AppendMenuW(hMenu, MF_STRING, 1007, moveDownText.c_str());
            AppendMenuW(hMenu, MF_STRING, 1008, L"📍 Move to position");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1009, L"➕ Insert PDF Files After This Page");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1010, L"📑 Export as New PDF");
            AppendMenuW(hMenu, MF_STRING, 1001, L"🖼️ Export Selected Pages as JPG");
            AppendMenuW(hMenu, MF_STRING, 1002, L"📄 Add Blank Page After This Page");
            AppendMenuW(hMenu, MF_STRING, 1003, L"🗑️ Delete Selected Pages");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, app.CanUndo() ? MF_STRING : (MF_STRING | MF_GRAYED), 1004, L"↶ Undo (Ctrl+Z)");
            AppendMenuW(hMenu, app.CanRedo() ? MF_STRING : (MF_STRING | MF_GRAYED), 1005, L"↷ Redo (Ctrl+Y)");

            POINT pt = { x, y };
            ClientToScreen(hWnd, &pt);

            PDFApp::ApplyUxThemeDarkMode(app.GetMainWindow(), app.IsDarkMode());
            PDFApp::ApplyUxThemeDarkMode(hWnd, app.IsDarkMode());

            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == 1006) {
                app.MoveSelectedPagesUp();
            } else if (cmd == 1007) {
                app.MoveSelectedPagesDown();
            } else if (cmd == 1008) {
                if (!app.GetSelectedPages().empty()) {
                    int firstSel = *app.GetSelectedPages().begin();
                    int targetPage = firstSel + 1;
                    if (ShowMoveToPositionDialog(app.GetMainWindow(), totalPages, targetPage, targetPage)) {
                        int K = (int)app.GetSelectedPages().size();
                        int F = *app.GetSelectedPages().begin();
                        int destIndex = 0;
                        if (targetPage <= F + 1) {
                            destIndex = targetPage - 1;
                        } else {
                            int unselectedBefore = std::min(targetPage - 1, totalPages - K);
                            destIndex = unselectedBefore + K;
                        }
                        app.MoveSelectedPages(destIndex);
                    }
                }
            } else if (cmd == 1009) {
                app.AppendPdfFiles();
            } else if (cmd == 1004) {
                app.Undo();
            } else if (cmd == 1005) {
                app.Redo();
            } else if (cmd == 1010) {
                app.ExportSelectedPagesToPdf();
            } else if (cmd == 1001) {
                app.ExportSelectedPagesToJpg();
            } else if (cmd == 1002) {
                app.AddBlankPage(clickedIdx + 1);
            } else if (cmd == 1003) {
                app.DeleteSelectedPages();
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRc;
        GetClientRect(hWnd, &clientRc);
        int clientW = clientRc.right - clientRc.left;
        int clientH = clientRc.bottom - clientRc.top;

        // Double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientW, clientH);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        bool isDark = app.IsDarkMode();

        // Sidebar background
        COLORREF sbBg = isDark ? RGB(24, 24, 27) : RGB(248, 249, 250);
        COLORREF sbBorder = isDark ? RGB(39, 39, 42) : RGB(225, 228, 232);

        HBRUSH hBg = CreateSolidBrush(sbBg);
        FillRect(memDC, &clientRc, hBg);
        DeleteObject(hBg);

        // Right separator border
        HPEN hSepPen = CreatePen(PS_SOLID, 1, sbBorder);
        HPEN hOldPen = (HPEN)SelectObject(memDC, hSepPen);
        MoveToEx(memDC, clientW - 1, 0, NULL);
        LineTo(memDC, clientW - 1, clientH);
        SelectObject(memDC, hOldPen);
        DeleteObject(hSepPen);

        int totalPages = app.GetPageCount();
        if (totalPages == 0) {
            // Draw empty hint
            SelectObject(memDC, g_hSidebarFont);
            SetTextColor(memDC, isDark ? RGB(161, 161, 170) : RGB(140, 145, 155));
            SetBkMode(memDC, TRANSPARENT);
            RECT textRc = { 10, 50, clientW - 10, 150 };
            DrawTextW(memDC, L"No PDF document available.\nPlease open a file to view the page list.", -1, &textRc, DT_CENTER | DT_WORDBREAK);
        } else {
            int thumbX = (clientW - ITEM_WIDTH) / 2;

            int firstVisible = std::max(0, (g_scrollY - 10) / ITEM_HEIGHT);
            int lastVisible = std::min(totalPages - 1, (g_scrollY + clientH + 10) / ITEM_HEIGHT);

            for (int i = firstVisible; i <= lastVisible; ++i) {
                int itemTop = i * ITEM_HEIGHT + 10 - g_scrollY;
                int itemBottom = itemTop + ITEM_HEIGHT - 10;

                bool isSelected = app.IsPageSelected(i);

                // Card background & selection highlight
                RECT cardRc = { thumbX - 8, itemTop - 4, thumbX + ITEM_WIDTH + 8, itemTop + ITEM_THUMB_HEIGHT + 28 };
                if (isSelected) {
                    COLORREF selBg = isDark ? RGB(12, 45, 72) : RGB(232, 240, 254);         // #0C2D48 ở Dark Mode
                    COLORREF selBorder = isDark ? RGB(56, 189, 248) : RGB(26, 115, 232);    // #38BDF8 ở Dark Mode
                    HBRUSH hSelBg = CreateSolidBrush(selBg);
                    FillRect(memDC, &cardRc, hSelBg);
                    DeleteObject(hSelBg);

                    HPEN hSelPen = CreatePen(PS_SOLID, 2, selBorder);
                    HPEN hOldP = (HPEN)SelectObject(memDC, hSelPen);
                    HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
                    HBRUSH hOldBr = (HBRUSH)SelectObject(memDC, hNullBr);
                    RoundRect(memDC, cardRc.left, cardRc.top, cardRc.right, cardRc.bottom, 8, 8);
                    SelectObject(memDC, hOldBr);
                    SelectObject(memDC, hOldP);
                    DeleteObject(hSelPen);
                }

                // Thumbnail page background (white paper with subtle drop shadow)
                RECT shadowRc = { thumbX + 2, itemTop + 2, thumbX + ITEM_WIDTH + 2, itemTop + ITEM_THUMB_HEIGHT + 2 };
                COLORREF shadowCol = isDark ? RGB(8, 8, 10) : RGB(220, 224, 230);
                HBRUSH hShadow = CreateSolidBrush(shadowCol);
                FillRect(memDC, &shadowRc, hShadow);
                DeleteObject(hShadow);

                RECT paperRc = { thumbX, itemTop, thumbX + ITEM_WIDTH, itemTop + ITEM_THUMB_HEIGHT };
                HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(memDC, &paperRc, hWhite);
                DeleteObject(hWhite);

                // Draw thumbnail content using dedicated thumbnail cache (never touches full-size page cache)
                HBITMAP hThumbBmp = NULL;
                auto it = g_thumbCache.find(i);
                if (it != g_thumbCache.end() && it->second) {
                    hThumbBmp = it->second;
                } else {
                    HDC thumbMemDC = CreateCompatibleDC(hdc);
                    hThumbBmp = CreateCompatibleBitmap(hdc, ITEM_WIDTH, ITEM_THUMB_HEIGHT);
                    HBITMAP oldThumbBmp = (HBITMAP)SelectObject(thumbMemDC, hThumbBmp);

                    // Initialize thumbnail with clean white background so it can never be a black box
                    RECT cleanThumbRc = { 0, 0, ITEM_WIDTH, ITEM_THUMB_HEIGHT };
                    FillRect(thumbMemDC, &cleanThumbRc, (HBRUSH)GetStockObject(WHITE_BRUSH));

                    app.RenderThumbnailToDC(thumbMemDC, i, 0, 0, ITEM_WIDTH, ITEM_THUMB_HEIGHT);

                    SelectObject(thumbMemDC, oldThumbBmp);
                    DeleteDC(thumbMemDC);
                    g_thumbCache[i] = hThumbBmp;
                }

                if (hThumbBmp) {
                    HDC bmpDC = CreateCompatibleDC(hdc);
                    HBITMAP oldB = (HBITMAP)SelectObject(bmpDC, hThumbBmp);
                    BitBlt(memDC, thumbX, itemTop, ITEM_WIDTH, ITEM_THUMB_HEIGHT, bmpDC, 0, 0, SRCCOPY);
                    SelectObject(bmpDC, oldB);
                    DeleteDC(bmpDC);
                }

                // Page outline border
                COLORREF borderCol = isSelected ? (isDark ? RGB(56, 189, 248) : RGB(26, 115, 232))
                                                : (isDark ? RGB(63, 63, 70) : RGB(200, 205, 212));
                HPEN hPaperPen = CreatePen(PS_SOLID, 1, borderCol);
                HPEN hOldP = (HPEN)SelectObject(memDC, hPaperPen);
                HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
                HBRUSH hOldBr = (HBRUSH)SelectObject(memDC, hNullBr);
                Rectangle(memDC, paperRc.left, paperRc.top, paperRc.right, paperRc.bottom);
                SelectObject(memDC, hOldBr);
                SelectObject(memDC, hOldP);
                DeleteObject(hPaperPen);

                // Page number text below thumbnail
                SelectObject(memDC, isSelected ? g_hSidebarBoldFont : g_hSidebarFont);
                COLORREF textCol;
                if (isSelected) {
                    textCol = isDark ? RGB(255, 255, 255) : RGB(26, 115, 232); // Trắng sáng tương phản cao trên nền #0C2D48
                } else {
                    textCol = isDark ? RGB(212, 212, 216) : RGB(60, 64, 67);   // Xám sáng #D4D4D8 cho trang chưa chọn
                }
                SetTextColor(memDC, textCol);
                SetBkMode(memDC, TRANSPARENT);
                RECT labelRc = { thumbX - 8, itemTop + ITEM_THUMB_HEIGHT + 5, thumbX + ITEM_WIDTH + 8, itemTop + ITEM_THUMB_HEIGHT + 25 };
                std::wstring label = L"Page " + std::to_wstring(i + 1);
                DrawTextW(memDC, label.c_str(), -1, &labelRc, DT_CENTER | DT_SINGLELINE);
            }

            // Draw drag destination insertion indicator line and floating count badge
            if (g_isDragging && g_isDragInside && g_dragDest >= 0) {
                int lineY = g_dragDest * ITEM_HEIGHT + 10 - g_scrollY - 3;
                HPEN hDragPen = CreatePen(PS_SOLID, 3, RGB(26, 115, 232)); // Google / Windows primary blue
                HPEN hOldP = (HPEN)SelectObject(memDC, hDragPen);
                MoveToEx(memDC, 10, lineY, NULL);
                LineTo(memDC, clientW - 10, lineY);
                SelectObject(memDC, hOldP);
                DeleteObject(hDragPen);

                // Draw drag badge
                int selCount = app.GetSelectedCount();
                std::wstring badgeText = (selCount > 1) ? 
                    (L"📦 Move " + std::to_wstring(selCount) + L" pages") : 
                    L"📄 Move 1 page";
                
                SelectObject(memDC, g_hSidebarBoldFont);
                SIZE sz;
                GetTextExtentPoint32W(memDC, badgeText.c_str(), (int)badgeText.size(), &sz);

                int badgeW = sz.cx + 20;
                int badgeH = sz.cy + 10;
                int badgeX = (clientW - badgeW) / 2;
                int badgeY = lineY - badgeH - 4;
                if (badgeY < 5) badgeY = lineY + 6;

                RECT bRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
                HBRUSH hBadgeBr = CreateSolidBrush(RGB(32, 33, 36)); // Dark sleek badge
                HPEN hBadgePen = CreatePen(PS_SOLID, 1, RGB(95, 99, 104));
                HPEN hOldBadgePen = (HPEN)SelectObject(memDC, hBadgePen);
                HBRUSH hOldBadgeBr = (HBRUSH)SelectObject(memDC, hBadgeBr);
                RoundRect(memDC, bRc.left, bRc.top, bRc.right, bRc.bottom, 6, 6);
                SelectObject(memDC, hOldBadgeBr);
                SelectObject(memDC, hOldBadgePen);
                DeleteObject(hBadgePen);
                DeleteObject(hBadgeBr);

                SetTextColor(memDC, RGB(255, 255, 255));
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, badgeText.c_str(), -1, &bRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        BitBlt(hdc, 0, 0, clientW, clientH, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool NavigationSidebar::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = SidebarWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FeathrPDFNavigationSidebarClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    return RegisterClassExW(&wc) != 0;
}

HWND NavigationSidebar::Create(HWND hWndParent, HINSTANCE hInstance) {
    return CreateWindowExW(
        0, L"FeathrPDFNavigationSidebarClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
        0, 88, 220, 600,
        hWndParent, NULL, hInstance, NULL
    );
}
