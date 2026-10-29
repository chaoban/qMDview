# qMDView

以 Qt 6 Widgets 開發的 Markdown 檢視／編輯器。渲染使用 Qt 內建的 `QTextDocument::setMarkdown()`（md4c 解析器、GitHub 方言），不需要 WebEngine，MinGW 與 MSVC 都能編譯。

## 功能

| 功能 | 說明 |
|---|---|
| Markdown 顯示 | 標題、清單、待辦清單、表格（含對齊）、程式碼區塊、引用、圖片、水平線；GitHub 風格外觀，自動適應深色／淺色主題 |
| 資料夾檔案樹 | 開啟資料夾後列出所有 `.md` 檔，點選即開啟 |
| 目錄（TOC） | 依標題產生大綱，點選跳轉；捲動時自動標示目前章節 |
| 編輯＋即時預覽 | `Ctrl+E` 切換；左邊編輯（含語法上色）、右邊預覽，捲動同步 |
| 檔案監看 | 檔案被其他程式修改後自動重新載入；編輯中有未存變更時會先詢問 |
| 連結 | `#標題` 錨點（支援中文）、相對路徑 `.md` 連結（可帶 `#錨點`）、外部網址以瀏覽器開啟 |
| 編碼 | 自動判斷 UTF-8／UTF-8 BOM／UTF-16／系統編碼（Big5），存檔沿用原編碼與換行（CRLF/LF） |
| 其他 | 搜尋（全部醒目提示）、縮放、最近開啟清單、拖放開檔、記住視窗配置與上次開啟的檔案；「檔案 › 關閉檔案」關閉後，下次啟動只開回資料夾、不再自動載入該檔 |
| 介面語言 | 繁體中文、English；預設跟隨 Windows 顯示語言（其他語言顯示英文），可在「檢視 › 語言」切換，重新啟動後套用 |

## 建置

**需求**：Qt 6.2 以上（建議 6.5 LTS 或更新）、CMake 3.16 以上、Qt Creator。用到 `Qt6::Widgets` 模組與 Qt Linguist 工具（`LinguistTools`，安裝 Qt 時已附帶）。

1. Qt Creator → **檔案 › 開啟檔案或專案** → 選 `CMakeLists.txt`
2. 選擇 Kit（例如 *Desktop Qt 6.x MinGW 64-bit* 或 *MSVC 2019/2022 64-bit*）
3. 按 **建置**（Ctrl+B）→ **執行**（Ctrl+R）

> 專案放在 OneDrive 時，Qt Creator 預設會把建置輸出放在專案資料夾內的 `build/`，會一起同步上雲端。建議在 Kit 設定頁把 **Build directory** 改到 OneDrive 以外（例如 `C:\build\qMDView`）。

指令列建置：

```bat
cmake -S . -B C:\build\qMDView -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build C:\build\qMDView
```

### 介面翻譯（多國語系）

- 原始字串是繁體中文，寫在程式碼的 `tr()` 與 `mainwindow.ui`；英文翻譯在 `translations/qMDView_en.ts`
- 建置時自動把 `.ts` 編成 `.qm` 並嵌入 exe（資源路徑 `:/i18n/`），不需另外部署
- 語言的選擇在 `src/uilanguage.cpp`：「檢視 › 語言」的設定（存在設定的 `ui/language`）→ 未設定時依 Windows 慣用語言 → 都不支援時用英文；簡體中文系統目前顯示繁體中文
- Qt 內建字串（對話框按鈕、編輯器右鍵選單）：繁中用 windeployqt 部署的 `qtbase_zh_TW.qm`；英文是 Qt 的原始語言，不需翻譯檔

**新增或修改介面字串後**，更新英文翻譯：

1. 命令提示字元，切到專案資料夾，執行 `C:\Qt\6.6.2\mingw_64\bin\lupdate.exe src -ts translations\qMDView_en.ts`（把新字串加入 .ts；也可在 Qt Creator 按 `Ctrl+K` 輸入 `cm update_translations`）
2. 用 Qt Linguist（`C:\Qt\6.6.2\mingw_64\bin\linguist.exe`）開啟 `translations\qMDView_en.ts`，補上未翻譯的字串並存檔（沒翻譯的字串在英文介面會顯示中文）
3. 重新建置

**新增語言**（例如日文 `ja`）：`CMakeLists.txt` 的 `TS_FILES` 加 `translations/qMDView_ja.ts`、`src/uilanguage.cpp` 的 `available()` 與 `nativeName()` 加上 `ja`，再依上面步驟產生並翻譯 .ts（第一次執行 lupdate 時加上 `-source-language zh_TW`）；`build_installer.bat` 的 `windeployqt --translations` 加上 `ja`（Qt 內建字串）；Store 版另外在 `msix/AppxManifest.xml.in` 的 `<Resources>` 加 `ja-JP`。

