#include "printer.h"
#include "app.h"
#include <winspool.h>
#include <sstream>
#include <algorithm>
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>

#define IDC_PRINTER_COMBO       1001
#define IDC_RADIO_ALL           1002
#define IDC_RADIO_SELECTED      1003
#define IDC_RADIO_CUSTOM        1004
#define IDC_EDIT_CUSTOM         1005
#define IDC_COMBO_SUBSET        1006
#define IDC_CHK_REVERSE         1007
#define IDC_COMBO_PAPER         1008
#define IDC_COMBO_ORIENT        1009
#define IDC_COMBO_NUP           1010
#define IDC_EDIT_COPIES         1011
#define IDC_LBL_PREVIEW         1012
#define IDC_BTN_PRINT           1013
#define IDC_BTN_CANCEL          1014

#ifndef ECM_FIRST
#define ECM_FIRST 0x1500
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (ECM_FIRST + 1)
#endif

static PrintOptions g_dialogOptions;
static bool g_dialogResult = false;
static HFONT g_hDlgFont = NULL;
static HFONT g_hDlgBoldFont = NULL;
static HWND g_hLblPreviewCtrl = NULL;
static HWND s_hPrintDlgInstance = NULL;

static LRESULT CALLBACK GroupBoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        bool isDark = PDFApp::Instance().IsDarkMode();

        // Get groupbox caption
        WCHAR text[128] = { 0 };
        GetWindowTextW(hWnd, text, 128);

        HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
        if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE szText = { 0, 0 };
        if (text[0] != L'\0') {
            GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &szText);
        }

        int textMargin = 10;
        int topOffset = szText.cy > 0 ? (szText.cy / 2) : 8;

        // Draw flat rounded border without filling the interior
        COLORREF borderCol = isDark ? RGB(63, 63, 70) : RGB(212, 212, 216);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hNull);

        RoundRect(hdc, rc.left, rc.top + topOffset, rc.right - 1, rc.bottom - 1, 6, 6);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        // Draw caption text, masking only the small segment of the top border
        if (text[0] != L'\0') {
            RECT rcTextBg = { rc.left + textMargin - 4, rc.top, rc.left + textMargin + szText.cx + 4, rc.top + szText.cy };
            HBRUSH hTextBg = CreateSolidBrush(isDark ? RGB(26, 26, 28) : RGB(244, 244, 245));
            FillRect(hdc, &rcTextBg, hTextBg);
            DeleteObject(hTextBg);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isDark ? RGB(240, 240, 240) : RGB(20, 20, 20));
            RECT rcText = { rc.left + textMargin, rc.top, rc.left + textMargin + szText.cx, rc.top + szText.cy };
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_SINGLELINE);
        }

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    } else if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK FlatBorderSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    LRESULT lr = DefSubclassProc(hWnd, msg, wParam, lParam);
    if (msg == WM_PAINT || msg == WM_NCPAINT) {
        bool isDark = PDFApp::Instance().IsDarkMode();
        HDC hdc = GetWindowDC(hWnd);
        if (hdc) {
            RECT rc;
            GetWindowRect(hWnd, &rc);
            OffsetRect(&rc, -rc.left, -rc.top);
            HPEN hPen = CreatePen(PS_SOLID, 1, isDark ? RGB(63, 63, 70) : RGB(212, 212, 216));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
            ReleaseDC(hWnd, hdc);
        }
    }
    return lr;
}

static void ApplyPrintDialogControlsTheme(HWND hDlg, bool isDark) {
    if (!hDlg || !IsWindow(hDlg)) return;

    // Set UxTheme for ComboBoxes & Edit Controls (DarkMode_CFD / Explorer)
    int comboAndEditIds[] = {
        IDC_PRINTER_COMBO,
        IDC_COMBO_PAPER,
        IDC_COMBO_ORIENT,
        IDC_COMBO_NUP,
        IDC_COMBO_SUBSET,
        IDC_EDIT_CUSTOM,
        IDC_EDIT_COPIES
    };
    for (int id : comboAndEditIds) {
        HWND hCtrl = GetDlgItem(hDlg, id);
        if (hCtrl) {
            PDFApp::ApplyUxThemeDarkMode(hCtrl, isDark);
            if (isDark) {
                if (FAILED(SetWindowTheme(hCtrl, L"DarkMode_CFD", NULL))) {
                    SetWindowTheme(hCtrl, L"DarkMode_Explorer", NULL);
                }
            } else {
                SetWindowTheme(hCtrl, L"Explorer", NULL);
            }
        }
    }

    // Strip UxTheme on RadioButtons and CheckBoxes so WM_CTLCOLORSTATIC takes full effect
    int buttonIds[] = {
        IDC_RADIO_ALL,
        IDC_RADIO_SELECTED,
        IDC_RADIO_CUSTOM,
        IDC_CHK_REVERSE
    };
    for (int id : buttonIds) {
        HWND hCtrl = GetDlgItem(hDlg, id);
        if (hCtrl) {
            SetWindowTheme(hCtrl, L"", L"");
        }
    }
}

