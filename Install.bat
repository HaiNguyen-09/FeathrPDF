@echo off
chcp 65001 >nul
echo ========================================================
echo       CÀI ĐẶT PDF READER CHO WINDOWS 10 64-BIT
echo ========================================================
echo.

set "TARGET_DIR=%LOCALAPPDATA%\Programs\FeathrPDF"
echo [*] Đang cài đặt vào thư mục: %TARGET_DIR%

if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

copy /y "%~dp0FeathrPDF.exe" "%TARGET_DIR%\" >nul
copy /y "%~dp0pdfium.dll" "%TARGET_DIR%\" >nul

if exist "%~dp0sample.pdf" (
    copy /y "%~dp0sample.pdf" "%TARGET_DIR%\" >nul
)

echo [*] Đang tạo Shortcut trên Desktop và Start Menu...

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\PDF Reader.lnk'); $s.TargetPath = '%TARGET_DIR%\FeathrPDF.exe'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.Save()"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('StartMenu') + '\Programs\PDF Reader.lnk'); $s.TargetPath = '%TARGET_DIR%\FeathrPDF.exe'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.Save()"

echo [*] Đăng ký trình xem PDF trong Windows...
reg add "HKCU\Software\Classes\Applications\FeathrPDF.exe\shell\open\command" /ve /t REG_SZ /d "\"%TARGET_DIR%\FeathrPDF.exe\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\.pdf\OpenWithProgids" /v "FeathrPDF.Document" /t REG_NONE /f >nul
reg add "HKCU\Software\Classes\FeathrPDF.Document\shell\open\command" /ve /t REG_SZ /d "\"%TARGET_DIR%\FeathrPDF.exe\" \"%%1\"" /f >nul

echo.
echo ========================================================
echo [✓] CÀI ĐẶT THÀNH CÔNG!
echo     - Đã tạo icon 'PDF Reader' trên Desktop
echo     - Đã tạo icon trong Start Menu
echo     - Chạy hoàn toàn offline, nhẹ ~12MB RAM!
echo ========================================================
echo.
pause
