@echo off
chcp 65001 >nul
setlocal

echo ===================================================================
echo     ĐĂNG KÝ LIÊN KẾT FILE PDF ^& ICON TÀI LIỆU RIÊNG (FEATHRPDF)
echo ===================================================================
echo.

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "APP_EXE=%APP_DIR%\FeathrPDF.exe"
set "DOC_ICO=%APP_DIR%\pdf_doc.ico"
set "APP_ICO=%APP_DIR%\app.ico"
set "THUMB_DLL=%APP_DIR%\pdfthumb.dll"

if not exist "%APP_EXE%" (
    echo [LỖI] Không tìm thấy file FeathrPDF.exe tại: %APP_EXE%
    echo Vui lòng chạy script này trong thư mục chứa file FeathrPDF.exe!
    pause
    exit /b 1
)

echo [*] Thư mục ứng dụng: %APP_DIR%
echo [*] File thực thi chính: %APP_EXE%
if exist "%DOC_ICO%" echo [*] File biểu tượng tài liệu PDF: %DOC_ICO%
if not exist "%DOC_ICO%" echo [*] Sử dụng Icon Index 2 nhúng sẵn trong FeathrPDF.exe
echo.

echo [*] Đang thiết lập Registry tại HKCU\Software\Classes...

REM 1. Khai báo định danh ProgID cho tài liệu PDF: FeathrPDF.Document
reg add "HKCU\Software\Classes\FeathrPDF.Document" /ve /t REG_SZ /d "Tài liệu PDF (FeathrPDF Document)" /f >nul

REM Cấu hình DefaultIcon trỏ thẳng tới pdf_doc.ico hoặc icon index 2 trong FeathrPDF.exe
if exist "%DOC_ICO%" reg add "HKCU\Software\Classes\FeathrPDF.Document\DefaultIcon" /ve /t REG_SZ /d "\"%DOC_ICO%\"" /f >nul
if not exist "%DOC_ICO%" reg add "HKCU\Software\Classes\FeathrPDF.Document\DefaultIcon" /ve /t REG_SZ /d "\"%APP_EXE%\",2" /f >nul

REM Cấu hình lệnh mở tài liệu
reg add "HKCU\Software\Classes\FeathrPDF.Document\shell\open\command" /ve /t REG_SZ /d "\"%APP_EXE%\" \"%%1\"" /f >nul

REM 2. Đăng ký ứng dụng FeathrPDF.exe trong Applications
reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe" /v "FriendlyAppName" /t REG_SZ /d "FeathrPDF" /f >nul
if exist "%APP_ICO%" reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe\DefaultIcon" /ve /t REG_SZ /d "\"%APP_ICO%\"" /f >nul
if not exist "%APP_ICO%" reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe\DefaultIcon" /ve /t REG_SZ /d "\"%APP_EXE%\",1" /f >nul
reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe\shell\open\command" /ve /t REG_SZ /d "\"%APP_EXE%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe\SupportedTypes" /v ".pdf" /t REG_SZ /d "" /f >nul

REM 3. Liên kết phần mở rộng .pdf với FeathrPDF.Document
reg add "HKCU\Software\Classes\.pdf" /ve /t REG_SZ /d "FeathrPDF.Document" /f >nul
reg add "HKCU\Software\Classes\.pdf" /v "Content Type" /t REG_SZ /d "application/pdf" /f >nul
reg add "HKCU\Software\Classes\.pdf\OpenWithProgids" /v "FeathrPDF.Document" /t REG_NONE /f >nul
if exist "%DOC_ICO%" reg add "HKCU\Software\Classes\.pdf\DefaultIcon" /ve /t REG_SZ /d "\"%DOC_ICO%\"" /f >nul
if not exist "%DOC_ICO%" reg add "HKCU\Software\Classes\.pdf\DefaultIcon" /ve /t REG_SZ /d "\"%APP_EXE%\",2" /f >nul

REM 4. Cấu hình danh sách OpenWith trong Windows Explorer FileExts
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" /v "a" /t REG_SZ /d "FeathrPDF.exe" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" /v "MRUList" /t REG_SZ /d "a" /f >nul
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids" /v "FeathrPDF.Document" /t REG_NONE /f >nul

REM 5. Đăng ký Windows Shell Thumbnail Provider
if exist "%THUMB_DLL%" echo [*] Đang đăng ký Windows Shell Thumbnail Provider: pdfthumb.dll
if exist "%THUMB_DLL%" regsvr32.exe /s "%THUMB_DLL%"
if exist "%THUMB_DLL%" reg add "HKCU\Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}" /ve /t REG_SZ /d "FeathrPDF Thumbnail Provider" /f >nul
if exist "%THUMB_DLL%" reg add "HKCU\Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InprocServer32" /ve /t REG_SZ /d "%THUMB_DLL%" /f >nul
if exist "%THUMB_DLL%" reg add "HKCU\Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InprocServer32" /v "ThreadingModel" /t REG_SZ /d "Apartment" /f >nul
if exist "%THUMB_DLL%" reg add "HKCU\Software\Classes\.pdf\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /ve /t REG_SZ /d "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}" /f >nul
if exist "%THUMB_DLL%" reg add "HKCU\Software\Classes\FeathrPDF.Document\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /ve /t REG_SZ /d "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}" /f >nul

echo [*] Đang làm mới bộ đệm biểu tượng Icon Cache của Windows Explorer...
powershell -NoProfile -ExecutionPolicy Bypass -Command "[System.Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer((Add-Type -MemberDefinition '[DllImport(\"shell32.dll\")] public static extern void SHChangeNotify(int wEventId, uint uFlags, IntPtr dwItem1, IntPtr dwItem2);' -Name 'ShellNotify' -Namespace 'Win32' -PassThru)::GetMethod('SHChangeNotify').MethodHandle.GetFunctionPointer(), [Action[int, uint, IntPtr, IntPtr]]).Invoke(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)" >nul 2>&1

echo.
echo ===================================================================
echo [v] HOÀN TẤT ĐĂNG KÝ LIÊN KẾT FILE PDF ^& THUMBNAIL THÀNH CÔNG!
echo     - Các file .pdf ngoài Windows Explorer giờ đây sẽ tự động hiển thị
echo       hình ảnh thu nhỏ (Thumbnail) trang 1 ở chế độ Medium / Large Icons.
echo     - Chế độ Small Icons / Details hiển thị biểu tượng riêng: pdf_doc.ico.
echo     - Nhấp đúp chuột vào bất kỳ file PDF nào sẽ mở trực tiếp bằng
echo       FeathrPDF.exe với biểu tượng app.ico trên thanh tác vụ.
echo ===================================================================
echo.
if /i "%~1" neq "--no-pause" pause
