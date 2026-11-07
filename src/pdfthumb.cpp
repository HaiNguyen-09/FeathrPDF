#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <propsys.h>
#include <shlwapi.h>
#include <new>
#include <string>
#include <algorithm>
#include <cmath>
#include "fpdfview.h"

// CLSID for PDFReader Shell Thumbnail Provider: {B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}
static const CLSID CLSID_PdfThumbnailProvider = 
    { 0xb4c83244, 0x6c0f, 0x4d2a, { 0x9a, 0x3e, 0x74, 0xb6, 0xa6, 0x8d, 0x5d, 0x01 } };
static const WCHAR SZ_CLSID_PdfThumbnailProvider[] = L"{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}";

// GUID for IThumbnailProvider: {e357fccd-a995-4576-b01f-234630154e96}
static const IID IID_IThumbnailProvider_Local = 
    { 0xe357fccd, 0xa995, 0x4576, { 0xb0, 0x1f, 0x23, 0x46, 0x30, 0x15, 0x4e, 0x96 } };
static const WCHAR SZ_IID_IThumbnailProvider[] = L"{e357fccd-a995-4576-b01f-234630154e96}";

// GUID for IInitializeWithFile: {b7d14566-0509-4cce-a71f-0a554233bd9b}
static const IID IID_IInitializeWithFile_Local = 
    { 0xb7d14566, 0x0509, 0x4cce, { 0xa7, 0x1f, 0x0a, 0x55, 0x42, 0x33, 0xbd, 0x9b } };

// GUID for IInitializeWithStream: {b824b49d-22ac-4161-ac8a-9916e8fa3f7f}
static const IID IID_IInitializeWithStream_Local = 
    { 0xb824b49d, 0x22ac, 0x4161, { 0xac, 0x8a, 0x99, 0x16, 0xe8, 0xfa, 0x3f, 0x7f } };

static HINSTANCE g_hInst = NULL;
static long g_cDllRef = 0;

// Dynamic PDFium binding to ensure pdfthumb.dll loads seamlessly inside explorer.exe
typedef void (FPDF_CALLCONV *pfn_FPDF_InitLibrary)(void);
typedef void (FPDF_CALLCONV *pfn_FPDF_DestroyLibrary)(void);
typedef FPDF_DOCUMENT (FPDF_CALLCONV *pfn_FPDF_LoadDocument)(FPDF_STRING file_path, FPDF_BYTESTRING password);
typedef FPDF_DOCUMENT (FPDF_CALLCONV *pfn_FPDF_LoadCustomDocument)(FPDF_FILEACCESS* pFileAccess, FPDF_BYTESTRING password);
typedef int (FPDF_CALLCONV *pfn_FPDF_GetPageCount)(FPDF_DOCUMENT document);
typedef FPDF_PAGE (FPDF_CALLCONV *pfn_FPDF_LoadPage)(FPDF_DOCUMENT document, int page_index);
typedef void (FPDF_CALLCONV *pfn_FPDF_ClosePage)(FPDF_PAGE page);
typedef void (FPDF_CALLCONV *pfn_FPDF_CloseDocument)(FPDF_DOCUMENT document);
typedef double (FPDF_CALLCONV *pfn_FPDF_GetPageWidth)(FPDF_PAGE page);
typedef double (FPDF_CALLCONV *pfn_FPDF_GetPageHeight)(FPDF_PAGE page);
typedef FPDF_BITMAP (FPDF_CALLCONV *pfn_FPDFBitmap_CreateEx)(int width, int height, int format, void* first_scan, int stride);
typedef void (FPDF_CALLCONV *pfn_FPDFBitmap_FillRect)(FPDF_BITMAP bitmap, int left, int top, int width, int height, FPDF_DWORD color);
typedef void (FPDF_CALLCONV *pfn_FPDF_RenderPageBitmap)(FPDF_BITMAP bitmap, FPDF_PAGE page, int start_x, int start_y, int size_x, int size_y, int rotate, int flags);
typedef void (FPDF_CALLCONV *pfn_FPDFBitmap_Destroy)(FPDF_BITMAP bitmap);

struct PdfiumAPI {
    HMODULE hModule = NULL;
    bool isInitialized = false;

