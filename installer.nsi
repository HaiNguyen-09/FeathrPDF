Unicode true

; ===================================================================
;  FeathrPDF Installer & Uninstaller Script (NSIS Modern UI 2)
;  Tự động đóng gói Wizard cài đặt và gỡ cài đặt chuyên nghiệp
; ===================================================================

SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"

; --- Thông tin ứng dụng ---
!define PRODUCT_NAME "FeathrPDF"
!define PRODUCT_VERSION "1.2"
!define PRODUCT_PUBLISHER "h_nguynn0."
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_PROGID "FeathrPDF.Document"

; --- Version / Metadata Resource for Windows Defender & Authenticode compliance ---
VIProductVersion "1.2.0.0"
VIFileVersion "1.2.0.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 h_nguynn0. All rights reserved."
VIAddVersionKey "FileDescription" "FeathrPDF Setup"
VIAddVersionKey "FileVersion" "1.2.0.0"

Name "${PRODUCT_NAME}"
OutFile "FeathrPDF_Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT_NAME}"
RequestExecutionLevel user

; --- Cấu hình giao diện và Icon ---
!define MUI_ICON "app.ico"
!define MUI_UNICON "app.ico"
!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING

; --- Trang Wizard Cài đặt ---
!define MUI_WELCOMEPAGE_TITLE "Welcome to ${PRODUCT_NAME} Setup"
!define MUI_WELCOMEPAGE_TEXT "${PRODUCT_NAME} is an ultra-lightweight, high-performance native 64-bit PDF reader and editor for Windows 10/11.$\r$\n$\r$\n- 100% offline, ~12MB RAM idle footprint$\r$\n- Hardware-accelerated smooth rendering & instant Dark Mode$\r$\n- Professional annotations (Highlighter, Pen, Eraser)$\r$\n- Direct printing and high-res image export.$\r$\n$\r$\nClick 'Next' to continue setup."
!insertmacro MUI_PAGE_WELCOME

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\FeathrPDF.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME} now"
!insertmacro MUI_PAGE_FINISH

; --- Trang Wizard Gỡ cài đặt ---
!define MUI_UNWELCOMEPAGE_TITLE "Uninstall ${PRODUCT_NAME}"
!define MUI_UNWELCOMEPAGE_TEXT "The uninstaller will completely remove ${PRODUCT_NAME} files, shortcuts, and registry associations from your computer."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; --- Ngôn ngữ ---
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Vietnamese"

; ===================================================================
;  Các Section Cài đặt (Components / Tasks)
; ===================================================================

; 1. Tệp tin chương trình chính (Bắt buộc)
Section "!Core Application Files (Required)" SecMain
    SectionIn RO
    SetOverwrite on
    SetOutPath "$INSTDIR"

    File "FeathrPDF.exe"
    File "pdfthumb.dll"
    File "pdfium.dll"
    File "app.ico"
    File "pdf_doc.ico"
    File /nonfatal "sample.pdf"
    
    ; Tạo bộ gỡ cài đặt có giao diện Wizard
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Đăng ký Control Panel / Windows Settings (Apps & Features)
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayName" "FeathrPDF"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\FeathrPDF.exe,0"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "NoRepair" 1
    WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "EstimatedSize" 11000
SectionEnd

; 2. Tạo Shortcut ngoài Desktop
Section "Create Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\FeathrPDF.lnk" "$INSTDIR\FeathrPDF.exe" "" "$INSTDIR\app.ico" 0
SectionEnd

; 3. Tạo Shortcut trong Start Menu
Section "Create Start Menu Shortcuts" SecStartMenu
    CreateDirectory "$SMPROGRAMS\FeathrPDF"
    CreateShortCut "$SMPROGRAMS\FeathrPDF\FeathrPDF.lnk" "$INSTDIR\FeathrPDF.exe" "" "$INSTDIR\app.ico" 0
    CreateShortCut "$SMPROGRAMS\FeathrPDF\Uninstall FeathrPDF.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\app.ico" 0
SectionEnd

