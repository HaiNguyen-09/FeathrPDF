#pragma once

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <algorithm>
#include "fpdfview.h"
#include "fpdf_edit.h"
#include "fpdf_annot.h"
#include "fpdf_save.h"
#include "fpdf_ppo.h"

enum ToolMode {
    TOOL_PAN = 0,
    TOOL_HIGHLIGHT,
    TOOL_PEN,
    TOOL_ERASER
};

struct ToolConfig {
    COLORREF color;
    float strokeWidth; // 1.0f to 25.0f
    float opacity;     // 0.10f to 1.0f (10% to 100%)
};

enum AnnotationType {
    ANNOT_HIGHLIGHT = 0,
    ANNOT_INK
};

struct AnnotationItem {
    int id;
    AnnotationType type;
    float rectX1;
    float rectY1;
    float rectX2;
    float rectY2;
    std::vector<POINTFLOAT> points;
    COLORREF color;
    float strokeWidth;
    float opacity; // 0.10f to 1.00f
    bool is_active; // true = visible, false = soft-hidden / undone

    AnnotationItem()
        : id(0), type(ANNOT_HIGHLIGHT), rectX1(0), rectY1(0), rectX2(0), rectY2(0),
          color(0), strokeWidth(1.0f), opacity(1.0f), is_active(true) {}

    bool operator==(const AnnotationItem& o) const {
        if (id != o.id || type != o.type || color != o.color || is_active != o.is_active) return false;
        if (std::abs(strokeWidth - o.strokeWidth) > 0.01f) return false;
        if (std::abs(opacity - o.opacity) > 0.01f) return false;
        if (type == ANNOT_HIGHLIGHT) {
            return std::abs(rectX1 - o.rectX1) < 0.01f &&
                   std::abs(rectY1 - o.rectY1) < 0.01f &&
                   std::abs(rectX2 - o.rectX2) < 0.01f &&
                   std::abs(rectY2 - o.rectY2) < 0.01f;
        } else {
            if (points.size() != o.points.size()) return false;
            for (size_t i = 0; i < points.size(); ++i) {
                if (std::abs(points[i].x - o.points[i].x) > 0.01f ||
                    std::abs(points[i].y - o.points[i].y) > 0.01f) return false;
            }
            return true;
        }
    }
    bool operator!=(const AnnotationItem& o) const {
        return !(*this == o);
    }
};

struct PageItem {
    int id;                  // Unique ID cho mỗi item
    int source_file_id;      // ID trỏ tới file gốc trong fileStore (0 = blank)
    int original_page_index; // Chỉ số trang gốc trong file PDFium (0-based)
    bool is_blank_page;      // Đánh dấu nếu là trang trắng mới chèn
    double widthPt;          // Chiều rộng (points)
    double heightPt;         // Chiều cao (points)
    std::vector<AnnotationItem> annotations; // In-memory virtual annotations / strokes

    bool operator==(const PageItem& other) const {
        return id == other.id &&
               source_file_id == other.source_file_id &&
               original_page_index == other.original_page_index &&
               is_blank_page == other.is_blank_page &&
               std::abs(widthPt - other.widthPt) < 0.01 &&
               std::abs(heightPt - other.heightPt) < 0.01 &&
               annotations == other.annotations;
    }
    bool operator!=(const PageItem& other) const {
        return !(*this == other);
    }
};

struct SourceDocInfo {
    int id;
    std::wstring filePath;
    FPDF_DOCUMENT doc;
    int pageCount;
};

struct AppState {
    std::vector<PageItem> activePages;
    int activePageIndex;
    std::set<int> selectedPages;
};

struct PageInfo {
    double widthPt;   // In points (72 pt/inch)
    double heightPt;
    double baseLayoutTop;
    double baseLayoutBottom;
    int layoutTop;    // Y coordinate in scroll coordinates
    int layoutBottom;
    int displayW;     // Scaled width in pixels
    int displayH;     // Scaled height in pixels
};

class PDFApp {
public:
    static PDFApp& Instance() {
        static PDFApp inst;
        return inst;
    }

    PDFApp();
    ~PDFApp();

    bool Initialize(HINSTANCE hInstance, HWND hWndMain);
    void Shutdown();