    pfn_FPDF_InitLibrary InitLibrary = NULL;
    pfn_FPDF_DestroyLibrary DestroyLibrary = NULL;
    pfn_FPDF_LoadDocument LoadDocument = NULL;
    pfn_FPDF_LoadCustomDocument LoadCustomDocument = NULL;
    pfn_FPDF_GetPageCount GetPageCount = NULL;
    pfn_FPDF_LoadPage LoadPage = NULL;
    pfn_FPDF_ClosePage ClosePage = NULL;
    pfn_FPDF_CloseDocument CloseDocument = NULL;
    pfn_FPDF_GetPageWidth GetPageWidth = NULL;
    pfn_FPDF_GetPageHeight GetPageHeight = NULL;
    pfn_FPDFBitmap_CreateEx Bitmap_CreateEx = NULL;
    pfn_FPDFBitmap_FillRect Bitmap_FillRect = NULL;
    pfn_FPDF_RenderPageBitmap RenderPageBitmap = NULL;
    pfn_FPDFBitmap_Destroy Bitmap_Destroy = NULL;

    bool Load(HINSTANCE hDllInst) {
        if (hModule) return true;

        WCHAR szPath[MAX_PATH] = { 0 };
        GetModuleFileNameW(hDllInst, szPath, MAX_PATH);
        PathRemoveFileSpecW(szPath);
        PathAppendW(szPath, L"pdfium.dll");

        hModule = LoadLibraryW(szPath);
        if (!hModule) {
            hModule = LoadLibraryW(L"pdfium.dll");
        }
        if (!hModule) return false;

        InitLibrary = (pfn_FPDF_InitLibrary)GetProcAddress(hModule, "FPDF_InitLibrary");
        DestroyLibrary = (pfn_FPDF_DestroyLibrary)GetProcAddress(hModule, "FPDF_DestroyLibrary");
        LoadDocument = (pfn_FPDF_LoadDocument)GetProcAddress(hModule, "FPDF_LoadDocument");
        LoadCustomDocument = (pfn_FPDF_LoadCustomDocument)GetProcAddress(hModule, "FPDF_LoadCustomDocument");
        GetPageCount = (pfn_FPDF_GetPageCount)GetProcAddress(hModule, "FPDF_GetPageCount");
        LoadPage = (pfn_FPDF_LoadPage)GetProcAddress(hModule, "FPDF_LoadPage");
        ClosePage = (pfn_FPDF_ClosePage)GetProcAddress(hModule, "FPDF_ClosePage");
        CloseDocument = (pfn_FPDF_CloseDocument)GetProcAddress(hModule, "FPDF_CloseDocument");
        GetPageWidth = (pfn_FPDF_GetPageWidth)GetProcAddress(hModule, "FPDF_GetPageWidth");
        GetPageHeight = (pfn_FPDF_GetPageHeight)GetProcAddress(hModule, "FPDF_GetPageHeight");
        Bitmap_CreateEx = (pfn_FPDFBitmap_CreateEx)GetProcAddress(hModule, "FPDFBitmap_CreateEx");
        Bitmap_FillRect = (pfn_FPDFBitmap_FillRect)GetProcAddress(hModule, "FPDFBitmap_FillRect");
        RenderPageBitmap = (pfn_FPDF_RenderPageBitmap)GetProcAddress(hModule, "FPDF_RenderPageBitmap");
        Bitmap_Destroy = (pfn_FPDFBitmap_Destroy)GetProcAddress(hModule, "FPDFBitmap_Destroy");

        if (!InitLibrary || !LoadDocument || !GetPageCount || !LoadPage ||
            !ClosePage || !CloseDocument || !GetPageWidth || !GetPageHeight ||
            !Bitmap_CreateEx || !Bitmap_FillRect || !RenderPageBitmap || !Bitmap_Destroy) {
            FreeLibrary(hModule);
            hModule = NULL;
            return false;
        }

        if (!isInitialized) {
            InitLibrary();
            isInitialized = true;
        }
        return true;
    }
};

static PdfiumAPI g_pdfium;

