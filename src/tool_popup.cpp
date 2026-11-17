#include "tool_popup.h"
#include "color_picker.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>
#include <uxtheme.h>

static HWND g_hPopup = NULL;
static ToolMode g_currentTool = TOOL_PEN;
static ToolConfig g_currentConfig;
static HWND g_hTrackSize = NULL;
static HWND g_hTrackOpacity = NULL;
static HWND g_hLblSize = NULL;
static HWND g_hLblOpacity = NULL;
static HWND g_hEditHex = NULL;
static HWND g_hBtnCustomColor = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static bool g_isUpdatingHex = false;

// Fixed 15-Color Palette for Highlight (3 rows x 5 columns)
const COLORREF g_highlightSwatches[15] = {
    // Hàng 1:
    RGB(255, 255, 0),   // Vàng (#FFFF00)
    RGB(0, 255, 0),     // Xanh neon (#00FF00)
    RGB(0, 255, 255),   // Xanh lơ (#00FFFF)
    RGB(255, 0, 255),   // Hồng cánh sen (#FF00FF)
    RGB(0, 0, 255),     // Xanh dương (#0000FF)
    // Hàng 2:
    RGB(255, 0, 0),     // Đỏ (#FF0000)
    RGB(0, 0, 128),     // Xanh navy (#000080)
    RGB(0, 128, 128),   // Xanh mòng két (#008080)
    RGB(0, 128, 0),     // Xanh lá (#008000)
    RGB(128, 0, 128),   // Tím (#800080)
    // Hàng 3:
    RGB(128, 0, 0),     // Đỏ thẫm/Nâu đỏ (#800000)
    RGB(128, 128, 0),   // Vàng ô liu (#808000)
    RGB(128, 128, 128), // Xám tối (#808080)
    RGB(192, 192, 192), // Xám sáng (#C0C0C0)
    RGB(0, 0, 0)        // Đen (#000000)
};

// Fixed 16-Color Palette for Draw (8 Light/Dark Pairs)
const COLORREF g_drawSwatches[16] = {
    RGB(255, 255, 0),   // 1. yellow (#FFFF00)
    RGB(139, 69, 19),   // 2. brown (#8B4513)
    RGB(255, 0, 0),     // 3. red (#FF0000)
    RGB(128, 0, 0),     // 4. darkred (#800000)
    RGB(255, 0, 255),   // 5. pink (#FF00FF)
    RGB(128, 0, 128),   // 6. darkpink (#800080)
    RGB(0, 0, 255),     // 7. blue (#0000FF)
    RGB(0, 0, 128),     // 8. darkblue (#000080)
    RGB(0, 255, 0),     // 9. green (#00FF00)
    RGB(0, 128, 0),     // 10. darkgreen (#008000)
    RGB(0, 255, 255),   // 11. cyan (#00FFFF)
    RGB(0, 128, 128),   // 12. darkcyan (#008080)
    RGB(255, 255, 255), // 13. white (#FFFFFF)
    RGB(0, 0, 0),       // 14. black (#000000)
    RGB(192, 192, 192), // 15. lightgray (#C0C0C0)
    RGB(128, 128, 128)  // 16. darkgray (#808080)
};

