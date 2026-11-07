#include "app.h"
#include "ribbon.h"
#include "sidebar.h"
#include "canvas.h"
#include "printer.h"
#include "history.h"
#include "resource.h"
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <functional>

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return -1;
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

struct StreamingFileWriter : FPDF_FILEWRITE {
    HANDLE hFile;
};

static int WriteBlock(FPDF_FILEWRITE* pThis, const void* pData, unsigned long size) {
    if (!pThis || !pData || size == 0) return 1;
    StreamingFileWriter* writer = static_cast<StreamingFileWriter*>(pThis);
    DWORD written = 0;
    if (WriteFile(writer->hFile, pData, size, &written, NULL)) {
        return (written == size);
    }
    return 0;
}

static bool SaveDocToFileDirect(FPDF_DOCUMENT doc, const std::wstring& path) {
    if (!doc) return false;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    StreamingFileWriter writer;
    writer.version = 1;
    writer.WriteBlock = WriteBlock;
    writer.hFile = hFile;

    FPDF_BOOL ok = FPDF_SaveAsCopy(doc, &writer, 0);
    CloseHandle(hFile);
    return (ok != 0);
}

PDFApp::PDFApp()
    : m_hInstance(NULL)
    , m_hWndMain(NULL)
    , m_hWndRibbon(NULL)
    , m_hWndSidebar(NULL)
    , m_hWndCanvas(NULL)
    , m_nextDocId(0)
    , m_nextPageId(0)
    , m_nextAnnotId(0)
    , m_anchorPageIndex(0)
    , m_activePageIndex(0)
    , m_zoom(1.0f)
    , m_tool(TOOL_PAN)
    , m_isModified(false)
    , m_sidebarVisible(true)
    , m_ribbonCollapsed(false)
    , m_baseTotalHeight(0.0)
    , m_baseMaxDisplayWidth(0.0)
    , m_baseTotalWidth(0.0)
    , m_totalLayoutHeight(0)
    , m_totalLayoutWidth(0)
    , m_maxDisplayWidth(0)
    , m_isZooming(false)
    , m_isDarkMode(false)
    , m_gdiplusToken(0)
{
    m_penConfig = { RGB(220, 38, 38), 2.5f, 1.0f };
    m_highlightConfig = { RGB(255, 235, 59), 15.0f, 0.35f };
}

PDFApp::~PDFApp() {
    Shutdown();
}

bool PDFApp::Initialize(HINSTANCE hInstance, HWND hWndMain) {
    m_hInstance = hInstance;
    m_hWndMain = hWndMain;

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    // Initialize PDFium
    FPDF_InitLibrary();

    // Load reading session history & preferences
    SessionHistory::Instance().Load();
    m_isDarkMode = SessionHistory::Instance().IsDarkMode();
    SetDarkMode(m_isDarkMode);

    UpdateTitleBar();
    return true;
}

void PDFApp::InvalidateBasePageCache(int pageIdx) {
    if (pageIdx == -1) {
        for (auto& pair : m_basePageCache) {
            if (pair.second.hBitmap) {
                DeleteObject(pair.second.hBitmap);
            }
        }
        m_basePageCache.clear();
    } else {
        auto it = m_basePageCache.find(pageIdx);
        if (it != m_basePageCache.end()) {
            if (it->second.hBitmap) {
                DeleteObject(it->second.hBitmap);
            }
            m_basePageCache.erase(it);
        }
    }
}

void PDFApp::InvalidateAnnotCache(int pageIdx) {
    if (pageIdx == -1) {
        for (auto& pair : m_annotOverlayCache) {
            if (pair.second.hBitmap) {
                DeleteObject(pair.second.hBitmap);
            }
        }
        m_annotOverlayCache.clear();
    } else {
        auto it = m_annotOverlayCache.find(pageIdx);
        if (it != m_annotOverlayCache.end()) {
            if (it->second.hBitmap) {
                DeleteObject(it->second.hBitmap);
            }
            m_annotOverlayCache.erase(it);
        }
    }
}

void PDFApp::InvalidateD2DCache(int pageIdx) {
    if (pageIdx == -1) {
        for (auto& pair : m_d2dPageCache) {
            if (pair.second.pBitmap) {
                pair.second.pBitmap->Release();
            }
        }
        m_d2dPageCache.clear();
    } else {
        auto it = m_d2dPageCache.find(pageIdx);
        if (it != m_d2dPageCache.end()) {
            if (it->second.pBitmap) {
                it->second.pBitmap->Release();
            }
            m_d2dPageCache.erase(it);
        }
    }
}

void PDFApp::InvalidatePageCache(int pageIdx) {
    InvalidateBasePageCache(pageIdx);
    InvalidateAnnotCache(pageIdx);
    InvalidateD2DCache(pageIdx);
}

void PDFApp::SaveCurrentSession() {
    if (!m_filePath.empty() && HasDocument()) {
        int lastPage = m_activePageIndex + 1;
        int scrollY = ContinuousCanvas::GetScrollY();
        float zoom = m_zoom;
        SessionHistory::Instance().SaveSession(m_filePath, lastPage, scrollY, zoom);
    }
}