// Forward declaration
class CPdfThumbnailProvider : public IThumbnailProvider,
                              public IInitializeWithFile,
                              public IInitializeWithStream
{
public:
    CPdfThumbnailProvider() : m_cRef(1), m_pStream(NULL) {
        InterlockedIncrement(&g_cDllRef);
    }

    virtual ~CPdfThumbnailProvider() {
        if (m_pStream) {
            m_pStream->Release();
            m_pStream = NULL;
        }
        InterlockedDecrement(&g_cDllRef);
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = NULL;

        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IThumbnailProvider_Local)) {
            *ppv = static_cast<IThumbnailProvider*>(this);
        } else if (IsEqualIID(riid, IID_IInitializeWithFile_Local)) {
            *ppv = static_cast<IInitializeWithFile*>(this);
        } else if (IsEqualIID(riid, IID_IInitializeWithStream_Local)) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) {
            delete this;
        }
        return cRef;
    }

    // IInitializeWithFile
    IFACEMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode) override {
        if (!pszFilePath) return E_INVALIDARG;
        m_filePath = pszFilePath;
        if (m_pStream) {
            m_pStream->Release();
            m_pStream = NULL;
        }
        return S_OK;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pstream, DWORD grfMode) override {
        if (!pstream) return E_INVALIDARG;
        if (m_pStream) {
            m_pStream->Release();
            m_pStream = NULL;
        }
        m_pStream = pstream;
        m_pStream->AddRef();
        m_filePath.clear();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = NULL;
        *pdwAlpha = WTSAT_UNKNOWN;

        if (!g_pdfium.Load(g_hInst)) {
            return E_FAIL;
        }

        FPDF_DOCUMENT doc = NULL;
        if (m_pStream && g_pdfium.LoadCustomDocument) {
            STATSTG stat;
            if (SUCCEEDED(m_pStream->Stat(&stat, STATFLAG_NONAME))) {
                struct StreamCtx {
                    IStream* pStream;
                };
                static auto StreamRead = [](void* param, unsigned long pos, unsigned char* buf, unsigned long size) -> int {
                    StreamCtx* ctx = (StreamCtx*)param;
                    if (!ctx || !ctx->pStream) return 0;
                    LARGE_INTEGER li;
                    li.QuadPart = pos;
                    if (FAILED(ctx->pStream->Seek(li, STREAM_SEEK_SET, NULL))) return 0;
                    ULONG read = 0;
                    if (FAILED(ctx->pStream->Read(buf, size, &read))) return 0;
                    return (read == size);
                };

                StreamCtx ctx = { m_pStream };
                FPDF_FILEACCESS fa;
                fa.m_FileLen = (unsigned long)stat.cbSize.QuadPart;
                fa.m_GetBlock = StreamRead;
                fa.m_Param = &ctx;
                doc = g_pdfium.LoadCustomDocument(&fa, NULL);
            }
        }

        if (!doc && !m_filePath.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, m_filePath.c_str(), -1, NULL, 0, NULL, NULL);
            std::string u8(len, 0);
            WideCharToMultiByte(CP_UTF8, 0, m_filePath.c_str(), -1, &u8[0], len, NULL, NULL);
            doc = g_pdfium.LoadDocument(u8.c_str(), NULL);
        }

        if (!doc) return E_FAIL;

        int pageCount = g_pdfium.GetPageCount(doc);
        if (pageCount <= 0) {
            g_pdfium.CloseDocument(doc);
            return E_FAIL;
        }

        FPDF_PAGE page = g_pdfium.LoadPage(doc, 0);
        if (!page) {
            g_pdfium.CloseDocument(doc);
            return E_FAIL;
        }

        double ptW = g_pdfium.GetPageWidth(page);
        double ptH = g_pdfium.GetPageHeight(page);
        if (ptW <= 0.0 || ptH <= 0.0) {
            ptW = 595.28;
            ptH = 841.89;
        }

        if (cx == 0) cx = 256;
        if (cx > 2048) cx = 2048;

        UINT targetW = cx;
        UINT targetH = cx;
        if (ptW >= ptH) {
            targetW = cx;
            targetH = (UINT)std::max(1.0, std::round((double)cx * ptH / ptW));
        } else {
            targetH = cx;
            targetW = (UINT)std::max(1.0, std::round((double)cx * ptW / ptH));
        }

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = targetW;
        bmi.bmiHeader.biHeight = -(LONG)targetH; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = NULL;
        HDC hScreenDC = GetDC(NULL);
        HBITMAP hBmp = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        ReleaseDC(NULL, hScreenDC);

        if (!hBmp || !pBits) {
            g_pdfium.ClosePage(page);
            g_pdfium.CloseDocument(doc);
            return E_OUTOFMEMORY;
        }

        int stride = targetW * 4;
        // Fill clean white background
        memset(pBits, 0xFF, stride * targetH);

        FPDF_BITMAP fbmp = g_pdfium.Bitmap_CreateEx(targetW, targetH, 2, pBits, stride); // 2 = FPDFBitmap_BGRx
        if (fbmp) {
            g_pdfium.Bitmap_FillRect(fbmp, 0, 0, targetW, targetH, 0xFFFFFFFF);
            // Render with annotations (0x01 = FPDF_ANNOT)
            g_pdfium.RenderPageBitmap(fbmp, page, 0, 0, targetW, targetH, 0, 0x01);
            g_pdfium.Bitmap_Destroy(fbmp);
        }

        g_pdfium.ClosePage(page);
        g_pdfium.CloseDocument(doc);

        *phbmp = hBmp;
        *pdwAlpha = WTSAT_RGB; // 32-bit RGB with white background (allows Explorer to render document drop-shadow)
        return S_OK;
    }