static std::wstring ColorToHexW(COLORREF c) {
    WCHAR buf[16];
    swprintf(buf, 16, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

static bool TryParseHex(const std::wstring& str, COLORREF& outColor) {
    std::wstring s = str;
    if (!s.empty() && s[0] == L'#') {
        s = s.substr(1);
    }
    if (s.length() != 6) return false;
    for (WCHAR ch : s) {
        if (!iswxdigit(ch)) return false;
    }
    unsigned int val = 0;
    std::wstringstream ss;
    ss << std::hex << s;
    if (ss >> val) {
        int r = (val >> 16) & 0xFF;
        int g = (val >> 8) & 0xFF;
        int b = val & 0xFF;
        outColor = RGB(r, g, b);
        return true;
    }
    return false;
}

static int GetSwatchCount() {
    return (g_currentTool == TOOL_HIGHLIGHT) ? 15 : 16;
}

static COLORREF GetSwatchColor(int index) {
    if (g_currentTool == TOOL_HIGHLIGHT) {
        if (index >= 0 && index < 15) return g_highlightSwatches[index];
    } else {
        if (index >= 0 && index < 16) return g_drawSwatches[index];
    }
    return RGB(0, 0, 0);
}

static RECT GetSwatchRect(int index) {
    RECT rc = { 0, 0, 0, 0 };
    if (g_currentTool == TOOL_HIGHLIGHT) {
        // 3 rows x 5 columns
        int row = index / 5;
        int col = index % 5;
        int startX = 16;
        int startY = 46;
        int swatchW = 38;
        int swatchH = 20;
        int gapX = 6;
        int gapY = 5;

        rc.left = startX + col * (swatchW + gapX);
        rc.top = startY + row * (swatchH + gapY);
        rc.right = rc.left + swatchW;
        rc.bottom = rc.top + swatchH;
    } else {
        // TOOL_PEN: 8 columns x 2 rows (Light/Dark pairs vertically stacked)
        int col = index / 2;
        int row = index % 2;
        int startX = 16;
        int startY = 46;
        int swatchW = 23;
        int swatchH = 20;
        int gapX = 4;
        int gapY = 4;

        rc.left = startX + col * (swatchW + gapX);
        rc.top = startY + row * (swatchH + gapY);
        rc.right = rc.left + swatchW;
        rc.bottom = rc.top + swatchH;
    }
    return rc;
}

static void UpdateLabels(HWND hWnd) {
    if (g_hTrackSize && g_hLblSize) {
        int sizeVal = (int)SendMessageW(g_hTrackSize, TBM_GETPOS, 0, 0);
        std::wstring sizeText = L"Stroke Size: " + std::to_wstring(sizeVal) + L" px";
        SetWindowTextW(g_hLblSize, sizeText.c_str());
    }

    if (g_hTrackOpacity && g_hLblOpacity) {
        int opacityVal = (int)SendMessageW(g_hTrackOpacity, TBM_GETPOS, 0, 0);
        std::wstring opText = L"Opacity: " + std::to_wstring(opacityVal) + L" %";
        SetWindowTextW(g_hLblOpacity, opText.c_str());
    }
}

static LRESULT CALLBACK ToolPopupWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        g_hFontNormal = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_hFontBold = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        int hexY = (g_currentTool == TOOL_PEN) ? 98 : 124;

        // Custom Color Button
        g_hBtnCustomColor = CreateWindowW(L"BUTTON", L"Custom Color...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            16, hexY, 120, 24, hWnd, (HMENU)104, hInst, NULL);
        SendMessageW(g_hBtnCustomColor, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // HEX input box
        g_hEditHex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", ColorToHexW(g_currentConfig.color).c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP,
            176, hexY, 58, 24, hWnd, (HMENU)103, hInst, NULL);
        SendMessageW(g_hEditHex, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessageW(g_hEditHex, EM_SETLIMITTEXT, 7, 0);

        bool isDark = PDFApp::Instance().IsDarkMode();
        SetWindowTheme(g_hEditHex, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);

        if (g_currentTool == TOOL_PEN) {
            // Draw tool: Size Label & Slider
            g_hLblSize = CreateWindowExW(0, L"STATIC", L"Stroke Size:", WS_CHILD | WS_VISIBLE,
                16, 128, 218, 16, hWnd, NULL, hInst, NULL);
            SendMessageW(g_hLblSize, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            g_hTrackSize = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                12, 146, 226, 24, hWnd, (HMENU)101, hInst, NULL);
            SendMessageW(g_hTrackSize, TBM_SETRANGE, TRUE, MAKELPARAM(1, 25));
            SendMessageW(g_hTrackSize, TBM_SETPOS, TRUE, (LPARAM)(int)g_currentConfig.strokeWidth);
            SetWindowTheme(g_hTrackSize, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);

            // Opacity Label & Slider
            g_hLblOpacity = CreateWindowExW(0, L"STATIC", L"Opacity:", WS_CHILD | WS_VISIBLE,
                16, 174, 218, 16, hWnd, NULL, hInst, NULL);
            SendMessageW(g_hLblOpacity, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            g_hTrackOpacity = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                12, 192, 226, 24, hWnd, (HMENU)102, hInst, NULL);
            SendMessageW(g_hTrackOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
            int opPercent = (int)(g_currentConfig.opacity * 100.0f + 0.5f);
            opPercent = std::clamp(opPercent, 10, 100);
            SendMessageW(g_hTrackOpacity, TBM_SETPOS, TRUE, (LPARAM)opPercent);
            SetWindowTheme(g_hTrackOpacity, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        } else {
            // Highlight tool: Omit stroke size! Only Opacity Slider
            g_hLblSize = NULL;
            g_hTrackSize = NULL;

            g_hLblOpacity = CreateWindowExW(0, L"STATIC", L"Opacity:", WS_CHILD | WS_VISIBLE,
                16, 154, 218, 16, hWnd, NULL, hInst, NULL);
            SendMessageW(g_hLblOpacity, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            g_hTrackOpacity = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                12, 172, 226, 24, hWnd, (HMENU)102, hInst, NULL);
            SendMessageW(g_hTrackOpacity, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
            int opPercent = (int)(g_currentConfig.opacity * 100.0f + 0.5f);
            opPercent = std::clamp(opPercent, 10, 100);
            SendMessageW(g_hTrackOpacity, TBM_SETPOS, TRUE, (LPARAM)opPercent);
            SetWindowTheme(g_hTrackOpacity, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        }

        UpdateLabels(hWnd);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 104) {
            // 🎨 Custom Color button clicked -> Open CustomColorPickerDialog
            COLORREF chosen = g_currentConfig.color;
            if (CustomColorPickerDialog::Show(PDFApp::Instance().GetMainWindow(), g_currentConfig.color, chosen)) {
                g_currentConfig.color = chosen;
                if (g_currentTool == TOOL_PEN) {
                    PDFApp::Instance().SetPenConfig(g_currentConfig);
                } else {
                    PDFApp::Instance().SetHighlightConfig(g_currentConfig);
                }
                g_isUpdatingHex = true;
                SetWindowTextW(g_hEditHex, ColorToHexW(g_currentConfig.color).c_str());
                g_isUpdatingHex = false;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        if (id == 103 && HIWORD(wParam) == EN_CHANGE) {
            if (g_isUpdatingHex) break;
            WCHAR buf[16] = { 0 };
            GetWindowTextW(g_hEditHex, buf, 16);
            COLORREF parsed;
            if (TryParseHex(buf, parsed)) {
                g_currentConfig.color = parsed;
                if (g_currentTool == TOOL_PEN) {
                    PDFApp::Instance().SetPenConfig(g_currentConfig);
                } else {
                    PDFApp::Instance().SetHighlightConfig(g_currentConfig);
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_HSCROLL: {
        if (g_hTrackSize) {
            int sizeVal = (int)SendMessageW(g_hTrackSize, TBM_GETPOS, 0, 0);
            g_currentConfig.strokeWidth = (float)sizeVal;
        }
        if (g_hTrackOpacity) {
            int opacityVal = (int)SendMessageW(g_hTrackOpacity, TBM_GETPOS, 0, 0);
            g_currentConfig.opacity = opacityVal / 100.0f;
        }

        if (g_currentTool == TOOL_PEN) {
            PDFApp::Instance().SetPenConfig(g_currentConfig);
        } else {
            PDFApp::Instance().SetHighlightConfig(g_currentConfig);
        }

        UpdateLabels(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // Close button at top right (224, 4, 246, 24)
        if (x >= 222 && x <= 246 && y >= 4 && y <= 24) {
            DestroyWindow(hWnd);
            return 0;
        }

        // Swatches hit test
        int swatchCount = GetSwatchCount();
        for (int i = 0; i < swatchCount; ++i) {
            RECT rc = GetSwatchRect(i);
            if (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom) {
                g_currentConfig.color = GetSwatchColor(i);
                if (g_currentTool == TOOL_PEN) {
                    PDFApp::Instance().SetPenConfig(g_currentConfig);
                } else {
                    PDFApp::Instance().SetHighlightConfig(g_currentConfig);
                }

                g_isUpdatingHex = true;
                SetWindowTextW(g_hEditHex, ColorToHexW(g_currentConfig.color).c_str());
                g_isUpdatingHex = false;

                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis || dis->CtlID != 104) break;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
        bool isDark = PDFApp::Instance().IsDarkMode();

        // Fill background with popup bg (#18181b / #ffffff) first to prevent corner artifacts
        COLORREF popBg = isDark ? RGB(24, 24, 27) : RGB(255, 255, 255);
        HBRUSH hPopBg = CreateSolidBrush(popBg);
        FillRect(hdc, &rc, hPopBg);
        DeleteObject(hPopBg);

        COLORREF btnBg = isPressed ? (isDark ? RGB(24, 24, 27) : RGB(229, 231, 235))
                                   : (isDark ? RGB(39, 39, 42) : RGB(243, 244, 246));
        COLORREF borderCol = isDark ? RGB(63, 63, 70) : RGB(209, 213, 219);
        COLORREF textCol = isDark ? RGB(228, 228, 231) : RGB(24, 24, 27);

        HBRUSH hBr = CreateSolidBrush(btnBg);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBr);
        DeleteObject(hPen);

        WCHAR text[64] = {0};
        GetWindowTextW(dis->hwndItem, text, 64);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontNormal);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();

        RECT clientRc;
        GetClientRect(hWnd, &clientRc);

        // Window background
        COLORREF bgCol = isDark ? RGB(24, 24, 27) : RGB(255, 255, 255);
        COLORREF borderCol = isDark ? RGB(39, 39, 42) : RGB(209, 213, 219);
        COLORREF textPrimary = isDark ? RGB(244, 244, 245) : RGB(30, 41, 59);
        COLORREF textSecondary = isDark ? RGB(161, 161, 170) : RGB(71, 85, 105);

        HBRUSH hBgBrush = CreateSolidBrush(bgCol);
        FillRect(hdc, &clientRc, hBgBrush);
        DeleteObject(hBgBrush);

        // Title bar header (y = 0..26)
        RECT rcHeader = { 0, 0, clientRc.right, 26 };
        COLORREF headerBg = isDark ? RGB(17, 17, 19) : RGB(241, 245, 249);
        HBRUSH hHdrBrush = CreateSolidBrush(headerBg);
        FillRect(hdc, &rcHeader, hHdrBrush);
        DeleteObject(hHdrBrush);

        HPEN hHdrPen = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldP = (HPEN)SelectObject(hdc, hHdrPen);
        MoveToEx(hdc, 0, 26, NULL);
        LineTo(hdc, clientRc.right, 26);
        SelectObject(hdc, hOldP);
        DeleteObject(hHdrPen);

        // Header text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textPrimary);
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontBold);
        LPCWSTR title = (g_currentTool == TOOL_PEN) ? L"✏️ Draw Settings" : L"🖍️ Highlight Settings";
        TextOutW(hdc, 14, 5, title, (int)wcslen(title));

        // Close button '✕'
        SetTextColor(hdc, isDark ? RGB(161, 161, 170) : RGB(148, 163, 184));
        TextOutW(hdc, 230, 5, L"✕", 1);

        // Color grid label
        SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, textSecondary);
        LPCWSTR palLabel = L"Palette";
        TextOutW(hdc, 16, 30, palLabel, (int)wcslen(palLabel));

        // Swatches
        int swatchCount = GetSwatchCount();
        for (int i = 0; i < swatchCount; ++i) {
            RECT rc = GetSwatchRect(i);
            COLORREF c = GetSwatchColor(i);

            HBRUSH hBrush = CreateSolidBrush(c);
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);

            HPEN hPen = CreatePen(PS_SOLID, 1, isDark ? RGB(63, 63, 70) : RGB(203, 213, 225));
            HPEN hOld = (HPEN)SelectObject(hdc, hPen);
            HBRUSH hOldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, hOldB);
            SelectObject(hdc, hOld);
            DeleteObject(hPen);

            if (c == g_currentConfig.color) {
                HPEN hRing = CreatePen(PS_SOLID, 2, RGB(37, 99, 235));
                HPEN hOld2 = (HPEN)SelectObject(hdc, hRing);
                HBRUSH hOldB2 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left - 1, rc.top - 1, rc.right + 1, rc.bottom + 1);
                SelectObject(hdc, hOldB2);
                SelectObject(hdc, hOld2);
                DeleteObject(hRing);
            }
        }

        // HEX label
        int hexY = (g_currentTool == TOOL_PEN) ? 102 : 128;
        SetTextColor(hdc, textSecondary);
        TextOutW(hdc, 145, hexY, L"HEX:", 4);

        // Preview Box Placement
        int prevTop = (g_currentTool == TOOL_PEN) ? 226 : 206;
        int prevHeight = 50;

        RECT rcPreview = { 16, prevTop, 234, prevTop + prevHeight };
        COLORREF preBg = isDark ? RGB(17, 17, 19) : RGB(248, 250, 252);
        HBRUSH hPreBg = CreateSolidBrush(preBg);
        FillRect(hdc, &rcPreview, hPreBg);
        DeleteObject(hPreBg);

        HPEN hPreBorder = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldPb = (HPEN)SelectObject(hdc, hPreBorder);
        HBRUSH hOldB3 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rcPreview.left, rcPreview.top, rcPreview.right, rcPreview.bottom);
        SelectObject(hdc, hOldB3);
        SelectObject(hdc, hOldPb);
        DeleteObject(hPreBorder);

        // GDI+ Drawing inside Preview Box
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            BYTE alpha = (BYTE)std::clamp((int)(g_currentConfig.opacity * 255.0f), 10, 255);
            COLORREF c = g_currentConfig.color;
            int r = GetRValue(c);
            int gVal = GetGValue(c);
            int b = GetBValue(c);

            if (g_currentTool == TOOL_HIGHLIGHT) {
                // Background sample text
                SetTextColor(hdc, isDark ? RGB(161, 161, 170) : RGB(148, 163, 184));
                LPCWSTR sampleTxt = L"Sample PDF Highlight Document Text";
                TextOutW(hdc, rcPreview.left + 14, rcPreview.top + 16, sampleTxt, (int)wcslen(sampleTxt));

                // Semi-transparent highlight bar over text with high quality blend
                float hlX = (float)(rcPreview.left + 10);
                float hlY = (float)(rcPreview.top + 10);
                float hlW = (float)(rcPreview.right - rcPreview.left - 20);
                float hlH = (float)(rcPreview.bottom - rcPreview.top - 20);

                Gdiplus::SolidBrush hlBrush(Gdiplus::Color(alpha, r, gVal, b));
                g.FillRectangle(&hlBrush, hlX, hlY, hlW, hlH);
            } else {
                // Smooth curved stroke
                float penW = g_currentConfig.strokeWidth;
                if (penW < 1.0f) penW = 1.0f;
                if (penW > 25.0f) penW = 25.0f;

                Gdiplus::Pen pen(Gdiplus::Color(alpha, r, gVal, b), penW);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                pen.SetLineJoin(Gdiplus::LineJoinRound);

                float startX = (float)(rcPreview.left + 20);
                float endX = (float)(rcPreview.right - 20);
                float midY = (float)((rcPreview.top + rcPreview.bottom) / 2);

                Gdiplus::GraphicsPath path;
                path.AddBezier(
                    startX, midY + 8.0f,
                    startX + 40.0f, midY - 14.0f,
                    endX - 40.0f, midY + 14.0f,
                    endX, midY - 8.0f
                );
                g.DrawPath(&pen, &path);
            }
        }

        // Outer window border
        HPEN hBorderPen = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldBrd = (HPEN)SelectObject(hdc, hBorderPen);
        HBRUSH hOldB4 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, clientRc.right, clientRc.bottom);
        SelectObject(hdc, hOldB4);
        SelectObject(hdc, hOldBrd);
        DeleteObject(hBorderPen);

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        COLORREF bgCol = isDark ? RGB(24, 24, 27) : RGB(255, 255, 255);
        COLORREF textCol = isDark ? RGB(161, 161, 170) : RGB(71, 85, 105);
        SetTextColor(hdcStatic, textCol);
        SetBkColor(hdcStatic, bgCol);
        static HBRUSH hBrDark = CreateSolidBrush(RGB(24, 24, 27));
        static HBRUSH hBrLight = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hBrDark : hBrLight);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        PDFApp& app = PDFApp::Instance();
        bool isDark = app.IsDarkMode();
        COLORREF bgCol = isDark ? RGB(39, 39, 42) : RGB(255, 255, 255);
        COLORREF textCol = isDark ? RGB(244, 244, 245) : RGB(30, 41, 59);
        SetTextColor(hdcEdit, textCol);
        SetBkColor(hdcEdit, bgCol);
        static HBRUSH hBrEditDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrEditLight = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hBrEditDark : hBrEditLight);
    }

    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY: {
        if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
        if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
        g_hPopup = NULL;
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ToolSettingsPopup::Show(HWND hWndParent, ToolMode tool, int anchorX, int anchorY) {
    if (g_hPopup && IsWindow(g_hPopup)) {
        DestroyWindow(g_hPopup);
        g_hPopup = NULL;
    }

    g_currentTool = tool;
    if (tool == TOOL_PEN) {
        g_currentConfig = PDFApp::Instance().GetPenConfig();
    } else {
        g_currentConfig = PDFApp::Instance().GetHighlightConfig();
    }

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWndParent, GWLP_HINSTANCE);

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = ToolPopupWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"FeathrPDF_ToolSettingsPopup";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    int width = 250;
    int height = (tool == TOOL_HIGHLIGHT) ? 275 : 295;

    HMONITOR hMon = MonitorFromPoint({ anchorX, anchorY }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfoW(hMon, &mi);

    int x = anchorX;
    int y = anchorY;
    if (x + width > mi.rcWork.right) x = mi.rcWork.right - width - 8;
    if (y + height > mi.rcWork.bottom) y = anchorY - height - 32;
    if (x < mi.rcWork.left) x = mi.rcWork.left + 8;

    g_hPopup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FeathrPDF_ToolSettingsPopup",
        L"",
        WS_POPUP,
        x, y, width, height,
        hWndParent,
        NULL,
        hInst,
        NULL
    );

    if (g_hPopup) {
        ShowWindow(g_hPopup, SW_SHOW);
        UpdateWindow(g_hPopup);
        SetFocus(g_hPopup);
    }
}

void ToolSettingsPopup::Close() {
    if (g_hPopup && IsWindow(g_hPopup)) {
        DestroyWindow(g_hPopup);
        g_hPopup = NULL;
    }
}

bool ToolSettingsPopup::IsOpen() {
    return g_hPopup && IsWindow(g_hPopup);
}