static std::vector<std::wstring> EnumerateSystemPrinters(std::wstring& defaultPrinter) {
    std::vector<std::wstring> list;
    DWORD needed = 0, returned = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, NULL, 0, &needed, &returned);
    if (needed > 0) {
        std::vector<BYTE> buffer(needed);
        if (EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, buffer.data(), needed, &needed, &returned)) {
            PRINTER_INFO_2W* pinfo = (PRINTER_INFO_2W*)buffer.data();
            for (DWORD i = 0; i < returned; ++i) {
                list.push_back(pinfo[i].pPrinterName);
            }
        }
    }

    WCHAR defName[256] = { 0 };
    DWORD defLen = 256;
    if (GetDefaultPrinterW(defName, &defLen)) {
        defaultPrinter = defName;
    }
    return list;
}

static void UpdateLivePreview(HWND hWnd) {
    if (!g_hLblPreviewCtrl) return;
    PDFApp& app = PDFApp::Instance();
    int totalDocPages = app.GetPageCount();

    PrintOptions cur;
    if (SendMessageW(GetDlgItem(hWnd, IDC_RADIO_SELECTED), BM_GETCHECK, 0, 0) == BST_CHECKED)
        cur.rangeType = PRINT_RANGE_SELECTED;
    else if (SendMessageW(GetDlgItem(hWnd, IDC_RADIO_CUSTOM), BM_GETCHECK, 0, 0) == BST_CHECKED)
        cur.rangeType = PRINT_RANGE_CUSTOM;
    else
        cur.rangeType = PRINT_RANGE_ALL;

    WCHAR customBuf[256] = { 0 };
    GetWindowTextW(GetDlgItem(hWnd, IDC_EDIT_CUSTOM), customBuf, 256);
    cur.customRangeStr = customBuf;

    int subSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_SUBSET), CB_GETCURSEL, 0, 0);
    if (subSel == 1) cur.subsetType = PRINT_SUBSET_ODD;
    else if (subSel == 2) cur.subsetType = PRINT_SUBSET_EVEN;
    else cur.subsetType = PRINT_SUBSET_ALL;

    cur.reversePrint = (SendMessageW(GetDlgItem(hWnd, IDC_CHK_REVERSE), BM_GETCHECK, 0, 0) == BST_CHECKED);

    std::vector<int> finalPages = PDFPrinter::ComputeFinalPages(cur, totalDocPages, app.GetSelectedPages());

    std::wstring text;
    if (finalPages.empty()) {
        text = L"⚠️ No pages match the selected filter to print!";
    } else {
        text = L"About to print: ";
        size_t showMax = std::min((size_t)12, finalPages.size());
        for (size_t i = 0; i < showMax; ++i) {
            text += std::to_wstring(finalPages[i] + 1);
            if (i + 1 < showMax) text += L", ";
        }
        std::wstring pageStr = (finalPages.size() == 1) ? L"page" : L"pages";
        if (finalPages.size() > showMax) {
            text += L", ... (Total: " + std::to_wstring(finalPages.size()) + L" " + pageStr + L")";
        } else {
            text += L" (Total: " + std::to_wstring(finalPages.size()) + L" " + pageStr + L")";
        }
    }
    SetWindowTextW(g_hLblPreviewCtrl, text.c_str());
}