private:
    long m_cRef;
    std::wstring m_filePath;
    IStream *m_pStream;
};

// Class factory
class CClassFactory : public IClassFactory {
public:
    CClassFactory() : m_cRef(1) {
        InterlockedIncrement(&g_cDllRef);
    }
    virtual ~CClassFactory() {
        InterlockedDecrement(&g_cDllRef);
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) {
            delete this;
        }
        return cRef;
    }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (pUnkOuter != NULL) {
            return CLASS_E_NOAGGREGATION;
        }
        CPdfThumbnailProvider* pProvider = new (std::nothrow) CPdfThumbnailProvider();
        if (!pProvider) {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            InterlockedIncrement(&g_cDllRef);
        } else {
            InterlockedDecrement(&g_cDllRef);
        }
        return S_OK;
    }

private:
    long m_cRef;
};

// DLL Entry Points
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) {
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualCLSID(rclsid, CLSID_PdfThumbnailProvider)) {
        CClassFactory* pFactory = new (std::nothrow) CClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

// Registry Helper Functions
static HRESULT SetRegValue(HKEY hRoot, LPCWSTR subKey, LPCWSTR valueName, LPCWSTR valueData) {
    HKEY hKey = NULL;
    LONG res = RegCreateKeyExW(hRoot, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (res != ERROR_SUCCESS) return HRESULT_FROM_WIN32(res);
    if (valueData) {
        res = RegSetValueExW(hKey, valueName, 0, REG_SZ, (const BYTE*)valueData, (DWORD)(wcslen(valueData) + 1) * sizeof(WCHAR));
    }
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(res);
}

STDAPI DllRegisterServer(void) {
    WCHAR szModule[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(g_hInst, szModule, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + std::wstring(SZ_CLSID_PdfThumbnailProvider);
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";
    std::wstring extKey = L"Software\\Classes\\.pdf\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);
    std::wstring progIdKey = L"Software\\Classes\\FeathrPDF.Document\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);

    // Register under HKCU (supports standard non-admin users)
    SetRegValue(HKEY_CURRENT_USER, clsidKey.c_str(), NULL, L"FeathrPDF Thumbnail Provider");
    SetRegValue(HKEY_CURRENT_USER, inprocKey.c_str(), NULL, szModule);
    SetRegValue(HKEY_CURRENT_USER, inprocKey.c_str(), L"ThreadingModel", L"Apartment");
    SetRegValue(HKEY_CURRENT_USER, extKey.c_str(), NULL, SZ_CLSID_PdfThumbnailProvider);
    SetRegValue(HKEY_CURRENT_USER, progIdKey.c_str(), NULL, SZ_CLSID_PdfThumbnailProvider);

    // Also attempt HKLM / HKCR if elevated
    std::wstring rootClsidKey = L"CLSID\\" + std::wstring(SZ_CLSID_PdfThumbnailProvider);
    std::wstring rootInprocKey = rootClsidKey + L"\\InprocServer32";
    std::wstring rootExtKey = L".pdf\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);
    std::wstring rootProgIdKey = L"FeathrPDF.Document\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);

    SetRegValue(HKEY_CLASSES_ROOT, rootClsidKey.c_str(), NULL, L"FeathrPDF Thumbnail Provider");
    SetRegValue(HKEY_CLASSES_ROOT, rootInprocKey.c_str(), NULL, szModule);
    SetRegValue(HKEY_CLASSES_ROOT, rootInprocKey.c_str(), L"ThreadingModel", L"Apartment");
    SetRegValue(HKEY_CLASSES_ROOT, rootExtKey.c_str(), NULL, SZ_CLSID_PdfThumbnailProvider);
    SetRegValue(HKEY_CLASSES_ROOT, rootProgIdKey.c_str(), NULL, SZ_CLSID_PdfThumbnailProvider);

    // Notify shell of association update
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + std::wstring(SZ_CLSID_PdfThumbnailProvider);
    std::wstring extKey = L"Software\\Classes\\.pdf\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);
    std::wstring progIdKey = L"Software\\Classes\\FeathrPDF.Document\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);

    RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, extKey.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, progIdKey.c_str());

    std::wstring rootClsidKey = L"CLSID\\" + std::wstring(SZ_CLSID_PdfThumbnailProvider);
    std::wstring rootExtKey = L".pdf\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);
    std::wstring rootProgIdKey = L"FeathrPDF.Document\\ShellEx\\" + std::wstring(SZ_IID_IThumbnailProvider);

    RegDeleteTreeW(HKEY_CLASSES_ROOT, rootClsidKey.c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, rootExtKey.c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, rootProgIdKey.c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
