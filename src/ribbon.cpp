#include "ribbon.h"
#include "app.h"
#include "history.h"
#include "tool_popup.h"
#include <string>
#include <algorithm>
#include <cmath>
#include <shlwapi.h>
#include <uxtheme.h>

static HFONT g_hRibbonFont = NULL;
static HFONT g_hRibbonSmallFont = NULL;
static HFONT g_hRibbonBoldFont = NULL;

// Row 1 buttons
static HWND g_hBtnFile = NULL;
static HWND g_hBtnUndo = NULL;
static HWND g_hBtnRedo = NULL;
static HWND g_hBtnNewPage = NULL;
static HWND g_hBtnDelPage = NULL;
static HWND g_hBtnToolHL = NULL;
static HWND g_hBtnToolHLDrop = NULL;
static HWND g_hBtnToolPen = NULL;
static HWND g_hBtnToolPenDrop = NULL;
static HWND g_hBtnToolEraser = NULL;
static HWND g_hBtnCollapse = NULL;

// Row 2 controls
static HWND g_hBtnToggleSide = NULL;
static HWND g_hLblPageInfo = NULL;
static HWND g_hEditGoTo = NULL;
static HWND g_hSliderZoom = NULL;
static HWND g_hLblZoomVal = NULL;
static HWND g_hBtnFitWidth = NULL;
static bool g_isSyncingPageInfo = false;

int RibbonBar::GetHeight(bool isCollapsed) {
    return isCollapsed ? 34 : 84;
}

