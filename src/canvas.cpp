#include "canvas.h"
#include "app.h"
#include "ribbon.h"
#include "sidebar.h"
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <d2d1.h>
#include <dwrite.h>

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

// Direct2D GPU Hardware Acceleration Resources
static ID2D1Factory* g_pD2DFactory = nullptr;
static IDWriteFactory* g_pDWriteFactory = nullptr;
static IDWriteTextFormat* g_pTextFormatLarge = nullptr;
static IDWriteTextFormat* g_pTextFormatNormal = nullptr;
static IDWriteTextFormat* g_pTextFormatTag = nullptr;
static ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;
static ID2D1SolidColorBrush* g_pSolidBrush = nullptr;
static ID2D1StrokeStyle* g_pDashStrokeStyle = nullptr;

ID2D1HwndRenderTarget* ContinuousCanvas::GetRenderTarget() {
    return g_pRenderTarget;
}

static HRESULT CreateDeviceIndependentResources() {
    HRESULT hr = S_OK;
    if (!g_pD2DFactory) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    }
    if (SUCCEEDED(hr) && !g_pDWriteFactory) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_pDWriteFactory);
    }
    if (SUCCEEDED(hr) && !g_pTextFormatLarge) {
        hr = g_pDWriteFactory->CreateTextFormat(
            L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-us", &g_pTextFormatLarge
        );
        if (SUCCEEDED(hr)) {
            g_pTextFormatLarge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTextFormatLarge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (SUCCEEDED(hr) && !g_pTextFormatNormal) {
        hr = g_pDWriteFactory->CreateTextFormat(
            L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &g_pTextFormatNormal
        );
        if (SUCCEEDED(hr)) {
            g_pTextFormatNormal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTextFormatNormal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (SUCCEEDED(hr) && !g_pTextFormatTag) {
        hr = g_pDWriteFactory->CreateTextFormat(
            L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &g_pTextFormatTag
        );
        if (SUCCEEDED(hr)) {
            g_pTextFormatTag->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTextFormatTag->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (SUCCEEDED(hr) && !g_pDashStrokeStyle) {
        D2D1_STROKE_STYLE_PROPERTIES dashProps = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
            D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f
        );
        g_pD2DFactory->CreateStrokeStyle(dashProps, NULL, 0, &g_pDashStrokeStyle);
    }
    return hr;
}

static HRESULT CreateDeviceResources(HWND hWnd) {
    HRESULT hr = S_OK;
    if (!g_pRenderTarget) {
        RECT rc;
        GetClientRect(hWnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(
            std::max(1L, rc.right - rc.left),
            std::max(1L, rc.bottom - rc.top)
        );

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0.0f, 0.0f,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT
        );

        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hWnd,
            size,
            D2D1_PRESENT_OPTIONS_IMMEDIATELY
        );

        hr = g_pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &g_pRenderTarget);
        if (SUCCEEDED(hr)) {
            g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &g_pSolidBrush);
        }
    }
    return hr;
}

static void DiscardDeviceResources() {
    if (g_pSolidBrush) { g_pSolidBrush->Release(); g_pSolidBrush = nullptr; }
    if (g_pRenderTarget) { g_pRenderTarget->Release(); g_pRenderTarget = nullptr; }
    PDFApp::Instance().InvalidateD2DCache(-1);
}

// Smooth scroll state
static int g_canvasScrollX = 0;
static int g_canvasScrollY = 0;
static double g_subpixelScrollX = 0.0;
static double g_subpixelScrollY = 0.0;
static int g_targetScrollX = 0;
static int g_targetScrollY = 0;
static double g_wheelAccumX = 0.0;
static double g_wheelAccumY = 0.0;
static bool g_hasQueuedScroll = false;
static LARGE_INTEGER g_lastScrollTick = { 0 };
static LARGE_INTEGER g_perfFreq = { 0 };

static const UINT_PTR TIMER_ID_SCROLL_COALESCE = 991;
static const UINT_PTR TIMER_ID_SYNC_ACTIVE_PAGE = 992;
static const UINT_PTR ZOOM_RENDER_TIMER = 993; // 120ms Debounce Timer for Viewport Hardware Blit
static const UINT_PTR TIMER_ID_PRELOAD_DEBOUNCE = 994; // Throttled Preload Queue (Max 1 adjacent page)
static const UINT_PTR TIMER_ID_IDLE_TRIM = 995; // 300ms Idle Memory Trimming Timer

static HFONT g_hCanvasFontLarge = NULL;
static HFONT g_hCanvasFontNormal = NULL;
static HWND g_hBtnEmptyOpen = NULL;

// Active drawing state
static bool g_isDrawing = false;
static int g_activeDrawPage = -1;
static POINT g_drawStart = { 0, 0 };
static POINT g_drawCurrent = { 0, 0 };
static std::vector<POINT> g_inkPoints;

// Pan state
static bool g_isPanning = false;
static POINT g_panStart = { 0, 0 };
static int g_panStartScrollX = 0;
static int g_panStartScrollY = 0;

// Eraser state
static bool g_isErasing = false;
static bool g_erasedAnyInGesture = false;
static POINT g_lastErasePos = { 0, 0 };
static HCURSOR g_hEraserCursor = NULL;

static HCURSOR CreateEraserCursor(int radius = 8) {
    int size = (radius + 4) * 2;
    if (size % 2 != 0) size++;
    int center = size / 2;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    RGBQUAD* pixels = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    memset(pixels, 0, size * size * sizeof(RGBQUAD));

    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Outer dark ring
        Gdiplus::Pen darkPen(Gdiplus::Color(220, 30, 41, 59), 1.5f);
        g.DrawEllipse(&darkPen, (float)(center - radius), (float)(center - radius), (float)(radius * 2), (float)(radius * 2));

        // Inner white ring for high contrast
        Gdiplus::Pen whitePen(Gdiplus::Color(255, 255, 255, 255), 1.0f);
        g.DrawEllipse(&whitePen, (float)(center - radius + 1), (float)(center - radius + 1), (float)((radius - 1) * 2), (float)((radius - 1) * 2));

        // Center dot
        Gdiplus::SolidBrush dotBrush(Gdiplus::Color(220, 15, 23, 42));
        g.FillEllipse(&dotBrush, center - 1, center - 1, 2, 2);
    }

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    HBITMAP hMask = CreateBitmap(size, size, 1, 1, NULL);

    ICONINFO ii = { 0 };
    ii.fIcon = FALSE;
    ii.xHotspot = center;
    ii.yHotspot = center;
    ii.hbmMask = hMask;
    ii.hbmColor = hBmp;

    HCURSOR hCursor = CreateIconIndirect(&ii);

    DeleteObject(hMask);
    DeleteObject(hBmp);

    return hCursor;
}

void ContinuousCanvas::ResetScroll() {
    g_canvasScrollX = 0;
    g_canvasScrollY = 0;
    g_subpixelScrollX = 0.0;
    g_subpixelScrollY = 0.0;
    g_targetScrollX = 0;
    g_targetScrollY = 0;
    g_wheelAccumX = 0.0;
    g_wheelAccumY = 0.0;
    g_hasQueuedScroll = false;
}

