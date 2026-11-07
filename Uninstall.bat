@echo off
chcp 65001 >nul
echo ========================================================
echo       GỠ CÀI ĐẶT PDF READER
echo ========================================================
echo.

set "TARGET_DIR=%LOCALAPPDATA%\Programs\FeathrPDF"
taskkill /f /im FeathrPDF.exe >nul 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-Item -Path ([Environment]::GetFolderPath('Desktop') + '\PDF Reader.lnk') -ErrorAction SilentlyContinue"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-Item -Path ([Environment]::GetFolderPath('StartMenu') + '\Programs\PDF Reader.lnk') -ErrorAction SilentlyContinue"

reg delete "HKCU\Software\Classes\Applications\FeathrPDF.exe" /f >nul 2>&1
reg delete "HKCU\Software\Classes\.pdf\OpenWithProgids" /v "FeathrPDF.Document" /f >nul 2>&1
reg delete "HKCU\Software\Classes\FeathrPDF.Document" /f >nul 2>&1

rmdir /s /q "%TARGET_DIR%" >nul 2>&1

echo.
echo [✓] Đã gỡ bỏ PDF Reader thành công!
echo.
pause