static LRESULT CALLBACK RibbonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // Prevent background erasing flicker

    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        if (!g_hRibbonFont) {
            g_hRibbonFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hRibbonSmallFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hRibbonBoldFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }

        auto MakeBtn = [&](LPCWSTR text, int id, int x, int y, int w, int h) -> HWND {
            HWND hBtn = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                x, y, w, h, hWnd, (HMENU)(INT_PTR)id, hInst, NULL);
            SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hRibbonFont, TRUE);
            return hBtn;
        };

        // --- ROW 1: Minimalist Top Toolbar ---
        // Cluster 1: File Menu
        g_hBtnFile          = MakeBtn(L"File ▾", IDC_RIBBON_BTN_FILE, 10, 6, 68, 26);

        // Cluster 2: History (Undo / Redo) - placed naturally right after File without separator
        g_hBtnUndo          = MakeBtn(L"↶ Undo", IDC_RIBBON_BTN_UNDO, 82, 6, 65, 26);
        g_hBtnRedo          = MakeBtn(L"↷ Redo", IDC_RIBBON_BTN_REDO, 151, 6, 65, 26);

        // Cluster 3: Page Operations (Add Page / Delete Page)
        g_hBtnNewPage       = MakeBtn(L"📄 Add Page", IDC_RIBBON_BTN_NEWPAGE, 232, 6, 85, 26);
        g_hBtnDelPage       = MakeBtn(L"🗑️ Delete Page", IDC_RIBBON_BTN_DELPAGE, 321, 6, 95, 26);

        // Cluster 4: Annotations (Highlight / Draw / Eraser)
        g_hBtnToolHL        = MakeBtn(L"🖍️ Highlight", IDC_RIBBON_BTN_TOOLHL, 432, 6, 85, 26);
        g_hBtnToolHLDrop    = MakeBtn(L"▾", IDC_RIBBON_BTN_TOOLHL_DROP, 518, 6, 18, 26);
        g_hBtnToolPen       = MakeBtn(L"✏️ Draw", IDC_RIBBON_BTN_TOOLPEN, 540, 6, 65, 26);
        g_hBtnToolPenDrop   = MakeBtn(L"▾", IDC_RIBBON_BTN_TOOLPEN_DROP, 606, 6, 18, 26);
        g_hBtnToolEraser    = MakeBtn(L"🧹 Eraser (E)", IDC_RIBBON_BTN_TOOLERASER, 628, 6, 85, 26);

        // Collapse / Expand toggle button at top right
        g_hBtnCollapse      = MakeBtn(L"▲ Hide Ribbon", IDC_RIBBON_BTN_COLLAPSE, 940, 6, 95, 26);

        // --- ROW 2: View Navigation & Zoom Toolbar ---
        g_hBtnToggleSide = MakeBtn(L"📑 Page List", IDC_RIBBON_BTN_TOGGLESIDE, 10, 42, 95, 26);

        g_hLblPageInfo = CreateWindowW(L"STATIC", L"Page: 0 / 0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            115, 44, 130, 22, hWnd, NULL, hInst, NULL);
        SendMessageW(g_hLblPageInfo, WM_SETFONT, (WPARAM)g_hRibbonBoldFont, TRUE);

        HWND hLblGo = CreateWindowW(L"STATIC", L"Go to:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            250, 44, 45, 22, hWnd, NULL, hInst, NULL);
        SendMessageW(hLblGo, WM_SETFONT, (WPARAM)g_hRibbonFont, TRUE);

        g_hEditGoTo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            298, 43, 40, 24, hWnd, (HMENU)IDC_RIBBON_EDIT_GOTOPAGE, hInst, NULL);
        SendMessageW(g_hEditGoTo, WM_SETFONT, (WPARAM)g_hRibbonFont, TRUE);

        HWND hLblZoom = CreateWindowW(L"STATIC", L"Zoom:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            348, 44, 42, 22, hWnd, NULL, hInst, NULL);
        SendMessageW(hLblZoom, WM_SETFONT, (WPARAM)g_hRibbonFont, TRUE);

        g_hSliderZoom = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
            392, 43, 140, 24, hWnd, (HMENU)IDC_RIBBON_SLIDER_ZOOM, hInst, NULL);
        SendMessageW(g_hSliderZoom, TBM_SETRANGE, TRUE, MAKELPARAM(25, 600));
        SendMessageW(g_hSliderZoom, TBM_SETPOS, TRUE, 100);

        g_hLblZoomVal = CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            536, 44, 45, 22, hWnd, NULL, hInst, NULL);
        SendMessageW(g_hLblZoomVal, WM_SETFONT, (WPARAM)g_hRibbonFont, TRUE);

        g_hBtnFitWidth = MakeBtn(L"Fit Width", IDC_RIBBON_BTN_FITWIDTH, 585, 42, 75, 26);

        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        if (g_hBtnCollapse) {
            SetWindowPos(g_hBtnCollapse, NULL, w - 105, 6, 95, 26, SWP_NOZORDER);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;

        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
        bool isDisabled = (dis->itemState & ODS_DISABLED) != 0;
        int btnId = (int)dis->CtlID;

        bool isActiveTool = false;
        ToolMode curTool = app.GetTool();
        if (btnId == IDC_RIBBON_BTN_TOOLHL && curTool == TOOL_HIGHLIGHT) isActiveTool = true;
        if (btnId == IDC_RIBBON_BTN_TOOLPEN && curTool == TOOL_PEN) isActiveTool = true;
        if (btnId == IDC_RIBBON_BTN_TOOLERASER && curTool == TOOL_ERASER) isActiveTool = true;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;

        // BẮT BUỘC: Lấp đầy toàn bộ diện tích rc bằng cọ vẽ (HBRUSH) có màu trùng khớp 100%
        // với màu nền Ribbon (#18181b ở Dark Mode, hoặc #f4f4f5 ở Light Mode).
        // Sau đó mới vẽ RoundRect đè lên trên để góc ngoài bo cong hòa lẫn hoàn toàn vào nền,
        // triệt tiêu 100% hiện tượng viền đen hay răng cưa ở 4 góc!
        COLORREF ribBg = isDark ? RGB(24, 24, 27) : RGB(244, 244, 245);
        HBRUSH hRibBgBrush = CreateSolidBrush(ribBg);
        FillRect(hdc, &rc, hRibBgBrush);
        DeleteObject(hRibBgBrush);

        COLORREF bgCol, borderCol, textCol;
        if (btnId == IDC_RIBBON_BTN_FILE) {
            bgCol = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235); // #1d4ed8 vs #2563eb
            borderCol = isPressed ? RGB(29, 78, 216) : RGB(59, 130, 246);
            textCol = RGB(255, 255, 255);
        } else if (isDark) {
            if (isDisabled) {
                bgCol = RGB(31, 31, 35);
                borderCol = RGB(45, 45, 50);
                textCol = RGB(113, 113, 122);
            } else if (isActiveTool) {
                bgCol = RGB(30, 58, 138); // Dark blue accent
                borderCol = RGB(59, 130, 246);
                textCol = RGB(255, 255, 255);
            } else if (isPressed) {
                bgCol = RGB(24, 24, 27);
                borderCol = RGB(63, 63, 70);
                textCol = RGB(228, 228, 231);
            } else {
                bgCol = RGB(39, 39, 42); // #27272a
                borderCol = RGB(63, 63, 70); // #3f3f46
                textCol = RGB(228, 228, 231); // #e4e4e7
            }
        } else {
            if (isDisabled) {
                bgCol = RGB(243, 244, 246);
                borderCol = RGB(229, 231, 235);
                textCol = RGB(156, 163, 175);
            } else if (isActiveTool) {
                bgCol = RGB(219, 234, 254); // Light blue accent
                borderCol = RGB(59, 130, 246);
                textCol = RGB(29, 78, 216);
            } else if (isPressed) {
                bgCol = RGB(229, 231, 235);
                borderCol = RGB(209, 213, 219);
                textCol = RGB(24, 24, 27);
            } else {
                bgCol = RGB(255, 255, 255);
                borderCol = RGB(209, 213, 219);
                textCol = RGB(24, 24, 27);
            }
        }

        HBRUSH hBr = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBr);
        DeleteObject(hPen);

        WCHAR text[64] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 64);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);
        HFONT hFont = (btnId == IDC_RIBBON_BTN_FILE) ? g_hRibbonBoldFont : g_hRibbonFont;
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        return TRUE;
    }

    case WM_HSCROLL: {
        if ((HWND)lParam == g_hSliderZoom) {
            int pos = (int)SendMessageW(g_hSliderZoom, TBM_GETPOS, 0, 0);
            HWND hCanvas = PDFApp::Instance().GetCanvasWindow();
            if (hCanvas) {
                PDFApp::Instance().SetIsZooming(true);
                SetTimer(hCanvas, 993, 120, NULL);
            }
            PDFApp::Instance().SetZoom(pos / 100.0f, false, -1, -1);
            std::wstring str = std::to_wstring(pos) + L"%";
            SetWindowTextW(g_hLblZoomVal, str.c_str());
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        PDFApp& app = PDFApp::Instance();

        switch (id) {
        case IDC_RIBBON_BTN_FILE: {
            RECT rcBtn;
            GetWindowRect(g_hBtnFile, &rcBtn);

            HMENU hMenu = CreatePopupMenu();
            HMENU hSubRecent = CreatePopupMenu();

            bool hasDoc = app.HasDocument();
            bool hasSel = hasDoc && (app.GetSelectedCount() > 0);

            // Populate Recent Files
            const auto& recents = SessionHistory::Instance().GetRecentList();
            if (recents.empty()) {
                AppendMenuW(hSubRecent, MF_STRING | MF_GRAYED, 0, L"(No recent files)");
            } else {
                int count = std::min((int)recents.size(), 15);
                for (int i = 0; i < count; ++i) {
                    std::wstring label = recents[i].filePath;
                    size_t slash = label.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) {
                        label = label.substr(slash + 1);
                    }
                    if (recents[i].lastPage > 1) {
                        label += L" (Page " + std::to_wstring(recents[i].lastPage) + L")";
                    }
                    UINT flags = MF_STRING;
                    if (GetFileAttributesW(recents[i].filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        flags |= MF_GRAYED;
                        label += L" (Not found)";
                    }
                    AppendMenuW(hSubRecent, flags, 3200 + i, label.c_str());
                }
                AppendMenuW(hSubRecent, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hSubRecent, MF_STRING, 3999, L"Clear Recent History");
            }

            // Top-level File Menu items (Text-only, right-aligned shortcuts, no ellipses)
            AppendMenuW(hMenu, MF_STRING, 3101, L"Open\tCtrl+O");
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubRecent, L"Recent Files");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, hasDoc ? MF_STRING : (MF_STRING | MF_GRAYED), 3102, L"Save\tCtrl+S");
            AppendMenuW(hMenu, hasDoc ? MF_STRING : (MF_STRING | MF_GRAYED), 3103, L"Save As\tCtrl+Shift+S");
            AppendMenuW(hMenu, hasDoc ? MF_STRING : (MF_STRING | MF_GRAYED), 3104, L"Print\tCtrl+P");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, hasDoc ? MF_STRING : (MF_STRING | MF_GRAYED), 3105, L"Insert PDF");
            AppendMenuW(hMenu, (hasDoc && hasSel) ? MF_STRING : (MF_STRING | MF_GRAYED), 3106, L"Extract Selected Pages to PDF");
            AppendMenuW(hMenu, (hasDoc && hasSel) ? MF_STRING : (MF_STRING | MF_GRAYED), 3107, L"Extract Selected Pages to JPG");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

            LPCWSTR themeStr = app.IsDarkMode() ? L"Switch to Light Mode" : L"Switch to Dark Mode";
            AppendMenuW(hMenu, MF_STRING, 3108, themeStr);
            AppendMenuW(hMenu, MF_STRING, 3110, L"About");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 3109, L"Exit\tAlt+F4");

            // Ép Dark Mode cho Menu File (Win32 Context Menu Dark Theme)
            PDFApp::ApplyUxThemeDarkMode(app.GetMainWindow(), app.IsDarkMode());
            PDFApp::ApplyUxThemeDarkMode(hWnd, app.IsDarkMode());

            int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, rcBtn.left, rcBtn.bottom + 2, 0, hWnd, NULL);

            DestroyMenu(hSubRecent);
            DestroyMenu(hMenu);

            if (cmd == 3101) {
                OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
                WCHAR szFile[MAX_PATH] = { 0 };
                ofn.hwndOwner = app.GetMainWindow();
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    app.OpenDocument(szFile);
                }
            } else if (cmd == 3102) {
                app.SaveDocument();
            } else if (cmd == 3103) {
                app.SaveDocumentAs();
            } else if (cmd == 3104) {
                app.ShowPrintDialog();
            } else if (cmd == 3105) {
                app.AppendPdfFiles();
            } else if (cmd == 3106) {
                app.ExportSelectedPagesToPdf();
            } else if (cmd == 3107) {
                app.ExportSelectedPagesToJpg();
            } else if (cmd == 3108) {
                app.ToggleDarkMode();
            } else if (cmd == 3109) {
                PostMessage(app.GetMainWindow(), WM_CLOSE, 0, 0);
            } else if (cmd == 3110) {
                app.ShowAboutDialog();
            } else if (cmd == 3999) {
                SessionHistory::Instance().ClearAll();
            } else if (cmd >= 3200 && cmd < 3300) {
                int idx = cmd - 3200;
                if (idx >= 0 && idx < (int)recents.size()) {
                    app.OpenDocument(recents[idx].filePath);
                }
            }
            break;
        }

        case IDC_RIBBON_BTN_UNDO:
            app.Undo();
            break;
        case IDC_RIBBON_BTN_REDO:
            app.Redo();
            break;
        case IDC_RIBBON_BTN_NEWPAGE:
            app.AddBlankPage();
            break;
        case IDC_RIBBON_BTN_DELPAGE:
            app.DeleteSelectedPages();
            break;

        case IDC_RIBBON_BTN_TOOLHL: {
            if (app.GetTool() == TOOL_HIGHLIGHT) {
                RECT rc;
                GetWindowRect(g_hBtnToolHL, &rc);
                ToolSettingsPopup::Show(app.GetMainWindow(), TOOL_HIGHLIGHT, rc.left, rc.bottom + 2);
            } else {
                app.SetTool(TOOL_HIGHLIGHT);
            }
            break;
        }
        case IDC_RIBBON_BTN_TOOLHL_DROP: {
            app.SetTool(TOOL_HIGHLIGHT);
            RECT rc;
            GetWindowRect(g_hBtnToolHL, &rc);
            ToolSettingsPopup::Show(app.GetMainWindow(), TOOL_HIGHLIGHT, rc.left, rc.bottom + 2);
            break;
        }
        case IDC_RIBBON_BTN_TOOLPEN: {
            if (app.GetTool() == TOOL_PEN) {
                RECT rc;
                GetWindowRect(g_hBtnToolPen, &rc);
                ToolSettingsPopup::Show(app.GetMainWindow(), TOOL_PEN, rc.left, rc.bottom + 2);
            } else {
                app.SetTool(TOOL_PEN);
            }
            break;
        }
        case IDC_RIBBON_BTN_TOOLPEN_DROP: {
            app.SetTool(TOOL_PEN);
            RECT rc;
            GetWindowRect(g_hBtnToolPen, &rc);
            ToolSettingsPopup::Show(app.GetMainWindow(), TOOL_PEN, rc.left, rc.bottom + 2);
            break;
        }
        case IDC_RIBBON_BTN_TOOLERASER: {
            if (app.GetTool() == TOOL_ERASER) {
                app.SetTool(TOOL_PAN);
            } else {
                app.SetTool(TOOL_ERASER);
            }
            break;
        }

        case IDC_RIBBON_BTN_FITWIDTH:
            app.ZoomFitWidth();
            break;
        case IDC_RIBBON_BTN_TOGGLESIDE:
            app.ToggleSidebar();
            break;
        case IDC_RIBBON_BTN_COLLAPSE:
            app.ToggleRibbon();
            break;

        case IDC_RIBBON_EDIT_GOTOPAGE:
            if (HIWORD(wParam) == EN_CHANGE) {
                if (g_isSyncingPageInfo) break;
                WCHAR buf[32] = { 0 };
                GetWindowTextW(g_hEditGoTo, buf, 32);
                int p = _wtoi(buf);
                if (p > 0) {
                    app.GoToPage(p);
                }
            }
            break;
        }
        return 0;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wParam;
        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        COLORREF bgCol = isDark ? RGB(39, 39, 42) : RGB(244, 244, 245);
        COLORREF textCol = isDark ? RGB(228, 228, 231) : RGB(24, 24, 27);
        SetTextColor(hdcBtn, textCol);
        SetBkColor(hdcBtn, bgCol);
        static HBRUSH hBrBtnDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrBtnLight = CreateSolidBrush(RGB(244, 244, 245));
        return (LRESULT)(isDark ? hBrBtnDark : hBrBtnLight);
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        COLORREF bgCol = isDark ? RGB(24, 24, 27) : RGB(244, 244, 245);
        COLORREF textCol = isDark ? RGB(228, 228, 231) : RGB(24, 24, 27);
        SetTextColor(hdcStatic, textCol);
        SetBkColor(hdcStatic, bgCol);
        static HBRUSH hBrDark = CreateSolidBrush(RGB(24, 24, 27));
        static HBRUSH hBrLight = CreateSolidBrush(RGB(244, 244, 245));
        return (LRESULT)(isDark ? hBrDark : hBrLight);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        COLORREF bgCol = isDark ? RGB(39, 39, 42) : RGB(255, 255, 255);
        COLORREF textCol = isDark ? RGB(228, 228, 231) : RGB(24, 24, 27);
        SetTextColor(hdcEdit, textCol);
        SetBkColor(hdcEdit, bgCol);
        static HBRUSH hBrEditDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrEditLight = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hBrEditDark : hBrEditLight);
    }

    case WM_NOTIFY: {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (pnm && pnm->hwndFrom == g_hSliderZoom && pnm->code == NM_CUSTOMDRAW) {
            LPNMCUSTOMDRAW lpcd = (LPNMCUSTOMDRAW)lParam;
            PDFApp& app = PDFApp::Instance();
            bool isDark = app.IsDarkMode();
            if (lpcd->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (lpcd->dwDrawStage == CDDS_ITEMPREPAINT) {
                if (lpcd->dwItemSpec == TBCD_CHANNEL) {
                    if (isDark) {
                        RECT rcChan = lpcd->rc;
                        HBRUSH hBr = CreateSolidBrush(RGB(63, 63, 70));
                        FillRect(lpcd->hdc, &rcChan, hBr);
                        DeleteObject(hBr);
                        return CDRF_SKIPDEFAULT;
                    }
                } else if (lpcd->dwItemSpec == TBCD_THUMB) {
                    if (isDark) {
                        RECT rcThumb = lpcd->rc;
                        HBRUSH hBr = CreateSolidBrush(RGB(37, 99, 235));
                        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(59, 130, 246));
                        HBRUSH hOldB = (HBRUSH)SelectObject(lpcd->hdc, hBr);
                        HPEN hOldP = (HPEN)SelectObject(lpcd->hdc, hPen);
                        RoundRect(lpcd->hdc, rcThumb.left, rcThumb.top, rcThumb.right, rcThumb.bottom, 4, 4);
                        SelectObject(lpcd->hdc, hOldP);
                        SelectObject(lpcd->hdc, hOldB);
                        DeleteObject(hPen);
                        DeleteObject(hBr);
                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
            return CDRF_DODEFAULT;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();

        COLORREF ribBg = isDark ? RGB(24, 24, 27) : RGB(244, 244, 245);
        COLORREF ribBorder = isDark ? RGB(39, 39, 42) : RGB(228, 228, 231);
        COLORREF sepCol = isDark ? RGB(39, 39, 42) : RGB(209, 213, 219);

        // Ribbon background
        HBRUSH hBr = CreateSolidBrush(ribBg);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        // Bottom border line
        HPEN hPen = CreatePen(PS_SOLID, 1, ribBorder);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right, rc.bottom - 1);

        // Group separators between Row 1 clusters (Separator between File and Undo removed)
        HPEN hSepPen = CreatePen(PS_SOLID, 1, sepCol);
        HPEN hOldSepPen = (HPEN)SelectObject(hdc, hSepPen);
        const int sepPositions[] = { 224, 424 };
        for (int sepX : sepPositions) {
            MoveToEx(hdc, sepX, 6, NULL);
            LineTo(hdc, sepX, 32);
        }
        SelectObject(hdc, hOldSepPen);
        DeleteObject(hSepPen);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool RibbonBar::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = RibbonWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FeathrPDFRibbonBarClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    return RegisterClassExW(&wc) != 0;
}

HWND RibbonBar::Create(HWND hWndParent, HINSTANCE hInstance) {
    return CreateWindowExW(
        0, L"FeathrPDFRibbonBarClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 1000, 84,
        hWndParent, NULL, hInstance, NULL
    );
}

void RibbonBar::UpdateUI(HWND hRibbon) {
    if (!hRibbon) return;
    PDFApp& app = PDFApp::Instance();
    bool isDark = app.IsDarkMode();
    bool hasDoc = app.HasDocument();

    EnableWindow(g_hBtnFile, TRUE);
    EnableWindow(g_hBtnUndo, hasDoc && app.CanUndo());
    EnableWindow(g_hBtnRedo, hasDoc && app.CanRedo());
    EnableWindow(g_hBtnNewPage, hasDoc);
    EnableWindow(g_hBtnDelPage, hasDoc && app.GetSelectedCount() > 0);
    EnableWindow(g_hBtnToolHL, hasDoc);
    EnableWindow(g_hBtnToolHLDrop, hasDoc);
    EnableWindow(g_hBtnToolPen, hasDoc);
    EnableWindow(g_hBtnToolPenDrop, hasDoc);
    EnableWindow(g_hBtnToolEraser, hasDoc);
    EnableWindow(g_hBtnFitWidth, hasDoc);
    EnableWindow(g_hSliderZoom, hasDoc);
    EnableWindow(g_hEditGoTo, hasDoc);

    bool collapsed = app.IsRibbonCollapsed();
    SetWindowTextW(g_hBtnCollapse, collapsed ? L"▼ Show Ribbon" : L"▲ Hide Ribbon");

    int showCmd = collapsed ? SW_HIDE : SW_SHOW;
    ShowWindow(g_hBtnToggleSide, showCmd);
    ShowWindow(g_hLblPageInfo, showCmd);
    ShowWindow(g_hEditGoTo, showCmd);
    ShowWindow(g_hSliderZoom, showCmd);
    ShowWindow(g_hLblZoomVal, showCmd);
    ShowWindow(g_hBtnFitWidth, showCmd);

    // Apply native dark control themes
    if (g_hSliderZoom) SetWindowTheme(g_hSliderZoom, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    if (g_hEditGoTo) SetWindowTheme(g_hEditGoTo, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);

    // BẮT BUỘC: Dùng RDW_ALLCHILDREN để tất cả các nút bấm con trên Ribbon được vẽ lại tức thì theo theme
    RedrawWindow(hRibbon, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void RibbonBar::SetZoomSlider(HWND hRibbon, int zoomPercent) {
    if (g_hSliderZoom) {
        SendMessageW(g_hSliderZoom, TBM_SETPOS, TRUE, zoomPercent);
        std::wstring str = std::to_wstring(zoomPercent) + L"%";
        SetWindowTextW(g_hLblZoomVal, str.c_str());
    }
}

void RibbonBar::SetPageInfo(HWND hRibbon, int curPage, int totalPages, int selectedCount) {
    if (g_hLblPageInfo) {
        std::wstring info = L"Page: " + std::to_wstring(curPage) + L" / " + std::to_wstring(totalPages);
        if (selectedCount > 1) {
            info += L" (" + std::to_wstring(selectedCount) + L" selected)";
        }
        SetWindowTextW(g_hLblPageInfo, info.c_str());
    }
    if (g_hEditGoTo) {
        if (GetFocus() != g_hEditGoTo) {
            g_isSyncingPageInfo = true;
            std::wstring strPage = std::to_wstring(curPage);
            SetWindowTextW(g_hEditGoTo, strPage.c_str());
            g_isSyncingPageInfo = false;
        }
    }
}
