#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <set>

enum PrintRangeType {
    PRINT_RANGE_ALL = 0,
    PRINT_RANGE_SELECTED,
    PRINT_RANGE_CUSTOM
};

enum PrintSubsetType {
    PRINT_SUBSET_ALL = 0, // Tất cả trang trong phạm vi
    PRINT_SUBSET_ODD,     // Chỉ trang LẺ trong phạm vi (1, 3, 5...)
    PRINT_SUBSET_EVEN     // Chỉ trang CHẴN trong phạm vi (2, 4, 6...)
};

enum PageOrientation {
    ORIENT_PORTRAIT = 0,
    ORIENT_LANDSCAPE
};

struct PrintOptions {
    std::wstring printerName;
    PrintRangeType rangeType;
    PrintSubsetType subsetType;
    std::wstring customRangeStr;
    bool reversePrint;
    int paperSize; // DMPAPER_A4, DMPAPER_LETTER, etc.
    PageOrientation orientation;
    int pagesPerSheet; // 1, 2, 4, 8, 16
    int copies;
};

class PDFPrinter {
public:
    static bool ShowPrintDialog(HWND hWndParent, PrintOptions& outOptions);
    static bool ExecutePrint(HWND hWndParent, const PrintOptions& options);
    static std::vector<int> ParsePageRange(const std::wstring& str, int maxPages);
    static std::vector<int> ComputeFinalPages(const PrintOptions& options, int totalDocPages, const std::set<int>& selectedPages);
    static HWND GetPrintDialogHwnd();
    static void UpdateDialogTheme(HWND hDlg, bool isDark);
};