static void GetScrollBounds(HWND hWnd, int& maxScrollX, int& maxScrollY, int& clientW, int& clientH, int& maxDisplayW) {
    PDFApp& app = PDFApp::Instance();
    RECT rc;
    GetClientRect(hWnd, &rc);
    clientW = rc.right - rc.left;
    clientH = rc.bottom - rc.top;

    int totalH = app.GetTotalLayoutHeight();
    maxScrollY = std::max(0, totalH - clientH);

    maxDisplayW = app.GetMaxDisplayWidth();
    int totalW = app.GetTotalLayoutWidth();
    maxScrollX = std::max(0, totalW - clientW);
}

static int GetPageDisplayX(int clientW, int pageDisplayW, int maxDisplayW) {
    int totalW = maxDisplayW + 40;
    if (totalW <= clientW) {
        return std::max(20, (clientW - pageDisplayW) / 2);
    }
    return 20 + (maxDisplayW - pageDisplayW) / 2 - g_canvasScrollX;
}

void ContinuousCanvas::UpdateScroll(HWND hCanvas) {
    PDFApp& app = PDFApp::Instance();
    if (!hCanvas) return;

    RECT rc;
    GetClientRect(hCanvas, &rc);
    int clientH = rc.bottom - rc.top;
    int clientW = rc.right - rc.left;

    app.RecalculateLayout(clientW);
    int totalH = app.GetTotalLayoutHeight();
    int totalW = app.GetTotalLayoutWidth();

    // Vertical scrollbar
    SCROLLINFO siV = { sizeof(SCROLLINFO) };
    siV.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siV.nMin = 0;
    siV.nMax = totalH;
    siV.nPage = clientH;
    siV.nPos = g_canvasScrollY;
    SetScrollInfo(hCanvas, SB_VERT, &siV, TRUE);

    if (g_canvasScrollY + clientH > totalH) {
        g_canvasScrollY = std::max(0, totalH - clientH);
        g_subpixelScrollY = (double)g_canvasScrollY;
        g_targetScrollY = g_canvasScrollY;
        SetScrollPos(hCanvas, SB_VERT, g_canvasScrollY, TRUE);
    }

    // Horizontal scrollbar
    SCROLLINFO siH = { sizeof(SCROLLINFO) };
    siH.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siH.nMin = 0;
    siH.nMax = totalW;
    siH.nPage = clientW;
    siH.nPos = g_canvasScrollX;
    SetScrollInfo(hCanvas, SB_HORZ, &siH, TRUE);

    if (g_canvasScrollX + clientW > totalW) {
        g_canvasScrollX = std::max(0, totalW - clientW);
        g_subpixelScrollX = (double)g_canvasScrollX;
        g_targetScrollX = g_canvasScrollX;
        SetScrollPos(hCanvas, SB_HORZ, g_canvasScrollX, TRUE);
    }
}