    // Document operations
    bool OpenDocument(const std::wstring& filePath);
    bool NewBlankDocument();
    bool SaveDocument(const std::wstring& filePath = L"");
    bool SaveDocumentAs();
    void CloseDocument();
    void SaveCurrentSession();

    // History (Undo / Redo)
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_activePages.empty() && !m_undoStack.empty(); }
    bool CanRedo() const { return !m_activePages.empty() && !m_redoStack.empty(); }

    // Page editing operations
    void AddBlankPage(int afterIndex = -1);
    void AppendPdfFiles();
    void DeleteSelectedPages();
    void MovePage(int srcIndex, int destIndex);
    bool MoveSelectedPages(int destIndex);
    bool MoveSelectedPagesUp();
    bool MoveSelectedPagesDown();
    void ExportSelectedPagesToJpg();
    void ExportSelectedPagesToPdf();

    // Navigation & View
    void SetZoom(float newZoom, bool updateSlider = true, int anchorX = -1, int anchorY = -1);
    void ZoomFitWidth();
    void GoToPage(int page1Based);
    void PageUp();
    void PageDown();
    void ScrollCurrentPageTop();
    void ScrollCurrentPageBottom();
    void ScrollByDelta(int deltaX, int deltaY);
    void UpdateActivePageFromScroll(int scrollY, int viewportH);
    void SetTool(ToolMode tool);

    // Multi-selection (File Explorer style)
    void SelectPageSingle(int pageIdx);
    void SelectPageToggle(int pageIdx);
    void SelectPageRange(int pageIdx);
    void SelectAllPages();
    bool IsPageSelected(int pageIdx) const;
    int GetSelectedCount() const { return (int)m_selectedPages.size(); }
    const std::set<int>& GetSelectedPages() const { return m_selectedPages; }
    const std::vector<PageItem>& GetActivePages() const { return m_activePages; }

    // Tool configuration & Erase
    ToolConfig GetPenConfig() const { return m_penConfig; }
    void SetPenConfig(const ToolConfig& cfg) { m_penConfig = cfg; }
    ToolConfig GetHighlightConfig() const { return m_highlightConfig; }
    void SetHighlightConfig(const ToolConfig& cfg) { m_highlightConfig = cfg; }
    bool EraseAnnotationAt(int pageIdx, int screenX, int screenY, int pageDisplayX, int pageDisplayY, int pageDisplayW, int pageDisplayH, bool recordUndo = true);

    // Theme (Dark / Light mode)
    bool IsDarkMode() const { return m_isDarkMode; }
    void SetDarkMode(bool dark);
    void ToggleDarkMode();

    // State queries
    bool HasDocument() const { return !m_activePages.empty(); }
    int GetPageCount() const { return (int)m_activePages.size(); }
    float GetZoom() const { return m_zoom; }
    ToolMode GetTool() const { return m_tool; }
    const std::wstring& GetCurrentFilePath() const { return m_filePath; }
    bool IsModified() const { return m_isModified; }
    bool IsSidebarVisible() const { return m_sidebarVisible; }
    bool IsRibbonCollapsed() const { return m_ribbonCollapsed; }
    void ToggleSidebar();
    void ToggleRibbon();

    // Layout
    void RecalculateLayout(int canvasWidth);
    const std::vector<PageInfo>& GetPages() const { return m_pages; }
    int GetTotalLayoutHeight() const { return m_totalLayoutHeight; }
    int GetTotalLayoutWidth() const { return m_totalLayoutWidth; }
    int GetMaxDisplayWidth() const { return m_maxDisplayWidth; }
    bool IsZooming() const { return m_isZooming; }
    void SetIsZooming(bool zooming) { m_isZooming = zooming; }

    // Window handles
    HWND GetMainWindow() const { return m_hWndMain; }
    HWND GetCanvasWindow() const { return m_hWndCanvas; }
    HWND GetSidebarWindow() const { return m_hWndSidebar; }
    HWND GetRibbonWindow() const { return m_hWndRibbon; }
    void SetSubWindows(HWND hRibbon, HWND hSidebar, HWND hCanvas);

    // Rendering helpers (Two-Layer Composite & Direct2D Hardware Acceleration)
    void RenderPageToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH, bool forPrinting = false);
    void RenderThumbnailToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH);
    void RenderAnnotationsToDC(HDC hdc, int pageIdx, int destX, int destY, int destW, int destH);
    void RenderAnnotationsToGraphics(Gdiplus::Graphics& g, int pageIdx, int destW, int destH);
    void ApplyHighlight(int pageIdx, float x1, float y1, float x2, float y2);
    void ApplyInkPath(int pageIdx, const std::vector<POINT>& screenPoints, int pageDisplayX, int pageDisplayY, int pageDisplayW, int pageDisplayH);

    // Direct2D GPU Hardware Acceleration
    ID2D1Bitmap* GetPageD2DBitmap(ID2D1RenderTarget* pRT, int pageIdx, int destW, int destH);
    void RenderAnnotationsToD2D(ID2D1RenderTarget* pRT, int pageIdx, float destX, float destY, float destW, float destH);
    void InvalidateD2DCache(int pageIdx = -1);

    void InvalidateBasePageCache(int pageIdx = -1);
    void InvalidateAnnotCache(int pageIdx = -1);
    void InvalidatePageCache(int pageIdx = -1); // invalidates Base, Annotation, and Direct2D caches
    void PreloadPageCache(int pageIdx);
    void PruneCache(int centerPageIdx);
    void TrimWorkingSetMemory();
    int FindPageAtY(int y) const;
    int GetActivePageIndex() const { return m_activePageIndex; }

    // Printing
    void ShowPrintDialog();

    // UI Updates
    void UpdateTitleBar();
    void ShowSavedNotificationOnTitleBar();
    void RefreshAllViews();
    static void ApplyUxThemeDarkMode(HWND hWnd, bool isDark);
    static void SetWindowTitlebarTheme(HWND hWnd, bool isDark);
    void ShowAboutDialog();
    void ShowExtractedPdfDialog(int pageCount, const std::wstring& filePath, const std::wstring& fileName);
    void ShowExtractedJpgSingleDialog(const std::wstring& filePath);
    void ShowExtractedJpgMultiDialog(int pageCount, const std::wstring& folderPath);

    void RecordStateBeforeEdit();
    void CommitStateAfterEdit();