### 打包安裝程式（給其他電腦安裝）

在專案資料夾雙擊 **`build_installer.bat`**，會依序：

1. 用 Qt 內附的 MinGW + CMake + Ninja 編譯 Release 版
2. `windeployqt` 收集 Qt DLL、外掛、MinGW 執行期與中文翻譯，組成可攜資料夾
3. 用 Inno Setup 產生 **`dist\qMDView-<版本>-setup.exe`**

第一次執行若找不到 Inno Setup，會詢問是否用 winget 安裝 Inno Setup 7（目前使用者、免費）。建置暫存檔放在 `%LOCALAPPDATA%\qMDView_ms-build`，不會同步到 OneDrive（與原本 qMDView 專案的 `qMDView-build` 分開，避免 CMake 快取指向另一個原始碼資料夾）。

| 參數 | 用途 |
|---|---|
| `clean` | 清掉上次的建置結果，完整重編 |
| `noinstaller` | 只編譯＋部署成可攜資料夾，不做安裝程式 |
| `nopause` | 結束時不等待按鍵（給自動化腳本用） |

- **版本號**：取自 `CMakeLists.txt` 的 `project(qMDView VERSION x.y.z)`，改這裡即可
- **Qt 路徑**：預設 `C:\Qt\6.6.2\mingw_64`；版本不同時改 bat 開頭的 `QT_VERSION`／`MINGW_NAME`

安裝程式（`installer\qMDView.iss`）的行為：

- 預設免管理員權限，裝在使用者目錄；安裝畫面可改選「為所有使用者安裝」
- 可勾選：桌面捷徑、`.md`／`.markdown` 加入「開啟檔案」清單、資料夾右鍵「以 qMDView 開啟」
- 介面依系統語系顯示繁體中文或英文；附解除安裝，重新安裝新版會直接覆蓋升級
- 只支援 64 位元 Windows 10／11（Qt 6 的限制）

> Windows 10／11 不允許安裝程式直接搶「預設程式」。安裝後第一次對 .md 檔按右鍵 →「開啟檔案」→ 選 qMDView 並勾選「一律使用此應用程式」即可設為預設。

### 上架 Microsoft Store（MSIX）

上架 Microsoft Store 使用 MSIX 套件：不需購買程式碼簽章憑證（Store 會簽），更新由 Store 推送；目前免費上架，日後改為收費也不必重新打包。完整步驟與 Partner Center 各欄位填法見 **`msix/STORE_SUBMISSION.md`**。

1. 第一次：把 Partner Center「產品身分識別」的三個值填進 `msix\store_identity.cmd`
2. 雙擊 **`build_msix.bat`** → 產出 **`dist\qMDView-<版本>.0-x64.msix`**（不需自行簽章，Store 會簽）
3. 本機測試：開啟 Windows「開發人員模式」後執行 `build_msix.bat register`

需要 Windows 10/11 SDK（`makeappx.exe`、`makepri.exe`），可由 Visual Studio Installer →「個別元件」→ Windows 11 SDK 安裝。編譯與 windeployqt 步驟直接沿用 `build_installer.bat noinstaller`。

Store 版與安裝程式版的差異：沒有資料夾右鍵選單（MSIX 需另寫 COM 外掛才能做）；設定獨立存放，解除安裝時一併清除；更新由 Store 推送，每次上架的版本號必須比上一版大。

## 專案結構