static LRESULT CALLBACK PrintDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
    case WM_CREATE: {
        s_hPrintDlgInstance = hWnd;
        bool isDark = PDFApp::Instance().IsDarkMode();
        PDFApp::SetWindowTitlebarTheme(hWnd, isDark);
        PDFApp::ApplyUxThemeDarkMode(hWnd, isDark);
        return 0;
    }

    case WM_DESTROY: {
        s_hPrintDlgInstance = NULL;
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        bool isDark = PDFApp::Instance().IsDarkMode();
        HBRUSH hBr = CreateSolidBrush(isDark ? RGB(26, 26, 28) : RGB(244, 244, 245));
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        bool isDark = PDFApp::Instance().IsDarkMode();
        HBRUSH hBr = CreateSolidBrush(isDark ? RGB(26, 26, 28) : RGB(244, 244, 245));
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
        bool isDark = PDFApp::Instance().IsDarkMode();

        HBRUSH hDlgBg = CreateSolidBrush(isDark ? RGB(26, 26, 28) : RGB(244, 244, 245));
        FillRect(hdc, &rc, hDlgBg);
        DeleteObject(hDlgBg);

        COLORREF btnBg, borderCol, textCol;
        if (dis->CtlID == IDC_BTN_PRINT) {
            btnBg = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);
            borderCol = isPressed ? RGB(29, 78, 216) : RGB(59, 130, 246);
            textCol = RGB(255, 255, 255);
        } else {
            if (isDark) {
                btnBg = isPressed ? RGB(26, 26, 28) : RGB(39, 39, 42);
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
        HFONT hOldFont = (HFONT)SelectObject(hdc, (dis->CtlID == IDC_BTN_PRINT) ? g_hDlgBoldFont : g_hDlgFont);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        return TRUE;
    }

    case WM_CTLCOLORDLG: {
        bool isDark = PDFApp::Instance().IsDarkMode();
        static HBRUSH hBrushDarkDlg = CreateSolidBrush(RGB(26, 26, 28));
        static HBRUSH hBrushLightDlg = CreateSolidBrush(RGB(244, 244, 245));
        return (LRESULT)(isDark ? hBrushDarkDlg : hBrushLightDlg);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        bool isDark = PDFApp::Instance().IsDarkMode();

        WCHAR clsName[32] = { 0 };
        GetClassNameW(hCtrl, clsName, 32);
        int ctlId = GetDlgCtrlID(hCtrl);

        // 1. EDIT Control (Disabled Edit sends WM_CTLCOLORSTATIC)
        if (_wcsicmp(clsName, L"EDIT") == 0 || ctlId == IDC_EDIT_CUSTOM || ctlId == IDC_EDIT_COPIES) {
            SetBkMode(hdc, OPAQUE);
            if (isDark) {
                SetTextColor(hdc, RGB(161, 161, 170));
                SetBkColor(hdc, RGB(39, 39, 42)); // #27272a
                static HBRUSH hBrEditDisDark = CreateSolidBrush(RGB(39, 39, 42));
                return (LRESULT)hBrEditDisDark;
            } else {
                SetTextColor(hdc, RGB(128, 128, 128));
                SetBkColor(hdc, RGB(244, 244, 245));
                static HBRUSH hBrEditDisLight = CreateSolidBrush(RGB(244, 244, 245));
                return (LRESULT)hBrEditDisLight;
            }
        }

        // 2. COMBOBOX Control (CBS_DROPDOWNLIST sends WM_CTLCOLORSTATIC for static box)
        if (_wcsicmp(clsName, L"COMBOBOX") == 0 ||
            ctlId == IDC_PRINTER_COMBO || ctlId == IDC_COMBO_SUBSET ||
            ctlId == IDC_COMBO_PAPER || ctlId == IDC_COMBO_ORIENT ||
            ctlId == IDC_COMBO_NUP) {
            SetBkMode(hdc, OPAQUE);
            if (isDark) {
                SetTextColor(hdc, RGB(240, 240, 240));
                SetBkColor(hdc, RGB(39, 39, 42)); // #27272a
                static HBRUSH hBrComboDark = CreateSolidBrush(RGB(39, 39, 42));
                return (LRESULT)hBrComboDark;
            } else {
                SetTextColor(hdc, RGB(20, 20, 20));
                SetBkColor(hdc, RGB(255, 255, 255));
                static HBRUSH hBrComboLight = CreateSolidBrush(RGB(255, 255, 255));
                return (LRESULT)hBrComboLight;
            }
        }

        // 3. Static labels, RadioButtons, CheckBoxes
        SetBkMode(hdc, TRANSPARENT);
        static HBRUSH hBrushDarkDlg = CreateSolidBrush(RGB(26, 26, 28));
        static HBRUSH hBrushLightDlg = CreateSolidBrush(RGB(244, 244, 245));

        if (isDark) {
            SetTextColor(hdc, RGB(240, 240, 240)); // Ép màu chữ trắng sáng rõ: RGB(240, 240, 240)
            return (LRESULT)hBrushDarkDlg;
        } else {
            SetTextColor(hdc, RGB(20, 20, 20));
            return (LRESULT)hBrushLightDlg;
        }
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        bool isDark = PDFApp::Instance().IsDarkMode();
        static HBRUSH hBrushEditDark = CreateSolidBrush(RGB(39, 39, 42));
        static HBRUSH hBrushEditLight = CreateSolidBrush(RGB(255, 255, 255));
        if (isDark) {
            SetTextColor(hdc, RGB(240, 240, 240));     // Chữ trắng sáng
            SetBkColor(hdc, RGB(39, 39, 42));          // Nền ô màu tối (#27272a)
            return (LRESULT)hBrushEditDark;            // HBRUSH màu RGB(39, 39, 42)
        } else {
            SetTextColor(hdc, RGB(20, 20, 20));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)hBrushEditLight;
        }
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_BTN_PRINT) {
            HWND hComboPrinter = GetDlgItem(hWnd, IDC_PRINTER_COMBO);
            int sel = (int)SendMessageW(hComboPrinter, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                WCHAR nameBuf[256];
                SendMessageW(hComboPrinter, CB_GETLBTEXT, sel, (LPARAM)nameBuf);
                g_dialogOptions.printerName = nameBuf;
            }

            if (SendMessageW(GetDlgItem(hWnd, IDC_RADIO_SELECTED), BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_dialogOptions.rangeType = PRINT_RANGE_SELECTED;
            else if (SendMessageW(GetDlgItem(hWnd, IDC_RADIO_CUSTOM), BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_dialogOptions.rangeType = PRINT_RANGE_CUSTOM;
            else
                g_dialogOptions.rangeType = PRINT_RANGE_ALL;

            WCHAR customBuf[256] = { 0 };
            GetWindowTextW(GetDlgItem(hWnd, IDC_EDIT_CUSTOM), customBuf, 256);
            g_dialogOptions.customRangeStr = customBuf;

            int subSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_SUBSET), CB_GETCURSEL, 0, 0);
            if (subSel == 1) g_dialogOptions.subsetType = PRINT_SUBSET_ODD;
            else if (subSel == 2) g_dialogOptions.subsetType = PRINT_SUBSET_EVEN;
            else g_dialogOptions.subsetType = PRINT_SUBSET_ALL;

            g_dialogOptions.reversePrint = (SendMessageW(GetDlgItem(hWnd, IDC_CHK_REVERSE), BM_GETCHECK, 0, 0) == BST_CHECKED);

            int paperSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_PAPER), CB_GETCURSEL, 0, 0);
            switch (paperSel) {
            case 0: g_dialogOptions.paperSize = DMPAPER_A4; break;
            case 1: g_dialogOptions.paperSize = DMPAPER_LETTER; break;
            case 2: g_dialogOptions.paperSize = DMPAPER_LEGAL; break;
            case 3: g_dialogOptions.paperSize = DMPAPER_A3; break;
            case 4: g_dialogOptions.paperSize = DMPAPER_A5; break;
            default: g_dialogOptions.paperSize = DMPAPER_A4; break;
            }

            int orientSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_ORIENT), CB_GETCURSEL, 0, 0);
            g_dialogOptions.orientation = (orientSel == 1) ? ORIENT_LANDSCAPE : ORIENT_PORTRAIT;

            int nupSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_NUP), CB_GETCURSEL, 0, 0);
            switch (nupSel) {
            case 1: g_dialogOptions.pagesPerSheet = 2; break;
            case 2: g_dialogOptions.pagesPerSheet = 4; break;
            case 3: g_dialogOptions.pagesPerSheet = 8; break;
            case 4: g_dialogOptions.pagesPerSheet = 16; break;
            default: g_dialogOptions.pagesPerSheet = 1; break;
            }

            WCHAR copiesBuf[32] = { 0 };
            GetWindowTextW(GetDlgItem(hWnd, IDC_EDIT_COPIES), copiesBuf, 32);
            int cp = _wtoi(copiesBuf);
            g_dialogOptions.copies = (cp > 0) ? cp : 1;

            g_dialogResult = true;
            DestroyWindow(hWnd);
        } else if (id == IDC_BTN_CANCEL) {
            g_dialogResult = false;
            DestroyWindow(hWnd);
        } else if (id >= IDC_RADIO_ALL && id <= IDC_RADIO_CUSTOM) {
            BOOL isCustom = (SendMessageW(GetDlgItem(hWnd, IDC_RADIO_CUSTOM), BM_GETCHECK, 0, 0) == BST_CHECKED);
            HWND hCustomEdit = GetDlgItem(hWnd, IDC_EDIT_CUSTOM);
            if (isCustom) {
                SetFocus(hCustomEdit);
            }
            InvalidateRect(hCustomEdit, NULL, TRUE);
            UpdateLivePreview(hWnd);
        } else if (id == IDC_EDIT_CUSTOM && code == EN_CHANGE) {
            WCHAR customBuf[256] = { 0 };
            GetWindowTextW(GetDlgItem(hWnd, IDC_EDIT_CUSTOM), customBuf, 256);
            bool hasChar = false;
            for (int i = 0; customBuf[i] != L'\0'; ++i) {
                if (!iswspace(customBuf[i])) {
                    hasChar = true;
                    break;
                }
            }
            if (hasChar) {
                SendMessageW(GetDlgItem(hWnd, IDC_RADIO_ALL), BM_SETCHECK, BST_UNCHECKED, 0);
                SendMessageW(GetDlgItem(hWnd, IDC_RADIO_SELECTED), BM_SETCHECK, BST_UNCHECKED, 0);
                SendMessageW(GetDlgItem(hWnd, IDC_RADIO_CUSTOM), BM_SETCHECK, BST_CHECKED, 0);
            }
            UpdateLivePreview(hWnd);
        } else if (id == IDC_COMBO_SUBSET && code == CBN_SELCHANGE) {
            UpdateLivePreview(hWnd);
        } else if (id == IDC_CHK_REVERSE) {
            UpdateLivePreview(hWnd);
        }
        return 0;
    }

    case WM_CLOSE:
        g_dialogResult = false;
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::vector<int> PDFPrinter::ComputeFinalPages(const PrintOptions& options, int totalDocPages, const std::set<int>& selectedPages) {
    std::vector<int> initial;
    if (options.rangeType == PRINT_RANGE_ALL) {
        for (int i = 0; i < totalDocPages; ++i) initial.push_back(i);
    } else if (options.rangeType == PRINT_RANGE_SELECTED) {
        for (int s : selectedPages) {
            if (s >= 0 && s < totalDocPages) initial.push_back(s);
        }
    } else if (options.rangeType == PRINT_RANGE_CUSTOM) {
        initial = ParsePageRange(options.customRangeStr, totalDocPages);
    }

    std::vector<int> filtered;
    for (int pIdx : initial) {
        int pageNum = pIdx + 1; // 1-based page number
        if (options.subsetType == PRINT_SUBSET_ODD) {
            if (pageNum % 2 != 0) filtered.push_back(pIdx);
        } else if (options.subsetType == PRINT_SUBSET_EVEN) {
            if (pageNum % 2 == 0) filtered.push_back(pIdx);
        } else {
            filtered.push_back(pIdx);
        }
    }

    if (options.reversePrint) {
        std::reverse(filtered.begin(), filtered.end());
    }
    return filtered;
}

