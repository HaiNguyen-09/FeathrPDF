@echo off
setlocal
cd /d "%~dp0"

echo ===================================================================
echo     BUILDING NATIVE SETUP WIZARD INSTALLER FOR FEATHRPDF
echo ===================================================================
echo.

set "MAKENSIS=%~dp0w64devkit\bin\makensis.exe"

REM 1. Check or rebuild main executable
if /i "%~1"=="--rebuild" (
    echo [*] Rebuilding C++ application from source before packaging...
    call "%~dp0build.bat" --no-pause
)
if not exist "%~dp0FeathrPDF.exe" (
    echo [*] FeathrPDF.exe not found. Building now...
    call "%~dp0build.bat" --no-pause
)

if not exist "%~dp0FeathrPDF.exe" (
    echo [ERROR] FeathrPDF.exe not found or build failed!
    if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
    exit /b 1
)

if not exist "%~dp0pdfium.dll" (
    echo [ERROR] Library pdfium.dll not found!
    if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
    exit /b 1
)

if not exist "%~dp0app.ico" (
    echo [ERROR] app.ico not found!
    if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
    exit /b 1
)

if not exist "%~dp0pdf_doc.ico" (
    echo [ERROR] pdf_doc.ico not found!
    if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
    exit /b 1
)

if not exist "%~dp0pdfthumb.dll" (
    echo [*] pdfthumb.dll not found. Building application components now...
    call "%~dp0build.bat" --no-pause
)

if not exist "%~dp0pdfthumb.dll" (
    echo [ERROR] pdfthumb.dll not found or build failed!
    if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
    exit /b 1
)

echo [*] Verified all required files for installer packaging:
    echo     - FeathrPDF.exe
    echo     - pdfthumb.dll (Shell Thumbnail Provider)
    echo     - pdfium.dll
    echo     - app.ico (App Icon)
    echo     - pdf_doc.ico (Document Icon)
echo.

REM Remove old installer before generating new one
del /f /q "%~dp0FeathrPDF_Setup.exe" 2>nul

REM 2. Try native NSIS packaging tool in w64devkit
if exist "%MAKENSIS%" (
    echo [*] Found NSIS packaging tool: %MAKENSIS%
    echo [*] Compiling installer.nsi to FeathrPDF_Setup.exe...
    echo.
    "%MAKENSIS%" /INPUTCHARSET UTF8 /V3 "%~dp0installer.nsi"
)

if exist "%~dp0FeathrPDF_Setup.exe" goto SUCCESS

REM 3. Try Inno Setup Compiler (iscc.exe)
set "ISCC="
for %%P in (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    "C:\Program Files\Inno Setup 6\ISCC.exe"
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
) do (
    if exist %%P set "ISCC=%%~P"
)

if defined ISCC (
    echo [*] Found Inno Setup Compiler: %ISCC%
    echo [*] Compiling installer.iss to FeathrPDF_Setup.exe...
    echo.
    "%ISCC%" "%~dp0installer.iss"
)

if exist "%~dp0FeathrPDF_Setup.exe" goto SUCCESS

echo [ERROR] Neither NSIS nor Inno Setup could produce FeathrPDF_Setup.exe!
if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
exit /b 1

:SUCCESS
echo.
echo ===================================================================
echo [SUCCESS] INSTALLER BUILD COMPLETED SUCCESSFULLY!
echo     - Output installer: FeathrPDF_Setup.exe
echo     - Setup Wizard: Directory selection, Desktop shortcut, Start Menu, .pdf association
echo     - Uninstaller Wizard: Professional clean uninstall
echo     - Windows Settings / Control Panel: Integrated in Apps and Features
echo ===================================================================
echo.
if not exist "%~dp0dist\FeathrPDF" mkdir "%~dp0dist\FeathrPDF"
copy /y "%~dp0FeathrPDF_Setup.exe" "%~dp0dist\FeathrPDF\" >nul
copy /y "%~dp0FeathrPDF.exe" "%~dp0dist\FeathrPDF\" >nul
copy /y "%~dp0pdfthumb.dll" "%~dp0dist\FeathrPDF\" >nul
copy /y "%~dp0pdfium.dll" "%~dp0dist\FeathrPDF\" >nul
copy /y "%~dp0app.ico" "%~dp0dist\FeathrPDF\" >nul
copy /y "%~dp0pdf_doc.ico" "%~dp0dist\FeathrPDF\" >nul
if exist "%~dp0sample.pdf" copy /y "%~dp0sample.pdf" "%~dp0dist\FeathrPDF\" >nul

if /i "%~1" neq "--no-pause" if /i "%~2" neq "--no-pause" pause
