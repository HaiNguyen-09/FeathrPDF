@echo off
setlocal
cd /d "%~dp0"
echo [FeathrPDF] Building native C++ 64-bit application with icons...

:: Close running instance if any to avoid file lock
taskkill /F /IM FeathrPDF.exe >nul 2>&1

set "GXX=%~dp0w64devkit\bin\g++.exe"
set "WINDRES=%~dp0w64devkit\bin\windres.exe"

if not exist "%GXX%" (
    echo [ERROR] Compiler not found at %GXX%!
    if /i "%~1" neq "--no-pause" pause
    exit /b 1
)

echo [*] Compiling resource and manifest with windres (app.ico ID=1, pdf_doc.ico ID=2)...
"%WINDRES%" -i resource.rc -o src\app_res.o

if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile resources with windres!
    if /i "%~1" neq "--no-pause" pause
    exit /b %errorlevel%
)

echo [*] Compiling C++ source files with -O2 optimization...
"%GXX%" -O2 -municode -mwindows -std=c++17 src\main.cpp src\app.cpp src\ribbon.cpp src\sidebar.cpp src\canvas.cpp src\printer.cpp src\history.cpp src\tool_popup.cpp src\color_picker.cpp src\app_res.o -Iinclude -Isrc lib\pdfium.dll.lib -ld2d1 -ldwrite -lgdiplus -lgdi32 -lmsimg32 -luser32 -lshell32 -lshlwapi -lcomctl32 -lcomdlg32 -lole32 -lwinspool -luxtheme -ldwmapi -o FeathrPDF.exe

if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    if /i "%~1" neq "--no-pause" pause
    exit /b %errorlevel%
)

echo [*] Compiling Windows Shell Thumbnail Provider (pdfthumb.dll)...
"%GXX%" -O2 -shared -municode -mwindows -std=c++17 src\pdfthumb.cpp src\pdfthumb.def -Iinclude -Isrc -lgdi32 -luser32 -lole32 -loleaut32 -lshlwapi -luuid -o pdfthumb.dll

if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile pdfthumb.dll!
    if /i "%~1" neq "--no-pause" pause
    exit /b %errorlevel%
)

echo [*] Updating dist\FeathrPDF folder...
if not exist dist\FeathrPDF mkdir dist\FeathrPDF
copy /y FeathrPDF.exe dist\FeathrPDF\ >nul
copy /y pdfthumb.dll dist\FeathrPDF\ >nul
copy /y pdfium.dll dist\FeathrPDF\ >nul
copy /y app.ico dist\FeathrPDF\ >nul
copy /y pdf_doc.ico dist\FeathrPDF\ >nul
if exist register_pdf_assoc.bat copy /y register_pdf_assoc.bat dist\FeathrPDF\ >nul

echo [SUCCESS] FeathrPDF.exe and pdfthumb.dll built successfully!
echo.
if /i "%~1" neq "--no-pause" pause
