#include "color_picker.h"
#include "app.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>

struct HSV {
    float h; // 0..360
    float s; // 0..1
    float v; // 0..1
};

static HSV RgbToHsv(COLORREF c) {
    float r = GetRValue(c) / 255.0f;
    float g = GetGValue(c) / 255.0f;
    float b = GetBValue(c) / 255.0f;

    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    HSV hsv;
    hsv.v = maxVal;
    hsv.s = (maxVal > 0.0001f) ? (delta / maxVal) : 0.0f;

    if (delta < 0.0001f) {
        hsv.h = 0.0f;
    } else if (maxVal == r) {
        hsv.h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
    } else if (maxVal == g) {
        hsv.h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hsv.h = 60.0f * (((r - g) / delta) + 4.0f);
    }

    if (hsv.h < 0.0f) hsv.h += 360.0f;
    return hsv;
}

static COLORREF HsvToRgb(float h, float s, float v) {
    h = std::clamp(h, 0.0f, 360.0f);
    if (h >= 360.0f) h = 0.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rPrime = 0, gPrime = 0, bPrime = 0;
    int sector = (int)(h / 60.0f);
    switch (sector) {
    case 0: rPrime = c; gPrime = x; bPrime = 0; break;
    case 1: rPrime = x; gPrime = c; bPrime = 0; break;
    case 2: rPrime = 0; gPrime = c; bPrime = x; break;
    case 3: rPrime = 0; gPrime = x; bPrime = c; break;
    case 4: rPrime = x; gPrime = 0; bPrime = c; break;
    default: rPrime = c; gPrime = 0; bPrime = x; break;
    }

    int r = (int)((rPrime + m) * 255.0f + 0.5f);
    int g = (int)((gPrime + m) * 255.0f + 0.5f);
    int b = (int)((bPrime + m) * 255.0f + 0.5f);
    return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

static std::wstring ColorToHexStr(COLORREF c) {
    WCHAR buf[16];
    swprintf(buf, 16, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

static bool TryParseHexStr(const std::wstring& str, COLORREF& outColor) {
    std::wstring s = str;
    if (!s.empty() && s[0] == L'#') s = s.substr(1);
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

// 64 Standard curated colors for Standard tab
static const COLORREF g_standardColors[64] = {
    // Row 1: Grayscale
    RGB(0,0,0), RGB(32,32,32), RGB(64,64,64), RGB(96,96,96),
    RGB(128,128,128), RGB(160,160,160), RGB(208,208,208), RGB(255,255,255),
    // Row 2: Deep Dark Shades
    RGB(128,0,0), RGB(128,64,0), RGB(128,128,0), RGB(0,100,0),
    RGB(0,128,128), RGB(0,0,128), RGB(64,0,128), RGB(128,0,128),
    // Row 3: Standard Primaries
    RGB(232,17,35), RGB(216,59,1), RGB(255,185,0), RGB(16,124,65),
    RGB(0,130,114), RGB(0,120,215), RGB(92,45,145), RGB(180,0,158),
    // Row 4: Bright Tones
    RGB(255,67,67), RGB(255,130,0), RGB(255,255,0), RGB(107,204,0),
    RGB(0,183,195), RGB(41,151,255), RGB(136,23,152), RGB(227,0,140),
    // Row 5: Pastels
    RGB(255,179,186), RGB(255,204,153), RGB(255,255,179), RGB(186,255,201),
    RGB(186,225,255), RGB(179,205,255), RGB(222,186,255), RGB(255,186,243),
    // Row 6: Earth / Muted
    RGB(139,69,19), RGB(160,82,45), RGB(189,183,107), RGB(46,139,87),
    RGB(32,178,170), RGB(70,130,180), RGB(106,90,205), RGB(199,21,133),
    // Row 7: Highlighter / Fluorescent
    RGB(255,255,51), RGB(57,255,20), RGB(0,255,255), RGB(255,0,127),
    RGB(255,102,0), RGB(204,255,0), RGB(0,255,128), RGB(0,191,255),
    // Row 8: Modern Slate / UI
    RGB(15,23,42), RGB(30,41,59), RGB(51,65,85), RGB(71,85,105),
    RGB(100,116,139), RGB(148,163,184), RGB(203,213,225), RGB(241,245,249)
};

struct ColorPickerState {
    COLORREF initialColor;
    COLORREF currentColor;
    HSV currentHsv;
    int activeTab; // 0 = Standard, 1 = Custom
    bool isDraggingSpectrum;
    bool isDraggingLuminance;
    bool confirmed;
    bool isSyncingEdits;
    bool isDark;

    HWND hEditR;
    HWND hSpinR;
    HWND hEditG;
    HWND hSpinG;
    HWND hEditB;
    HWND hSpinB;
    HWND hEditHex;
    HWND hComboModel;
    HWND hBtnOk;
    HWND hBtnCancel;

    HFONT hFontNormal;
    HFONT hFontBold;
};

#define IDC_TAB_STANDARD        2001
#define IDC_TAB_CUSTOM          2002
#define IDC_CP_EDIT_R           2011
#define IDC_CP_SPIN_R           2012
#define IDC_CP_EDIT_G           2013
#define IDC_CP_SPIN_G           2014
#define IDC_CP_EDIT_B           2015
#define IDC_CP_SPIN_B           2016
#define IDC_CP_EDIT_HEX         2017
#define IDC_CP_COMBO_MODEL      2018
#define IDC_CP_BTN_OK           IDOK
#define IDC_CP_BTN_CANCEL       IDCANCEL

static const RECT RECT_SPECTRUM = { 20, 52, 240, 252 };
static const RECT RECT_LUMINANCE = { 254, 52, 278, 252 };
static const RECT RECT_COMPARE_NEW = { 300, 52, 420, 84 };
static const RECT RECT_COMPARE_CUR = { 300, 84, 420, 116 };

static void SyncControlsFromColor(HWND hWnd, ColorPickerState* s) {
    if (s->isSyncingEdits) return;
    s->isSyncingEdits = true;

    int r = GetRValue(s->currentColor);
    int g = GetGValue(s->currentColor);
    int b = GetBValue(s->currentColor);

    SetWindowTextW(s->hEditR, std::to_wstring(r).c_str());
    SetWindowTextW(s->hEditG, std::to_wstring(g).c_str());
    SetWindowTextW(s->hEditB, std::to_wstring(b).c_str());

    SendMessageW(s->hSpinR, UDM_SETPOS32, 0, r);
    SendMessageW(s->hSpinG, UDM_SETPOS32, 0, g);
    SendMessageW(s->hSpinB, UDM_SETPOS32, 0, b);

    SetWindowTextW(s->hEditHex, ColorToHexStr(s->currentColor).c_str());

    s->isSyncingEdits = false;
    InvalidateRect(hWnd, NULL, FALSE);
}

static LRESULT CALLBACK ColorPickerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ColorPickerState* s = (ColorPickerState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        s = (ColorPickerState*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)s);

        s->isDark = PDFApp::Instance().IsDarkMode();
        PDFApp::SetWindowTitlebarTheme(hWnd, s->isDark);
        PDFApp::ApplyUxThemeDarkMode(hWnd, s->isDark);

        HINSTANCE hInst = cs->hInstance;

        s->hFontNormal = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        s->hFontBold = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        LPCWSTR themeName = s->isDark ? L"DarkMode_Explorer" : L"Explorer";

        // Color model combobox (locked to RGB)
        s->hComboModel = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            348, 122, 72, 100, hWnd, (HMENU)IDC_CP_COMBO_MODEL, hInst, NULL);
        SendMessageW(s->hComboModel, CB_ADDSTRING, 0, (LPARAM)L"RGB");
        SendMessageW(s->hComboModel, CB_SETCURSEL, 0, 0);
        SendMessageW(s->hComboModel, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);
        SetWindowTheme(s->hComboModel, themeName, NULL);

        // Edit R and Spin R
        s->hEditR = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
            348, 150, 48, 24, hWnd, (HMENU)IDC_CP_EDIT_R, hInst, NULL);
        SendMessageW(s->hEditR, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);
        SetWindowTheme(s->hEditR, themeName, NULL);

        s->hSpinR = CreateWindowExW(0, UPDOWN_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
            0, 0, 18, 24, hWnd, (HMENU)IDC_CP_SPIN_R, hInst, NULL);
        SendMessageW(s->hSpinR, UDM_SETBUDDY, (WPARAM)s->hEditR, 0);
        SendMessageW(s->hSpinR, UDM_SETRANGE32, 0, 255);

        // Edit G and Spin G
        s->hEditG = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
            348, 180, 48, 24, hWnd, (HMENU)IDC_CP_EDIT_G, hInst, NULL);
        SendMessageW(s->hEditG, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);
        SetWindowTheme(s->hEditG, themeName, NULL);

        s->hSpinG = CreateWindowExW(0, UPDOWN_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
            0, 0, 18, 24, hWnd, (HMENU)IDC_CP_SPIN_G, hInst, NULL);
        SendMessageW(s->hSpinG, UDM_SETBUDDY, (WPARAM)s->hEditG, 0);
        SendMessageW(s->hSpinG, UDM_SETRANGE32, 0, 255);

        // Edit B and Spin B
        s->hEditB = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
            348, 210, 48, 24, hWnd, (HMENU)IDC_CP_EDIT_B, hInst, NULL);
        SendMessageW(s->hEditB, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);
        SetWindowTheme(s->hEditB, themeName, NULL);

        s->hSpinB = CreateWindowExW(0, UPDOWN_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
            0, 0, 18, 24, hWnd, (HMENU)IDC_CP_SPIN_B, hInst, NULL);
        SendMessageW(s->hSpinB, UDM_SETBUDDY, (WPARAM)s->hEditB, 0);
        SendMessageW(s->hSpinB, UDM_SETRANGE32, 0, 255);

        // HEX edit
        s->hEditHex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#000000",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP,
            348, 240, 72, 24, hWnd, (HMENU)IDC_CP_EDIT_HEX, hInst, NULL);
        SendMessageW(s->hEditHex, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);
        SendMessageW(s->hEditHex, EM_SETLIMITTEXT, 7, 0);
        SetWindowTheme(s->hEditHex, themeName, NULL);

        // OK Button
        s->hBtnOk = CreateWindowW(L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            245, 308, 85, 30, hWnd, (HMENU)IDC_CP_BTN_OK, hInst, NULL);
        SendMessageW(s->hBtnOk, WM_SETFONT, (WPARAM)s->hFontBold, TRUE);

        // Cancel Button
        s->hBtnCancel = CreateWindowW(L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            340, 308, 85, 30, hWnd, (HMENU)IDC_CP_BTN_CANCEL, hInst, NULL);
        SendMessageW(s->hBtnCancel, WM_SETFONT, (WPARAM)s->hFontNormal, TRUE);

        SyncControlsFromColor(hWnd, s);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_CP_BTN_OK) {
            s->confirmed = true;
            DestroyWindow(hWnd);
            return 0;
        } else if (id == IDC_CP_BTN_CANCEL) {
            s->confirmed = false;
            DestroyWindow(hWnd);
            return 0;
        }

        if (!s->isSyncingEdits) {
            if ((id == IDC_CP_EDIT_R || id == IDC_CP_EDIT_G || id == IDC_CP_EDIT_B) && code == EN_CHANGE) {
                WCHAR bufR[16] = {0}, bufG[16] = {0}, bufB[16] = {0};
                GetWindowTextW(s->hEditR, bufR, 16);
                GetWindowTextW(s->hEditG, bufG, 16);
                GetWindowTextW(s->hEditB, bufB, 16);

                int r = std::clamp(_wtoi(bufR), 0, 255);
                int g = std::clamp(_wtoi(bufG), 0, 255);
                int b = std::clamp(_wtoi(bufB), 0, 255);

                s->currentColor = RGB(r, g, b);
                s->currentHsv = RgbToHsv(s->currentColor);

                s->isSyncingEdits = true;
                SetWindowTextW(s->hEditHex, ColorToHexStr(s->currentColor).c_str());
                s->isSyncingEdits = false;

                InvalidateRect(hWnd, NULL, FALSE);
            } else if (id == IDC_CP_EDIT_HEX && code == EN_CHANGE) {
                WCHAR bufHex[16] = {0};
                GetWindowTextW(s->hEditHex, bufHex, 16);
                COLORREF parsed;
                if (TryParseHexStr(bufHex, parsed)) {
                    s->currentColor = parsed;
                    s->currentHsv = RgbToHsv(s->currentColor);

                    s->isSyncingEdits = true;
                    int r = GetRValue(s->currentColor);
                    int g = GetGValue(s->currentColor);
                    int b = GetBValue(s->currentColor);
                    SetWindowTextW(s->hEditR, std::to_wstring(r).c_str());
                    SetWindowTextW(s->hEditG, std::to_wstring(g).c_str());
                    SetWindowTextW(s->hEditB, std::to_wstring(b).c_str());
                    SendMessageW(s->hSpinR, UDM_SETPOS32, 0, r);
                    SendMessageW(s->hSpinG, UDM_SETPOS32, 0, g);
                    SendMessageW(s->hSpinB, UDM_SETPOS32, 0, b);
                    s->isSyncingEdits = false;

                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // Tab header click: [Standard] (20, 12, 105, 36) vs [Custom] (110, 12, 195, 36)
        if (y >= 12 && y <= 36) {
            if (x >= 20 && x <= 105 && s->activeTab != 0) {
                s->activeTab = 0;
                // Hide custom-only controls
                int show = SW_HIDE;
                ShowWindow(s->hComboModel, show);
                ShowWindow(s->hEditR, show);
                ShowWindow(s->hSpinR, show);
                ShowWindow(s->hEditG, show);
                ShowWindow(s->hSpinG, show);
                ShowWindow(s->hEditB, show);
                ShowWindow(s->hSpinB, show);
                ShowWindow(s->hEditHex, show);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (x >= 110 && x <= 195 && s->activeTab != 1) {
                s->activeTab = 1;
                // Show custom-only controls
                int show = SW_SHOW;
                ShowWindow(s->hComboModel, show);
                ShowWindow(s->hEditR, show);
                ShowWindow(s->hSpinR, show);
                ShowWindow(s->hEditG, show);
                ShowWindow(s->hSpinG, show);
                ShowWindow(s->hEditB, show);
                ShowWindow(s->hSpinB, show);
                ShowWindow(s->hEditHex, show);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        if (s->activeTab == 0) {
            // Standard Palette: 8 cols x 8 rows in (20, 52, 280, 276)
            int startX = 20, startY = 52;
            int swatchW = 30, swatchH = 26;
            int gap = 3;
            for (int i = 0; i < 64; ++i) {
                int r = i / 8;
                int c = i % 8;
                RECT rc = { startX + c * (swatchW + gap), startY + r * (swatchH + gap),
                            startX + c * (swatchW + gap) + swatchW, startY + r * (swatchH + gap) + swatchH };
                if (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom) {
                    s->currentColor = g_standardColors[i];
                    s->currentHsv = RgbToHsv(s->currentColor);
                    SyncControlsFromColor(hWnd, s);
                    return 0;
                }
            }
        } else {
            // Custom Tab: Spectrum (20, 52, 240, 252)
            if (x >= RECT_SPECTRUM.left && x <= RECT_SPECTRUM.right &&
                y >= RECT_SPECTRUM.top && y <= RECT_SPECTRUM.bottom) {
                s->isDraggingSpectrum = true;
                SetCapture(hWnd);
                float hue = (float)(x - RECT_SPECTRUM.left) / (float)(RECT_SPECTRUM.right - RECT_SPECTRUM.left) * 360.0f;
                float sat = 1.0f - (float)(y - RECT_SPECTRUM.top) / (float)(RECT_SPECTRUM.bottom - RECT_SPECTRUM.top);
                s->currentHsv.h = std::clamp(hue, 0.0f, 359.9f);
                s->currentHsv.s = std::clamp(sat, 0.0f, 1.0f);
                s->currentColor = HsvToRgb(s->currentHsv.h, s->currentHsv.s, s->currentHsv.v);
                SyncControlsFromColor(hWnd, s);
                return 0;
            }

            // Custom Tab: Luminance Slider (254, 52, 278, 252)
            if (x >= RECT_LUMINANCE.left && x <= RECT_LUMINANCE.right &&
                y >= RECT_LUMINANCE.top && y <= RECT_LUMINANCE.bottom) {
                s->isDraggingLuminance = true;
                SetCapture(hWnd);
                float val = 1.0f - (float)(y - RECT_LUMINANCE.top) / (float)(RECT_LUMINANCE.bottom - RECT_LUMINANCE.top);
                s->currentHsv.v = std::clamp(val, 0.0f, 1.0f);
                s->currentColor = HsvToRgb(s->currentHsv.h, s->currentHsv.s, s->currentHsv.v);
                SyncControlsFromColor(hWnd, s);
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (s->isDraggingSpectrum) {
            float hue = (float)(x - RECT_SPECTRUM.left) / (float)(RECT_SPECTRUM.right - RECT_SPECTRUM.left) * 360.0f;
            float sat = 1.0f - (float)(y - RECT_SPECTRUM.top) / (float)(RECT_SPECTRUM.bottom - RECT_SPECTRUM.top);
            s->currentHsv.h = std::clamp(hue, 0.0f, 359.9f);
            s->currentHsv.s = std::clamp(sat, 0.0f, 1.0f);
            s->currentColor = HsvToRgb(s->currentHsv.h, s->currentHsv.s, s->currentHsv.v);
            SyncControlsFromColor(hWnd, s);
            return 0;
        }

        if (s->isDraggingLuminance) {
            float val = 1.0f - (float)(y - RECT_LUMINANCE.top) / (float)(RECT_LUMINANCE.bottom - RECT_LUMINANCE.top);
            s->currentHsv.v = std::clamp(val, 0.0f, 1.0f);
            s->currentColor = HsvToRgb(s->currentHsv.h, s->currentHsv.s, s->currentHsv.v);
            SyncControlsFromColor(hWnd, s);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (s->isDraggingSpectrum || s->isDraggingLuminance) {
            s->isDraggingSpectrum = false;
            s->isDraggingLuminance = false;
            ReleaseCapture();
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;

        // Fill background first with dialog background to eliminate corner artifacts
        COLORREF dlgBgCol = s->isDark ? RGB(24, 24, 27) : RGB(248, 249, 250);
        HBRUSH hDlgBg = CreateSolidBrush(dlgBgCol);
        FillRect(hdc, &rc, hDlgBg);
        DeleteObject(hDlgBg);

        COLORREF btnBg, borderCol, textCol;
        if (dis->CtlID == IDC_CP_BTN_OK) {
            btnBg = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235); // #1d4ed8 vs #2563eb
            borderCol = isPressed ? RGB(29, 78, 216) : RGB(59, 130, 246);
            textCol = RGB(255, 255, 255);
        } else {
            if (s->isDark) {
                btnBg = isPressed ? RGB(24, 24, 27) : RGB(39, 39, 42);   // #18181b vs #27272a
                borderCol = RGB(63, 63, 70);
                textCol = RGB(228, 228, 231);
            } else {
                btnBg = isPressed ? RGB(228, 228, 231) : RGB(244, 244, 245);
                borderCol = RGB(212, 212, 216);
                textCol = RGB(24, 24, 27);
            }
        }

        HBRUSH hBr = CreateSolidBrush(btnBg);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBr);
        DeleteObject(hPen);

        WCHAR text[32] = {0};
        GetWindowTextW(dis->hwndItem, text, 32);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);
        HFONT hOldFont = (HFONT)SelectObject(hdc, (dis->CtlID == IDC_CP_BTN_OK) ? s->hFontBold : s->hFontNormal);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        COLORREF bgCol = (s && !s->isDark) ? RGB(248, 249, 250) : RGB(24, 24, 27);
        COLORREF textCol = (s && !s->isDark) ? RGB(24, 24, 27) : RGB(228, 228, 231);
        SetTextColor(hdcStatic, textCol);
        SetBkColor(hdcStatic, bgCol);
        static HBRUSH hBrDark = CreateSolidBrush(RGB(24, 24, 27));
        static HBRUSH hBrLight = CreateSolidBrush(RGB(248, 249, 250));
        return (LRESULT)((s && !s->isDark) ? hBrLight : hBrDark);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        COLORREF bgCol = (s && !s->isDark) ? RGB(255, 255, 255) : RGB(39, 39, 42);
        COLORREF textCol = (s && !s->isDark) ? RGB(24, 24, 27) : RGB(244, 244, 245);
        SetTextColor(hdcEdit, textCol);
        SetBkColor(hdcEdit, bgCol);
        static HBRUSH hBrEditDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrEditLight = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)((s && !s->isDark) ? hBrEditLight : hBrEditDark);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRc;
        GetClientRect(hWnd, &clientRc);

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientRc.right, clientRc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // Dialog background
        HBRUSH hBg = CreateSolidBrush(s->isDark ? RGB(24, 24, 27) : RGB(248, 249, 250));
        FillRect(memDC, &clientRc, hBg);
        DeleteObject(hBg);

        // Tab Bar Buttons: [Standard] and [Custom]
        RECT rcTabStd = { 20, 12, 105, 36 };
        RECT rcTabCst = { 110, 12, 195, 36 };

        auto DrawTabBtn = [&](RECT rc, LPCWSTR text, bool active) {
            COLORREF tabBg, tabBorder, tabText;
            if (s->isDark) {
                tabBg = active ? RGB(39, 39, 42) : RGB(24, 24, 27);
                tabBorder = active ? RGB(59, 130, 246) : RGB(63, 63, 70);
                tabText = active ? RGB(255, 255, 255) : RGB(161, 161, 170);
            } else {
                tabBg = active ? RGB(255, 255, 255) : RGB(240, 240, 242);
                tabBorder = active ? RGB(37, 99, 235) : RGB(212, 212, 216);
                tabText = active ? RGB(24, 24, 27) : RGB(100, 100, 105);
            }

            HBRUSH hTabBr = CreateSolidBrush(tabBg);
            HPEN hTabPen = CreatePen(PS_SOLID, 1, tabBorder);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hTabBr);
            HPEN hOldP = (HPEN)SelectObject(memDC, hTabPen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            SelectObject(memDC, hOldP);
            SelectObject(memDC, hOldB);
            DeleteObject(hTabPen);
            DeleteObject(hTabBr);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, tabText);
            SelectObject(memDC, active ? s->hFontBold : s->hFontNormal);
            DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };

        DrawTabBtn(rcTabStd, L"Standard", s->activeTab == 0);
        DrawTabBtn(rcTabCst, L"Custom", s->activeTab == 1);

        if (s->activeTab == 0) {
            // Render 64 Standard Palettes
            int startX = 20, startY = 52;
            int swatchW = 30, swatchH = 26;
            int gap = 3;
            for (int i = 0; i < 64; ++i) {
                int r = i / 8;
                int c = i % 8;
                RECT rc = { startX + c * (swatchW + gap), startY + r * (swatchH + gap),
                            startX + c * (swatchW + gap) + swatchW, startY + r * (swatchH + gap) + swatchH };

                COLORREF col = g_standardColors[i];
                HBRUSH hSw = CreateSolidBrush(col);
                FillRect(memDC, &rc, hSw);
                DeleteObject(hSw);

                HPEN hPen = CreatePen(PS_SOLID, 1, (col == s->currentColor) ? RGB(59, 130, 246) : (s->isDark ? RGB(63, 63, 70) : RGB(212, 212, 216)));
                HPEN hOldP = (HPEN)SelectObject(memDC, hPen);
                HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
                HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hNull);
                Rectangle(memDC, rc.left, rc.top, rc.right, rc.bottom);

                if (col == s->currentColor) {
                    HPEN hRing = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                    SelectObject(memDC, hRing);
                    Rectangle(memDC, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
                    DeleteObject(hRing);
                }

                SelectObject(memDC, hOldB);
                SelectObject(memDC, hOldP);
                DeleteObject(hPen);
            }
        } else {
            // Render Spectrum Canvas (Hue x Saturation DIB)
            int specW = RECT_SPECTRUM.right - RECT_SPECTRUM.left;
            int specH = RECT_SPECTRUM.bottom - RECT_SPECTRUM.top;

            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = specW;
            bmi.bmiHeader.biHeight = -specH; // Top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            std::vector<DWORD> pixels(specW * specH);
            for (int y = 0; y < specH; ++y) {
                float sat = 1.0f - (float)y / (float)specH;
                for (int x = 0; x < specW; ++x) {
                    float hue = (float)x / (float)specW * 360.0f;
                    COLORREF c = HsvToRgb(hue, sat, 1.0f);
                    pixels[y * specW + x] = ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
                }
            }

            SetDIBitsToDevice(memDC, RECT_SPECTRUM.left, RECT_SPECTRUM.top, specW, specH,
                              0, 0, 0, specH, pixels.data(), &bmi, DIB_RGB_COLORS);

            // Spectrum Border
            HPEN hSpecPen = CreatePen(PS_SOLID, 1, s->isDark ? RGB(63, 63, 70) : RGB(212, 212, 216));
            HPEN hOldSp = (HPEN)SelectObject(memDC, hSpecPen);
            HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hNull);
            Rectangle(memDC, RECT_SPECTRUM.left, RECT_SPECTRUM.top, RECT_SPECTRUM.right, RECT_SPECTRUM.bottom);

            // Crosshair marker in spectrum
            int crossX = RECT_SPECTRUM.left + (int)(s->currentHsv.h / 360.0f * specW);
            int crossY = RECT_SPECTRUM.top + (int)((1.0f - s->currentHsv.s) * specH);
            HPEN hCrossPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(memDC, hCrossPen);
            Ellipse(memDC, crossX - 5, crossY - 5, crossX + 6, crossY + 6);
            DeleteObject(hCrossPen);

            // Render Luminance Slider
            int lumW = RECT_LUMINANCE.right - RECT_LUMINANCE.left;
            int lumH = RECT_LUMINANCE.bottom - RECT_LUMINANCE.top;
            std::vector<DWORD> lumPixels(lumW * lumH);
            for (int y = 0; y < lumH; ++y) {
                float v = 1.0f - (float)y / (float)lumH;
                COLORREF c = HsvToRgb(s->currentHsv.h, s->currentHsv.s, v);
                DWORD rgbVal = ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
                for (int x = 0; x < lumW; ++x) {
                    lumPixels[y * lumW + x] = rgbVal;
                }
            }

            bmi.bmiHeader.biWidth = lumW;
            bmi.bmiHeader.biHeight = -lumH;
            SetDIBitsToDevice(memDC, RECT_LUMINANCE.left, RECT_LUMINANCE.top, lumW, lumH,
                              0, 0, 0, lumH, lumPixels.data(), &bmi, DIB_RGB_COLORS);

            // Luminance Border
            SelectObject(memDC, hSpecPen);
            Rectangle(memDC, RECT_LUMINANCE.left, RECT_LUMINANCE.top, RECT_LUMINANCE.right, RECT_LUMINANCE.bottom);

            // Luminance marker indicator
            int markerY = RECT_LUMINANCE.top + (int)((1.0f - s->currentHsv.v) * lumH);
            HPEN hMarkPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(memDC, hMarkPen);
            MoveToEx(memDC, RECT_LUMINANCE.left - 2, markerY, NULL);
            LineTo(memDC, RECT_LUMINANCE.right + 2, markerY);
            DeleteObject(hMarkPen);

            SelectObject(memDC, hOldB);
            SelectObject(memDC, hOldSp);
            DeleteObject(hSpecPen);

            // Labels on Right Panel
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, s->isDark ? RGB(161, 161, 170) : RGB(100, 100, 105));
            SelectObject(memDC, s->hFontNormal);

            TextOutW(memDC, 300, 125, L"Model:", 6);
            TextOutW(memDC, 300, 153, L"Red:", 4);
            TextOutW(memDC, 300, 183, L"Green:", 6);
            TextOutW(memDC, 300, 213, L"Blue:", 5);
            TextOutW(memDC, 300, 243, L"HEX:", 4);
        }

        // Comparison Swatches: New (Top) vs Current (Bottom)
        HBRUSH hBrNew = CreateSolidBrush(s->currentColor);
        FillRect(memDC, &RECT_COMPARE_NEW, hBrNew);
        DeleteObject(hBrNew);

        HBRUSH hBrCur = CreateSolidBrush(s->initialColor);
        FillRect(memDC, &RECT_COMPARE_CUR, hBrCur);
        DeleteObject(hBrCur);

        HPEN hCmpBorder = CreatePen(PS_SOLID, 1, s->isDark ? RGB(63, 63, 70) : RGB(212, 212, 216));
        HPEN hOldCp = (HPEN)SelectObject(memDC, hCmpBorder);
        HBRUSH hOldCb = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, RECT_COMPARE_NEW.left, RECT_COMPARE_NEW.top, RECT_COMPARE_CUR.right, RECT_COMPARE_CUR.bottom);
        MoveToEx(memDC, RECT_COMPARE_NEW.left, RECT_COMPARE_NEW.bottom, NULL);
        LineTo(memDC, RECT_COMPARE_NEW.right, RECT_COMPARE_NEW.bottom);
        SelectObject(memDC, hOldCb);
        SelectObject(memDC, hOldCp);
        DeleteObject(hCmpBorder);

        // Labels for New / Current
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, (GetRValue(s->currentColor)*299 + GetGValue(s->currentColor)*587 + GetBValue(s->currentColor)*114 > 128000) ? RGB(0, 0, 0) : RGB(255, 255, 255));
        RECT lblNewRc = RECT_COMPARE_NEW;
        lblNewRc.left += 8;
        DrawTextW(memDC, L"New", 3, &lblNewRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(memDC, (GetRValue(s->initialColor)*299 + GetGValue(s->initialColor)*587 + GetBValue(s->initialColor)*114 > 128000) ? RGB(0, 0, 0) : RGB(255, 255, 255));
        RECT lblCurRc = RECT_COMPARE_CUR;
        lblCurRc.left += 8;
        DrawTextW(memDC, L"Current", 7, &lblCurRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Separator line before buttons
        HPEN hLinePen = CreatePen(PS_SOLID, 1, s->isDark ? RGB(39, 39, 42) : RGB(228, 228, 231));
        HPEN hOldLp = (HPEN)SelectObject(memDC, hLinePen);
        MoveToEx(memDC, 20, 292, NULL);
        LineTo(memDC, clientRc.right - 20, 292);
        SelectObject(memDC, hOldLp);
        DeleteObject(hLinePen);

        BitBlt(hdc, 0, 0, clientRc.right, clientRc.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            s->confirmed = false;
            DestroyWindow(hWnd);
            return 0;
        } else if (wParam == VK_RETURN) {
            s->confirmed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE: {
        s->confirmed = false;
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (s->hFontNormal) { DeleteObject(s->hFontNormal); s->hFontNormal = NULL; }
        if (s->hFontBold) { DeleteObject(s->hFontBold); s->hFontBold = NULL; }
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CustomColorPickerDialog::Show(HWND hWndParent, COLORREF currentColor, COLORREF& outColor) {
    static bool s_registered = false;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    if (!s_registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ColorPickerWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"FeathrPDFColorPickerDlgClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    ColorPickerState state;
    state.initialColor = currentColor;
    state.currentColor = currentColor;
    state.currentHsv = RgbToHsv(currentColor);
    state.activeTab = 1; // Default to Custom Tab with Spectrum & HEX
    state.isDraggingSpectrum = false;
    state.isDraggingLuminance = false;
    state.confirmed = false;
    state.isSyncingEdits = false;
    state.hFontNormal = NULL;
    state.hFontBold = NULL;

    int dlgW = 455;
    int dlgH = 390;

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"FeathrPDFColorPickerDlgClass",
        L"Color Picker - FeathrPDF",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x, y, dlgW, dlgH,
        hWndParent, NULL, hInst, &state
    );

    if (!hDlg) return false;

    EnableWindow(hWndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    SetFocus(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hWndParent, TRUE);
    SetFocus(hWndParent);

    if (state.confirmed) {
        outColor = state.currentColor;
        return true;
    }
    return false;
}
