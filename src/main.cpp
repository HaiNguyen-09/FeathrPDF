#include "app.h"
#include "ribbon.h"
#include "sidebar.h"
#include "canvas.h"
#include "printer.h"
#include "history.h"
#include "resource.h"
#include <shellapi.h>
#include <dwmapi.h>

static HWND g_hWndMain = NULL;
static HWND g_hRibbon = NULL;
static HWND g_hSidebar = NULL;
static HWND g_hCanvas = NULL;

static void LayoutChildren(HWND hWnd) {
    if (!g_hRibbon || !g_hCanvas) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    PDFApp& app = PDFApp::Instance();
    int ribbonH = RibbonBar::GetHeight(app.IsRibbonCollapsed());
    int sideW = app.IsSidebarVisible() ? 220 : 0;
    int bodyH = std::max(0, clientH - ribbonH);
    int canvasW = std::max(0, clientW - sideW);

    SetWindowPos(g_hRibbon, NULL, 0, 0, clientW, ribbonH, SWP_NOZORDER | SWP_NOACTIVATE);

    if (g_hSidebar) {
        if (sideW > 0) {
            SetWindowPos(g_hSidebar, NULL, 0, ribbonH, sideW, bodyH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(g_hSidebar, SW_HIDE);
        }
    }

    SetWindowPos(g_hCanvas, NULL, sideW, ribbonH, canvasW, bodyH, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // Prevent background erasing flicker on Intel Iris Xe

    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // Apply title bar theme according to user setting (Dark or Light Mode)
        bool isDark = SessionHistory::Instance().IsDarkMode();
        PDFApp::SetWindowTitlebarTheme(hWnd, isDark);
        PDFApp::ApplyUxThemeDarkMode(hWnd, isDark);

        RibbonBar::RegisterClass(hInst);
        NavigationSidebar::RegisterClass(hInst);
        ContinuousCanvas::RegisterClass(hInst);

        g_hRibbon = RibbonBar::Create(hWnd, hInst);
        g_hSidebar = NavigationSidebar::Create(hWnd, hInst);
        g_hCanvas = ContinuousCanvas::Create(hWnd, hInst);

        PDFApp::Instance().SetSubWindows(g_hRibbon, g_hSidebar, g_hCanvas);

        LayoutChildren(hWnd);
        DragAcceptFiles(hWnd, TRUE);
        return 0;
    }

    case WM_SIZE: {
        LayoutChildren(hWnd);
        return 0;
    }

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        PDFApp& app = PDFApp::Instance();
        HWND hSidebar = app.GetSidebarWindow();
        if (hSidebar && app.IsSidebarVisible() && IsWindowVisible(hSidebar)) {
            RECT rcSide;
            GetWindowRect(hSidebar, &rcSide);
            if (PtInRect(&rcSide, pt)) {
                SendMessageW(hSidebar, msg, wParam, lParam);
                return 0;
            }
        }
        HWND hCanvas = app.GetCanvasWindow();
        if (hCanvas && IsWindowVisible(hCanvas)) {
            RECT rcCanvas;
            GetWindowRect(hCanvas, &rcCanvas);
            if (PtInRect(&rcCanvas, pt)) {
                SendMessageW(hCanvas, msg, wParam, lParam);
                return 0;
            }
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        WCHAR szFile[MAX_PATH] = { 0 };
        if (DragQueryFileW(hDrop, 0, szFile, MAX_PATH)) {
            PDFApp::Instance().OpenDocument(szFile);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl) {
            switch (wParam) {
            case 'Z':
            case 'z':
                if (shift) {
                    PDFApp::Instance().Redo();
                } else {
                    PDFApp::Instance().Undo();
                }
                return 0;
            case 'Y':
            case 'y':
                PDFApp::Instance().Redo();
                return 0;
            case 'O':
            case 'o':
                SendMessageW(g_hRibbon, WM_COMMAND, MAKEWPARAM(IDC_RIBBON_BTN_OPEN, 0), 0);
                return 0;
            case 'S':
            case 's':
                PDFApp::Instance().SaveDocument();
                return 0;
            case 'P':
            case 'p':
                PDFApp::Instance().ShowPrintDialog();
                return 0;
            case 'A':
            case 'a':
                PDFApp::Instance().SelectAllPages();
                return 0;
            case '0':
                PDFApp::Instance().SetZoom(1.0f);
                return 0;
            case VK_ADD:
            case VK_OEM_PLUS: {
                HWND hCanvas = PDFApp::Instance().GetCanvasWindow();
                if (hCanvas) {
                    PDFApp::Instance().SetIsZooming(true);
                    SetTimer(hCanvas, 993, 150, NULL);
                }
                PDFApp::Instance().SetZoom(PDFApp::Instance().GetZoom() + 0.15f);
                return 0;
            }
            case VK_SUBTRACT:
            case VK_OEM_MINUS: {
                HWND hCanvas = PDFApp::Instance().GetCanvasWindow();
                if (hCanvas) {
                    PDFApp::Instance().SetIsZooming(true);
                    SetTimer(hCanvas, 993, 150, NULL);
                }
                PDFApp::Instance().SetZoom(PDFApp::Instance().GetZoom() - 0.15f);
                return 0;
            }
            }
        }
        break;
    }

    case WM_TIMER: {
        if (wParam == 998 /* TIMER_SAVED_NOTIFICATION */) {
            KillTimer(hWnd, 998);
            PDFApp::Instance().UpdateTitleBar();
            return 0;
        }
        break;
    }

    case WM_CLOSE: {
        PDFApp& app = PDFApp::Instance();
        app.SaveCurrentSession();
        if (app.IsModified()) {
            int ret = MessageBoxW(hWnd, L"The document has unsaved changes.\nDo you want to save changes before exiting?", L"Confirm Exit", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (ret == IDCANCEL) return 0;
            if (ret == IDYES) {
                if (!app.SaveDocument()) return 0; // Failed or cancelled save
            }
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY:
        PDFApp::Instance().SaveCurrentSession();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    if (!hIconBig) hIconBig = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));

    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!hIconSmall) hIconSmall = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FeathrPDFMainWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSmall;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    g_hWndMain = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"FeathrPDFMainWindowClass",
        L"FeathrPDF",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWndMain) {
        return 1;
    }

    // Set icons explicitly to ensure taskbar and title bar use app.ico (Resource ID 1)
    if (hIconBig) {
        SendMessageW(g_hWndMain, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    }
    if (hIconSmall) {
        SendMessageW(g_hWndMain, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }

    PDFApp::Instance().Initialize(hInstance, g_hWndMain);

    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

    // If file passed via command line (dragged onto exe, "Open with", or double-clicked from Explorer):
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && argv) {
        std::wstring targetPath = argv[1];
        if (targetPath.length() >= 2 && targetPath.front() == L'"' && targetPath.back() == L'"') {
            targetPath = targetPath.substr(1, targetPath.length() - 2);
        }
        if (!targetPath.empty()) {
            std::wstring ext = (targetPath.length() >= 4) ? targetPath.substr(targetPath.length() - 4) : L"";
            for (auto& c : ext) c = towlower(c);
            if (ext == L".pdf" || GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                PDFApp::Instance().OpenDocument(targetPath);
            }
        }
    }
    if (argv) LocalFree(argv);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_MOUSEWHEEL || msg.message == WM_MOUSEHWHEEL) {
            POINT pt = { (short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam) };
            PDFApp& app = PDFApp::Instance();
            HWND hSidebar = app.GetSidebarWindow();
            if (hSidebar && app.IsSidebarVisible() && IsWindowVisible(hSidebar)) {
                RECT rcSide;
                GetWindowRect(hSidebar, &rcSide);
                if (PtInRect(&rcSide, pt)) {
                    SendMessageW(hSidebar, msg.message, msg.wParam, msg.lParam);
                    continue;
                }
            }
        }

        if (msg.message == WM_KEYDOWN) {
            PDFApp& app = PDFApp::Instance();

            HWND hFocus = GetFocus();
            bool inEdit = false;
            if (hFocus) {
                WCHAR cls[64] = { 0 };
                GetClassNameW(hFocus, cls, 64);
                if (_wcsicmp(cls, L"EDIT") == 0) {
                    inEdit = true;
                }
            }
            if (!inEdit && msg.hwnd) {
                WCHAR cls[64] = { 0 };
                GetClassNameW(msg.hwnd, cls, 64);
                if (_wcsicmp(cls, L"EDIT") == 0) {
                    inEdit = true;
                }
            }

            if (inEdit) {
                if (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE) {
                    SetFocus(app.GetMainWindow());
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }

            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

            if (msg.wParam == VK_ESCAPE) {
                HWND hSidebar = app.GetSidebarWindow();
                if (hSidebar) {
                    SendMessageW(hSidebar, WM_KEYDOWN, VK_ESCAPE, 0);
                }
                continue;
            }

            // Alt + Up / Down: Move selected pages
            if (alt) {
                if (msg.wParam == VK_UP) {
                    app.MoveSelectedPagesUp();
                    continue;
                } else if (msg.wParam == VK_DOWN) {
                    app.MoveSelectedPagesDown();
                    continue;
                }
            }

            // Ctrl combinations
            if (ctrl) {
                if (msg.wParam == 'Z' || msg.wParam == 'z') {
                    if (shift) app.Redo(); else app.Undo();
                    continue;
                } else if (msg.wParam == 'Y' || msg.wParam == 'y') {
                    app.Redo();
                    continue;
                } else if (msg.wParam == 'O' || msg.wParam == 'o') {
                    SendMessageW(g_hRibbon, WM_COMMAND, MAKEWPARAM(IDC_RIBBON_BTN_OPEN, 0), 0);
                    continue;
                } else if (msg.wParam == 'S' || msg.wParam == 's') {
                    if (shift) app.SaveDocumentAs(); else app.SaveDocument();
                    continue;
                } else if (msg.wParam == 'P' || msg.wParam == 'p') {
                    app.ShowPrintDialog();
                    continue;
                } else if (msg.wParam == 'A' || msg.wParam == 'a') {
                    app.SelectAllPages();
                    continue;
                } else if (msg.wParam == 'I' || msg.wParam == 'i') {
                    app.AppendPdfFiles();
                    continue;
                } else if (msg.wParam == VK_HOME) {
                    app.GoToPage(1);
                    app.SelectPageSingle(0);
                    continue;
                } else if (msg.wParam == VK_END) {
                    if (app.GetPageCount() > 0) {
                        int last = app.GetPageCount() - 1;
                        app.GoToPage(last + 1);
                        app.SelectPageSingle(last);
                    }
                    continue;
                }
            }

            // Non-Ctrl shortcuts: Page Management & View Navigation & Tools
            if (msg.wParam == 'E' || msg.wParam == 'e') {
                if (app.GetTool() == TOOL_ERASER) {
                    app.SetTool(TOOL_PAN);
                } else {
                    app.SetTool(TOOL_ERASER);
                }
                continue;
            } else if (msg.wParam == VK_DELETE || msg.wParam == VK_BACK) {
                app.DeleteSelectedPages();
                continue;
            } else if (msg.wParam == VK_INSERT) {
                if (shift || ctrl) {
                    app.AppendPdfFiles();
                } else {
                    app.AddBlankPage(app.GetActivePageIndex());
                }
                continue;
            } else if (msg.wParam == VK_PRIOR) { // PageUp: Previous page
                app.PageUp();
                continue;
            } else if (msg.wParam == VK_NEXT) { // PageDown: Next page
                app.PageDown();
                continue;
            } else if (msg.wParam == VK_HOME) { // Home: Top edge of current page
                app.ScrollCurrentPageTop();
                continue;
            } else if (msg.wParam == VK_END) { // End: Bottom edge of current page
                app.ScrollCurrentPageBottom();
                continue;
            } else if (msg.wParam == VK_UP) { // Arrow Up: Smooth scroll up
                app.ScrollByDelta(0, -35);
                continue;
            } else if (msg.wParam == VK_DOWN) { // Arrow Down: Smooth scroll down
                app.ScrollByDelta(0, 35);
                continue;
            } else if (msg.wParam == VK_LEFT) { // Arrow Left: Smooth scroll left
                app.ScrollByDelta(-35, 0);
                continue;
            } else if (msg.wParam == VK_RIGHT) { // Arrow Right: Smooth scroll right
                app.ScrollByDelta(35, 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    PDFApp::Instance().Shutdown();
    return (int)msg.wParam;
}