void ContinuousCanvas::ApplyZoomWithAnchor(HWND hCanvas, float oldZoom, float newZoom, int anchorX, int anchorY) {
    PDFApp& app = PDFApp::Instance();
    if (!hCanvas) return;

    RECT rc;
    GetClientRect(hCanvas, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    if (clientW <= 0 || clientH <= 0) return;

    // 1. Resolve Anchor Point: if < 0, use Viewport Center
    if (anchorX < 0 || anchorY < 0) {
        anchorX = clientW / 2;
        anchorY = clientH / 2;
    }

    // Clamp anchor within client area
    anchorX = std::max(0, std::min(clientW, anchorX));
    anchorY = std::max(0, std::min(clientH, anchorY));

    // 2. Sub-pixel Pivot Preservation: determine document logical coordinates at the anchor point before zoom
    int oldTotalW = app.GetTotalLayoutWidth();
    double contentOriginX = (oldTotalW <= clientW) ? ((clientW - (double)oldTotalW) / 2.0) : (-g_subpixelScrollX);
    double doc_x = ((double)anchorX - contentOriginX) / (double)oldZoom;
    double doc_y = ((double)anchorY + g_subpixelScrollY) / (double)oldZoom;

    // 3. Recalculate layout with new zoom
    app.RecalculateLayout(clientW);
    int totalH = app.GetTotalLayoutHeight();
    int totalW = app.GetTotalLayoutWidth();
    int maxScrollY = std::max(0, totalH - clientH);
    int maxScrollX = std::max(0, totalW - clientW);

    // 4. Compute new scroll coordinates to lock doc_x, doc_y invariant under anchor point P(x, y)
    if (totalW <= clientW) {
        g_subpixelScrollX = 0.0;
    } else {
        double new_scroll_x = (doc_x * (double)newZoom) - (double)anchorX;
        g_subpixelScrollX = std::max(0.0, std::min(new_scroll_x, (double)maxScrollX));
    }

    double new_scroll_y = (doc_y * (double)newZoom) - (double)anchorY;
    g_subpixelScrollY = std::max(0.0, std::min(new_scroll_y, (double)maxScrollY));

    // 5. Update integer coordinates
    g_canvasScrollX = (int)std::round(g_subpixelScrollX);
    g_canvasScrollY = (int)std::round(g_subpixelScrollY);
    g_targetScrollX = g_canvasScrollX;
    g_targetScrollY = g_canvasScrollY;
    g_wheelAccumX = 0.0;
    g_wheelAccumY = 0.0;
    g_hasQueuedScroll = false;

    // 6. Synchronize Win32 scrollbars (SetScrollInfo for both SB_VERT and SB_HORZ)
    SCROLLINFO siV = { sizeof(SCROLLINFO) };
    siV.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siV.nMin = 0;
    siV.nMax = totalH;
    siV.nPage = clientH;
    siV.nPos = g_canvasScrollY;
    SetScrollInfo(hCanvas, SB_VERT, &siV, TRUE);

    SCROLLINFO siH = { sizeof(SCROLLINFO) };
    siH.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siH.nMin = 0;
    siH.nMax = totalW;
    siH.nPage = clientW;
    siH.nPos = g_canvasScrollX;
    SetScrollInfo(hCanvas, SB_HORZ, &siH, TRUE);

    // Center-Viewport Detection & Synchronization (only when not actively zooming)
    if (!app.IsZooming()) {
        PDFApp::Instance().UpdateActivePageFromScroll(g_canvasScrollY, clientH);
        PDFApp::Instance().PruneCache(PDFApp::Instance().GetActivePageIndex());
    }

    // 7. Redraw viewport with new scroll coordinates (preserves exact page position)
    InvalidateRect(hCanvas, NULL, FALSE);
    UpdateWindow(hCanvas);

    if (!app.IsZooming()) {
        SetTimer(hCanvas, TIMER_ID_IDLE_TRIM, 300, NULL);
    }
}

int ContinuousCanvas::GetScrollY() {
    return g_canvasScrollY;
}

int ContinuousCanvas::GetScrollX() {
    return g_canvasScrollX;
}

void ContinuousCanvas::SetScroll(HWND hCanvas, int scrollX, int scrollY) {
    if (!hCanvas) return;
    PDFApp& app = PDFApp::Instance();
    int clientW = 0, clientH = 0, maxScrollX = 0, maxScrollY = 0, maxDisplayW = 0;
    GetScrollBounds(hCanvas, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

    g_canvasScrollX = std::max(0, std::min(scrollX, maxScrollX));
    g_canvasScrollY = std::max(0, std::min(scrollY, maxScrollY));
    g_subpixelScrollX = (double)g_canvasScrollX;
    g_subpixelScrollY = (double)g_canvasScrollY;
    g_targetScrollX = g_canvasScrollX;
    g_targetScrollY = g_canvasScrollY;
    g_wheelAccumX = 0.0;
    g_wheelAccumY = 0.0;
    g_hasQueuedScroll = false;

    SetScrollPos(hCanvas, SB_HORZ, g_canvasScrollX, TRUE);
    SetScrollPos(hCanvas, SB_VERT, g_canvasScrollY, TRUE);

    if (clientH > 0) {
        app.UpdateActivePageFromScroll(g_canvasScrollY, clientH);
    }

    InvalidateRect(hCanvas, NULL, FALSE);
    UpdateWindow(hCanvas);
}

static void ScrollCanvasTo(HWND hWnd, int newX, int newY) {
    int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
    GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

    newX = std::max(0, std::min(newX, maxScrollX));
    newY = std::max(0, std::min(newY, maxScrollY));

    if (newX == g_canvasScrollX && newY == g_canvasScrollY) return;

    g_canvasScrollX = newX;
    g_canvasScrollY = newY;
    g_subpixelScrollX = (double)newX;
    g_subpixelScrollY = (double)newY;
    g_targetScrollX = newX;
    g_targetScrollY = newY;

    SetScrollPos(hWnd, SB_HORZ, g_canvasScrollX, TRUE);
    SetScrollPos(hWnd, SB_VERT, g_canvasScrollY, TRUE);

    // Debounce active page sync & memory pruning:
    // Only update active page, ribbon text, and sidebar after rapid scrolling has settled (100ms)
    SetTimer(hWnd, TIMER_ID_SYNC_ACTIVE_PAGE, 100, NULL);
    KillTimer(hWnd, TIMER_ID_PRELOAD_DEBOUNCE); // Cancel obsolete preloads during rapid motion
    KillTimer(hWnd, TIMER_ID_IDLE_TRIM);        // Postpone idle memory trimming during active motion

    // Direct2D GPU Viewport update
    InvalidateRect(hWnd, NULL, FALSE);
    UpdateWindow(hWnd);
}

static void ScheduleOrApplyScroll(HWND hWnd, int targetX, int targetY) {
    int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
    GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

    targetX = std::max(0, std::min(targetX, maxScrollX));
    targetY = std::max(0, std::min(targetY, maxScrollY));

    if (targetX == g_canvasScrollX && targetY == g_canvasScrollY) {
        return;
    }

    g_targetScrollX = targetX;
    g_targetScrollY = targetY;

    if (g_perfFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_perfFreq);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsedMs = (g_lastScrollTick.QuadPart == 0) ? 999.0 :
        ((double)(now.QuadPart - g_lastScrollTick.QuadPart) * 1000.0 / (double)g_perfFreq.QuadPart);

    if (elapsedMs >= 7.0) {
        // Redraw budget available immediately: blit without latency
        if (g_hasQueuedScroll) {
            KillTimer(hWnd, TIMER_ID_SCROLL_COALESCE);
            g_hasQueuedScroll = false;
        }
        g_lastScrollTick = now;
        ScrollCanvasTo(hWnd, g_targetScrollX, g_targetScrollY);
    } else {
        // Coalesce / debounce rapid trackpad events to monitor refresh interval
        if (!g_hasQueuedScroll) {
            g_hasQueuedScroll = true;
            SetTimer(hWnd, TIMER_ID_SCROLL_COALESCE, 8, NULL);
        }
    }
}

void ContinuousCanvas::ScrollToPage(HWND hCanvas, int pageIdx) {
    PDFApp& app = PDFApp::Instance();
    const auto& pages = app.GetPages();
    if (pageIdx >= 0 && pageIdx < (int)pages.size()) {
        g_canvasScrollX = 0;
        g_subpixelScrollX = 0.0;
        g_targetScrollX = 0;
        g_canvasScrollY = std::max(0, pages[pageIdx].layoutTop - 15);
        g_subpixelScrollY = (double)g_canvasScrollY;
        g_targetScrollY = g_canvasScrollY;
        g_wheelAccumX = 0.0;
        g_wheelAccumY = 0.0;
        g_hasQueuedScroll = false;
        UpdateScroll(hCanvas);
        InvalidateRect(hCanvas, NULL, FALSE);
        app.PruneCache(pageIdx);
    }
}

void ContinuousCanvas::ScrollToPageTop(HWND hCanvas, int pageIdx) {
    ScrollToPage(hCanvas, pageIdx);
}

void ContinuousCanvas::ScrollToPageBottom(HWND hCanvas, int pageIdx) {
    PDFApp& app = PDFApp::Instance();
    const auto& pages = app.GetPages();
    if (!hCanvas || pageIdx < 0 || pageIdx >= (int)pages.size()) return;

    RECT rc;
    GetClientRect(hCanvas, &rc);
    int clientH = rc.bottom - rc.top;
    int maxScrollX = 0, maxScrollY = 0, clientW = 0, maxDisplayW = 0;
    GetScrollBounds(hCanvas, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

    int targetY = pages[pageIdx].layoutBottom + 15 - clientH;
    targetY = std::max(0, std::min(maxScrollY, targetY));

    g_canvasScrollY = targetY;
    g_subpixelScrollY = (double)targetY;
    g_targetScrollY = targetY;
    g_wheelAccumX = 0.0;
    g_wheelAccumY = 0.0;
    g_hasQueuedScroll = false;

    SetScrollPos(hCanvas, SB_VERT, g_canvasScrollY, TRUE);
    UpdateScroll(hCanvas);
    InvalidateRect(hCanvas, NULL, FALSE);
    app.PruneCache(pageIdx);
}

void ContinuousCanvas::ScrollByDelta(HWND hCanvas, int deltaX, int deltaY) {
    if (!hCanvas) return;
    int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
    GetScrollBounds(hCanvas, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

    int targetX = std::max(0, std::min(maxScrollX, g_targetScrollX + deltaX));
    int targetY = std::max(0, std::min(maxScrollY, g_targetScrollY + deltaY));

    ScheduleOrApplyScroll(hCanvas, targetX, targetY);
}

static int HitTestPage(const std::vector<PageInfo>& pages, int clientW, int mouseX, int mouseY) {
    if (pages.empty()) return -1;
    int idx = PDFApp::Instance().FindPageAtY(mouseY);
    if (idx >= 0 && idx < (int)pages.size()) {
        const PageInfo& p = pages[idx];
        if (mouseY >= p.layoutTop && mouseY <= p.layoutBottom) {
            int maxDisplayW = PDFApp::Instance().GetMaxDisplayWidth();
            int pageX = GetPageDisplayX(clientW, p.displayW, maxDisplayW);
            if (mouseX >= pageX && mouseX <= pageX + p.displayW) {
                return idx;
            }
        }
    }
    return -1;
}

static void HandleEraseAt(HWND hWnd, int screenX, int screenY) {
    PDFApp& app = PDFApp::Instance();
    if (!app.HasDocument()) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    int clientW = rc.right - rc.left;

    int docY = screenY + g_canvasScrollY;
    int pageIdx = HitTestPage(app.GetPages(), clientW, screenX, docY);
    if (pageIdx < 0 || pageIdx >= (int)app.GetPages().size()) return;

    const auto& pages = app.GetPages();
    const PageInfo& p = pages[pageIdx];
    int maxDisplayW = app.GetMaxDisplayWidth();
    int pageX = GetPageDisplayX(clientW, p.displayW, maxDisplayW);
    int pageY = p.layoutTop - g_canvasScrollY;

    bool hit = app.EraseAnnotationAt(pageIdx, screenX, screenY, pageX, pageY, p.displayW, p.displayH, !g_erasedAnyInGesture);
    if (hit) {
        g_erasedAnyInGesture = true;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

static void HandleEraseLine(HWND hWnd, POINT p1, POINT p2) {
    float dx = (float)(p2.x - p1.x);
    float dy = (float)(p2.y - p1.y);
    float dist = std::sqrt(dx * dx + dy * dy);
    int steps = std::max(1, (int)(dist / 5.0f));

    for (int s = 0; s <= steps; ++s) {
        float t = (float)s / (float)steps;
        int sx = (int)(p1.x + t * dx + 0.5f);
        int sy = (int)(p1.y + t * dy + 0.5f);
        HandleEraseAt(hWnd, sx, sy);
    }
}

static LRESULT CALLBACK CanvasWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PDFApp& app = PDFApp::Instance();

    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // Prevent background erasing flicker on Intel Iris Xe

    case WM_CREATE: {
        DragAcceptFiles(hWnd, TRUE);

        CreateDeviceIndependentResources();
        CreateDeviceResources(hWnd);

        if (!g_hCanvasFontLarge) {
            g_hCanvasFontLarge = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hCanvasFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }

        // Open button in empty state
        g_hBtnEmptyOpen = CreateWindowW(L"BUTTON", L"Open PDF Document...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 260, 42, hWnd, (HMENU)100, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessageW(g_hBtnEmptyOpen, WM_SETFONT, (WPARAM)g_hCanvasFontNormal, TRUE);

        // Register window for native multi-touch gestures (Pinch-to-zoom)
        GESTURECONFIG gc = { 0, GC_ALLGESTURES, 0 };
        SetGestureConfig(hWnd, 0, 1, &gc, sizeof(GESTURECONFIG));
        return 0;
    }

    case WM_GESTURE: {
        GESTUREINFO gi = { sizeof(GESTUREINFO) };
        if (GetGestureInfo((HGESTUREINFO)lParam, &gi)) {
            if (gi.dwID == GID_ZOOM) {
                static double s_lastGestureDist = 0.0;

                if (gi.dwFlags & GF_BEGIN) {
                    s_lastGestureDist = (double)gi.ullArguments;
                    app.SetIsZooming(true);
                } else if (gi.dwFlags & GF_END) {
                    s_lastGestureDist = 0.0;
                    SetTimer(hWnd, ZOOM_RENDER_TIMER, 120, NULL);
                } else if (s_lastGestureDist > 0.0 && gi.ullArguments > 0) {
                    double currentDist = (double)gi.ullArguments;
                    double factor = currentDist / s_lastGestureDist;
                    s_lastGestureDist = currentDist;

                    POINT ptPivot = { gi.ptsLocation.x, gi.ptsLocation.y };
                    ScreenToClient(hWnd, &ptPivot);

                    app.SetIsZooming(true);
                    SetTimer(hWnd, ZOOM_RENDER_TIMER, 120, NULL);

                    float currentZoom = app.GetZoom();
                    float nextZoom = (float)(currentZoom * factor);
                    app.SetZoom(nextZoom, false, ptPivot.x, ptPivot.y);
                }
                CloseGestureInfoHandle((HGESTUREINFO)lParam);
                return 0;
            }
            CloseGestureInfoHandle((HGESTUREINFO)lParam);
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        WCHAR szFile[MAX_PATH] = { 0 };
        if (DragQueryFileW(hDrop, 0, szFile, MAX_PATH)) {
            app.OpenDocument(szFile);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);

        if (g_pRenderTarget) {
            g_pRenderTarget->Resize(D2D1::SizeU(std::max(1, w), std::max(1, h)));
        }

        ContinuousCanvas::UpdateScroll(hWnd);

        // Position open button at bottom of drop zone
        int btnW = 260;
        int btnH = 42;
        SetWindowPos(g_hBtnEmptyOpen, NULL, (w - btnW) / 2, h / 2 + 65, btnW, btnH, SWP_NOZORDER);
        ShowWindow(g_hBtnEmptyOpen, app.HasDocument() ? SW_HIDE : SW_SHOW);

        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == 100) {
            // "Mở File PDF..." button clicked in empty state
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            WCHAR szFile[MAX_PATH] = { 0 };
            ofn.hwndOwner = app.GetMainWindow();
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                app.OpenDocument(szFile);
            }
        }
        return 0;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wParam;
        COLORREF bgCol = RGB(37, 99, 235);
        SetTextColor(hdcBtn, RGB(255, 255, 255));
        SetBkColor(hdcBtn, bgCol);
        static HBRUSH hBrBtn = CreateSolidBrush(RGB(37, 99, 235));
        return (LRESULT)hBrBtn;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis || dis->CtlID != 100) break;

        bool isDark = app.IsDarkMode();
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;

        // BẮT BUỘC: Lấp đầy toàn bộ diện tích rc bằng cọ vẽ có màu trùng khớp 100% với màu nền
        // của thẻ Drop Zone (#18181b trong Dark Mode, #ffffff trong Light Mode)
        // để 4 góc bo cong RoundRect hòa lẫn hoàn toàn vào nền thẻ, không xuất hiện vệt đen hay răng cưa!
        COLORREF cardBg = isDark ? RGB(24, 24, 27) : RGB(255, 255, 255);
        HBRUSH hCardBgBrush = CreateSolidBrush(cardBg);
        FillRect(hdc, &rc, hCardBgBrush);
        DeleteObject(hCardBgBrush);

        // Nền nút #2563eb (hoặc #1d4ed8 khi nhấn), viền sáng, chữ trắng
        COLORREF btnBg = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);     // #1d4ed8 vs #2563eb
        COLORREF borderCol = isPressed ? RGB(29, 78, 216) : (isDark ? RGB(59, 130, 246) : RGB(29, 78, 216));
        COLORREF textCol = RGB(255, 255, 255);

        HBRUSH hBr = CreateSolidBrush(btnBg);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBr);
        DeleteObject(hPen);

        WCHAR text[128] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 128);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hCanvasFontNormal);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);

        return TRUE;
    }

    case WM_VSCROLL: {
        int action = LOWORD(wParam);
        int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
        GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

        int targetY = g_canvasScrollY;
        switch (action) {
        case SB_LINEUP: targetY -= 40; break;
        case SB_LINEDOWN: targetY += 40; break;
        case SB_PAGEUP: targetY -= clientH * 3 / 4; break;
        case SB_PAGEDOWN: targetY += clientH * 3 / 4; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            targetY = HIWORD(wParam);
            break;
        }

        targetY = std::max(0, std::min(targetY, maxScrollY));
        if (targetY != g_canvasScrollY) {
            g_wheelAccumY = 0.0;
            ScrollCanvasTo(hWnd, g_canvasScrollX, targetY);
        }
        return 0;
    }

    case WM_HSCROLL: {
        int action = LOWORD(wParam);
        int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
        GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

        int targetX = g_canvasScrollX;
        switch (action) {
        case SB_LINELEFT: targetX -= 40; break;
        case SB_LINERIGHT: targetX += 40; break;
        case SB_PAGELEFT: targetX -= clientW * 3 / 4; break;
        case SB_PAGERIGHT: targetX += clientW * 3 / 4; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            targetX = HIWORD(wParam);
            break;
        }

        targetX = std::max(0, std::min(targetX, maxScrollX));
        if (targetX != g_canvasScrollX) {
            g_wheelAccumX = 0.0;
            ScrollCanvasTo(hWnd, targetX, g_canvasScrollY);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT ptScreen = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        HWND hSidebar = app.GetSidebarWindow();
        if (hSidebar && app.IsSidebarVisible() && IsWindowVisible(hSidebar)) {
            RECT rcSide;
            GetWindowRect(hSidebar, &rcSide);
            if (PtInRect(&rcSide, ptScreen)) {
                SendMessageW(hSidebar, WM_MOUSEWHEEL, wParam, lParam);
                return 0;
            }
        }

        if (GetKeyState(VK_CONTROL) & 0x8000) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta != 0) {
                // Smooth exponential zoom: factor = pow(1.0015, wheel_delta)
                double factor = std::pow(1.0015, (double)delta);
                float currentZoom = app.GetZoom();
                float nextZoom = (float)(currentZoom * factor);

                // Viewport anchor: mouse cursor coordinates relative to canvas
                POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
                ScreenToClient(hWnd, &pt);

                app.SetIsZooming(true);
                SetTimer(hWnd, ZOOM_RENDER_TIMER, 120, NULL);

                app.SetZoom(nextZoom, false, pt.x, pt.y);
            }
            return 0;
        }

        // Smooth vertical scroll with fractional delta accumulator
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_wheelAccumY += (double)delta;

        // Scale: ~0.833 pixels per delta unit (100px per standard 120-unit notch)
        double pixelDeltaY = g_wheelAccumY * (100.0 / 120.0);
        if (std::abs(pixelDeltaY) >= 1.0) {
            int moveY = (int)pixelDeltaY;
            g_wheelAccumY -= ((double)moveY * (120.0 / 100.0));

            int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
            GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

            int targetY = g_targetScrollY - moveY;
            if (targetY <= 0 || targetY >= maxScrollY) {
                g_wheelAccumY = 0.0; // Clear inertia at boundaries
            }
            ScheduleOrApplyScroll(hWnd, g_targetScrollX, targetY);
        }
        return 0;
    }

    case WM_MOUSEHWHEEL: {
        POINT ptScreen = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        HWND hSidebar = app.GetSidebarWindow();
        if (hSidebar && app.IsSidebarVisible() && IsWindowVisible(hSidebar)) {
            RECT rcSide;
            GetWindowRect(hSidebar, &rcSide);
            if (PtInRect(&rcSide, ptScreen)) {
                SendMessageW(hSidebar, WM_MOUSEHWHEEL, wParam, lParam);
                return 0;
            }
        }

        // Smooth horizontal scroll for trackpad 2-finger horizontal swipe
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_wheelAccumX += (double)delta;

        // In Windows WM_MOUSEHWHEEL, positive delta indicates rotation/swipe to the right
        double pixelDeltaX = g_wheelAccumX * (100.0 / 120.0);
        if (std::abs(pixelDeltaX) >= 1.0) {
            int moveX = (int)pixelDeltaX;
            g_wheelAccumX -= ((double)moveX * (120.0 / 100.0));

            int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
            GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

            int targetX = g_targetScrollX + moveX;
            if (targetX <= 0 || targetX >= maxScrollX) {
                g_wheelAccumX = 0.0; // Clear inertia at boundaries
            }
            ScheduleOrApplyScroll(hWnd, targetX, g_targetScrollY);
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_ID_SCROLL_COALESCE) {
            KillTimer(hWnd, TIMER_ID_SCROLL_COALESCE);
            if (g_hasQueuedScroll) {
                g_hasQueuedScroll = false;
                QueryPerformanceCounter(&g_lastScrollTick);
                ScrollCanvasTo(hWnd, g_targetScrollX, g_targetScrollY);
            }
            return 0;
        }
        if (wParam == TIMER_ID_SYNC_ACTIVE_PAGE) {
            KillTimer(hWnd, TIMER_ID_SYNC_ACTIVE_PAGE);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int clientH = rc.bottom - rc.top;
            app.UpdateActivePageFromScroll(g_canvasScrollY, clientH);
            app.PruneCache(app.GetActivePageIndex());
            HWND hSidebar = app.GetSidebarWindow();
            if (hSidebar && app.IsSidebarVisible()) {
                NavigationSidebar::PruneThumbnails(hSidebar);
            }
            app.SaveCurrentSession();
            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }
        if (wParam == ZOOM_RENDER_TIMER) {
            KillTimer(hWnd, ZOOM_RENDER_TIMER);
            app.SetIsZooming(false);
            app.InvalidatePageCache(-1);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int clientH = rc.bottom - rc.top;
            app.UpdateActivePageFromScroll(g_canvasScrollY, clientH);
            app.PruneCache(app.GetActivePageIndex());
            if (app.GetRibbonWindow()) {
                RibbonBar::SetZoomSlider(app.GetRibbonWindow(), (int)(app.GetZoom() * 100.0f));
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }
        if (wParam == TIMER_ID_PRELOAD_DEBOUNCE) {
            KillTimer(hWnd, TIMER_ID_PRELOAD_DEBOUNCE);
            if (!app.IsZooming() && app.HasDocument() && app.GetZoom() <= 1.5f) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                int clientH = rc.bottom - rc.top;
                if (clientH > 0) {
                    int firstVis = app.FindPageAtY(g_canvasScrollY);
                    int lastVis = app.FindPageAtY(g_canvasScrollY + clientH);
                    if (firstVis < 0) firstVis = 0;
                    if (lastVis < 0) lastVis = firstVis;

                    // Render Queue Throttling: preload strictly at most 1 adjacent page (only when zoom <= 150%)
                    if (lastVis + 1 < app.GetPageCount()) {
                        app.PreloadPageCache(lastVis + 1);
                    }
                    if (firstVis > 0) {
                        app.PreloadPageCache(firstVis - 1);
                    }
                }
            }
            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }
        if (wParam == TIMER_ID_IDLE_TRIM) {
            KillTimer(hWnd, TIMER_ID_IDLE_TRIM);
            if (!app.IsZooming() && !g_isDrawing && !g_isPanning && !g_isErasing) {
                app.TrimWorkingSetMemory();
            }
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!app.HasDocument()) return 0;
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int docX = x + g_canvasScrollX;
        int docY = y + g_canvasScrollY;

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;

        ToolMode tool = app.GetTool();
        if (tool == TOOL_PAN) {
            g_isPanning = true;
            g_panStart = { x, y };
            g_panStartScrollX = g_canvasScrollX;
            g_panStartScrollY = g_canvasScrollY;
            SetCapture(hWnd);
        } else if (tool == TOOL_HIGHLIGHT || tool == TOOL_PEN) {
            int pageIdx = HitTestPage(app.GetPages(), clientW, x, docY);
            if (pageIdx >= 0) {
                g_isDrawing = true;
                g_activeDrawPage = pageIdx;
                g_drawStart = { docX, docY };
                g_drawCurrent = { docX, docY };
                g_inkPoints.clear();
                g_inkPoints.push_back({ docX, docY });
                SetCapture(hWnd);
            }
        } else if (tool == TOOL_ERASER) {
            g_isErasing = true;
            g_erasedAnyInGesture = false;
            g_lastErasePos = { x, y };
            SetCapture(hWnd);
            HandleEraseAt(hWnd, x, y);
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            ToolMode tool = app.GetTool();
            if (tool == TOOL_PAN) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
            } else if (tool == TOOL_HIGHLIGHT || tool == TOOL_PEN) {
                SetCursor(LoadCursor(NULL, IDC_CROSS));
            } else if (tool == TOOL_ERASER) {
                if (!g_hEraserCursor) g_hEraserCursor = CreateEraserCursor(8);
                SetCursor(g_hEraserCursor ? g_hEraserCursor : LoadCursor(NULL, IDC_CROSS));
            } else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (g_isPanning) {
            int dx = x - g_panStart.x;
            int dy = y - g_panStart.y;
            int maxScrollX = 0, maxScrollY = 0, clientW = 0, clientH = 0, maxDisplayW = 0;
            GetScrollBounds(hWnd, maxScrollX, maxScrollY, clientW, clientH, maxDisplayW);

            int targetX = std::max(0, std::min(g_panStartScrollX - dx, maxScrollX));
            int targetY = std::max(0, std::min(g_panStartScrollY - dy, maxScrollY));

            ScrollCanvasTo(hWnd, targetX, targetY);
            return 0;
        }

        if (g_isErasing) {
            POINT curPos = { x, y };
            HandleEraseLine(hWnd, g_lastErasePos, curPos);
            g_lastErasePos = curPos;
            return 0;
        }

        if (g_isDrawing && g_activeDrawPage >= 0) {
            int docX = x + g_canvasScrollX;
            int docY = y + g_canvasScrollY;
            g_drawCurrent = { docX, docY };
            if (app.GetTool() == TOOL_PEN) {
                g_inkPoints.push_back({ docX, docY });
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_isPanning) {
            g_isPanning = false;
            ReleaseCapture();
            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }

        if (g_isErasing) {
            g_isErasing = false;
            ReleaseCapture();
            if (g_erasedAnyInGesture) {
                app.CommitStateAfterEdit();
                app.RefreshAllViews();
            }
            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }

        if (g_isDrawing && g_activeDrawPage >= 0) {
            ReleaseCapture();
            g_isDrawing = false;
            int drawPage = g_activeDrawPage;
            g_activeDrawPage = -1;
            std::vector<POINT> inkCopy = g_inkPoints;
            g_inkPoints.clear();

            RECT rc;
            GetClientRect(hWnd, &rc);
            int clientW = rc.right - rc.left;
            const auto& pages = app.GetPages();

            if (drawPage >= 0 && drawPage < (int)pages.size()) {
                const PageInfo& p = pages[drawPage];
                int maxDisplayW = app.GetMaxDisplayWidth();
                int pageX = GetPageDisplayX(clientW, p.displayW, maxDisplayW);
                int docPageX = pageX + g_canvasScrollX;
                int pageY = p.layoutTop;

                ToolMode tool = app.GetTool();
                if (tool == TOOL_HIGHLIGHT) {
                    // Coordinates in pixels relative to page top-left
                    float relX1 = (float)(std::min(g_drawStart.x, g_drawCurrent.x) - docPageX);
                    float relY1 = (float)(std::min(g_drawStart.y, g_drawCurrent.y) - pageY);
                    float relX2 = (float)(std::max(g_drawStart.x, g_drawCurrent.x) - docPageX);
                    float relY2 = (float)(std::max(g_drawStart.y, g_drawCurrent.y) - pageY);

                    // Clamp to page
                    relX1 = std::max(0.0f, std::min((float)p.displayW, relX1));
                    relY1 = std::max(0.0f, std::min((float)p.displayH, relY1));
                    relX2 = std::max(0.0f, std::min((float)p.displayW, relX2));
                    relY2 = std::max(0.0f, std::min((float)p.displayH, relY2));

                    if (relX2 - relX1 > 3 && relY2 - relY1 > 3) {
                        // Map pixels to PDF points (origin is bottom-left in PDF)
                        float pdfX1 = (float)(relX1 * (p.widthPt / p.displayW));
                        float pdfX2 = (float)(relX2 * (p.widthPt / p.displayW));
                        float pdfY1 = (float)((p.displayH - relY2) * (p.heightPt / p.displayH));
                        float pdfY2 = (float)((p.displayH - relY1) * (p.heightPt / p.displayH));

                        app.ApplyHighlight(drawPage, pdfX1, pdfY1, pdfX2, pdfY2);
                    }
                } else if (tool == TOOL_PEN && inkCopy.size() > 1) {
                    // Convert doc coords to screen coords relative to page
                    std::vector<POINT> screenPts;
                    screenPts.reserve(inkCopy.size());
                    for (const auto& pt : inkCopy) {
                        screenPts.push_back({ pt.x - g_canvasScrollX, pt.y });
                    }
                    app.ApplyInkPath(drawPage, screenPts, pageX, pageY, p.displayW, p.displayH);
                }
            }

            SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            return 0;
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);

        HRESULT hr = CreateDeviceResources(hWnd);
        if (SUCCEEDED(hr) && g_pRenderTarget) {
            g_pRenderTarget->BeginDraw();

            RECT clientRc;
            GetClientRect(hWnd, &clientRc);
            int clientW = clientRc.right - clientRc.left;
            int clientH = clientRc.bottom - clientRc.top;

            bool isDark = app.IsDarkMode();

            D2D1_COLOR_F bgCol = isDark ? 
                D2D1::ColorF(17.0f / 255.0f, 17.0f / 255.0f, 19.0f / 255.0f) : 
                D2D1::ColorF(229.0f / 255.0f, 231.0f / 255.0f, 235.0f / 255.0f);
            g_pRenderTarget->Clear(bgCol);

            if (!app.HasDocument()) {
                ShowWindow(g_hBtnEmptyOpen, SW_SHOW);

                // Centered Drop Zone Card
                int cardW = 540;
                int cardH = 300;
                int cardX = (clientW - cardW) / 2;
                int cardY = (clientH - cardH) / 2 - 20;

                D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(
                    D2D1::RectF((float)cardX, (float)cardY, (float)(cardX + cardW), (float)(cardY + cardH)),
                    16.0f, 16.0f
                );

                // Fill card
                D2D1_COLOR_F cardBg = isDark ? 
                    D2D1::ColorF(24.0f / 255.0f, 24.0f / 255.0f, 27.0f / 255.0f) : 
                    D2D1::ColorF(1.0f, 1.0f, 1.0f);
                g_pSolidBrush->SetColor(cardBg);
                g_pRenderTarget->FillRoundedRectangle(roundedCard, g_pSolidBrush);

                // Stroke card border
                D2D1_COLOR_F cardBorder = isDark ? 
                    D2D1::ColorF(39.0f / 255.0f, 39.0f / 255.0f, 42.0f / 255.0f) : 
                    D2D1::ColorF(180.0f / 255.0f, 185.0f / 255.0f, 195.0f / 255.0f);
                g_pSolidBrush->SetColor(cardBorder);
                g_pRenderTarget->DrawRoundedRectangle(roundedCard, g_pSolidBrush, 2.0f, g_pDashStrokeStyle);

                // Title
                D2D1_COLOR_F titleCol = isDark ? 
                    D2D1::ColorF(244.0f / 255.0f, 244.0f / 255.0f, 245.0f / 255.0f) : 
                    D2D1::ColorF(32.0f / 255.0f, 33.0f / 255.0f, 36.0f / 255.0f);
                g_pSolidBrush->SetColor(titleCol);
                D2D1_RECT_F titleRc = D2D1::RectF((float)cardX, (float)(cardY + 50), (float)(cardX + cardW), (float)(cardY + 95));
                const WCHAR szTitle[] = L"Drag and drop PDF files here";
                if (g_pTextFormatLarge) {
                    g_pRenderTarget->DrawTextW(szTitle, (UINT32)wcslen(szTitle), g_pTextFormatLarge, titleRc, g_pSolidBrush);
                }

                // Subtitle
                D2D1_COLOR_F subCol = isDark ? 
                    D2D1::ColorF(161.0f / 255.0f, 161.0f / 255.0f, 170.0f / 255.0f) : 
                    D2D1::ColorF(95.0f / 255.0f, 99.0f / 255.0f, 104.0f / 255.0f);
                g_pSolidBrush->SetColor(subCol);
                D2D1_RECT_F subRc = D2D1::RectF((float)(cardX + 20), (float)(cardY + 105), (float)(cardX + cardW - 20), (float)(cardY + 145));
                const WCHAR szSub[] = L"Instant high-performance native reading • Smooth zooming & annotations";
                if (g_pTextFormatNormal) {
                    g_pRenderTarget->DrawTextW(szSub, (UINT32)wcslen(szSub), g_pTextFormatNormal, subRc, g_pSolidBrush);
                }
            } else {
                ShowWindow(g_hBtnEmptyOpen, SW_HIDE);

                const auto& pages = app.GetPages();
                int maxDisplayW = app.GetMaxDisplayWidth();

                int firstVisPage = -1;
                int lastVisPage = -1;

                if (!pages.empty()) {
                    int startIdx = app.FindPageAtY(g_canvasScrollY);
                    if (startIdx < 0) startIdx = 0;
                    else if (startIdx > 0) startIdx--;

                    for (size_t i = (size_t)startIdx; i < pages.size(); ++i) {
                        const PageInfo& p = pages[i];
                        int itemTop = p.layoutTop - g_canvasScrollY;
                        int itemBottom = p.layoutBottom - g_canvasScrollY;

                        // Stop iterating if completely below viewport
                        if (itemTop > clientH + 20) {
                            break;
                        }

                        // Skip pages completely above viewport
                        if (itemBottom < -20) {
                            continue;
                        }

                        if (firstVisPage == -1) firstVisPage = (int)i;
                        lastVisPage = (int)i;

                        int pageX = GetPageDisplayX(clientW, p.displayW, maxDisplayW);

                        // Page drop shadow
                        D2D1_RECT_F shadowRc = D2D1::RectF((float)(pageX + 4), (float)(itemTop + 4), (float)(pageX + p.displayW + 6), (float)(itemTop + p.displayH + 6));
                        D2D1_COLOR_F shadowCol = isDark ? 
                            D2D1::ColorF(8.0f / 255.0f, 8.0f / 255.0f, 10.0f / 255.0f) : 
                            D2D1::ColorF(205.0f / 255.0f, 210.0f / 255.0f, 218.0f / 255.0f);
                        g_pSolidBrush->SetColor(shadowCol);
                        g_pRenderTarget->FillRectangle(shadowRc, g_pSolidBrush);

                        // White paper
                        D2D1_RECT_F paperRc = D2D1::RectF((float)pageX, (float)itemTop, (float)(pageX + p.displayW), (float)(itemTop + p.displayH));
                        g_pSolidBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
                        g_pRenderTarget->FillRectangle(paperRc, g_pSolidBrush);

                        // Direct2D GPU Texture Upload & Bilinear Scaling
                        ID2D1Bitmap* pPageBmp = app.GetPageD2DBitmap(g_pRenderTarget, (int)i, p.displayW, p.displayH);
                        if (pPageBmp) {
                            g_pRenderTarget->DrawBitmap(pPageBmp, paperRc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                        }

                        // Layer 2: Annotations via GPU shaders
                        app.RenderAnnotationsToD2D(g_pRenderTarget, (int)i, (float)pageX, (float)itemTop, (float)p.displayW, (float)p.displayH);

                        // Page outline border
                        D2D1_COLOR_F borderCol = isDark ? 
                            D2D1::ColorF(39.0f / 255.0f, 39.0f / 255.0f, 42.0f / 255.0f) : 
                            D2D1::ColorF(189.0f / 255.0f, 193.0f / 255.0f, 198.0f / 255.0f);
                        g_pSolidBrush->SetColor(borderCol);
                        g_pRenderTarget->DrawRectangle(paperRc, g_pSolidBrush, 1.0f);

                        // Page number tag
                        std::wstring pgNumStr = std::to_wstring(i + 1);
                        D2D1_RECT_F tagRc = D2D1::RectF((float)pageX, (float)(itemTop + p.displayH + 4), (float)(pageX + p.displayW), (float)(itemTop + p.displayH + 20));
                        D2D1_COLOR_F tagCol = isDark ? 
                            D2D1::ColorF(161.0f / 255.0f, 161.0f / 255.0f, 170.0f / 255.0f) : 
                            D2D1::ColorF(128.0f / 255.0f, 134.0f / 255.0f, 139.0f / 255.0f);
                        g_pSolidBrush->SetColor(tagCol);
                        if (g_pTextFormatTag) {
                            g_pRenderTarget->DrawTextW(pgNumStr.c_str(), (UINT32)pgNumStr.length(), g_pTextFormatTag, tagRc, g_pSolidBrush);
                        }
                    }

                    // Live Highlight Preview
                    if (g_isDrawing && app.GetTool() == TOOL_HIGHLIGHT && g_activeDrawPage >= 0) {
                        float rLeft = (float)(std::min(g_drawStart.x, g_drawCurrent.x) - g_canvasScrollX);
                        float rTop = (float)(std::min(g_drawStart.y, g_drawCurrent.y) - g_canvasScrollY);
                        float rRight = (float)(std::max(g_drawStart.x, g_drawCurrent.x) - g_canvasScrollX);
                        float rBottom = (float)(std::max(g_drawStart.y, g_drawCurrent.y) - g_canvasScrollY);

                        ToolConfig cfg = app.GetHighlightConfig();
                        D2D1_COLOR_F hlCol = D2D1::ColorF(
                            GetRValue(cfg.color) / 255.0f,
                            GetGValue(cfg.color) / 255.0f,
                            GetBValue(cfg.color) / 255.0f,
                            cfg.opacity
                        );
                        g_pSolidBrush->SetColor(hlCol);
                        g_pRenderTarget->FillRectangle(D2D1::RectF(rLeft, rTop, rRight, rBottom), g_pSolidBrush);
                    }

                    // Live Ink Preview
                    if (g_isDrawing && app.GetTool() == TOOL_PEN && g_inkPoints.size() > 1) {
                        ToolConfig cfg = app.GetPenConfig();
                        D2D1_COLOR_F penCol = D2D1::ColorF(
                            GetRValue(cfg.color) / 255.0f,
                            GetGValue(cfg.color) / 255.0f,
                            GetBValue(cfg.color) / 255.0f,
                            cfg.opacity
                        );
                        g_pSolidBrush->SetColor(penCol);
                        float strokeW = cfg.strokeWidth * app.GetZoom();
                        if (strokeW < 1.0f) strokeW = 1.0f;

                        ID2D1StrokeStyle* pRoundStroke = nullptr;
                        D2D1_STROKE_STYLE_PROPERTIES roundProps = D2D1::StrokeStyleProperties(
                            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                            D2D1_LINE_JOIN_ROUND
                        );
                        g_pD2DFactory->CreateStrokeStyle(roundProps, NULL, 0, &pRoundStroke);

                        ID2D1PathGeometry* pPathGeo = nullptr;
                        if (SUCCEEDED(g_pD2DFactory->CreatePathGeometry(&pPathGeo))) {
                            ID2D1GeometrySink* pSink = nullptr;
                            if (SUCCEEDED(pPathGeo->Open(&pSink))) {
                                D2D1_POINT_2F startPt = D2D1::Point2F(
                                    (float)(g_inkPoints[0].x - g_canvasScrollX),
                                    (float)(g_inkPoints[0].y - g_canvasScrollY)
                                );
                                pSink->BeginFigure(startPt, D2D1_FIGURE_BEGIN_HOLLOW);
                                for (size_t k = 1; k < g_inkPoints.size(); ++k) {
                                    pSink->AddLine(D2D1::Point2F(
                                        (float)(g_inkPoints[k].x - g_canvasScrollX),
                                        (float)(g_inkPoints[k].y - g_canvasScrollY)
                                    ));
                                }
                                pSink->EndFigure(D2D1_FIGURE_END_OPEN);
                                pSink->Close();
                                pSink->Release();
                            }
                            g_pRenderTarget->DrawGeometry(pPathGeo, g_pSolidBrush, strokeW, pRoundStroke);
                            pPathGeo->Release();
                        }
                        if (pRoundStroke) pRoundStroke->Release();
                    }

                    // Preload adjacent page (strictly only at zoom <= 150%)
                    if (firstVisPage >= 0 && app.GetZoom() <= 1.5f) {
                        SetTimer(hWnd, TIMER_ID_PRELOAD_DEBOUNCE, 80, NULL);
                    }
                }
            }

            HRESULT hrEnd = g_pRenderTarget->EndDraw();
            if (hrEnd == D2DERR_RECREATE_TARGET) {
                DiscardDeviceResources();
                InvalidateRect(hWnd, NULL, FALSE);
            } else {
                // Schedule idle memory trimming after frame is fully presented
                SetTimer(hWnd, TIMER_ID_IDLE_TRIM, 300, NULL);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, TIMER_ID_IDLE_TRIM);
        KillTimer(hWnd, ZOOM_RENDER_TIMER);
        KillTimer(hWnd, TIMER_ID_SYNC_ACTIVE_PAGE);
        KillTimer(hWnd, TIMER_ID_PRELOAD_DEBOUNCE);
        KillTimer(hWnd, TIMER_ID_SCROLL_COALESCE);
        DiscardDeviceResources();
        if (g_pDashStrokeStyle) { g_pDashStrokeStyle->Release(); g_pDashStrokeStyle = nullptr; }
        if (g_pTextFormatTag) { g_pTextFormatTag->Release(); g_pTextFormatTag = nullptr; }
        if (g_pTextFormatNormal) { g_pTextFormatNormal->Release(); g_pTextFormatNormal = nullptr; }
        if (g_pTextFormatLarge) { g_pTextFormatLarge->Release(); g_pTextFormatLarge = nullptr; }
        if (g_pDWriteFactory) { g_pDWriteFactory->Release(); g_pDWriteFactory = nullptr; }
        if (g_pD2DFactory) { g_pD2DFactory->Release(); g_pD2DFactory = nullptr; }
        if (g_hCanvasFontLarge) { DeleteObject(g_hCanvasFontLarge); g_hCanvasFontLarge = NULL; }
        if (g_hCanvasFontNormal) { DeleteObject(g_hCanvasFontNormal); g_hCanvasFontNormal = NULL; }
        if (g_hEraserCursor) { DestroyIcon(g_hEraserCursor); g_hEraserCursor = NULL; }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool ContinuousCanvas::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = CanvasWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FeathrPDFContinuousCanvasClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    return RegisterClassExW(&wc) != 0;
}

HWND ContinuousCanvas::Create(HWND hWndParent, HINSTANCE hInstance) {
    return CreateWindowExW(
        WS_EX_ACCEPTFILES, L"FeathrPDFContinuousCanvasClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL,
        220, 88, 780, 600,
        hWndParent, NULL, hInstance, NULL
    );
}