void PDFApp::Shutdown() {
    SaveCurrentSession();
    InvalidatePageCache(-1);
    CloseDocument();
    ClearUndoRedo();
    FPDF_DestroyLibrary();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

void PDFApp::SetSubWindows(HWND hRibbon, HWND hSidebar, HWND hCanvas) {
    m_hWndRibbon = hRibbon;
    m_hWndSidebar = hSidebar;
    m_hWndCanvas = hCanvas;

    if (m_hWndCanvas) {
        SetWindowTheme(m_hWndCanvas, m_isDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
    if (m_hWndSidebar) {
        SetWindowTheme(m_hWndSidebar, m_isDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
}

void PDFApp::ApplyUxThemeDarkMode(HWND hWnd, bool isDark) {
    typedef bool (WINAPI *fnAllowDarkModeForWindow)(HWND hWnd, bool allow);
    typedef void (WINAPI *fnRefreshImmersiveColorPolicyState)();
    typedef int (WINAPI *fnSetPreferredAppMode)(int mode); // 1 = AllowDark, 2 = ForceDark, 0 = Default

    HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxtheme) hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        // Ordinal 133: AllowDarkModeForWindow
        auto AllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        if (AllowDarkModeForWindow && hWnd) {
            AllowDarkModeForWindow(hWnd, isDark);
        }

        // Ordinal 135: SetPreferredAppMode (1 = AllowDark, 2 = ForceDark, 0 = Default)
        auto SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (SetPreferredAppMode) {
            SetPreferredAppMode(isDark ? 2 : 0);
        }

        // Ordinal 104: RefreshImmersiveColorPolicyState
        auto Refresh = (fnRefreshImmersiveColorPolicyState)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104));
        if (Refresh) {
            Refresh();
        }
    }

    if (hWnd) {
        SetWindowTheme(hWnd, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
}

void PDFApp::SetWindowTitlebarTheme(HWND hWnd, bool isDark) {
    if (!hWnd) return;
    BOOL useDark = isDark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark)))) {
        DwmSetWindowAttribute(hWnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &useDark, sizeof(useDark));
    }
    COLORREF captionColor = isDark ? RGB(26, 26, 28) : 0xFFFFFFFF;
    COLORREF captionText = isDark ? RGB(244, 244, 245) : 0xFFFFFFFF;
    DwmSetWindowAttribute(hWnd, 35 /* DWMWA_CAPTION_COLOR */, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hWnd, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof(captionText));
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void PDFApp::SetDarkMode(bool dark) {
    m_isDarkMode = dark;
    SessionHistory::Instance().SetDarkMode(dark);

    if (m_hWndMain) {
        SetWindowTitlebarTheme(m_hWndMain, m_isDarkMode);
        ApplyUxThemeDarkMode(m_hWndMain, m_isDarkMode);
        if (m_hWndRibbon) ApplyUxThemeDarkMode(m_hWndRibbon, m_isDarkMode);
        if (m_hWndSidebar) ApplyUxThemeDarkMode(m_hWndSidebar, m_isDarkMode);
        if (m_hWndCanvas) ApplyUxThemeDarkMode(m_hWndCanvas, m_isDarkMode);
    }

    HWND hPrint = PDFPrinter::GetPrintDialogHwnd();
    if (hPrint && IsWindow(hPrint)) {
        PDFPrinter::UpdateDialogTheme(hPrint, m_isDarkMode);
    }

    // Apply dark scrollbars to Canvas and Sidebar
    if (m_hWndCanvas) {
        SetWindowTheme(m_hWndCanvas, m_isDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
    if (m_hWndSidebar) {
        SetWindowTheme(m_hWndSidebar, m_isDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }

    // Instant repaint without modifying or re-rendering PDF documents (< 5ms)
    // RedrawWindow with RDW_ALLCHILDREN ensures all buttons, controls, and labels repaint immediately!
    if (m_hWndRibbon) {
        RibbonBar::UpdateUI(m_hWndRibbon);
        RedrawWindow(m_hWndRibbon, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (m_hWndSidebar) {
        RedrawWindow(m_hWndSidebar, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (m_hWndCanvas) {
        RedrawWindow(m_hWndCanvas, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void PDFApp::ToggleDarkMode() {
    SetDarkMode(!m_isDarkMode);
}

void PDFApp::UpdateTitleBar() {
    if (!m_hWndMain) return;
    KillTimer(m_hWndMain, 998); // Cancel any active saved notification timer
    std::wstring title = L"Feathr PDF";
    if (HasDocument()) {
        if (m_filePath.empty()) {
            title = L"New Document* - Feathr PDF";
        } else {
            size_t slash = m_filePath.find_last_of(L"\\/");
            std::wstring fname = (slash != std::wstring::npos) ? m_filePath.substr(slash + 1) : m_filePath;
            title = fname + (m_isModified ? L"* - Feathr PDF" : L" - Feathr PDF");
        }
    }
    SetWindowTextW(m_hWndMain, title.c_str());
}

void PDFApp::ShowSavedNotificationOnTitleBar() {
    if (!m_hWndMain) return;
    std::wstring fname = L"Document.pdf";
    if (!m_filePath.empty()) {
        size_t slash = m_filePath.find_last_of(L"\\/");
        fname = (slash != std::wstring::npos) ? m_filePath.substr(slash + 1) : m_filePath;
    }
    std::wstring savedTitle = fname + L" - Feathr PDF - Saved";
    SetWindowTextW(m_hWndMain, savedTitle.c_str());

    // Thiết lập Timer 10 giây (10000ms) để tự động khôi phục tiêu đề chuẩn
    SetTimer(m_hWndMain, 998, 10000, NULL);
}

void PDFApp::RefreshAllViews() {
    UpdateTitleBar();
    if (m_hWndRibbon) {
        RibbonBar::UpdateUI(m_hWndRibbon);
        RibbonBar::SetZoomSlider(m_hWndRibbon, (int)(m_zoom * 100.0f));
        RibbonBar::SetPageInfo(m_hWndRibbon, m_activePageIndex + 1, GetPageCount(), GetSelectedCount());
    }

    if (m_hWndCanvas) {
        ContinuousCanvas::UpdateScroll(m_hWndCanvas);
        InvalidateRect(m_hWndCanvas, NULL, TRUE);
        UpdateWindow(m_hWndCanvas);
    }
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::InvalidateThumbnails(m_hWndSidebar);
        UpdateWindow(m_hWndSidebar);
    }
}

void PDFApp::ClearPages() {
    m_pages.clear();
    m_selectedPages.clear();
    m_anchorPageIndex = 0;
    m_activePageIndex = 0;
    m_baseTotalHeight = 0.0;
    m_baseMaxDisplayWidth = 0.0;
    m_baseTotalWidth = 0.0;
    m_totalLayoutHeight = 0;
    m_totalLayoutWidth = 0;
    m_maxDisplayWidth = 0;
    m_isZooming = false;
}

void PDFApp::UpdateLayoutFromActivePages() {
    m_pages.clear();
    double curY = 20.0;
    double maxW = 0.0;
    for (size_t i = 0; i < m_activePages.size(); ++i) {
        const auto& item = m_activePages[i];
        PageInfo info;
        info.widthPt = item.widthPt;
        info.heightPt = item.heightPt;
        info.baseLayoutTop = curY;
        info.baseLayoutBottom = curY + item.heightPt;
        info.layoutTop = 0;
        info.layoutBottom = 0;
        info.displayW = 0;
        info.displayH = 0;
        if (item.widthPt > maxW) maxW = item.widthPt;
        curY = info.baseLayoutBottom + 16.0;
        m_pages.push_back(info);
    }
    m_baseTotalHeight = curY + 20.0;
    m_baseMaxDisplayWidth = maxW;
    m_baseTotalWidth = maxW + 40.0;

    int count = (int)m_activePages.size();
    if (m_activePageIndex >= count) {
        m_activePageIndex = std::max(0, count - 1);
    }
    if (m_anchorPageIndex >= count) {
        m_anchorPageIndex = std::max(0, count - 1);
    }

    std::set<int> validSel;
    for (int s : m_selectedPages) {
        if (s >= 0 && s < count) validSel.insert(s);
    }
    m_selectedPages = validSel;
    if (m_selectedPages.empty() && count > 0) {
        m_selectedPages.insert(std::max(0, std::min(count - 1, m_activePageIndex)));
    }

    if (m_hWndCanvas) {
        RECT rc;
        GetClientRect(m_hWndCanvas, &rc);
        RecalculateLayout(rc.right - rc.left);
        ContinuousCanvas::UpdateScroll(m_hWndCanvas);
    }
}

void PDFApp::RecalculateLayout(int canvasWidth) {
    m_maxDisplayWidth = (int)std::round(m_baseMaxDisplayWidth * m_zoom);
    m_totalLayoutWidth = (int)std::round(m_baseTotalWidth * m_zoom);
    for (size_t i = 0; i < m_pages.size(); ++i) {
        m_pages[i].displayW = (int)std::round(m_pages[i].widthPt * m_zoom);
        m_pages[i].displayH = (int)std::round(m_pages[i].heightPt * m_zoom);
        m_pages[i].layoutTop = (int)std::round(m_pages[i].baseLayoutTop * m_zoom);
        m_pages[i].layoutBottom = m_pages[i].layoutTop + m_pages[i].displayH;
    }
    m_totalLayoutHeight = (int)std::round(m_baseTotalHeight * m_zoom);
}

void PDFApp::ClearUndoRedo() {
    m_undoStack.clear();
    m_redoStack.clear();
}

void PDFApp::RecordStateBeforeEdit() {
    if (m_activePages.empty()) return;

    m_undoStack.push_back({ m_activePages, m_activePageIndex, m_selectedPages });
    if (m_undoStack.size() > 50) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void PDFApp::CommitStateAfterEdit() {
    m_isModified = (m_activePages != m_savedPages);
    UpdateTitleBar();
    if (m_hWndRibbon) {
        RibbonBar::UpdateUI(m_hWndRibbon);
    }
}

bool PDFApp::Undo() {
    if (!CanUndo()) return false;

    m_redoStack.push_back({ m_activePages, m_activePageIndex, m_selectedPages });
    AppState prevState = m_undoStack.back();
    m_undoStack.pop_back();

    bool structureChanged = (m_activePages.size() != prevState.activePages.size());
    if (!structureChanged) {
        for (size_t i = 0; i < m_activePages.size(); ++i) {
            if (m_activePages[i].id != prevState.activePages[i].id ||
                m_activePages[i].source_file_id != prevState.activePages[i].source_file_id ||
                m_activePages[i].original_page_index != prevState.activePages[i].original_page_index ||
                std::abs(m_activePages[i].widthPt - prevState.activePages[i].widthPt) > 0.01 ||
                std::abs(m_activePages[i].heightPt - prevState.activePages[i].heightPt) > 0.01) {
                structureChanged = true;
                break;
            }
        }
    }

    if (!structureChanged) {
        // Fast path: Only annotations or page selection changed!
        std::set<int> changedPages;
        for (size_t i = 0; i < m_activePages.size(); ++i) {
            if (m_activePages[i].annotations != prevState.activePages[i].annotations) {
                changedPages.insert((int)i);
            }
        }

        m_activePages = prevState.activePages;
        m_activePageIndex = std::min(prevState.activePageIndex, (int)m_activePages.size() - 1);
        m_selectedPages = prevState.selectedPages;

        // Invalidate ONLY Layer 2 (Annotation Overlay) for changed pages!
        // Base PDF Surface (Layer 1) remains 100% in RAM!
        for (int pIdx : changedPages) {
            InvalidateAnnotCache(pIdx);
            if (m_hWndSidebar && m_sidebarVisible) {
                NavigationSidebar::InvalidatePageThumbnail(m_hWndSidebar, pIdx);
            }
        }

        m_isModified = (m_activePages != m_savedPages);
        UpdateTitleBar();

        if (m_hWndRibbon) {
            RibbonBar::UpdateUI(m_hWndRibbon);
        }
        if (m_hWndCanvas) {
            InvalidateRect(m_hWndCanvas, NULL, FALSE);
            UpdateWindow(m_hWndCanvas);
        }
        return true;
    }

    // Full structural Undo (page added, deleted, reordered)
    m_activePages = prevState.activePages;
    m_activePageIndex = std::min(prevState.activePageIndex, (int)m_activePages.size() - 1);
    m_selectedPages = prevState.selectedPages;

    InvalidatePageCache(-1);
    NavigationSidebar::ClearCache();
    UpdateLayoutFromActivePages();

    m_isModified = (m_activePages != m_savedPages);
    UpdateTitleBar();
    RefreshAllViews();
    return true;
}

bool PDFApp::Redo() {
    if (!CanRedo()) return false;

    m_undoStack.push_back({ m_activePages, m_activePageIndex, m_selectedPages });
    AppState nextState = m_redoStack.back();
    m_redoStack.pop_back();

    bool structureChanged = (m_activePages.size() != nextState.activePages.size());
    if (!structureChanged) {
        for (size_t i = 0; i < m_activePages.size(); ++i) {
            if (m_activePages[i].id != nextState.activePages[i].id ||
                m_activePages[i].source_file_id != nextState.activePages[i].source_file_id ||
                m_activePages[i].original_page_index != nextState.activePages[i].original_page_index ||
                std::abs(m_activePages[i].widthPt - nextState.activePages[i].widthPt) > 0.01 ||
                std::abs(m_activePages[i].heightPt - nextState.activePages[i].heightPt) > 0.01) {
                structureChanged = true;
                break;
            }
        }
    }

    if (!structureChanged) {
        // Fast path: Only annotations or page selection changed!
        std::set<int> changedPages;
        for (size_t i = 0; i < m_activePages.size(); ++i) {
            if (m_activePages[i].annotations != nextState.activePages[i].annotations) {
                changedPages.insert((int)i);
            }
        }

        m_activePages = nextState.activePages;
        m_activePageIndex = std::min(nextState.activePageIndex, (int)m_activePages.size() - 1);
        m_selectedPages = nextState.selectedPages;

        // Invalidate ONLY Layer 2 (Annotation Overlay) for changed pages!
        // Base PDF Surface (Layer 1) remains 100% in RAM!
        for (int pIdx : changedPages) {
            InvalidateAnnotCache(pIdx);
            if (m_hWndSidebar && m_sidebarVisible) {
                NavigationSidebar::InvalidatePageThumbnail(m_hWndSidebar, pIdx);
            }
        }

        m_isModified = (m_activePages != m_savedPages);
        UpdateTitleBar();

        if (m_hWndRibbon) {
            RibbonBar::UpdateUI(m_hWndRibbon);
        }
        if (m_hWndCanvas) {
            InvalidateRect(m_hWndCanvas, NULL, FALSE);
            UpdateWindow(m_hWndCanvas);
        }
        return true;
    }

    // Full structural Redo
    m_activePages = nextState.activePages;
    m_activePageIndex = std::min(nextState.activePageIndex, (int)m_activePages.size() - 1);
    m_selectedPages = nextState.selectedPages;

    InvalidatePageCache(-1);
    NavigationSidebar::ClearCache();
    UpdateLayoutFromActivePages();

    m_isModified = (m_activePages != m_savedPages);
    UpdateTitleBar();
    RefreshAllViews();
    return true;
}

bool PDFApp::OpenDocument(const std::wstring& filePath) {
    SaveCurrentSession();
    CloseDocument();

    int len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string u8(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &u8[0], len, NULL, NULL);

    FPDF_DOCUMENT doc = FPDF_LoadDocument(u8.c_str(), NULL);
    if (!doc) {
        MessageBoxW(m_hWndMain, L"Cannot open PDF file! The file may be damaged or password protected.", L"Open File Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_filePath = filePath;
    int pageCount = FPDF_GetPageCount(doc);
    int docId = ++m_nextDocId;
    m_docStore[docId] = { docId, filePath, doc, pageCount };

    // Lazy Page Metric Parsing:
    // Only load page 0 as standard sample page for scroll layout calculation (prevents memory spike)
    double defaultW = 595.28, defaultH = 841.89; // Standard A4 points
    if (pageCount > 0) {
        FPDF_PAGE p0 = FPDF_LoadPage(doc, 0);
        if (p0) {
            defaultW = FPDF_GetPageWidth(p0);
            defaultH = FPDF_GetPageHeight(p0);
            FPDF_ClosePage(p0);
        }
    }

    m_activePages.clear();
    m_activePages.reserve(pageCount);
    for (int i = 0; i < pageCount; ++i) {
        PageItem item;
        item.id = ++m_nextPageId;
        item.source_file_id = docId;
        item.original_page_index = i;
        item.is_blank_page = false;
        item.widthPt = defaultW;
        item.heightPt = defaultH;
        m_activePages.push_back(item);
    }

    m_savedPages = m_activePages;
    m_isModified = false;

    // Check reading history
    SessionItem session;
    bool hasHistory = SessionHistory::Instance().GetSession(filePath, session);

    bool shouldRestore = false;
    if (hasHistory && session.lastPage > 1 && session.lastPage <= (int)m_activePages.size()) {
        int zoomPercent = (int)std::round(session.zoom * 100.0f);
        if (zoomPercent < 25 || zoomPercent > 600) zoomPercent = 100;

        std::wstring prompt = L"You were previously reading at Page " + std::to_wstring(session.lastPage) + 
            L" (" + std::to_wstring(zoomPercent) + L"%). Would you like to resume from this page?";

        int ret = MessageBoxW(m_hWndMain, prompt.c_str(), L"Resume Reading?", MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            shouldRestore = true;
        }
    }

    if (shouldRestore) {
        if (session.zoom >= 0.25f && session.zoom <= 6.0f) {
            m_zoom = session.zoom;
        } else {
            m_zoom = 1.0f;
        }
        UpdateLayoutFromActivePages();

        int targetPage = session.lastPage - 1;
        SelectPageSingle(targetPage);

        if (session.scrollY > 0) {
            ContinuousCanvas::SetScroll(m_hWndCanvas, 0, session.scrollY);
        } else {
            ContinuousCanvas::ScrollToPage(m_hWndCanvas, targetPage);
        }

        ClearUndoRedo();
        RefreshAllViews();

        if (m_hWndSidebar && m_sidebarVisible) {
            NavigationSidebar::ScrollToPage(m_hWndSidebar, targetPage);
        }
    } else {
        m_zoom = 1.0f;
        UpdateLayoutFromActivePages();
        SelectPageSingle(0);
        ContinuousCanvas::ScrollToPage(m_hWndCanvas, 0);
        ClearUndoRedo();
        RefreshAllViews();

        if (m_hWndSidebar && m_sidebarVisible) {
            NavigationSidebar::ScrollToPage(m_hWndSidebar, 0);
        }
    }

    SaveCurrentSession();

    // Post-Operation Memory Trimming: immediately drop RAM to floor
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    return true;
}

bool PDFApp::NewBlankDocument() {
    CloseDocument();

    PageItem item;
    item.id = ++m_nextPageId;
    item.source_file_id = 0;
    item.original_page_index = -1;
    item.is_blank_page = true;
    item.widthPt = 595.28;
    item.heightPt = 841.89;
    m_activePages.push_back(item);

    m_filePath = L"";
    m_savedPages = m_activePages;
    m_isModified = true;
    m_zoom = 1.0f;

    UpdateLayoutFromActivePages();
    SelectPageSingle(0);
    ContinuousCanvas::ResetScroll();
    ClearUndoRedo();
    RefreshAllViews();
    return true;
}

bool PDFApp::SaveDocument(const std::wstring& filePath) {
    if (m_activePages.empty()) return false;

    std::wstring savePath = filePath.empty() ? m_filePath : filePath;
    if (savePath.empty()) {
        return SaveDocumentAs();
    }

    // Create destination PDF document
    FPDF_DOCUMENT destDoc = FPDF_CreateNewDocument();
    if (!destDoc) {
        MessageBoxW(m_hWndMain, L"Failed to initialize destination document for saving!", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }

    for (size_t i = 0; i < m_activePages.size(); ++i) {
        const PageItem& item = m_activePages[i];
        FPDF_PAGE p = NULL;
        if (item.is_blank_page) {
            p = FPDFPage_New(destDoc, (int)i, item.widthPt, item.heightPt);
        } else {
            auto itDoc = m_docStore.find(item.source_file_id);
            if (itDoc != m_docStore.end() && itDoc->second.doc) {
                int pageIdx = item.original_page_index;
                FPDF_ImportPagesByIndex(destDoc, itDoc->second.doc, &pageIdx, 1, (int)i);
                p = FPDF_LoadPage(destDoc, (int)i);
            } else {
                p = FPDFPage_New(destDoc, (int)i, item.widthPt, item.heightPt);
            }
        }

        if (p) {
            if (!item.annotations.empty()) {
                for (const auto& a : item.annotations) {
                    if (!a.is_active) continue;
                    int r = GetRValue(a.color);
                    int gVal = GetGValue(a.color);
                    int b = GetBValue(a.color);
                    int alpha = std::clamp((int)(a.opacity * 255.0f), 10, 255);

                    if (a.type == ANNOT_HIGHLIGHT) {
                        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(p, FPDF_ANNOT_HIGHLIGHT);
                        if (annot) {
                            FS_RECTF rect = { a.rectX1, a.rectY1, a.rectX2, a.rectY2 };
                            FPDFAnnot_SetRect(annot, &rect);
                            FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, r, gVal, b, alpha);
                            FS_QUADPOINTSF quad = { a.rectX1, a.rectY2, a.rectX2, a.rectY2, a.rectX1, a.rectY1, a.rectX2, a.rectY1 };
                            FPDFAnnot_AppendAttachmentPoints(annot, &quad);
                            FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
                            FPDFPage_CloseAnnot(annot);
                        }
                        FPDF_PAGEOBJECT rObj = FPDFPageObj_CreateNewRect(a.rectX1, a.rectY1, a.rectX2 - a.rectX1, a.rectY2 - a.rectY1);
                        if (rObj) {
                            FPDFPageObj_SetFillColor(rObj, r, gVal, b, (int)(alpha * 0.7f));
                            FPDFPath_SetDrawMode(rObj, 1, 0);
                            FPDFPage_InsertObject(p, rObj);
                        }
                    } else if (a.type == ANNOT_INK && a.points.size() > 1) {
                        FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(a.points[0].x, a.points[0].y);
                        if (path) {
                            for (size_t k = 1; k < a.points.size(); ++k) {
                                FPDFPath_LineTo(path, a.points[k].x, a.points[k].y);
                            }
                            FPDFPageObj_SetStrokeColor(path, r, gVal, b, alpha);
                            FPDFPageObj_SetStrokeWidth(path, a.strokeWidth > 0 ? a.strokeWidth : 2.5f);
                            FPDFPath_SetDrawMode(path, 0, 1);
                            FPDFPage_InsertObject(p, path);
                        }
                    }
                }
                FPDFPage_GenerateContent(p);
            } else if (item.is_blank_page) {
                FPDFPage_GenerateContent(p);
            }
            FPDF_ClosePage(p);
        }
    }

    WCHAR tempPath[MAX_PATH];
    WCHAR tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"pdf", 0, tempFile);

    HANDLE hFile = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        FPDF_CloseDocument(destDoc);
        MessageBoxW(m_hWndMain, L"Failed to create temporary file while saving!", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }

    StreamingFileWriter writer;
    writer.version = 1;
    writer.WriteBlock = WriteBlock;
    writer.hFile = hFile;

    FPDF_BOOL ok = FPDF_SaveAsCopy(destDoc, &writer, 0);
    CloseHandle(hFile);
    FPDF_CloseDocument(destDoc);

    if (!ok) {
        DeleteFileW(tempFile);
        MessageBoxW(m_hWndMain, L"Failed to save PDF document!", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Close any source documents locking savePath
    for (auto& pair : m_docStore) {
        if (pair.second.doc) {
            FPDF_CloseDocument(pair.second.doc);
            pair.second.doc = NULL;
        }
    }
    m_docStore.clear();

    CopyFileW(tempFile, savePath.c_str(), FALSE);
    DeleteFileW(tempFile);

    // Reopen the saved document as the single primary source document
    m_filePath = savePath;
    int len = WideCharToMultiByte(CP_UTF8, 0, m_filePath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string u8(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, m_filePath.c_str(), -1, &u8[0], len, NULL, NULL);

    FPDF_DOCUMENT newDoc = FPDF_LoadDocument(u8.c_str(), NULL);
    if (!newDoc) {
        MessageBoxW(m_hWndMain, L"Failed to reload document after saving!", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    int newPageCount = FPDF_GetPageCount(newDoc);
    int newDocId = ++m_nextDocId;
    m_docStore[newDocId] = { newDocId, m_filePath, newDoc, newPageCount };

    // Lazy Metric Parsing on re-opened doc:
    double defaultW = 595.28, defaultH = 841.89;
    if (newPageCount > 0) {
        FPDF_PAGE p0 = FPDF_LoadPage(newDoc, 0);
        if (p0) {
            defaultW = FPDF_GetPageWidth(p0);
            defaultH = FPDF_GetPageHeight(p0);
            FPDF_ClosePage(p0);
        }
    }

    m_activePages.clear();
    m_activePages.reserve(newPageCount);
    for (int i = 0; i < newPageCount; ++i) {
        PageItem item;
        item.id = ++m_nextPageId;
        item.source_file_id = newDocId;
        item.original_page_index = i;
        item.is_blank_page = false;
        item.widthPt = defaultW;
        item.heightPt = defaultH;
        m_activePages.push_back(item);
    }

    m_savedPages = m_activePages;
    m_isModified = false;
    ClearUndoRedo();

    InvalidatePageCache(-1);
    UpdateLayoutFromActivePages();
    RefreshAllViews();

    // Hiển thị thông báo Saved trên Titlebar trong 10 giây
    ShowSavedNotificationOnTitleBar();

    // Post-Operation Memory Trimming: release all temp working set memory
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    return true;
}

bool PDFApp::SaveDocumentAs() {
    if (m_activePages.empty()) return false;

    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    WCHAR szFile[MAX_PATH] = { 0 }; // Không copy tên file hiện tại vào szFile (đặt szFile[0] = L'\0')
    ofn.hwndOwner = m_hWndMain;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PDF Document (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        std::wstring targetPath = szFile;
        // Kiểm tra nếu chuỗi đường dẫn chưa kết thúc bằng ".pdf" (không phân biệt hoa thường)
        bool endsWithPdf = false;
        if (targetPath.length() >= 4) {
            std::wstring ext = targetPath.substr(targetPath.length() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".pdf") {
                endsWithPdf = true;
            }
        }
        if (!endsWithPdf) {
            // Kiểm tra xem có extension nào ở cuối không (dấu chấm sau dấu gạch chéo cuối cùng)
            size_t lastSlash = targetPath.find_last_of(L"\\/");
            size_t lastDot = targetPath.find_last_of(L".");
            if (lastDot == std::wstring::npos || (lastSlash != std::wstring::npos && lastDot < lastSlash)) {
                // Người dùng không gõ đuôi -> tự động thêm .pdf vào cuối
                targetPath += L".pdf";
            }
        }
        return SaveDocument(targetPath);
    }
    return false;
}

void PDFApp::CloseDocument() {
    SaveCurrentSession();
    InvalidatePageCache(-1);
    for (auto& pair : m_docStore) {
        if (pair.second.doc) {
            FPDF_CloseDocument(pair.second.doc);
            pair.second.doc = NULL;
        }
    }
    m_docStore.clear();
    m_activePages.clear();
    m_savedPages.clear();
    ClearUndoRedo();
    m_filePath = L"";
    m_isModified = false;
    ClearPages();
    RefreshAllViews();
    UpdateTitleBar();
    if (m_hWndRibbon) {
        RibbonBar::UpdateUI(m_hWndRibbon);
    }
}

void PDFApp::AddBlankPage(int afterIndex) {
    if (m_activePages.empty()) {
        NewBlankDocument();
        return;
    }

    RecordStateBeforeEdit();
    InvalidatePageCache(-1);

    int insertIdx = (afterIndex >= 0) ? afterIndex + 1 : (m_activePageIndex + 1);
    insertIdx = std::max(0, std::min((int)m_activePages.size(), insertIdx));

    PageItem item;
    item.id = ++m_nextPageId;
    item.source_file_id = 0;
    item.original_page_index = -1;
    item.is_blank_page = true;
    item.widthPt = 595.28;
    item.heightPt = 841.89;

    m_activePages.insert(m_activePages.begin() + insertIdx, item);
    m_isModified = true;

    UpdateLayoutFromActivePages();
    SelectPageSingle(insertIdx);
    GoToPage(insertIdx + 1);
    CommitStateAfterEdit();
    RefreshAllViews();
}

void PDFApp::AppendPdfFiles() {
    if (m_activePages.empty()) {
        OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
        WCHAR szFile[MAX_PATH] = { 0 };
        ofn.hwndOwner = m_hWndMain;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            OpenDocument(szFile);
        }
        return;
    }

    std::vector<WCHAR> buffer(65536, 0);
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hWndMain;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Select one or more PDF files to append...";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    std::vector<std::wstring> filesToAppend;
    const WCHAR* p = buffer.data();
    std::wstring firstPath = p;
    p += firstPath.length() + 1;

    if (*p == L'\0') {
        filesToAppend.push_back(firstPath);
    } else {
        std::wstring dir = firstPath;
        while (*p != L'\0') {
            std::wstring filename = p;
            std::wstring fullPath = dir + L"\\" + filename;
            filesToAppend.push_back(fullPath);
            p += filename.length() + 1;
        }
    }

    if (filesToAppend.empty()) return;

    RecordStateBeforeEdit();

    int totalAppendedPages = 0;
    for (const auto& filePath : filesToAppend) {
        int len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
        std::string u8(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &u8[0], len, NULL, NULL);

        FPDF_DOCUMENT srcDoc = FPDF_LoadDocument(u8.c_str(), NULL);
        if (!srcDoc) continue;

        int srcCount = FPDF_GetPageCount(srcDoc);
        if (srcCount > 0) {
            int docId = ++m_nextDocId;
            m_docStore[docId] = { docId, filePath, srcDoc, srcCount };

            double defaultW = 595.28, defaultH = 841.89;
            FPDF_PAGE p0 = FPDF_LoadPage(srcDoc, 0);
            if (p0) {
                defaultW = FPDF_GetPageWidth(p0);
                defaultH = FPDF_GetPageHeight(p0);
                FPDF_ClosePage(p0);
            }

            for (int i = 0; i < srcCount; ++i) {
                PageItem item;
                item.id = ++m_nextPageId;
                item.source_file_id = docId;
                item.original_page_index = i;
                item.is_blank_page = false;
                item.widthPt = defaultW;
                item.heightPt = defaultH;
                m_activePages.push_back(item);
            }
            totalAppendedPages += srcCount;
        } else {
            FPDF_CloseDocument(srcDoc);
        }
    }

    if (totalAppendedPages == 0) {
        MessageBoxW(m_hWndMain, L"Failed to insert pages from the selected files!", L"Notice", MB_OK | MB_ICONWARNING);
        return;
    }

    m_isModified = true;
    UpdateLayoutFromActivePages();
    CommitStateAfterEdit();
    RefreshAllViews();

    // Post-Operation Memory Trimming:
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    std::wstring msg = L"Successfully appended " + std::to_wstring(totalAppendedPages) + 
        L" new page(s) to the document!\nTotal pages: " + std::to_wstring(GetPageCount());
    MessageBoxW(m_hWndMain, msg.c_str(), L"Pages Appended", MB_OK | MB_ICONINFORMATION);
}

void PDFApp::DeleteSelectedPages() {
    if (m_activePages.empty()) return;
    if (m_selectedPages.empty()) {
        if (m_activePageIndex >= 0 && m_activePageIndex < (int)m_activePages.size()) {
            m_selectedPages.insert(m_activePageIndex);
        } else {
            return;
        }
    }

    int selCount = (int)m_selectedPages.size();
    if (selCount <= 0) return;

    if (selCount > 1) {
        std::wstring confirmMsg = L"Are you sure you want to delete the selected " + std::to_wstring(selCount) + L" page(s)?";
        if (MessageBoxW(m_hWndMain, confirmMsg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
    }

    RecordStateBeforeEdit();
    InvalidatePageCache(-1);

    std::vector<PageItem> remaining;
    remaining.reserve(m_activePages.size());
    for (size_t i = 0; i < m_activePages.size(); ++i) {
        if (!m_selectedPages.count((int)i)) {
            remaining.push_back(m_activePages[i]);
        }
    }

    if (remaining.empty()) {
        PageItem item;
        item.id = ++m_nextPageId;
        item.source_file_id = 0;
        item.original_page_index = -1;
        item.is_blank_page = true;
        item.widthPt = 595.28;
        item.heightPt = 841.89;
        remaining.push_back(item);
    }

    m_activePages = remaining;
    m_isModified = true;

    UpdateLayoutFromActivePages();
    SelectPageSingle(std::min(m_activePageIndex, (int)m_activePages.size() - 1));
    CommitStateAfterEdit();
    RefreshAllViews();
}

void PDFApp::MovePage(int srcIndex, int destIndex) {
    if (m_activePages.empty()) return;
    int total = GetPageCount();
    if (srcIndex < 0 || srcIndex >= total || destIndex < 0 || destIndex > total) return;
    SelectPageSingle(srcIndex);
    MoveSelectedPages(destIndex);
}

bool PDFApp::MoveSelectedPages(int destIndex) {
    if (m_activePages.empty() || m_selectedPages.empty()) return false;
    int total = (int)m_activePages.size();
    if (destIndex < 0 || destIndex > total) return false;

    std::vector<int> selIndices(m_selectedPages.begin(), m_selectedPages.end());

    int countSelBefore = 0;
    for (int s : selIndices) {
        if (s < destIndex) countSelBefore++;
    }
    int insertPos = destIndex - countSelBefore;

    bool isNoOp = true;
    for (size_t k = 0; k < selIndices.size(); ++k) {
        if (selIndices[k] != insertPos + (int)k) {
            isNoOp = false;
            break;
        }
    }
    if (isNoOp) return false;

    RecordStateBeforeEdit();
    InvalidatePageCache(-1);

    std::vector<PageItem> selectedItems;
    std::vector<PageItem> unselectedItems;
    for (size_t i = 0; i < m_activePages.size(); ++i) {
        if (m_selectedPages.count((int)i)) {
            selectedItems.push_back(m_activePages[i]);
        } else {
            unselectedItems.push_back(m_activePages[i]);
        }
    }

    insertPos = std::max(0, std::min((int)unselectedItems.size(), insertPos));
    unselectedItems.insert(unselectedItems.begin() + insertPos, selectedItems.begin(), selectedItems.end());
    m_activePages = unselectedItems;

    m_selectedPages.clear();
    for (size_t k = 0; k < selectedItems.size(); ++k) {
        m_selectedPages.insert(insertPos + (int)k);
    }
    m_activePageIndex = insertPos;
    m_anchorPageIndex = insertPos;

    m_isModified = true;
    UpdateLayoutFromActivePages();
    CommitStateAfterEdit();
    RefreshAllViews();

    if (m_hWndCanvas) {
        ContinuousCanvas::ScrollToPage(m_hWndCanvas, insertPos);
    }
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::ScrollToPage(m_hWndSidebar, insertPos);
    }
    PruneCache(insertPos);
    return true;
}

bool PDFApp::MoveSelectedPagesUp() {
    if (m_activePages.empty() || m_selectedPages.empty()) return false;
    int firstSel = *m_selectedPages.begin();
    if (firstSel <= 0) return false;
    return MoveSelectedPages(firstSel - 1);
}

bool PDFApp::MoveSelectedPagesDown() {
    if (m_activePages.empty() || m_selectedPages.empty()) return false;
    int lastSel = *m_selectedPages.rbegin();
    int total = GetPageCount();
    if (lastSel >= total - 1) return false;
    return MoveSelectedPages(lastSel + 2);
}

void PDFApp::ExportSelectedPagesToJpg() {
    if (!HasDocument() || m_selectedPages.empty()) {
        MessageBoxW(m_hWndMain, L"Please select at least 1 page to extract!", L"Notice", MB_OK | MB_ICONWARNING);
        return;
    }

    // 1. Resolve Downloads directory
    static const GUID GUID_FOLDERID_Downloads = { 0x374de290, 0x123f, 0x4565, { 0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b } };
    PWSTR pPath = NULL;
    std::wstring downloadsDir;
    if (SUCCEEDED(SHGetKnownFolderPath(GUID_FOLDERID_Downloads, 0, NULL, &pPath)) && pPath) {
        downloadsDir = pPath;
        CoTaskMemFree(pPath);
    } else {
        WCHAR profilePath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath))) {
            downloadsDir = std::wstring(profilePath) + L"\\Downloads";
        } else {
            downloadsDir = L".";
        }
    }

    // 2. Prepare GDI+ JPEG encoder
    CLSID jpgClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpgClsid) < 0) {
        MessageBoxW(m_hWndMain, L"Windows JPEG encoder was not found!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    ULONG quality = 95;
    Gdiplus::EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    encoderParams.Parameter[0].Value = &quality;

    bool isSinglePage = (m_selectedPages.size() == 1);
    std::wstring targetFolder;
    std::wstring singleFilePath;

    if (isSinglePage) {
        int pageIdx = *m_selectedPages.begin();
        std::wstring defaultName = L"page " + std::to_wstring(pageIdx + 1) + L".jpg";

        OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
        WCHAR szFile[MAX_PATH] = { 0 };
        wcsncpy_s(szFile, defaultName.c_str(), _TRUNCATE);

        ofn.hwndOwner = m_hWndMain;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = L"jpg";
        ofn.lpstrInitialDir = downloadsDir.c_str();
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

        if (!GetSaveFileNameW(&ofn)) {
            return; // User cancelled
        }

        std::wstring chosenPath = szFile;
        if (chosenPath.length() < 4 || (chosenPath.substr(chosenPath.length() - 4) != L".jpg" && 
            (chosenPath.length() < 5 || chosenPath.substr(chosenPath.length() - 5) != L".jpeg"))) {
            chosenPath += L".jpg";
        }
        singleFilePath = chosenPath;
    } else {
        // Multiple pages: Create a dedicated separate folder in Downloads: "pdf_{format d-M-yyyy}_extracted"
        // If already exists, create next separate folder: "pdf_{format d-M-yyyy}_extracted (1)", "(2)", etc.
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &tt);

        std::wstringstream dateSs;
        dateSs << tm.tm_mday << L"-" << (tm.tm_mon + 1) << L"-" << (tm.tm_year + 1900);
        std::wstring baseFolderName = L"pdf_" + dateSs.str() + L"_extracted";
        targetFolder = downloadsDir + L"\\" + baseFolderName;

        int folderCounter = 1;
        while (GetFileAttributesW(targetFolder.c_str()) != INVALID_FILE_ATTRIBUTES) {
            targetFolder = downloadsDir + L"\\" + baseFolderName + L" (" + std::to_wstring(folderCounter++) + L")";
        }
        CreateDirectoryW(targetFolder.c_str(), NULL);
    }

    int successCount = 0;
    for (int pageIdx : m_selectedPages) {
        if (pageIdx < 0 || pageIdx >= (int)m_activePages.size()) continue;

        const PageItem& item = m_activePages[pageIdx];
        double ptW = item.widthPt;
        double ptH = item.heightPt;

        // Render at 300 DPI for Full HD / standard A4 sharp resolution (2480 x 3508 pixels)
        int targetW = (int)(ptW * (300.0 / 72.0));
        int targetH = (int)(ptH * (300.0 / 72.0));

        FPDF_BITMAP bmp = FPDFBitmap_Create(targetW, targetH, 0);
        FPDFBitmap_FillRect(bmp, 0, 0, targetW, targetH, 0xFFFFFFFF);

        if (!item.is_blank_page) {
            auto itDoc = m_docStore.find(item.source_file_id);
            if (itDoc != m_docStore.end() && itDoc->second.doc) {
                FPDF_PAGE p = FPDF_LoadPage(itDoc->second.doc, item.original_page_index);
                if (p) {
                    FPDF_RenderPageBitmap(bmp, p, 0, 0, targetW, targetH, 0, FPDF_ANNOT);
                    FPDF_ClosePage(p);
                }
            }
        }

        const void* buf = FPDFBitmap_GetBuffer(bmp);
        int stride = FPDFBitmap_GetStride(bmp);

        Gdiplus::Bitmap gdiBmp(targetW, targetH, stride, PixelFormat32bppRGB, (BYTE*)buf);

        if (!item.annotations.empty()) {
            Gdiplus::Graphics g(&gdiBmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            double scaleX = (double)targetW / ptW;
            double scaleY = (double)targetH / ptH;
            for (const auto& a : item.annotations) {
                if (!a.is_active) continue;
                if (a.type == ANNOT_HIGHLIGHT) {
                    float x1 = (float)(a.rectX1 * scaleX);
                    float x2 = (float)(a.rectX2 * scaleX);
                    float yTop = (float)((ptH - a.rectY2) * scaleY);
                    float yBottom = (float)((ptH - a.rectY1) * scaleY);

                    float rX = std::min(x1, x2);
                    float rY = std::min(yTop, yBottom);
                    float rW = std::abs(x2 - x1);
                    float rH = std::abs(yBottom - yTop);

                    Gdiplus::SolidBrush hlBrush(Gdiplus::Color(115, 255, 235, 59));
                    g.FillRectangle(&hlBrush, rX, rY, rW, rH);
                } else if (a.type == ANNOT_INK && a.points.size() > 1) {
                    float penW = (float)(a.strokeWidth * scaleX);
                    if (penW < 1.5f) penW = 1.5f;
                    Gdiplus::Pen inkPen(Gdiplus::Color(255, 220, 38, 38), penW);
                    inkPen.SetStartCap(Gdiplus::LineCapRound);
                    inkPen.SetEndCap(Gdiplus::LineCapRound);
                    inkPen.SetLineJoin(Gdiplus::LineJoinRound);

                    Gdiplus::GraphicsPath path;
                    for (size_t k = 0; k < a.points.size() - 1; ++k) {
                        float sx1 = (float)(a.points[k].x * scaleX);
                        float sy1 = (float)((ptH - a.points[k].y) * scaleY);
                        float sx2 = (float)(a.points[k + 1].x * scaleX);
                        float sy2 = (float)((ptH - a.points[k + 1].y) * scaleY);
                        path.AddLine(sx1, sy1, sx2, sy2);
                    }
                    g.DrawPath(&inkPen, &path);
                }
            }
        }

        // File naming
        std::wstring fullPath;
        if (isSinglePage) {
            fullPath = singleFilePath;
        } else {
            fullPath = targetFolder + L"\\page " + std::to_wstring(pageIdx + 1) + L".jpg";
        }

        Gdiplus::Status st = gdiBmp.Save(fullPath.c_str(), &jpgClsid, &encoderParams);
        if (st == Gdiplus::Ok) {
            successCount++;
        }

        FPDFBitmap_Destroy(bmp);
    }

    if (isSinglePage) {
        if (successCount > 0) {
            ShowExtractedJpgSingleDialog(singleFilePath);
        }
    } else {
        if (successCount > 0) {
            ShowExtractedJpgMultiDialog(successCount, targetFolder);
        }
    }
}

void PDFApp::ExportSelectedPagesToPdf() {
    if (!HasDocument() || m_selectedPages.empty()) {
        MessageBoxW(m_hWndMain, L"Please select at least 1 page to extract!", L"Notice", MB_OK | MB_ICONWARNING);
        return;
    }

    // 1. Determine default file name
    std::wstring baseDocName = L"document";
    if (!m_filePath.empty()) {
        size_t lastSlash = m_filePath.find_last_of(L"\\/");
        std::wstring fileName = (lastSlash != std::wstring::npos) ? m_filePath.substr(lastSlash + 1) : m_filePath;
        size_t lastDot = fileName.rfind(L'.');
        if (lastDot != std::wstring::npos) {
            baseDocName = fileName.substr(0, lastDot);
        } else {
            baseDocName = fileName;
        }
    }

    std::wstring defaultFileName;
    if (m_selectedPages.size() == 1) {
        int pageNum = (*m_selectedPages.begin()) + 1;
        defaultFileName = L"page" + std::to_wstring(pageNum) + L"_" + baseDocName + L".pdf";
    } else {
        defaultFileName = L"selected_" + baseDocName + L".pdf";
    }

    // 2. Open Save File dialog
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    WCHAR szFile[MAX_PATH] = { 0 };
    wcsncpy_s(szFile, defaultFileName.c_str(), _TRUNCATE);

    ofn.hwndOwner = m_hWndMain;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) {
        return; // User cancelled
    }

    // Guard against overwriting actively open source files
    for (const auto& pair : m_docStore) {
        if (!pair.second.filePath.empty() && _wcsicmp(szFile, pair.second.filePath.c_str()) == 0) {
            MessageBoxW(m_hWndMain, L"Cannot overwrite the PDF file currently open in the application! Please choose a different file name.", L"Extract PDF", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    // 3. Collect selected pages in display order
    std::vector<int> pagesToExport;
    for (int idx : m_selectedPages) {
        if (idx >= 0 && idx < (int)m_activePages.size()) {
            pagesToExport.push_back(idx);
        }
    }

    if (pagesToExport.empty()) return;

    // 4. Create new PDF document
    FPDF_DOCUMENT new_doc = FPDF_CreateNewDocument();
    if (!new_doc) {
        MessageBoxW(m_hWndMain, L"Failed to create new PDF document!", L"Extract PDF Error", MB_OK | MB_ICONERROR);
        return;
    }

    int destIndex = 0;
    for (int pageIdx : pagesToExport) {
        const PageItem& item = m_activePages[pageIdx];
        FPDF_PAGE p = NULL;
        if (item.is_blank_page) {
            p = FPDFPage_New(new_doc, destIndex, item.widthPt, item.heightPt);
        } else {
            auto itDoc = m_docStore.find(item.source_file_id);
            if (itDoc != m_docStore.end() && itDoc->second.doc) {
                int srcIdx = item.original_page_index;
                FPDF_ImportPagesByIndex(new_doc, itDoc->second.doc, &srcIdx, 1, destIndex);
                p = FPDF_LoadPage(new_doc, destIndex);
            } else {
                p = FPDFPage_New(new_doc, destIndex, item.widthPt, item.heightPt);
            }
        }

        if (p) {
            // Replay virtual annotations (highlight, ink) if present
            if (!item.annotations.empty()) {
                for (const auto& a : item.annotations) {
                    if (!a.is_active) continue;
                    int r = GetRValue(a.color);
                    int gVal = GetGValue(a.color);
                    int b = GetBValue(a.color);
                    int alpha = std::clamp((int)(a.opacity * 255.0f), 10, 255);

                    if (a.type == ANNOT_HIGHLIGHT) {
                        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(p, FPDF_ANNOT_HIGHLIGHT);
                        if (annot) {
                            FS_RECTF rect = { a.rectX1, a.rectY1, a.rectX2, a.rectY2 };
                            FPDFAnnot_SetRect(annot, &rect);
                            FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, r, gVal, b, alpha);
                            FS_QUADPOINTSF quad = { a.rectX1, a.rectY2, a.rectX2, a.rectY2, a.rectX1, a.rectY1, a.rectX2, a.rectY1 };
                            FPDFAnnot_AppendAttachmentPoints(annot, &quad);
                            FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
                            FPDFPage_CloseAnnot(annot);
                        }
                        FPDF_PAGEOBJECT rObj = FPDFPageObj_CreateNewRect(a.rectX1, a.rectY1, a.rectX2 - a.rectX1, a.rectY2 - a.rectY1);
                        if (rObj) {
                            FPDFPageObj_SetFillColor(rObj, r, gVal, b, (int)(alpha * 0.7f));
                            FPDFPath_SetDrawMode(rObj, 1, 0);
                            FPDFPage_InsertObject(p, rObj);
                        }
                    } else if (a.type == ANNOT_INK && a.points.size() > 1) {
                        FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(a.points[0].x, a.points[0].y);
                        if (path) {
                            for (size_t k = 1; k < a.points.size(); ++k) {
                                FPDFPath_LineTo(path, a.points[k].x, a.points[k].y);
                            }
                            FPDFPageObj_SetStrokeColor(path, r, gVal, b, alpha);
                            FPDFPageObj_SetStrokeWidth(path, a.strokeWidth > 0 ? a.strokeWidth : 2.5f);
                            FPDFPath_SetDrawMode(path, 0, 1);
                            FPDFPage_InsertObject(p, path);
                        }
                    }
                }
                FPDFPage_GenerateContent(p);
            } else if (item.is_blank_page) {
                FPDFPage_GenerateContent(p);
            }
            FPDF_ClosePage(p);
        }
        destIndex++;
    }

    // 5. Streaming Disk Write directly to Win32 file handle
    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        FPDF_CloseDocument(new_doc);
        MessageBoxW(m_hWndMain, L"Failed to create file for writing!", L"File Write Error", MB_OK | MB_ICONERROR);
        return;
    }

    StreamingFileWriter writer;
    writer.version = 1;
    writer.WriteBlock = WriteBlock;
    writer.hFile = hFile;

    FPDF_BOOL ok = FPDF_SaveWithVersion(new_doc, &writer, 0, 14);
    CloseHandle(hFile);
    FPDF_CloseDocument(new_doc);

    if (!ok) {
        DeleteFileW(szFile);
        MessageBoxW(m_hWndMain, L"An error occurred while writing the PDF file!", L"Export Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Immediate memory trim to keep RAM minimal
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    // 6. Notify user of success via custom dialog
    std::wstring outFileName = szFile;
    size_t lastSlash = outFileName.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        outFileName = outFileName.substr(lastSlash + 1);
    }

    ShowExtractedPdfDialog((int)pagesToExport.size(), szFile, outFileName);
}

void PDFApp::SetZoom(float newZoom, bool updateSlider, int anchorX, int anchorY) {
    float clamped = std::max(0.25f, std::min(6.0f, newZoom));
    if (std::abs(clamped - m_zoom) < 0.0001f) {
        return;
    }
    float oldZoom = m_zoom;
    m_zoom = clamped;

    if (!m_isZooming) {
        InvalidatePageCache(-1);
    }

    if (updateSlider && m_hWndRibbon) {
        RibbonBar::SetZoomSlider(m_hWndRibbon, (int)(m_zoom * 100.0f));
    }

    if (m_hWndCanvas) {
        ContinuousCanvas::ApplyZoomWithAnchor(m_hWndCanvas, oldZoom, m_zoom, anchorX, anchorY);
    }
}

void PDFApp::ZoomFitWidth() {
    if (!m_hWndCanvas || m_pages.empty()) return;
    RECT rc;
    GetClientRect(m_hWndCanvas, &rc);
    int clientW = rc.right - rc.left;
    int availableW = std::max(100, clientW - 60);

    float fitZoom = (float)availableW / (float)m_pages[0].widthPt;
    SetZoom(fitZoom, true, -1, -1);
}

void PDFApp::GoToPage(int page1Based) {
    int total = GetPageCount();
    if (total <= 0) return;
    int target0 = std::max(0, std::min(total - 1, page1Based - 1));
    m_activePageIndex = target0;

    if (m_hWndCanvas) {
        ContinuousCanvas::ScrollToPage(m_hWndCanvas, target0);
    }
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::ScrollToPage(m_hWndSidebar, target0);
    }
    RibbonBar::SetPageInfo(m_hWndRibbon, m_activePageIndex + 1, GetPageCount(), GetSelectedCount());
}

void PDFApp::PageUp() {
    if (!HasDocument() || m_pages.empty()) return;
    if (m_activePageIndex > 0) {
        int prev = m_activePageIndex - 1;
        SelectPageSingle(prev);
        GoToPage(prev + 1);
    }
}

void PDFApp::PageDown() {
    if (!HasDocument() || m_pages.empty()) return;
    if (m_activePageIndex < (int)m_pages.size() - 1) {
        int next = m_activePageIndex + 1;
        SelectPageSingle(next);
        GoToPage(next + 1);
    }
}

void PDFApp::ScrollCurrentPageTop() {
    if (!HasDocument() || m_pages.empty()) return;
    if (m_hWndCanvas) {
        ContinuousCanvas::ScrollToPageTop(m_hWndCanvas, m_activePageIndex);
    }
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::ScrollToPage(m_hWndSidebar, m_activePageIndex);
    }
}

void PDFApp::ScrollCurrentPageBottom() {
    if (!HasDocument() || m_pages.empty()) return;
    if (m_hWndCanvas) {
        ContinuousCanvas::ScrollToPageBottom(m_hWndCanvas, m_activePageIndex);
    }
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::ScrollToPage(m_hWndSidebar, m_activePageIndex);
    }
}

void PDFApp::ScrollByDelta(int deltaX, int deltaY) {
    if (!HasDocument() || m_pages.empty()) return;
    if (m_hWndCanvas) {
        ContinuousCanvas::ScrollByDelta(m_hWndCanvas, deltaX, deltaY);
    }
}

int PDFApp::FindPageAtY(int y) const {
    if (m_pages.empty()) return -1;
    if (y < m_pages[0].layoutTop) return 0;
    if (y > m_pages.back().layoutBottom) return (int)m_pages.size() - 1;

    int low = 0, high = (int)m_pages.size() - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (y < m_pages[mid].layoutTop) {
            high = mid - 1;
        } else if (y > m_pages[mid].layoutBottom) {
            low = mid + 1;
        } else {
            return mid;
        }
    }
    return std::max(0, std::min((int)m_pages.size() - 1, high >= 0 ? high : low));
}

void PDFApp::UpdateActivePageFromScroll(int scrollY, int viewportH) {
    if (!HasDocument() || m_pages.empty()) return;

    int center_y = scrollY + (viewportH / 2);
    int current_page = FindPageAtY(center_y);

    if (current_page < 0 || current_page >= (int)m_pages.size()) return;

    if (current_page != m_activePageIndex) {
        m_activePageIndex = current_page;
        m_selectedPages.clear();
        m_selectedPages.insert(m_activePageIndex);
        m_anchorPageIndex = m_activePageIndex;

        // Synchronize Ribbon
        if (m_hWndRibbon) {
            RibbonBar::SetPageInfo(m_hWndRibbon, m_activePageIndex + 1, GetPageCount(), 1);
        }

        // Synchronize Sidebar (blue highlight & auto-scroll to visible)
        if (m_hWndSidebar && m_sidebarVisible) {
            NavigationSidebar::ScrollToPage(m_hWndSidebar, m_activePageIndex);
            InvalidateRect(m_hWndSidebar, NULL, FALSE);
        }
    }
}

void PDFApp::PruneCache(int centerPageIdx) {
    if (!HasDocument()) return;

    // Requirement 3: When zoom > 150%, strictly ONLY keep the single page currently visible in viewport (maxDist = 0)
    int maxDist = (m_zoom > 1.5f) ? 0 : 1;

    for (auto it = m_d2dPageCache.begin(); it != m_d2dPageCache.end(); ) {
        if (std::abs(it->first - centerPageIdx) > maxDist) {
            if (it->second.pBitmap) {
                it->second.pBitmap->Release();
            }
            it = m_d2dPageCache.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_basePageCache.begin(); it != m_basePageCache.end(); ) {
        if (std::abs(it->first - centerPageIdx) > maxDist || m_zoom > 1.5f) {
            if (it->second.hBitmap) {
                DeleteObject(it->second.hBitmap);
            }
            it = m_basePageCache.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_annotOverlayCache.begin(); it != m_annotOverlayCache.end(); ) {
        if (std::abs(it->first - centerPageIdx) > maxDist || m_zoom > 1.5f) {
            if (it->second.hBitmap) {
                DeleteObject(it->second.hBitmap);
            }
            it = m_annotOverlayCache.erase(it);
        } else {
            ++it;
        }
    }
}

void PDFApp::TrimWorkingSetMemory() {
    if (!HasDocument()) return;

    // 1. Prune page texture caches (at zoom > 150%, only keeps the single visible page)
    PruneCache(m_activePageIndex);

    // 2. If zoom > 150%, completely discard any GDI base and annotation overlay caches
    if (m_zoom > 1.5f) {
        InvalidateBasePageCache(-1);
        InvalidateAnnotCache(-1);
    }

    // 3. Compact default process heap to coalesce free blocks
    HANDLE hHeap = GetProcessHeap();
    if (hHeap) {
        HeapCompact(hHeap, 0);
    }

    // 4. Force Windows Memory Manager to trim working set back to physical RAM baseline (< 20MB - 30MB)
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

void PDFApp::SetTool(ToolMode tool) {
    m_tool = tool;
    RibbonBar::UpdateUI(m_hWndRibbon);
}

void PDFApp::SelectPageSingle(int pageIdx) {
    m_selectedPages.clear();
    if (pageIdx >= 0 && pageIdx < GetPageCount()) {
        m_selectedPages.insert(pageIdx);
        m_anchorPageIndex = pageIdx;
        m_activePageIndex = pageIdx;
    }
    RefreshAllViews();
}

void PDFApp::SelectPageToggle(int pageIdx) {
    if (pageIdx >= 0 && pageIdx < GetPageCount()) {
        if (m_selectedPages.count(pageIdx)) {
            m_selectedPages.erase(pageIdx);
        } else {
            m_selectedPages.insert(pageIdx);
        }
        m_anchorPageIndex = pageIdx;
        m_activePageIndex = pageIdx;
    }
    RefreshAllViews();
}

void PDFApp::SelectPageRange(int pageIdx) {
    if (pageIdx < 0 || pageIdx >= GetPageCount()) return;
    int start = std::min(m_anchorPageIndex, pageIdx);
    int end = std::max(m_anchorPageIndex, pageIdx);
    m_selectedPages.clear();
    for (int i = start; i <= end; ++i) {
        m_selectedPages.insert(i);
    }
    m_activePageIndex = pageIdx;
    RefreshAllViews();
}

void PDFApp::SelectAllPages() {
    m_selectedPages.clear();
    for (int i = 0; i < GetPageCount(); ++i) {
        m_selectedPages.insert(i);
    }
    RefreshAllViews();
}

bool PDFApp::IsPageSelected(int pageIdx) const {
    return m_selectedPages.count(pageIdx) > 0;
}

void PDFApp::ToggleSidebar() {
    m_sidebarVisible = !m_sidebarVisible;
    ShowWindow(m_hWndSidebar, m_sidebarVisible ? SW_SHOW : SW_HIDE);

    // Trigger main window resize to reposition canvas
    RECT rc;
    GetClientRect(m_hWndMain, &rc);
    SendMessageW(m_hWndMain, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
}

void PDFApp::ToggleRibbon() {
    m_ribbonCollapsed = !m_ribbonCollapsed;
    RibbonBar::UpdateUI(m_hWndRibbon);

    RECT rc;
    GetClientRect(m_hWndMain, &rc);
    SendMessageW(m_hWndMain, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
}

void PDFApp::PreloadPageCache(int pageIdx) {
    if (m_isZooming) return; // Don't preload during active zoom gestures
    if (m_zoom > 1.5f) return; // Requirement 3: Strictly NO preloading adjacent pages when zoom > 150%!
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;
    if (pageIdx >= (int)m_pages.size()) return;

    // Strict Throttling: Abort immediately if page is not adjacent to current active page
    if (std::abs(pageIdx - m_activePageIndex) > 1) return;

    const PageInfo& p = m_pages[pageIdx];
    int destW = p.displayW;
    int destH = p.displayH;
    if (destW <= 0 || destH <= 0) return;

    ID2D1HwndRenderTarget* pRT = ContinuousCanvas::GetRenderTarget();
    if (pRT) {
        GetPageD2DBitmap(pRT, pageIdx, destW, destH);
    }
}

ID2D1Bitmap* PDFApp::GetPageD2DBitmap(ID2D1RenderTarget* pRT, int pageIdx, int destW, int destH) {
    if (!pRT || !HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return nullptr;
    if (destW <= 0 || destH <= 0) return nullptr;

    const PageItem& item = m_activePages[pageIdx];

    // Check VRAM GPU Texture Cache
    auto it = m_d2dPageCache.find(pageIdx);
    if (it != m_d2dPageCache.end() && it->second.pBitmap) {
        if (it->second.width == destW && it->second.height == destH) {
            return it->second.pBitmap; // Exact match
        } else if (m_isZooming) {
            return it->second.pBitmap; // Instant GPU Bilinear Scaling
        }
    }

    if (m_isZooming) {
        // While zooming, NEVER invoke PDFium decoding to guarantee CPU < 1%
        return (it != m_d2dPageCache.end()) ? it->second.pBitmap : nullptr;
    }

    // Cache Miss (idle): Decode page from PDFium and upload directly to VRAM
    if (item.is_blank_page) {
        D2D1_SIZE_U size = D2D1::SizeU(destW, destH);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
        );
        std::vector<uint32_t> whitePixels(destW * destH, 0xFFFFFFFF);
        ID2D1Bitmap* pBitmap = nullptr;
        HRESULT hr = pRT->CreateBitmap(size, whitePixels.data(), destW * 4, props, &pBitmap);
        if (SUCCEEDED(hr) && pBitmap) {
            if (it != m_d2dPageCache.end() && it->second.pBitmap) {
                it->second.pBitmap->Release();
            }
            m_d2dPageCache[pageIdx] = { pBitmap, destW, destH, m_zoom };
            return pBitmap;
        }
        return nullptr;
    }

    FPDF_DOCUMENT doc = nullptr;
    auto itDoc = m_docStore.find(item.source_file_id);
    if (itDoc != m_docStore.end() && itDoc->second.doc) {
        doc = itDoc->second.doc;
    } else if (!m_docStore.empty()) {
        doc = m_docStore.begin()->second.doc;
    }
    if (!doc) return nullptr;

    int pageCount = FPDF_GetPageCount(doc);
    int safeIdx = std::max(0, std::min(pageCount - 1, item.original_page_index));
    FPDF_PAGE p = FPDF_LoadPage(doc, safeIdx);
    if (!p) return nullptr;

    // Update real page size if needed
    double realW = FPDF_GetPageWidth(p);
    double realH = FPDF_GetPageHeight(p);
    if (std::abs(realW - item.widthPt) > 1.0 || std::abs(realH - item.heightPt) > 1.0) {
        PageItem& nonConstItem = m_activePages[pageIdx];
        nonConstItem.widthPt = realW;
        nonConstItem.heightPt = realH;
        if (pageIdx < (int)m_pages.size()) {
            m_pages[pageIdx].widthPt = realW;
            m_pages[pageIdx].heightPt = realH;
        }
    }

    FPDF_BITMAP fpdfBmp = FPDFBitmap_Create(destW, destH, 0);
    FPDFBitmap_FillRect(fpdfBmp, 0, 0, destW, destH, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(fpdfBmp, p, 0, 0, destW, destH, 0, FPDF_ANNOT);

    const void* buffer = FPDFBitmap_GetBuffer(fpdfBmp);
    int stride = FPDFBitmap_GetStride(fpdfBmp);

    // Direct2D GPU Texture Upload
    D2D1_SIZE_U size = D2D1::SizeU(destW, destH);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
    );

    ID2D1Bitmap* pBitmap = nullptr;
    HRESULT hr = pRT->CreateBitmap(size, buffer, stride, props, &pBitmap);

    FPDFBitmap_Destroy(fpdfBmp);
    FPDF_ClosePage(p);

    if (SUCCEEDED(hr) && pBitmap) {
        if (it != m_d2dPageCache.end() && it->second.pBitmap) {
            it->second.pBitmap->Release();
        }
        m_d2dPageCache[pageIdx] = { pBitmap, destW, destH, m_zoom };
        return pBitmap;
    }
    return nullptr;
}

void PDFApp::RenderAnnotationsToD2D(ID2D1RenderTarget* pRT, int pageIdx, float destX, float destY, float destW, float destH) {
    if (!pRT || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;
    const PageItem& item = m_activePages[pageIdx];
    if (item.annotations.empty()) return;

    double ptW = item.widthPt;
    double ptH = item.heightPt;
    if (ptW <= 0.0 || ptH <= 0.0 || destW <= 0.0f || destH <= 0.0f) return;

    float scaleX = destW / (float)ptW;
    float scaleY = destH / (float)ptH;

    ID2D1SolidColorBrush* pBrush = nullptr;
    pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBrush);
    if (!pBrush) return;

    ID2D1Factory* pFactory = nullptr;
    pRT->GetFactory(&pFactory);
    ID2D1StrokeStyle* pStrokeStyle = nullptr;
    if (pFactory) {
        D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND
        );
        pFactory->CreateStrokeStyle(strokeProps, NULL, 0, &pStrokeStyle);
    }

    for (const auto& a : item.annotations) {
        if (!a.is_active) continue;

        D2D1_COLOR_F color = D2D1::ColorF(
            GetRValue(a.color) / 255.0f,
            GetGValue(a.color) / 255.0f,
            GetBValue(a.color) / 255.0f,
            a.opacity
        );
        pBrush->SetColor(color);

        if (a.type == ANNOT_HIGHLIGHT) {
            float x1 = destX + a.rectX1 * scaleX;
            float x2 = destX + a.rectX2 * scaleX;
            float yTop = destY + (float)(ptH - a.rectY2) * scaleY;
            float yBottom = destY + (float)(ptH - a.rectY1) * scaleY;

            D2D1_RECT_F r = D2D1::RectF(
                std::min(x1, x2),
                std::min(yTop, yBottom),
                std::max(x1, x2),
                std::max(yTop, yBottom)
            );
            pRT->FillRectangle(r, pBrush);
        } else if (a.type == ANNOT_INK && a.points.size() > 1) {
            float penW = a.strokeWidth * scaleX;
            if (penW < 1.0f) penW = 1.0f;

            if (pFactory) {
                ID2D1PathGeometry* pPath = nullptr;
                HRESULT hr = pFactory->CreatePathGeometry(&pPath);
                if (SUCCEEDED(hr) && pPath) {
                    ID2D1GeometrySink* pSink = nullptr;
                    hr = pPath->Open(&pSink);
                    if (SUCCEEDED(hr) && pSink) {
                        float startX = destX + a.points[0].x * scaleX;
                        float startY = destY + (float)(ptH - a.points[0].y) * scaleY;
                        pSink->BeginFigure(D2D1::Point2F(startX, startY), D2D1_FIGURE_BEGIN_HOLLOW);
                        for (size_t k = 1; k < a.points.size(); ++k) {
                            float px = destX + a.points[k].x * scaleX;
                            float py = destY + (float)(ptH - a.points[k].y) * scaleY;
                            pSink->AddLine(D2D1::Point2F(px, py));
                        }
                        pSink->EndFigure(D2D1_FIGURE_END_OPEN);
                        pSink->Close();
                        pSink->Release();

                        pRT->DrawGeometry(pPath, pBrush, penW, pStrokeStyle);
                    }
                    pPath->Release();
                }
            }
        }
    }

    if (pStrokeStyle) pStrokeStyle->Release();
    if (pFactory) pFactory->Release();
    if (pBrush) pBrush->Release();
}

void PDFApp::RenderAnnotationsToGraphics(Gdiplus::Graphics& g, int pageIdx, int destW, int destH) {
    if (pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;
    const PageItem& item = m_activePages[pageIdx];
    if (item.annotations.empty()) return;

    double ptW = item.widthPt;
    double ptH = item.heightPt;
    if (ptW <= 0.0 || ptH <= 0.0 || destW <= 0 || destH <= 0) return;

    double scaleX = (double)destW / ptW;
    double scaleY = (double)destH / ptH;

    for (const auto& a : item.annotations) {
        if (!a.is_active) continue;

        if (a.type == ANNOT_HIGHLIGHT) {
            float x1 = (float)(a.rectX1 * scaleX);
            float x2 = (float)(a.rectX2 * scaleX);
            float yTop = (float)((ptH - a.rectY2) * scaleY);
            float yBottom = (float)((ptH - a.rectY1) * scaleY);

            float rX = std::min(x1, x2);
            float rY = std::min(yTop, yBottom);
            float rW = std::abs(x2 - x1);
            float rH = std::abs(yBottom - yTop);

            BYTE alpha = (BYTE)std::clamp((int)(a.opacity * 255.0f), 10, 255);
            COLORREF c = (a.color != 0) ? a.color : RGB(255, 235, 59);
            Gdiplus::SolidBrush hlBrush(Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)));
            g.FillRectangle(&hlBrush, rX, rY, rW, rH);
        } else if (a.type == ANNOT_INK && a.points.size() > 1) {
            float penW = (float)(a.strokeWidth * scaleX);
            if (penW < 1.0f) penW = 1.0f;

            BYTE alpha = (BYTE)std::clamp((int)(a.opacity * 255.0f), 10, 255);
            COLORREF c = (a.color != 0) ? a.color : RGB(220, 38, 38);
            Gdiplus::Pen inkPen(Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)), penW);
            inkPen.SetStartCap(Gdiplus::LineCapRound);
            inkPen.SetEndCap(Gdiplus::LineCapRound);
            inkPen.SetLineJoin(Gdiplus::LineJoinRound);

            Gdiplus::GraphicsPath path;
            for (size_t k = 0; k < a.points.size() - 1; ++k) {
                float sx1 = (float)(a.points[k].x * scaleX);
                float sy1 = (float)((ptH - a.points[k].y) * scaleY);
                float sx2 = (float)(a.points[k + 1].x * scaleX);
                float sy2 = (float)((ptH - a.points[k + 1].y) * scaleY);
                path.AddLine(sx1, sy1, sx2, sy2);
            }
            g.DrawPath(&inkPen, &path);
        }
    }
}

void PDFApp::RenderAnnotationsToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH) {
    if (pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;
    const PageItem& item = m_activePages[pageIdx];
    if (item.annotations.empty()) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.TranslateTransform((float)destX, (float)destY);
    RenderAnnotationsToGraphics(g, pageIdx, destW, destH);
}

void PDFApp::RenderPageToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH, bool forPrinting) {
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;

    const PageItem& item = m_activePages[pageIdx];

    if (forPrinting) {
        if (item.is_blank_page) {
            RECT rc = { destX, destY, destX + destW, destY + destH };
            HBRUSH hWhite = (HBRUSH)GetStockObject(WHITE_BRUSH);
            FillRect(hdc, &rc, hWhite);
            RenderAnnotationsToDC(hdc, pageIdx, destX, destY, destW, destH);
            return;
        }

        FPDF_DOCUMENT doc = NULL;
        auto itDoc = m_docStore.find(item.source_file_id);
        if (itDoc != m_docStore.end() && itDoc->second.doc) {
            doc = itDoc->second.doc;
        } else if (!m_docStore.empty()) {
            doc = m_docStore.begin()->second.doc;
        }
        if (doc) {
            int pageCount = FPDF_GetPageCount(doc);
            int safeIdx = std::max(0, std::min(pageCount - 1, item.original_page_index));
            FPDF_PAGE p = FPDF_LoadPage(doc, safeIdx);
            if (p) {
                FPDF_RenderPage(hdc, p, destX, destY, destW, destH, 0, FPDF_PRINTING | FPDF_ANNOT);
                FPDF_ClosePage(p);
            }
        }
        RenderAnnotationsToDC(hdc, pageIdx, destX, destY, destW, destH);
        return;
    }

    // =========================================================================
    // LAYER 1: BASE PDF SURFACE (Cached Bitmap without baked annotations)
    // =========================================================================
    bool baseRendered = false;
    auto itBase = m_basePageCache.find(pageIdx);
    if (itBase != m_basePageCache.end() && itBase->second.hBitmap) {
        if (itBase->second.width == destW && itBase->second.height == destH) {
            // Exact resolution match: 1:1 instant BitBlt (< 0.2ms)
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, itBase->second.hBitmap);
            BitBlt(hdc, destX, destY, destW, destH, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
            baseRendered = true;
        } else if (m_isZooming) {
            // INSTANT 60 FPS GPU/GDI BITMAP SCALING (StretchBlt with HALFTONE mode)
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, itBase->second.hBitmap);
            int oldMode = SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            StretchBlt(hdc, destX, destY, destW, destH, memDC, 0, 0, itBase->second.width, itBase->second.height, SRCCOPY);
            SetStretchBltMode(hdc, oldMode);
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
            baseRendered = true;
        }
    }

    if (!baseRendered) {
        if (m_isZooming) {
            // While actively zooming, NEVER invoke PDFium decoding to guarantee CPU < 2%
            RECT rc = { destX, destY, destX + destW, destY + destH };
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            baseRendered = true;
        } else {
            // Cache Miss: Decode clean base page into Base Page Cache (Layer 1)
            HDC cacheDC = CreateCompatibleDC(hdc);
            HBITMAP hBaseBmp = CreateCompatibleBitmap(hdc, destW, destH);
            HBITMAP oldBaseBmp = (HBITMAP)SelectObject(cacheDC, hBaseBmp);

            if (item.is_blank_page) {
                RECT rc = { 0, 0, destW, destH };
                FillRect(cacheDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            } else {
                FPDF_DOCUMENT doc = NULL;
                auto itDoc = m_docStore.find(item.source_file_id);
                if (itDoc != m_docStore.end() && itDoc->second.doc) {
                    doc = itDoc->second.doc;
                } else if (!m_docStore.empty()) {
                    doc = m_docStore.begin()->second.doc;
                }

                if (!doc) {
                    RECT rc = { 0, 0, destW, destH };
                    FillRect(cacheDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
                } else {
                    int pageCount = FPDF_GetPageCount(doc);
                    int safeIdx = std::max(0, std::min(pageCount - 1, item.original_page_index));
                    FPDF_PAGE p = FPDF_LoadPage(doc, safeIdx);
                    if (!p) {
                        RECT rc = { 0, 0, destW, destH };
                        FillRect(cacheDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    } else {
                        // On-demand lazy page metric update
                        double realW = FPDF_GetPageWidth(p);
                        double realH = FPDF_GetPageHeight(p);
                        if (std::abs(realW - item.widthPt) > 1.0 || std::abs(realH - item.heightPt) > 1.0) {
                            PageItem& nonConstItem = m_activePages[pageIdx];
                            nonConstItem.widthPt = realW;
                            nonConstItem.heightPt = realH;
                            if (pageIdx < (int)m_pages.size()) {
                                m_pages[pageIdx].widthPt = realW;
                                m_pages[pageIdx].heightPt = realH;
                            }
                        }

                        FPDF_BITMAP bmp = FPDFBitmap_Create(destW, destH, 0);
                        FPDFBitmap_FillRect(bmp, 0, 0, destW, destH, 0xFFFFFFFF);
                        FPDF_RenderPageBitmap(bmp, p, 0, 0, destW, destH, 0, FPDF_ANNOT);

                        BITMAPINFO bmi = { 0 };
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = destW;
                        bmi.bmiHeader.biHeight = -destH; // Top-down DIB
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        const void* buffer = FPDFBitmap_GetBuffer(bmp);
                        StretchDIBits(cacheDC, 0, 0, destW, destH,
                            0, 0, destW, destH,
                            buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

                        FPDFBitmap_Destroy(bmp);
                        FPDF_ClosePage(p);
                    }
                }
            }

            // Blit Layer 1 to screen
            BitBlt(hdc, destX, destY, destW, destH, cacheDC, 0, 0, SRCCOPY);
            SelectObject(cacheDC, oldBaseBmp);
            DeleteDC(cacheDC);

            // Save clean base surface into Layer 1 cache
            auto existingIt = m_basePageCache.find(pageIdx);
            if (existingIt != m_basePageCache.end() && existingIt->second.hBitmap) {
                DeleteObject(existingIt->second.hBitmap);
            }
            m_basePageCache[pageIdx] = { hBaseBmp, destW, destH, m_zoom };
            baseRendered = true;
        }
    }

    // =========================================================================
    // LAYER 2: ANNOTATION OVERLAY (32-bit Transparent PARGB Surface)
    // =========================================================================
    bool hasActiveAnnots = false;
    for (const auto& a : item.annotations) {
        if (a.is_active) {
            hasActiveAnnots = true;
            break;
        }
    }

    if (!hasActiveAnnots) {
        // Zero active annotations: Done immediately!
        return;
    }

    auto itAnnot = m_annotOverlayCache.find(pageIdx);
    if (itAnnot != m_annotOverlayCache.end() && itAnnot->second.hBitmap) {
        if (itAnnot->second.width == destW && itAnnot->second.height == destH) {
            // Exact resolution match: Instant AlphaBlend (< 0.1ms)
            HDC annotDC = CreateCompatibleDC(hdc);
            HBITMAP oldAnnotBmp = (HBITMAP)SelectObject(annotDC, itAnnot->second.hBitmap);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, destX, destY, destW, destH, annotDC, 0, 0, destW, destH, bf);
            SelectObject(annotDC, oldAnnotBmp);
            DeleteDC(annotDC);
            return;
        } else if (m_isZooming) {
            // Stretched AlphaBlend during active zooming
            HDC annotDC = CreateCompatibleDC(hdc);
            HBITMAP oldAnnotBmp = (HBITMAP)SelectObject(annotDC, itAnnot->second.hBitmap);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, destX, destY, destW, destH, annotDC, 0, 0, itAnnot->second.width, itAnnot->second.height, bf);
            SelectObject(annotDC, oldAnnotBmp);
            DeleteDC(annotDC);
            return;
        }
    }

    // Build fresh Layer 2 Overlay (32-bit PARGB DIBSection)
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = destW;
    bmi.bmiHeader.biHeight = -destH; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hOverlay = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hOverlay && pBits) {
        memset(pBits, 0, destW * destH * 4); // Clear to fully transparent
        Gdiplus::Bitmap gdiBmp(destW, destH, destW * 4, PixelFormat32bppPARGB, (BYTE*)pBits);
        Gdiplus::Graphics g(&gdiBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        RenderAnnotationsToGraphics(g, pageIdx, destW, destH);

        // AlphaBlend to screen
        HDC annotDC = CreateCompatibleDC(hdc);
        HBITMAP oldAnnotBmp = (HBITMAP)SelectObject(annotDC, hOverlay);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, destX, destY, destW, destH, annotDC, 0, 0, destW, destH, bf);
        SelectObject(annotDC, oldAnnotBmp);
        DeleteDC(annotDC);

        // Requirement 2: Do NOT maintain dual bitmaps (Base + Transparent Overlay) at zoom > 150%!
        // Release intermediate buffer immediately after compositing to screen
        if (m_zoom > 1.5f) {
            DeleteObject(hOverlay);
        } else {
            // Cache Layer 2 Overlay only at normal/low zoom levels
            auto itOld = m_annotOverlayCache.find(pageIdx);
            if (itOld != m_annotOverlayCache.end() && itOld->second.hBitmap) {
                DeleteObject(itOld->second.hBitmap);
            }
            m_annotOverlayCache[pageIdx] = { hOverlay, destW, destH, m_zoom, true };
        }
    }
}

void PDFApp::RenderThumbnailToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH) {
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) {
        RECT rc = { destX, destY, destX + destW, destY + destH };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return;
    }

    const PageItem& item = m_activePages[pageIdx];
    if (item.is_blank_page) {
        RECT rc = { destX, destY, destX + destW, destY + destH };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        RenderAnnotationsToDC(hdc, pageIdx, destX, destY, destW, destH);
        return;
    }

    FPDF_DOCUMENT doc = NULL;
    auto itDoc = m_docStore.find(item.source_file_id);
    if (itDoc != m_docStore.end() && itDoc->second.doc) {
        doc = itDoc->second.doc;
    } else if (!m_docStore.empty()) {
        // Fallback recovery: use primary document in docStore
        doc = m_docStore.begin()->second.doc;
    }

    if (!doc) {
        RECT rc = { destX, destY, destX + destW, destY + destH };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return;
    }

    int pageCount = FPDF_GetPageCount(doc);
    int safeIdx = std::max(0, std::min(pageCount - 1, item.original_page_index));
    FPDF_PAGE p = FPDF_LoadPage(doc, safeIdx);
    if (!p) {
        RECT rc = { destX, destY, destX + destW, destY + destH };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return;
    }

    FPDF_BITMAP bmp = FPDFBitmap_Create(destW, destH, 0);
    FPDFBitmap_FillRect(bmp, 0, 0, destW, destH, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp, p, 0, 0, destW, destH, 0, FPDF_ANNOT);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = destW;
    bmi.bmiHeader.biHeight = -destH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    const void* buffer = FPDFBitmap_GetBuffer(bmp);
    StretchDIBits(hdc, destX, destY, destW, destH,
        0, 0, destW, destH,
        buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(p);

    // Overlay in-memory annotations on thumbnail
    RenderAnnotationsToDC(hdc, pageIdx, destX, destY, destW, destH);
}

static float DistanceSqPointToSegment(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    if (l2 < 0.0001f) {
        float ex = px - x1;
        float ey = py - y1;
        return ex * ex + ey * ey;
    }
    float t = ((px - x1) * dx + (py - y1) * dy) / l2;
    t = std::max(0.0f, std::min(1.0f, t));
    float projX = x1 + t * dx;
    float projY = y1 + t * dy;
    float ex = px - projX;
    float ey = py - projY;
    return ex * ex + ey * ey;
}

bool PDFApp::EraseAnnotationAt(int pageIdx, int screenX, int screenY, int pageDisplayX, int pageDisplayY, int pageDisplayW, int pageDisplayH, bool recordUndo) {
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return false;

    PageItem& item = m_activePages[pageIdx];
    if (item.annotations.empty()) return false;

    double ptW = item.widthPt;
    double ptH = item.heightPt;
    if (ptW <= 0.0 || ptH <= 0.0 || pageDisplayW <= 0 || pageDisplayH <= 0) return false;

    double scaleX = (double)pageDisplayW / ptW;
    double scaleY = (double)pageDisplayH / ptH;

    bool erasedAny = false;

    for (int i = (int)item.annotations.size() - 1; i >= 0; --i) {
        auto& a = item.annotations[i];
        if (!a.is_active) continue; // Skip already inactive / hidden strokes
        bool isHit = false;

        if (a.type == ANNOT_INK && a.points.size() > 1) {
            float hitRadius = std::max(10.0f, (a.strokeWidth * (float)scaleX) / 2.0f + 8.0f);
            float hitRadiusSq = hitRadius * hitRadius;

            for (size_t k = 0; k < a.points.size() - 1; ++k) {
                float sx1 = (float)(pageDisplayX + a.points[k].x * scaleX);
                float sy1 = (float)(pageDisplayY + (ptH - a.points[k].y) * scaleY);
                float sx2 = (float)(pageDisplayX + a.points[k + 1].x * scaleX);
                float sy2 = (float)(pageDisplayY + (ptH - a.points[k + 1].y) * scaleY);

                float dSq = DistanceSqPointToSegment((float)screenX, (float)screenY, sx1, sy1, sx2, sy2);
                if (dSq <= hitRadiusSq) {
                    isHit = true;
                    break;
                }
            }
        } else if (a.type == ANNOT_HIGHLIGHT) {
            float x1 = (float)(pageDisplayX + a.rectX1 * scaleX);
            float x2 = (float)(pageDisplayX + a.rectX2 * scaleX);
            float yTop = (float)(pageDisplayY + (ptH - a.rectY2) * scaleY);
            float yBottom = (float)(pageDisplayY + (ptH - a.rectY1) * scaleY);

            float rLeft = std::min(x1, x2) - 8.0f;
            float rRight = std::max(x1, x2) + 8.0f;
            float rTop = std::min(yTop, yBottom) - 8.0f;
            float rBottom = std::max(yTop, yBottom) + 8.0f;

            if (screenX >= rLeft && screenX <= rRight && screenY >= rTop && screenY <= rBottom) {
                isHit = true;
            }
        }

        if (isHit) {
            if (recordUndo && !erasedAny) {
                RecordStateBeforeEdit();
            }
            a.is_active = false; // Soft Erase: Instant hidden flag
            erasedAny = true;
        }
    }

    if (erasedAny) {
        InvalidateAnnotCache(pageIdx); // DO NOT TOUCH Layer 1 Base PDF Cache!
        if (m_hWndSidebar && m_sidebarVisible) {
            NavigationSidebar::InvalidatePageThumbnail(m_hWndSidebar, pageIdx);
        }
    }

    return erasedAny;
}

void PDFApp::ApplyHighlight(int pageIdx, float x1, float y1, float x2, float y2) {
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size()) return;

    RecordStateBeforeEdit();

    AnnotationItem annot;
    annot.id = ++m_nextAnnotId;
    annot.type = ANNOT_HIGHLIGHT;
    annot.rectX1 = std::min(x1, x2);
    annot.rectY1 = std::min(y1, y2);
    annot.rectX2 = std::max(x1, x2);
    annot.rectY2 = std::max(y1, y2);
    annot.color = m_highlightConfig.color;
    annot.strokeWidth = m_highlightConfig.strokeWidth;
    annot.opacity = m_highlightConfig.opacity;
    annot.is_active = true;

    m_activePages[pageIdx].annotations.push_back(annot);

    InvalidateAnnotCache(pageIdx); // Base PDF is NOT touched!
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::InvalidatePageThumbnail(m_hWndSidebar, pageIdx);
    }
    CommitStateAfterEdit();
    RefreshAllViews();
}

void PDFApp::ApplyInkPath(int pageIdx, const std::vector<POINT>& screenPoints, int pageX, int pageY, int pageW, int pageH) {
    if (!HasDocument() || pageIdx < 0 || pageIdx >= (int)m_activePages.size() || screenPoints.size() < 2) return;

    RecordStateBeforeEdit();

    double ptW = m_pages[pageIdx].widthPt;
    double ptH = m_pages[pageIdx].heightPt;

    AnnotationItem annot;
    annot.id = ++m_nextAnnotId;
    annot.type = ANNOT_INK;
    annot.rectX1 = annot.rectY1 = annot.rectX2 = annot.rectY2 = 0;
    annot.color = m_penConfig.color;
    annot.strokeWidth = m_penConfig.strokeWidth;
    annot.opacity = m_penConfig.opacity;
    annot.is_active = true;
    annot.points.reserve(screenPoints.size());

    for (const auto& pt : screenPoints) {
        float relX = (float)(pt.x - pageX);
        float relY = (float)(pt.y - pageY);
        float pdfX = (float)(relX * (ptW / (double)pageW));
        float pdfY = (float)(((double)pageH - relY) * (ptH / (double)pageH));
        annot.points.push_back({ pdfX, pdfY });
    }

    m_activePages[pageIdx].annotations.push_back(annot);

    InvalidateAnnotCache(pageIdx); // Base PDF is NOT touched!
    if (m_hWndSidebar && m_sidebarVisible) {
        NavigationSidebar::InvalidatePageThumbnail(m_hWndSidebar, pageIdx);
    }
    CommitStateAfterEdit();
    RefreshAllViews();
}

void PDFApp::ShowPrintDialog() {
    if (!HasDocument() || m_activePages.empty()) {
        MessageBoxW(m_hWndMain, L"No document available to print!", L"Print Notice", MB_OK | MB_ICONWARNING);
        return;
    }

    PrintOptions opt;
    if (PDFPrinter::ShowPrintDialog(m_hWndMain, opt)) {
        PDFPrinter::ExecutePrint(m_hWndMain, opt);
    }
}

void PDFApp::ShowAboutDialog() {
    bool isDark = m_isDarkMode;
    HINSTANCE hInst = GetModuleHandleW(NULL);

    static bool s_aboutRegistered = false;
    if (!s_aboutRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            bool isDark = PDFApp::Instance().IsDarkMode();
            switch (msg) {
            case WM_CREATE: {
                PDFApp::SetWindowTitlebarTheme(hWnd, isDark);
                PDFApp::ApplyUxThemeDarkMode(hWnd, isDark);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
                RECT rc;
                GetClientRect(hWnd, &rc);
                HBRUSH hBg = CreateSolidBrush(isDark ? RGB(24, 24, 27) : RGB(248, 249, 250));
                FillRect(hdc, &rc, hBg);
                DeleteObject(hBg);

                // Draw App Icon centered
                HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
                if (hIcon) {
                    DrawIconEx(hdc, (rc.right - 48) / 2, 22, hIcon, 48, 48, 0, NULL, DI_NORMAL);
                    DestroyIcon(hIcon);
                }

                // Title: Feathr PDF
                HFONT hTitleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, isDark ? RGB(244, 244, 245) : RGB(24, 24, 27));
                RECT rcTitle = { 0, 78, rc.right, 105 };
                DrawTextW(hdc, L"Feathr PDF", -1, &rcTitle, DT_CENTER | DT_SINGLELINE);

                // Author: by @h_nguynn0.
                HFONT hAuthorFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(hdc, hAuthorFont);
                SetTextColor(hdc, isDark ? RGB(161, 161, 170) : RGB(100, 100, 105));
                RECT rcAuthor = { 0, 108, rc.right, 128 };
                DrawTextW(hdc, L"by @h_nguynn0.", -1, &rcAuthor, DT_CENTER | DT_SINGLELINE);

                // Subtitle / Description
                HFONT hDescFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(hdc, hDescFont);
                SetTextColor(hdc, isDark ? RGB(161, 161, 170) : RGB(100, 100, 105));
                RECT rcDesc = { 20, 136, rc.right - 20, 200 };
                DrawTextW(hdc, L"Native Win32 PDF Reader & Annotation Editor\nPowered by Google PDFium & Direct2D GPU Acceleration\nLightweight, Instant Startup & Ultra-low Memory", -1, &rcDesc, DT_CENTER);

                SelectObject(hdc, hOldFont);
                DeleteObject(hTitleFont);
                DeleteObject(hAuthorFont);
                DeleteObject(hDescFont);

                EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
                if (!dis) break;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                bool isPressed = (dis->itemState & ODS_SELECTED) != 0;

                HBRUSH hDlgBg = CreateSolidBrush(isDark ? RGB(24, 24, 27) : RGB(248, 249, 250));
                FillRect(hdc, &rc, hDlgBg);
                DeleteObject(hDlgBg);

                COLORREF btnBg = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);
                COLORREF borderCol = isPressed ? RGB(29, 78, 216) : RGB(59, 130, 246);
                HBRUSH hBr = CreateSolidBrush(btnBg);
                HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
                HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(hdc, hOldBr);
                SelectObject(hdc, hOldPen);
                DeleteObject(hBr);
                DeleteObject(hPen);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                HFONT hBtnFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HFONT hOldF = (HFONT)SelectObject(hdc, hBtnFont);
                DrawTextW(hdc, L"OK", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldF);
                DeleteObject(hBtnFont);
                return TRUE;
            }
            case WM_COMMAND: {
                if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
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
            case WM_CLOSE: {
                DestroyWindow(hWnd);
                return 0;
            }
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        };
        wc.hInstance = hInst;
        wc.lpszClassName = L"FeathrPDFAboutDlgClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_aboutRegistered = true;
    }

    int dlgW = 380;
    int dlgH = 285;
    RECT rcParent;
    GetWindowRect(m_hWndMain, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"FeathrPDFAboutDlgClass",
        L"About Feathr PDF",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, dlgW, dlgH,
        m_hWndMain, NULL, hInst, NULL
    );

    if (!hDlg) return;

    HWND hBtnOk = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        (dlgW - 100) / 2, 205, 100, 32, hDlg, (HMENU)IDOK, hInst, NULL);

    EnableWindow(m_hWndMain, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(m_hWndMain, TRUE);
    SetForegroundWindow(m_hWndMain);
}

struct ResultDialogAction {
    std::wstring label;
    bool isPrimary;
    std::function<void()> onClick;
};

struct ResultDialogData {
    std::wstring message;
    std::vector<ResultDialogAction> actions;
    std::vector<HWND> btnHwnds;
    HFONT hFontNormal;
    HFONT hFontBold;
    bool isDark;
};

static LRESULT CALLBACK ResultDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ResultDialogData* d = (ResultDialogData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        d = (ResultDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)d);

        d->isDark = PDFApp::Instance().IsDarkMode();
        PDFApp::SetWindowTitlebarTheme(hWnd, d->isDark);
        PDFApp::ApplyUxThemeDarkMode(hWnd, d->isDark);

        d->hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        d->hFontBold = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        RECT clientRc;
        GetClientRect(hWnd, &clientRc);

        int btnH = 32;
        int btnY = clientRc.bottom - btnH - 16;
        int gap = 10;
        int rightMargin = 20;

        HDC hdc = GetDC(hWnd);
        HFONT oldF = (HFONT)SelectObject(hdc, d->hFontBold);
        std::vector<int> btnWidths;
        int totalBtnsW = 0;
        for (const auto& act : d->actions) {
            SIZE sz;
            GetTextExtentPoint32W(hdc, act.label.c_str(), (int)act.label.length(), &sz);
            int w = std::max(80, (int)sz.cx + 28);
            btnWidths.push_back(w);
            totalBtnsW += w;
        }
        totalBtnsW += (int)(d->actions.size() - 1) * gap;
        SelectObject(hdc, oldF);
        ReleaseDC(hWnd, hdc);

        int curX = clientRc.right - rightMargin - totalBtnsW;
        for (size_t i = 0; i < d->actions.size(); ++i) {
            HWND hBtn = CreateWindowW(L"BUTTON", d->actions[i].label.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP | (i == 0 ? WS_GROUP : 0),
                curX, btnY, btnWidths[i], btnH,
                hWnd, (HMENU)(UINT_PTR)(1000 + i), cs->hInstance, NULL);
            d->btnHwnds.push_back(hBtn);
            curX += btnWidths[i] + gap;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HBRUSH hBg = CreateSolidBrush(d->isDark ? RGB(24, 24, 27) : RGB(248, 249, 250));
        FillRect(hdc, &rc, hBg);
        DeleteObject(hBg);

        HFONT hOldFont = (HFONT)SelectObject(hdc, d->hFontNormal);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, d->isDark ? RGB(244, 244, 245) : RGB(24, 24, 27));

        RECT rcText = { 24, 20, rc.right - 24, rc.bottom - 56 };
        DrawTextW(hdc, d->message.c_str(), -1, &rcText, DT_LEFT | DT_WORDBREAK);

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis || !d) break;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;

        HBRUSH hDlgBg = CreateSolidBrush(d->isDark ? RGB(24, 24, 27) : RGB(248, 249, 250));
        FillRect(hdc, &rc, hDlgBg);
        DeleteObject(hDlgBg);

        int actIdx = (int)dis->CtlID - 1000;
        bool isPrimary = (actIdx >= 0 && actIdx < (int)d->actions.size()) ? d->actions[actIdx].isPrimary : false;

        COLORREF btnBg, borderCol, textCol;
        if (isPrimary) {
            btnBg = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);
            borderCol = isPressed ? RGB(29, 78, 216) : RGB(59, 130, 246);
            textCol = RGB(255, 255, 255);
        } else {
            if (d->isDark) {
                btnBg = isPressed ? RGB(24, 24, 27) : RGB(39, 39, 42);
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

        WCHAR text[64] = {0};
        GetWindowTextW(dis->hwndItem, text, 64);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);
        HFONT hOldFont = (HFONT)SelectObject(hdc, isPrimary ? d->hFontBold : d->hFontNormal);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int actIdx = id - 1000;
        if (d && actIdx >= 0 && actIdx < (int)d->actions.size()) {
            auto fn = d->actions[actIdx].onClick;
            DestroyWindow(hWnd);
            if (fn) fn();
            return 0;
        }
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hWnd);
            return 0;
        } else if (wParam == VK_RETURN) {
            if (d && !d->actions.empty()) {
                auto fn = d->actions[0].onClick;
                DestroyWindow(hWnd);
                if (fn) fn();
                return 0;
            }
        }
        break;
    }

    case WM_CLOSE: {
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (d) {
            if (d->hFontNormal) { DeleteObject(d->hFontNormal); d->hFontNormal = NULL; }
            if (d->hFontBold) { DeleteObject(d->hFontBold); d->hFontBold = NULL; }
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowCustomResultDialog(HWND hWndParent, const std::wstring& title, const std::wstring& message, const std::vector<ResultDialogAction>& actions, int dlgWidth = 470, int dlgHeight = 180) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    static bool s_resRegistered = false;
    if (!s_resRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ResultDialogProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"FeathrPDFResultDlgClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_resRegistered = true;
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - dlgWidth) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgHeight) / 2;

    ResultDialogData data;
    data.message = message;
    data.actions = actions;
    data.hFontNormal = NULL;
    data.hFontBold = NULL;
    data.isDark = PDFApp::Instance().IsDarkMode();

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"FeathrPDFResultDlgClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, dlgWidth, dlgHeight,
        hWndParent, NULL, hInst, &data
    );

    if (!hDlg) return;

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
}

void PDFApp::ShowExtractedPdfDialog(int pageCount, const std::wstring& filePath, const std::wstring& fileName) {
    std::wstring pageStr = (pageCount == 1) ? L"page" : L"pages";
    std::wstring msg = L"Extracted successfully " + std::to_wstring(pageCount) + L" " + pageStr + L" into " + fileName;

    std::vector<ResultDialogAction> actions = {
        { L"Open extracted PDF", true, [filePath]() {
            ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }},
        { L"Open directory", false, [filePath]() {
            ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + filePath + L"\"").c_str(), NULL, SW_SHOWNORMAL);
        }},
        { L"Close", false, []() {} }
    };

    ShowCustomResultDialog(m_hWndMain, L"Extracted", msg, actions, 490, 175);
}