; 4. Đặt làm trình đọc PDF mặc định & Gán Document Icon riêng biệt
Section "Associate .pdf files & Assign dedicated icon" SecAssoc
    ; 1. Định nghĩa ProgID
    WriteRegStr HKCU "Software\Classes\${PRODUCT_PROGID}" "" "PDF Document"
    WriteRegStr HKCU "Software\Classes\${PRODUCT_PROGID}\DefaultIcon" "" "$INSTDIR\pdf_doc.ico"
    WriteRegStr HKCU "Software\Classes\${PRODUCT_PROGID}\shell\open\command" "" '"$INSTDIR\FeathrPDF.exe" "%1"'

    ; 2. Đăng ký Applications
    WriteRegStr HKCU "Software\Classes\Applications\FeathrPDF.exe" "FriendlyAppName" "FeathrPDF"
    WriteRegStr HKCU "Software\Classes\Applications\FeathrPDF.exe\DefaultIcon" "" "$INSTDIR\app.ico"
    WriteRegStr HKCU "Software\Classes\Applications\FeathrPDF.exe\shell\open\command" "" '"$INSTDIR\FeathrPDF.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\Applications\FeathrPDF.exe\SupportedTypes" ".pdf" ""

    ; 3. Gán phần mở rộng .pdf
    WriteRegStr HKCU "Software\Classes\.pdf" "" "${PRODUCT_PROGID}"
    WriteRegStr HKCU "Software\Classes\.pdf" "Content Type" "application/pdf"
    WriteRegNone HKCU "Software\Classes\.pdf\OpenWithProgids" "${PRODUCT_PROGID}"
    WriteRegStr HKCU "Software\Classes\.pdf\DefaultIcon" "" "$INSTDIR\pdf_doc.ico"

    ; 4. Explorer OpenWith List
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" "a" "FeathrPDF.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" "MRUList" "a"
    WriteRegNone HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids" "${PRODUCT_PROGID}"

    ; 5. Đăng ký Windows Shell Thumbnail Provider (Xem trước trang 1 ở Medium/Large/Extra Large Icons)
    RegDLL "$INSTDIR\pdfthumb.dll"
    WriteRegStr HKCU "Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}" "" "FeathrPDF Thumbnail Provider"
    WriteRegStr HKCU "Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InprocServer32" "" "$INSTDIR\pdfthumb.dll"
    WriteRegStr HKCU "Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InprocServer32" "ThreadingModel" "Apartment"
    WriteRegStr HKCU "Software\Classes\.pdf\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" "" "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"
    WriteRegStr HKCU "Software\Classes\${PRODUCT_PROGID}\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" "" "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"

    ; Natively notify Windows Shell of file association changes
    ; SHCNE_ASSOCCHANGED = 0x08000000, SHCNF_IDLIST = 0
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

; --- Chú thích cho từng mục chọn ---
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Core executable files for FeathrPDF."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a shortcut on the Desktop."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create shortcuts in the Windows Start Menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} "Associate .pdf files with FeathrPDF and register thumbnail provider."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ===================================================================
;  Bộ Gỡ Cài Đặt (Uninstaller Wizard)
; ===================================================================
Section "Uninstall"
    ; 1. Xóa các Shortcut
    Delete "$DESKTOP\FeathrPDF.lnk"
    Delete "$DESKTOP\PDF Reader.lnk"
    Delete "$SMPROGRAMS\FeathrPDF\FeathrPDF.lnk"
    Delete "$SMPROGRAMS\FeathrPDF\Uninstall FeathrPDF.lnk"
    Delete "$SMPROGRAMS\FeathrPDF\Gỡ cài đặt FeathrPDF.lnk"
    Delete "$SMPROGRAMS\FeathrPDF\PDF Reader.lnk"
    RMDir "$SMPROGRAMS\FeathrPDF"

    ; 2. Dọn sạch các tệp tin trong thư mục cài đặt
    ; Hủy đăng ký Thumbnail Provider trước khi xóa DLL
    UnRegDLL "$INSTDIR\pdfthumb.dll"
    Delete "$INSTDIR\FeathrPDF.exe"
    Delete "$INSTDIR\pdfium.dll"
    Delete "$INSTDIR\pdfthumb.dll"
    Delete "$INSTDIR\app.ico"
    Delete "$INSTDIR\pdf_doc.ico"
    Delete "$INSTDIR\sample.pdf"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; 3. Dọn sạch Registry Control Panel
    DeleteRegKey HKCU "${PRODUCT_UNINST_KEY}"

    ; 4. Dọn sạch Registry liên kết file & Shell Thumbnail Provider
    DeleteRegKey HKCU "Software\Classes\CLSID\{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"
    DeleteRegKey HKCU "Software\Classes\.pdf\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}"
    DeleteRegKey HKCU "Software\Classes\${PRODUCT_PROGID}\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}"
    DeleteRegKey HKCU "Software\Classes\${PRODUCT_PROGID}"
    DeleteRegKey HKCU "Software\Classes\Applications\FeathrPDF.exe"
    DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "${PRODUCT_PROGID}"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids" "${PRODUCT_PROGID}"

    ; Natively notify Windows Shell of file association changes
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