std::vector<int> PDFPrinter::ParsePageRange(const std::wstring& str, int maxPages) {
    std::vector<int> result;
    std::wstring normalized = str;
    for (auto& ch : normalized) {
        if (ch == L';') ch = L',';
    }
    std::wstringstream ss(normalized);
    std::wstring part;
    while (std::getline(ss, part, L',')) {
        size_t first = part.find_first_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        size_t last = part.find_last_not_of(L" \t");
        part = part.substr(first, (last - first + 1));

        size_t dash = part.find(L'-');
        if (dash != std::wstring::npos) {
            int start = _wtoi(part.substr(0, dash).c_str());
            int end = _wtoi(part.substr(dash + 1).c_str());
            if (start > end) std::swap(start, end);
            for (int p = start; p <= end; ++p) {
                if (p >= 1 && p <= maxPages) {
                    result.push_back(p - 1);
                }
            }
        } else {
            int p = _wtoi(part.c_str());
            if (p >= 1 && p <= maxPages) {
                result.push_back(p - 1);
            }
        }
    }
    return result;
}

bool PDFPrinter::ShowPrintDialog(HWND hWndParent, PrintOptions& outOptions) {
    g_dialogResult = false;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = PrintDialogProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"FeathrPDFCustomPrintDialogClass";
    wc.hbrBackground = NULL; // No background brush to prevent flickering
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    int dlgW = 600;
    int dlgH = 460;
    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"FeathrPDFCustomPrintDialogClass",
        L"Print",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN,
        posX, posY, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandleW(NULL), NULL
    );

    if (!hDlg) return false;

    bool isDark = PDFApp::Instance().IsDarkMode();
    PDFApp::SetWindowTitlebarTheme(hDlg, isDark);
    PDFApp::ApplyUxThemeDarkMode(hDlg, isDark);

    if (!g_hDlgFont) {
        g_hDlgFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hDlgBoldFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    auto SetCtrlFont = [](HWND hCtrl) {
        SendMessageW(hCtrl, WM_SETFONT, (WPARAM)g_hDlgFont, TRUE);
    };

    // 1. Printer:
    HWND hLblPrinter = CreateWindowW(L"STATIC", L"Printer:", WS_CHILD | WS_VISIBLE, 20, 15, 115, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblPrinter);

    HWND hComboPrinter = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 12, 425, 200, hDlg, (HMENU)IDC_PRINTER_COMBO, NULL, NULL);
    SetCtrlFont(hComboPrinter);
    SetWindowSubclass(hComboPrinter, FlatBorderSubclassProc, IDC_PRINTER_COMBO, 0);

    std::wstring defaultPrinter;
    std::vector<std::wstring> printers = EnumerateSystemPrinters(defaultPrinter);
    int canonIdx = -1;
    int defIdx = 0;
    for (size_t i = 0; i < printers.size(); ++i) {
        SendMessageW(hComboPrinter, CB_ADDSTRING, 0, (LPARAM)printers[i].c_str());
        if (printers[i].find(L"LBP2900") != std::wstring::npos || printers[i].find(L"Canon") != std::wstring::npos) {
            canonIdx = (int)i;
        }
        if (printers[i] == defaultPrinter) {
            defIdx = (int)i;
        }
    }
    int chooseIdx = (canonIdx >= 0) ? canonIdx : defIdx;
    SendMessageW(hComboPrinter, CB_SETCURSEL, chooseIdx, 0);

    // Group box: Page range
    HWND hGrpRange = CreateWindowW(L"BUTTON", L"Page range", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 48, 545, 145, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hGrpRange);
    SetWindowTheme(hGrpRange, L"", L"");
    SetWindowSubclass(hGrpRange, GroupBoxSubclassProc, 1, 0);

    HWND hRadAll = CreateWindowW(L"BUTTON", L"All pages", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 35, 72, 170, 20, hDlg, (HMENU)IDC_RADIO_ALL, NULL, NULL);
    SetCtrlFont(hRadAll);
    SendMessageW(hRadAll, BM_SETCHECK, BST_CHECKED, 0);

    int selCount = PDFApp::Instance().GetSelectedCount();
    std::wstring selPageStr = (selCount == 1) ? L"page" : L"pages";
    std::wstring selText = L"Selected pages (" + std::to_wstring(selCount) + L" " + selPageStr + L")";
    HWND hRadSel = CreateWindowW(L"BUTTON", selText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 230, 72, 280, 20, hDlg, (HMENU)IDC_RADIO_SELECTED, NULL, NULL);
    SetCtrlFont(hRadSel);
    if (selCount == 0) EnableWindow(hRadSel, FALSE);

    HWND hRadCustom = CreateWindowW(L"BUTTON", L"Pages:", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 35, 100, 80, 22, hDlg, (HMENU)IDC_RADIO_CUSTOM, NULL, NULL);
    SetCtrlFont(hRadCustom);

    HWND hEditCustom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 120, 99, 425, 24, hDlg, (HMENU)IDC_EDIT_CUSTOM, NULL, NULL);
    SetCtrlFont(hEditCustom);
    SetWindowSubclass(hEditCustom, FlatBorderSubclassProc, IDC_EDIT_CUSTOM, 0);
    SendMessageW(hEditCustom, EM_SETCUEBANNER, TRUE, (LPARAM)L"e.g. 1-5, 8");

    // Subset: All pages / Odd pages only / Even pages only
    HWND hLblFilter = CreateWindowW(L"STATIC", L"Subset:", WS_CHILD | WS_VISIBLE, 35, 131, 80, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblFilter);

    HWND hComboSubset = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 120, 128, 425, 120, hDlg, (HMENU)IDC_COMBO_SUBSET, NULL, NULL);
    SetCtrlFont(hComboSubset);
    SetWindowSubclass(hComboSubset, FlatBorderSubclassProc, IDC_COMBO_SUBSET, 0);
    SendMessageW(hComboSubset, CB_ADDSTRING, 0, (LPARAM)L"All pages");
    SendMessageW(hComboSubset, CB_ADDSTRING, 0, (LPARAM)L"Odd pages only");
    SendMessageW(hComboSubset, CB_ADDSTRING, 0, (LPARAM)L"Even pages only");
    SendMessageW(hComboSubset, CB_SETCURSEL, 0, 0);

    // Live Preview
    g_hLblPreviewCtrl = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 35, 162, 510, 22, hDlg, (HMENU)IDC_LBL_PREVIEW, NULL, NULL);
    SendMessageW(g_hLblPreviewCtrl, WM_SETFONT, (WPARAM)g_hDlgBoldFont, TRUE);

    // Group box: Print options
    HWND hGrpOpt = CreateWindowW(L"BUTTON", L"Print options", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 202, 545, 148, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hGrpOpt);
    SetWindowTheme(hGrpOpt, L"", L"");
    SetWindowSubclass(hGrpOpt, GroupBoxSubclassProc, 2, 0);

    // Reverse print checkbox
    HWND hChkReverse = CreateWindowW(L"BUTTON", L"Reverse print", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 226, 510, 22, hDlg, (HMENU)IDC_CHK_REVERSE, NULL, NULL);
    SetCtrlFont(hChkReverse);

    // Paper size:
    HWND hLblPaper = CreateWindowW(L"STATIC", L"Paper size:", WS_CHILD | WS_VISIBLE, 35, 258, 85, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblPaper);

    HWND hComboPaper = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 125, 255, 130, 150, hDlg, (HMENU)IDC_COMBO_PAPER, NULL, NULL);
    SetCtrlFont(hComboPaper);
    SetWindowSubclass(hComboPaper, FlatBorderSubclassProc, IDC_COMBO_PAPER, 0);
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, (LPARAM)L"A4 (210 x 297 mm)");
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, (LPARAM)L"Letter (8.5 x 11 in)");
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, (LPARAM)L"Legal");
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, (LPARAM)L"A3");
    SendMessageW(hComboPaper, CB_ADDSTRING, 0, (LPARAM)L"A5");
    SendMessageW(hComboPaper, CB_SETCURSEL, 0, 0); // Default A4

    // Print orientation
    HWND hLblOrient = CreateWindowW(L"STATIC", L"Print orientation", WS_CHILD | WS_VISIBLE, 275, 258, 110, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblOrient);

    // Page orientation ComboBox widened to 160px so text is never truncated
    HWND hComboOrient = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 390, 255, 160, 100, hDlg, (HMENU)IDC_COMBO_ORIENT, NULL, NULL);
    SetCtrlFont(hComboOrient);
    SetWindowSubclass(hComboOrient, FlatBorderSubclassProc, IDC_COMBO_ORIENT, 0);
    SendMessageW(hComboOrient, CB_ADDSTRING, 0, (LPARAM)L"Portrait orientation");
    SendMessageW(hComboOrient, CB_ADDSTRING, 0, (LPARAM)L"Landscape orientation");
    SendMessageW(hComboOrient, CB_SETCURSEL, 0, 0); // Default Portrait

    // Page per sheet
    HWND hLblNup = CreateWindowW(L"STATIC", L"Page per sheet", WS_CHILD | WS_VISIBLE, 35, 294, 85, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblNup);

    HWND hComboNup = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 125, 291, 130, 150, hDlg, (HMENU)IDC_COMBO_NUP, NULL, NULL);
    SetCtrlFont(hComboNup);
    SetWindowSubclass(hComboNup, FlatBorderSubclassProc, IDC_COMBO_NUP, 0);
    SendMessageW(hComboNup, CB_ADDSTRING, 0, (LPARAM)L"1 page per sheet");
    SendMessageW(hComboNup, CB_ADDSTRING, 0, (LPARAM)L"2 pages per sheet");
    SendMessageW(hComboNup, CB_ADDSTRING, 0, (LPARAM)L"4 pages per sheet (2x2)");
    SendMessageW(hComboNup, CB_ADDSTRING, 0, (LPARAM)L"8 pages per sheet");
    SendMessageW(hComboNup, CB_ADDSTRING, 0, (LPARAM)L"16 pages per sheet");
    SendMessageW(hComboNup, CB_SETCURSEL, 0, 0);

    // Copies
    HWND hLblCopies = CreateWindowW(L"STATIC", L"Copies", WS_CHILD | WS_VISIBLE, 275, 294, 110, 20, hDlg, NULL, NULL, NULL);
    SetCtrlFont(hLblCopies);

    HWND hEditCopies = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER, 390, 291, 70, 24, hDlg, (HMENU)IDC_EDIT_COPIES, NULL, NULL);
    SetCtrlFont(hEditCopies);
    SetWindowSubclass(hEditCopies, FlatBorderSubclassProc, IDC_EDIT_COPIES, 0);

    // Buttons: Print & Cancel
    HWND hBtnPrint = CreateWindowW(L"BUTTON", L"Print", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 350, 365, 105, 34, hDlg, (HMENU)IDC_BTN_PRINT, NULL, NULL);
    SetCtrlFont(hBtnPrint);

    HWND hBtnCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 465, 365, 95, 34, hDlg, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
    SetCtrlFont(hBtnCancel);

    // Apply dark/light theme to controls:
    // ComboBoxes & Edit boxes: DarkMode_CFD
    // RadioButtons & CheckBoxes: L"", L"" (UxTheme disabled for transparent & crisp text)
    ApplyPrintDialogControlsTheme(hDlg, isDark);

    UpdateLivePreview(hDlg);

    UpdateLivePreview(hDlg);

    // Modal message loop
    EnableWindow(hWndParent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hWndParent, TRUE);
    SetForegroundWindow(hWndParent);
    g_hLblPreviewCtrl = NULL;

    if (g_dialogResult) {
        outOptions = g_dialogOptions;
        return true;
    }
    return false;
}

