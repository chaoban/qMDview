; ============================================================================
; qMDView 安裝程式（Inno Setup 6.5+ / 7）
;
; 通常由專案根目錄的 build_installer.bat 呼叫，它會傳入：
;   /DAppVersion=1.0.0              版本（取自 CMakeLists.txt）
;   /DDeployDir=<windeployqt 輸出>   要打包的檔案
;   /O<輸出資料夾>                  setup.exe 放置位置
; 也可以用 Inno Setup 編輯器直接開啟本檔編譯（需先手動定義 DeployDir）。
; ============================================================================

#define AppName      "qMDView"
#define AppExeName   "qMDView.exe"
#define AppPublisher "SAISAI.Dev"
#define AppCopyrightYear "2026"
#define AppContact   "aisais.dev@gmail.com"
#define AppProgId    "qMDView.md"

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef DeployDir
  #error 未定義 DeployDir。請用 build_installer.bat 建置，或以 /DDeployDir=<部署資料夾> 呼叫 ISCC。
#endif

; 繁體中文介面：Inno Setup 7 內建；Inno Setup 6 則使用本資料夾附帶的翻譯檔
#if FileExists(CompilerPath + "Languages\ChineseTraditional.isl")
  #define ChineseIsl "compiler:Languages\ChineseTraditional.isl"
#elif FileExists(SourcePath + "ChineseTraditional.isl")
  #define ChineseIsl AddBackslash(SourcePath) + "ChineseTraditional.isl"
#endif

[Setup]
; AppId 用來辨識「同一個程式」，升級安裝會覆蓋舊版。發佈後請勿更改。
AppId={{3A39F384-1889-4F44-AA3D-AF355646CECE}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright (C) {#AppCopyrightYear} {#AppPublisher}
VersionInfoCompany={#AppPublisher}
; 顯示在 Windows「應用程式與功能」的支援資訊
AppContact={#AppContact}
AppSupportURL=mailto:{#AppContact}
VersionInfoDescription={#AppName} Setup
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; 預設免管理員權限（裝在使用者目錄）；安裝畫面可改選「為所有使用者安裝」
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Qt 6 只支援 64 位元 Windows 10 以上
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
ChangesAssociations=yes

[Languages]
#ifdef ChineseIsl
Name: "chinesetrad"; MessagesFile: "{#ChineseIsl}"
#endif
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.Integration=Windows integration:
english.AssocMd=Add {#AppName} to "Open with" for .md / .markdown files
english.FolderMenu=Add "Open with {#AppName}" to the folder right-click menu
english.OpenWithApp=Open with {#AppName}
english.MarkdownDocument=Markdown Document
#ifdef ChineseIsl
chinesetrad.Integration=Windows 整合：
chinesetrad.AssocMd=將 {#AppName} 加入 .md／.markdown 檔案的「開啟檔案」清單
chinesetrad.FolderMenu=資料夾右鍵選單加入「以 {#AppName} 開啟」
chinesetrad.OpenWithApp=以 {#AppName} 開啟
chinesetrad.MarkdownDocument=Markdown 文件
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocmd";     Description: "{cm:AssocMd}";    GroupDescription: "{cm:Integration}"
Name: "foldermenu";  Description: "{cm:FolderMenu}"; GroupDescription: "{cm:Integration}"

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; HKA = 依安裝模式自動選 HKCU（目前使用者）或 HKLM（所有使用者）

; ---- .md 檔案關聯 ----
; Windows 10/11 不允許安裝程式直接搶「預設程式」，這裡把 qMDView 加入「開啟檔案」清單；
; 使用者第一次用 qMDView 開啟時勾選「一律使用此應用程式」即可設為預設。
Root: HKA; Subkey: "Software\Classes\{#AppProgId}"; ValueType: string; ValueName: ""; ValueData: "{cm:MarkdownDocument}"; Flags: uninsdeletekey; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\{#AppProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\{#AppProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "{#AppProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\.markdown\OpenWithProgids"; ValueType: string; ValueName: "{#AppProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".md"; ValueData: ""; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".markdown"; ValueData: ""; Tasks: assocmd
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: assocmd

; ---- 資料夾右鍵選單（在資料夾上按右鍵，以及在資料夾空白處按右鍵）----
; Windows 11 會放在「顯示其他選項」裡
Root: HKA; Subkey: "Software\Classes\Directory\shell\{#AppName}"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenWithApp}"; Flags: uninsdeletekey; Tasks: foldermenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\{#AppName}"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeName},0"; Tasks: foldermenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\{#AppName}\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: foldermenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\{#AppName}"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenWithApp}"; Flags: uninsdeletekey; Tasks: foldermenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\{#AppName}"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#AppExeName},0"; Tasks: foldermenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\{#AppName}\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%V"""; Tasks: foldermenu

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