private:
    void ClearPages();
    void UpdateLayoutFromActivePages();
    void ClearUndoRedo();

    HINSTANCE m_hInstance;
    HWND m_hWndMain;
    HWND m_hWndRibbon;
    HWND m_hWndSidebar;
    HWND m_hWndCanvas;

    std::map<int, SourceDocInfo> m_docStore;
    std::vector<PageItem> m_activePages;
    std::vector<PageItem> m_savedPages;
    int m_nextDocId;
    int m_nextPageId;
    int m_nextAnnotId;

    std::wstring m_filePath;
    std::vector<PageInfo> m_pages;
    std::set<int> m_selectedPages;
    int m_anchorPageIndex;
    int m_activePageIndex;

    float m_zoom;
    ToolMode m_tool;
    ToolConfig m_penConfig;
    ToolConfig m_highlightConfig;
    bool m_isDarkMode;
    bool m_isModified;
    bool m_sidebarVisible;
    bool m_ribbonCollapsed;
    double m_baseTotalHeight;
    double m_baseMaxDisplayWidth;
    double m_baseTotalWidth;
    int m_totalLayoutHeight;
    int m_totalLayoutWidth;
    int m_maxDisplayWidth;
    bool m_isZooming;

    std::vector<AppState> m_undoStack;
    std::vector<AppState> m_redoStack;

    // Direct2D GPU VRAM Texture Cache
    struct D2DPageTexture {
        ID2D1Bitmap* pBitmap;
        int width;
        int height;
        float zoom;
    };
    std::map<int, D2DPageTexture> m_d2dPageCache;

    // Two-Layer Composite Architecture Caches
    struct BasePageCache {
        HBITMAP hBitmap; // Pure PDFium rendered page
        int width;
        int height;
        float zoom;
    };
    struct AnnotOverlayCache {
        HBITMAP hBitmap; // 32-bit transparent ARGB DIBSection with premultiplied alpha
        int width;
        int height;
        float zoom;
        bool hasContent; // whether this overlay contains active strokes/highlights
    };
    std::map<int, BasePageCache> m_basePageCache;
    std::map<int, AnnotOverlayCache> m_annotOverlayCache;

    ULONG_PTR m_gdiplusToken;
};