bool PDFPrinter::ExecutePrint(HWND hWndParent, const PrintOptions& options) {
    PDFApp& app = PDFApp::Instance();
    if (!app.HasDocument()) return false;

    int totalDocPages = app.GetPageCount();
    if (totalDocPages <= 0) return false;

    std::vector<int> pagesToPrint = ComputeFinalPages(options, totalDocPages, app.GetSelectedPages());

    if (pagesToPrint.empty()) {
        MessageBoxW(hWndParent, L"No pages match the selected range and filter!", L"Print Notice", MB_OK | MB_ICONWARNING);
        return false;
    }

    HANDLE hPrinter = NULL;
    if (!OpenPrinterW((LPWSTR)options.printerName.c_str(), &hPrinter, NULL)) {
        MessageBoxW(hWndParent, L"Could not connect to the selected printer!", L"Print Error", MB_OK | MB_ICONERROR);
        return false;
    }

    LONG devModeSize = DocumentPropertiesW(hWndParent, hPrinter, (LPWSTR)options.printerName.c_str(), NULL, NULL, 0);
    std::vector<BYTE> devModeBuf(devModeSize);
    DEVMODEW* pDevMode = (DEVMODEW*)devModeBuf.data();
    DocumentPropertiesW(hWndParent, hPrinter, (LPWSTR)options.printerName.c_str(), pDevMode, NULL, DM_OUT_BUFFER);

    pDevMode->dmFields |= DM_PAPERSIZE | DM_ORIENTATION | DM_COPIES;
    pDevMode->dmPaperSize = options.paperSize;
    pDevMode->dmOrientation = (options.orientation == ORIENT_LANDSCAPE) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    pDevMode->dmCopies = (short)std::max(1, options.copies);

    DocumentPropertiesW(hWndParent, hPrinter, (LPWSTR)options.printerName.c_str(), pDevMode, pDevMode, DM_IN_BUFFER | DM_OUT_BUFFER);

    HDC hdcPrint = CreateDCW(L"WINSPOOL", options.printerName.c_str(), NULL, pDevMode);
    ClosePrinter(hPrinter);

    if (!hdcPrint) {
        MessageBoxW(hWndParent, L"Could not create Device Context for printer!", L"Print Error", MB_OK | MB_ICONERROR);
        return false;
    }

    DOCINFOW docInfo = { sizeof(DOCINFOW) };
    docInfo.lpszDocName = L"PDF Document - FeathrPDF";
    if (StartDocW(hdcPrint, &docInfo) <= 0) {
        DeleteDC(hdcPrint);
        MessageBoxW(hWndParent, L"Could not start print job on printer!", L"Print Error", MB_OK | MB_ICONERROR);
        return false;
    }

    int printableW = GetDeviceCaps(hdcPrint, HORZRES);
    int printableH = GetDeviceCaps(hdcPrint, VERTRES);

    int nup = options.pagesPerSheet;
    int cols = 1, rows = 1;
    if (nup == 2) {
        if (options.orientation == ORIENT_LANDSCAPE) { cols = 2; rows = 1; }
        else { cols = 1; rows = 2; }
    } else if (nup == 4) {
        cols = 2; rows = 2;
    } else if (nup == 8) {
        if (options.orientation == ORIENT_LANDSCAPE) { cols = 4; rows = 2; }
        else { cols = 2; rows = 4; }
    } else if (nup == 16) {
        cols = 4; rows = 4;
    }

    int slotW = printableW / cols;
    int slotH = printableH / rows;

    size_t totalPrintPages = pagesToPrint.size();
    size_t pageIdx = 0;

    while (pageIdx < totalPrintPages) {
        StartPage(hdcPrint);

        for (int r = 0; r < rows && pageIdx < totalPrintPages; ++r) {
            for (int c = 0; c < cols && pageIdx < totalPrintPages; ++c) {
                int curDocPage = pagesToPrint[pageIdx];

                int cellX = c * slotW;
                int cellY = r * slotH;

                int marginX = slotW / 30;
                int marginY = slotH / 30;
                int availW = slotW - 2 * marginX;
                int availH = slotH - 2 * marginY;

                const auto& pageInfos = app.GetPages();
                double ptW = 595.28, ptH = 841.89;
                if (curDocPage >= 0 && curDocPage < (int)pageInfos.size()) {
                    ptW = pageInfos[curDocPage].widthPt;
                    ptH = pageInfos[curDocPage].heightPt;
                }

                double scale = std::min((double)availW / ptW, (double)availH / ptH);
                int destW = (int)(ptW * scale);
                int destH = (int)(ptH * scale);
                int destX = cellX + marginX + (availW - destW) / 2;
                int destY = cellY + marginY + (availH - destH) / 2;

                app.RenderPageToDC(hdcPrint, curDocPage, destX, destY, destW, destH, true);
                pageIdx++;
            }
        }

        EndPage(hdcPrint);
    }

    EndDoc(hdcPrint);
    DeleteDC(hdcPrint);
    return true;
}

HWND PDFPrinter::GetPrintDialogHwnd() {
    return s_hPrintDlgInstance;
}

void PDFPrinter::UpdateDialogTheme(HWND hDlg, bool isDark) {
    if (!hDlg || !IsWindow(hDlg)) return;
    PDFApp::SetWindowTitlebarTheme(hDlg, isDark);
    PDFApp::ApplyUxThemeDarkMode(hDlg, isDark);
    ApplyPrintDialogControlsTheme(hDlg, isDark);
    RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
