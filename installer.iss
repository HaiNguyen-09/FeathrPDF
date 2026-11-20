; ===================================================================
;  FeathrPDF Installer & Uninstaller Script (Inno Setup 6)
;  Tự động đóng gói Wizard cài đặt và gỡ cài đặt chuyên nghiệp
; ===================================================================

#define MyAppName "FeathrPDF"
#define MyAppVersion "1.2"
#define MyAppPublisher "h_nguynn0."
#define MyAppExeName "FeathrPDF.exe"

[Setup]
AppId={{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.2.0.0
VersionInfoCompany=h_nguynn0.
VersionInfoDescription=FeathrPDF Setup
VersionInfoCopyright=Copyright (C) 2026 h_nguynn0. All rights reserved.
DefaultDirName={localappdata}\Programs\{#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=.
OutputBaseFilename=FeathrPDF_Setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a Desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "associate"; Description: "Associate as default PDF reader & Assign dedicated document icon (.pdf)"; GroupDescription: "File associations:"

[Files]
Source: "{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "pdfthumb.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "app.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "pdf_doc.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "sample.pdf"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"
Name: "{autoprograms}\{#MyAppName}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\app.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"; Tasks: desktopicon

[Registry]
; 1. ProgID FeathrPDF.Document
Root: HKCU; Subkey: "Software\Classes\FeathrPDF.Document"; ValueType: string; ValueData: "PDF Document"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\FeathrPDF.Document\DefaultIcon"; ValueType: string; ValueData: "{app}\pdf_doc.ico"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\FeathrPDF.Document\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey; Tasks: associate

; 2. Applications\FeathrPDF.exe
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "FeathrPDF"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\DefaultIcon"; ValueType: string; ValueData: "{app}\app.ico"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Flags: uninsdeletekey; Tasks: associate

; 3. .pdf Association
Root: HKCU; Subkey: "Software\Classes\.pdf"; ValueType: string; ValueData: "FeathrPDF.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\.pdf"; ValueType: string; ValueName: "Content Type"; ValueData: "application/pdf"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: none; ValueName: "FeathrPDF.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\.pdf\DefaultIcon"; ValueType: string; ValueData: "{app}\pdf_doc.ico"; Flags: uninsdeletevalue; Tasks: associate

; 4. Explorer OpenWith
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList"; ValueType: string; ValueName: "a"; ValueData: "{#MyAppExeName}"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList"; ValueType: string; ValueName: "MRUList"; ValueData: "a"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids"; ValueType: none; ValueName: "FeathrPDF.Document"; Flags: uninsdeletevalue; Tasks: associate

; 5. Windows Shell Thumbnail Provider
Root: HKCU; Subkey: "Software\Classes\CLSID\{{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"; ValueType: string; ValueData: "FeathrPDF Thumbnail Provider"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\CLSID\{{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InProcServer32"; ValueType: string; ValueData: "{app}\pdfthumb.dll"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\CLSID\{{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}\InProcServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\.pdf\ShellEx\{{e357fccd-a995-4576-b01f-234630154e96}"; ValueType: string; ValueData: "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\FeathrPDF.Document\ShellEx\{{e357fccd-a995-4576-b01f-234630154e96}"; ValueType: string; ValueData: "{B4C83244-6C0F-4D2A-9A3E-74B6A68D5D01}"; Flags: uninsdeletekey; Tasks: associate

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch FeathrPDF now"; Flags: nowait postinstall skipifsilent