void PDFApp::ShowExtractedJpgSingleDialog(const std::wstring& filePath) {
    size_t lastSlash = filePath.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash != std::wstring::npos) ? filePath.substr(lastSlash + 1) : filePath;
    std::wstring msg = L"Extracted successfully 1 page into " + fileName + L"\n\nDo you want to see the image right away?";

    std::vector<ResultDialogAction> actions = {
        { L"Yes", true, [filePath]() {
            ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }},
        { L"Open directory", false, [filePath]() {
            ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + filePath + L"\"").c_str(), NULL, SW_SHOWNORMAL);
        }},
        { L"No", false, []() {} }
    };

    ShowCustomResultDialog(m_hWndMain, L"Extracted", msg, actions, 480, 185);
}

void PDFApp::ShowExtractedJpgMultiDialog(int pageCount, const std::wstring& folderPath) {
    std::wstring msg = L"Extracted successfully " + std::to_wstring(pageCount) + L" pages into folder:\n" + folderPath + L"\n\nDo you want to open where the image saved?";

    std::vector<ResultDialogAction> actions = {
        { L"Yes", true, [folderPath]() {
            ShellExecuteW(NULL, L"open", folderPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }},
        { L"No", false, []() {} }
    };

    ShowCustomResultDialog(m_hWndMain, L"Extracted", msg, actions, 510, 200);
}