```
qMDView/
├─ CMakeLists.txt
├─ build_installer.bat          一鍵編譯＋打包安裝程式
├─ build_msix.bat               一鍵編譯＋打包 Microsoft Store 用的 MSIX
├─ msix/
│  ├─ AppxManifest.xml.in       MSIX 資訊清單範本（身分、檔案關聯、runFullTrust）
│  ├─ store_identity.cmd        Partner Center 產品身分識別（第一次上架前填寫）
│  ├─ Assets/                   開始功能表／工作列／Store 用的各尺寸圖示
│  ├─ store_listing/            Partner Center 商店清單用的圖片
│  └─ STORE_SUBMISSION.md       上架步驟與 Partner Center 填寫指南
├─ installer/
│  ├─ qMDView.iss               Inno Setup 安裝程式腳本
│  └─ ChineseTraditional.isl    安裝畫面繁中翻譯（給 Inno Setup 6 用；7 已內建）
├─ src/
│  ├─ main.cpp                  進入點、命令列參數、載入介面語言
│  ├─ uilanguage.h / .cpp       介面語言：支援清單、依設定／系統語系選擇、載入翻譯
│  ├─ mainwindow.ui / .h / .cpp 主視窗（選單、工具列、面板、開存檔、監看、搜尋、設定）
│  ├─ markdownview.h / .cpp     預覽元件（QTextBrowser 子類）：渲染、圖片、連結、標題擷取
│  ├─ markdownstyler.h / .cpp   渲染後的外觀調整（程式碼框、表格、引用、行高）
│  ├─ markdownhighlighter.*     編輯器語法上色
│  ├─ tocwidget.*               目錄面板
│  ├─ findbar.*                 搜尋列
│  ├─ aboutdialog.*             「關於」對話框（SAISAI.Dev logo、著作權、授權資訊）
│  └─ textfileio.*              讀寫檔案與編碼偵測
├─ translations/                介面翻譯（qMDView_en.ts；原始字串為繁中，不需要 zh_TW 檔）
├─ resources/                   圖示、SAISAI.Dev logo、app.rc.in（exe 圖示與版本資訊範本）
├─ licenses/                    第三方授權聲明與 LGPL／GPL 全文（隨安裝包發佈，程式內也可檢視）
└─ samples/                     功能示範文件（sample.md、sample-en.md、second.md）
```

## 常見修改位置

| 想改的東西 | 位置 |
|---|---|
| 選單、工具列、面板配置、快捷鍵 | `src/mainwindow.ui`（在 Qt Creator 雙擊即可用 Designer 編輯） |
| 預覽的顏色、間距、程式碼框、表格樣式 | `src/markdownstyler.cpp` 的 `colorsFor()` 與各 `style…()` / `wrap…()` 函式 |
| 預覽字級、文件邊界 | `MarkdownView` 建構子（`m_basePointSize`）與 `rebuild()`（`setDocumentMargin`） |
| 編輯器語法上色規則與顏色 | `src/markdownhighlighter.cpp` 的 `updateColors()` |
| 檔案樹顯示的副檔名 | `MainWindow::markdownNameFilters()` |
| 英文介面用語 | `translations/qMDView_en.ts`（用 Qt Linguist 編輯） |
| 支援的介面語言 | `src/uilanguage.cpp` 的 `available()`＋`CMakeLists.txt` 的 `TS_FILES`＋`msix/AppxManifest.xml.in` 的 `<Resources>` |
| 發行者名稱、著作權年份、聯絡信箱 | `CMakeLists.txt` 的 `APP_PUBLISHER`／`APP_COPYRIGHT_YEAR`／`APP_CONTACT_EMAIL`（程式與 exe 版本資訊），以及 `installer/qMDView.iss` 開頭的 `AppPublisher`／`AppContact`（安裝程式） |
| 「關於」對話框的 logo | `resources/images/saisai_logo.png`（直接替換同名檔案） |
| 編輯時預覽的更新延遲 | `MainWindow` 建構子的 `m_renderTimer.setInterval(250)` |

## 快捷鍵

| 動作 | 按鍵 |
|---|---|
| 開啟檔案／資料夾 | `Ctrl+O` ／ `Ctrl+Shift+O` |
| 新增／儲存／另存 | `Ctrl+N` ／ `Ctrl+S` ／ `Ctrl+Shift+S` |
| 重新載入 | `F5` |
| 關閉檔案 | `Ctrl+W` ／ `Ctrl+F4` |
| 編輯模式 | `Ctrl+E` |
| 搜尋／下一個／上一個 | `Ctrl+F` ／ `F3`、`Enter` ／ `Shift+F3`、`Shift+Enter` |
| 放大／縮小／原始大小 | `Ctrl++`、`Ctrl+滾輪` ／ `Ctrl+-` ／ `Ctrl+0` |
| 顯示／隱藏檔案樹、目錄 | `Ctrl+1` ／ `Ctrl+2` |

## 已知限制（Qt 內建 Markdown 引擎）

- 預覽中的程式碼沒有語法上色；不支援 Mermaid、LaTeX 數學式
- 內嵌 HTML 只支援 Qt RichText 子集
- 不下載網路圖片，只顯示本機圖片
- 引用（`>`）內的程式碼區塊不會加框
- 文件很大（數千行）時，編輯模式每次重繪約 0.1～0.2 秒

需要上述功能時，可改用 Qt WebEngine + JavaScript 渲染器（例如 markdown-it），但 Windows 上須改用 MSVC 編譯。
