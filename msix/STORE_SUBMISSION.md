# qMDView 上架 Microsoft Store 指南

> 整理日期：2026-10-20；2026-10-22 更新：1.1.0 加入英文介面（見 2.5、3.7a）。Partner Center 的介面與規定會變動，送審前以畫面上的欄位說明為準。
> 本文件中的授權條款與隱私權政策是範本，不構成法律意見。

## 0. 流程總覽

| 步驟 | 在哪裡做 | 說明 |
|---|---|---|
| 1 | Partner Center | 建立產品、保留名稱 **qMDView** |
| 2 | Partner Center | 複製「產品身分識別」三個值 → 填入 `msix\store_identity.cmd` |
| 3 | Partner Center 帳戶設定 | **收費必做**：完成付款（Payout）與稅務（Tax）設定 |
| 4 | 自己的電腦 | `build_msix.bat register` 本機測試 → `build_msix.bat` 產出 `.msix` |
| 5 | 自己準備 | 截圖 4～8 張、隱私權政策網頁 |
| 6 | Partner Center | 建立提交（Submission），依第 3 節逐項填寫 → 送出認證 |

---

## 1. 為什麼改用 MSIX（不用現有的 setup.exe）

| | MSIX（本專案採用） | EXE／MSI（現有 Inno Setup 安裝程式） |
|---|---|---|
| Store 代收款項 | 可以（買斷、試用、特價） | 不行；「Paid」只是標示，須自建金流 |
| 程式碼簽章 | 不用買憑證，Store 會重新簽章 | 必須向受信任 CA 購買程式碼簽章憑證 |
| 檔案託管 | Store 託管 | 自備 HTTPS 下載網址，且檔案上架後不能變動 |
| 更新 | Store 自動推送 | 程式自己處理 |

你選的販售模式是「一次買斷、不提供試用」，因此程式碼**不需要**呼叫任何 Store 授權 API，Store 只讓付款的使用者下載安裝。

---

## 2. 程式與打包的調整

### 2.1 已完成的修改

| 檔案 | 內容 |
|---|---|
| `build_msix.bat`（新增） | 呼叫 `build_installer.bat noinstaller` 編譯＋windeployqt → 組 MSIX 版面 → `makepri` → `makeappx pack` → `dist\qMDView-<x.y.z>.0-x64.msix`；加 `register` 參數可在本機安裝測試 |
| `msix\AppxManifest.xml.in`（新增） | 資訊清單範本：完全信任桌面程式（`runFullTrust`）、`.md`／`.markdown` 檔案關聯（加入「開啟檔案」清單，檔案路徑以 `"%1"` 傳給程式）、命令別名 `qmdview`、最低 Windows 10 1809、僅 x64、語言 zh-TW（1.1.0 起加 en-US） |
| `msix\store_identity.cmd`（新增） | 放 Partner Center 給的身分識別值（需你填寫） |
| `msix\Assets\`（新增） | 由 `resources\icons\app.png` 產生的 28 張圖示：StoreLogo、Square44x44（含工作列用 targetsize／unplated）、Square150x150、Wide310x150，各種縮放比例 |
| `msix\store_listing\qMDView_AppTileIcon_300x300.png`（新增） | 上傳到商店清單的 1:1 應用程式磚圖示 |
| `licenses\THIRD-PARTY-NOTICES.txt` | 新增「Microsoft Store 版本說明」：Store 版的 Qt DLL 無法直接替換，購買者可來信免費索取可替換 DLL 的一般安裝版（LGPL v3 義務） |
| `README.md` | 新增「上架 Microsoft Store（MSIX）」一節與專案結構 |

C++ 原始碼不需要修改，原因：

- 程式只讀寫使用者開啟的檔案，不寫入安裝資料夾（MSIX 的安裝資料夾是唯讀的）
- `QSettings` 寫入 `HKCU\Software\SAISAI.Dev\qMDView`，MSIX 會自動為每個套件獨立存放，解除安裝時一併清除
- 沒有自動更新機制（Store 版必須由 Store 更新）
- MinGW 執行期 DLL 已由 windeployqt 一併打包，不需要 VC++ 可轉散發套件
- `main.cpp` 已處理命令列的檔案路徑，檔案關聯開檔可直接運作

### 2.2 Store 版與安裝程式版的差異

| 項目 | Inno Setup 安裝程式 | Microsoft Store（MSIX） |
|---|---|---|
| .md 加入「開啟檔案」清單 | 有（可勾選） | 有（自動） |
| 資料夾右鍵「以 qMDView 開啟」 | 有 | **沒有**（MSIX 需另寫 IExplorerCommand COM 外掛；使用程式內「開啟資料夾」Ctrl+Shift+O） |
| 桌面捷徑 | 可勾選 | 無（使用者可自行從開始功能表釘選） |
| 設定存放 | 一般登錄檔 | 套件專屬，解除安裝時清除 |
| 更新 | 重新執行新版安裝程式 | Store 自動更新 |

兩種版本可以同時安裝，互不影響。現有 `installer\qMDView.iss` 與 AppId 都不需要更動。

### 2.3 你需要做的事

1. **安裝 Windows SDK**：Visual Studio Installer → 修改 VS 2022 → 個別元件 → 勾選「Windows 11 SDK（10.0.26100）」或任一 Windows 10/11 SDK。`build_msix.bat` 會自動找最新版的 `makeappx.exe`／`makepri.exe`。
2. **填寫 `msix\store_identity.cmd`**（見 3.1）。
3. **本機測試**（建議每次上架前都做）：
   1. 設定 → 系統 → 開發人員專用 → 開啟「開發人員模式」
   2. 雙擊或在命令列執行 `build_msix.bat register`
   3. 測試清單：
      - [ ] 從開始功能表啟動 qMDView，圖示正常
      - [ ] 對 .md 檔按右鍵 →「開啟檔案」→ 選 qMDView，檔案正確開啟
      - [ ] Win+R 輸入 `qmdview` 可啟動
      - [ ] 開啟資料夾、編輯、存檔、搜尋、縮放都正常
      - [ ] 說明 → 關於 → 授權資訊，可看到新增的 Store 版本說明
      - [ ] 關閉再開，視窗配置與最近開啟清單有被記住
      - [ ] 設定 → 應用程式 → qMDView → 解除安裝，可正常移除
4. **產出上傳檔**：執行 `build_msix.bat`，上傳 `dist\qMDView-1.0.0.0-x64.msix`。這個檔案沒有簽章是正常的，Store 認證後會幫你簽。
5. **（建議）重做高解析度圖示**：目前圖示來源 `app.png` 是 256×256（實際圖案 206 px），300×300 磚圖示與 256 px 工作列圖示是小幅放大產生的。若能提供 1024×1024 的原始圖，重新產生會更清晰。

### 2.4 之後發布更新

1. 改 `CMakeLists.txt` 的 `project(qMDView VERSION 1.0.1 ...)`（MSIX 版本會變成 1.0.1.0；第 4 碼固定為 0，由 Store 保留）
2. 執行 `build_msix.bat`
3. Partner Center → qMDView → 「更新」建立新提交 → Packages 上傳新的 .msix → 填 What's new → 送出

每次上傳的版本號都必須比上一版大。

### 2.5 1.1.0 版：加入英文介面

程式已改為多國語系（繁體中文＋英文，見 README「介面翻譯」），資訊清單也宣告了 `en-US`。版本為 1.1.0（MSIX 1.1.0.0）。

| 步驟 | 在哪裡做 | 內容 |
|---|---|---|
| 1 | 電腦（檔案總管） | 雙擊 `build_msix.bat` → 產出 `dist\qMDView-1.1.0.0-x64.msix` |
| 2 | 電腦（命令提示字元） | `build_msix.bat register` 安裝到本機 → 從開始功能表開 qMDView →「檢視 → 語言 → English」→「立即重新啟動」，確認介面變成英文；再切回「跟隨系統」 |
| 3 | 網頁（Partner Center） | qMDView：1.0.0 已上架 → 按「Update」建立新提交；1.0.0 還沒送出 → 直接在原本的提交操作 |
| 4 | 網頁（Partner Center）→ Packages | 上傳 `qMDView-1.1.0.0-x64.msix`；還沒上架過的話，把舊的 1.0.0.0 刪掉 |
| 5 | 網頁（Partner Center）→ Store listings | 清單語言會多出 English (United States)，依 **3.7a** 填寫。不打算提供英文清單可按 Remove（官方說明：每個套件語言都提供清單是「建議」，至少一種語言即可） |
| 6 | 網頁（Partner Center）→ Store listings → 繁體中文 | 依 **3.7** 更新描述開頭的「※ 程式介面…」、Short description、Product features（原本寫「介面為繁體中文」） |
| 7 | 網頁（Partner Center）→ Submission options | Notes for certification 換成 **3.8** 的新版本（已改成中英文介面） |
| 8 | 網頁（Partner Center） | 送出認證 |

**What's new in this version**（更新提交才需要填；第一次上架留空）：

- 繁體中文：`新增英文介面：預設依 Windows 顯示語言，也可在「檢視 → 語言」切換。`
- English：`Added an English user interface. It follows your Windows display language and can be changed in View > Language.`

英文清單的截圖要用英文介面拍：「檢視 → 語言 → English」重新啟動後，開啟 `samples\sample-en.md`（英文版示範文件）所在資料夾再截圖。

---

## 3. Partner Center 逐項填寫

入口：<https://partner.microsoft.com/dashboard> → **Apps and games**。Partner Center 若顯示中文介面，欄位名稱會不同，以下以英文欄位名稱為主、括號附中文說明。

### 3.1 建立產品與產品身分識別

1. **Apps and games → New product → MSIX or PWA app**
2. 保留名稱輸入 **`qMDView`**。若名稱已被他人使用，需改用其他名稱，並同步修改 `msix\AppxManifest.xml.in` 裡的兩處 `DisplayName`（資訊清單的顯示名稱必須等於保留的名稱）。
3. 進入產品 → **Product management → Product identity**，把三個值**複製貼上**到 `msix\store_identity.cmd`：

| Partner Center 顯示 | 填到 | 範例格式 |
|---|---|---|
| `Package/Identity/Name` | `MSIX_IDENTITY_NAME` | `12345SAISAIDev.qMDView` |
| `Package/Identity/Publisher` | `MSIX_PUBLISHER` | `CN=XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX` |
| `Package/Properties/PublisherDisplayName` | `MSIX_PUBLISHER_DISPLAY_NAME` | 目前預設填 `SAISAI.Dev` |

> `PublisherDisplayName` 必須和你開發人員帳戶的發行者顯示名稱一致；Store 頁面上的「發行者」也會顯示這個名稱。若帳戶目前顯示的是本名而你想顯示 SAISAI.Dev，請到 帳戶設定（齒輪）→ Account settings → Legal info / Developer 頁面修改發行者顯示名稱，再回來複製。

### 3.2 帳戶：付款與稅務（收費必做）

齒輪 → **Account settings → Payout and tax → Payout and tax profiles**：

- **Payout account**：填可收外幣匯款的銀行帳戶（戶名需與開發人員帳戶的法定名稱一致）
- **Tax profile**：依精靈作答；台灣個人通常會被導向 **W-8BEN**（非美國個人）表單

沒完成這兩項，付費 App 無法上架或無法收到款項。Store 抽成比例與付款門檻以你在 Partner Center 接受的 App Developer Agreement 為準。稅務處理（例如美國扣繳與台灣申報）建議洽詢會計師。

### 3.3 Pricing and availability（定價與可用性）

| 欄位 | 建議填法 | 說明 |
|---|---|---|
| Markets | 保留「所有市場」 | 1.1.0 起介面有繁中與英文，已在說明首段標示；若想先小規模販售，可只勾台灣、香港、澳門、新加坡、馬來西亞 |
| Audience | Public audience | |
| Discoverability | Make this product available and discoverable in the Microsoft Store | 可被搜尋、可購買 |
| Schedule | Release as soon as possible／Stop acquisition: Never | 首次上架可改在 3.9 用「手動發布」控制時間 |
| Base price | 從價格級距選一個（以美元級距為基準，自動換算各市場；可對台灣另設價格） | 價格隨時可改。政策要求定價不可明顯高於功能價值 |
| Free trial | **No free trial** | 你選擇不提供試用 |
| Sale pricing | 不設定 | 之後可設限時特價 |
| Organizational licensing | 保留預設 | 允許企業透過大量授權購買 |

### 3.4 Properties（內容）

| 欄位 | 建議填法 |
|---|---|
| Category | **Productivity**（生產力；無子類別） |
| Secondary category | **Developer tools** → Subcategory **Utilities**（選填） |
| Does this product access, collect, or transmit personal information? | **Yes**，並填隱私權政策網址（見下方說明） |
| Privacy policy URL | 你架設的隱私權政策網頁（範本見附錄 A） |
| Website | 選填；沒有可留空 |
| Support contact info | `aisais.dev@gmail.com` |
| Display mode | 全部不勾 |
| System requirements | Minimum hardware：Keyboard、Mouse（選填，其餘不填） |

**為什麼隱私權問題要答 Yes**：Microsoft Store Policies 10.5.1 寫明「本質上可存取個人資訊的產品類型必須有隱私權政策，包括 Desktop Bridge 與 Win32 產品」。qMDView 是完全信任的 Win32 程式，可讀取使用者開啟的檔案，因此要提供隱私權政策網址，否則可能認證失敗。政策內容照實寫「不蒐集、不傳送」即可。

**Product declarations（產品聲明）**：

| 聲明 | 設定 |
|---|---|
| This product allows users to make purchases, but does not use the Microsoft Store commerce system | 不勾 |
| This product has been tested to meet accessibility guidelines | 不勾（除非你實際做過協助工具測試） |
| Customers can install this product to alternate drives or removable storage | 保留勾選 |
| Windows can include this product's data in automatic backups to OneDrive | 保留勾選 |
| This product supports pen and ink input | 不勾 |
| This product incorporates generative AI features | 不勾 |

### 3.5 Age ratings（年齡分級，IARC 問卷）

- 產品類型選最接近「工具／生產力／其他（Utility, Productivity, Communication, or Other）」的選項，**不要**選遊戲
- 暴力、性、粗話、藥物、賭博等內容題：全部 **No**
- 使用者之間能否互動或分享內容（聊天、上傳）：**No**
- 是否分享使用者位置：**No**
- 是否可購買數位商品（App 內購買）：**No**（App 本身付費不算 App 內購買）
- 是否提供不受限制的網際網路存取（如內建瀏覽器）：**No**（外部連結是交給系統瀏覽器開啟）

預期結果為各地區的最低年齡分級（例如 3+）。

### 3.6 Packages（套件）

- 上傳 `dist\qMDView-1.0.0.0-x64.msix`
- Device family availability：只勾 **Windows 10/11 Desktop**，其他（Xbox、HoloLens、Team）不勾
- 上傳後顯示 Validated 才算成功。常見錯誤：

| 錯誤訊息大意 | 原因與處理 |
|---|---|
| Identity Name／Publisher 不符 | `store_identity.cmd` 的值打錯（大小寫、空白），重新複製貼上 |
| PublisherDisplayName 不符 | 和帳戶的發行者顯示名稱不同（見 3.1） |
| DisplayName 不是保留的名稱 | 保留的名稱不是 qMDView，需改資訊清單（見 3.1） |
| 版本號必須大於上一版 | 改 CMakeLists.txt 版本後重建 |

### 3.7 Store listings（商店清單）— 繁體中文（zh-tw）

1.1.0 起資訊清單宣告 zh-TW 與 en-US，Partner Center 會列出這兩種清單語言；英文清單見 **3.7a**。政策 10.1.1 要求清單如實描述功能、限制與支援語言。

**Product name**：qMDView

**Description**（必填，上限 10,000 字）：

```
qMDView 是一套輕巧、快速的 Markdown 檢視與編輯工具，專為 Windows 打造。不需要瀏覽器引擎，開啟長篇文件也很流暢。

※ 程式介面提供繁體中文與英文，預設依 Windows 顯示語言，可在「檢視 → 語言」切換。

主要功能
• 即時預覽：標題、清單、待辦清單、表格（含對齊）、程式碼區塊、引用、圖片、水平線，GitHub 風格外觀，自動配合 Windows 深色／淺色主題
• 編輯＋即時預覽：Ctrl+E 切換左右分割，編輯器有 Markdown 語法上色，左右捲動同步
• 目錄（TOC）：依標題自動產生大綱，點選即跳轉，捲動時標示目前章節
• 資料夾檔案樹：開啟資料夾後列出所有 Markdown 檔，點選即開啟
• 檔案監看：檔案被其他程式修改後自動重新載入，編輯中有未存變更時會先詢問
• 連結：支援中文標題錨點、相對路徑 .md 連結，外部網址以瀏覽器開啟
• 編碼：自動判斷 UTF-8、UTF-8 BOM、UTF-16、Big5，存檔沿用原本的編碼與換行格式（CRLF／LF）
• 其他：全文搜尋並醒目提示所有結果、縮放、最近開啟清單、拖放開檔、記住視窗配置與上次開啟的檔案
• 可在 .md／.markdown 檔案的「開啟檔案」清單中直接選用 qMDView

適合用來
閱讀專案的 README 與技術文件、撰寫筆記、檢視各種工具產生的 Markdown 報告。

隱私
qMDView 只讀寫你自行開啟的本機檔案。不需要帳號、不連線到任何伺服器、不蒐集任何資料。

已知限制
• 預覽不支援 Mermaid 圖表與 LaTeX 數學式
• 不下載網路圖片，只顯示本機圖片
• 預覽中的程式碼區塊不做語法上色
• 內嵌 HTML 只支援部分語法

開放原始碼元件
本軟體使用 Qt（GNU LGPL v3）等開放原始碼元件，授權資訊可在程式的「說明 → 關於 → 授權資訊」查看。
```

**What's new in this version**：第一次上架留空。

**Product features**（最多 20 項，每項 200 字內，不要自己加項目符號）：

```
GitHub 風格 Markdown 預覽，支援表格、待辦清單、程式碼區塊與圖片
編輯與即時預覽左右並排，捲動同步
依標題自動產生目錄，點選即跳轉
資料夾檔案樹，快速切換多份文件
檔案被其他程式修改時自動重新載入
支援中文標題錨點與相對路徑連結
自動判斷 UTF-8、UTF-16、Big5 編碼，存檔保留原編碼與換行
全文搜尋並醒目提示所有結果
自動配合 Windows 深色／淺色主題
介面提供繁體中文與英文
不需帳號、不連網、不蒐集資料
```

**Screenshots**（必填至少 1 張，建議 4～8 張；PNG，至少 1366×768，單檔 50 MB 內）：

在 1920×1080、顯示比例 100% 的螢幕上把 qMDView 最大化，用 Alt+PrtScn 或剪取工具截圖。建議畫面：

1. 預覽＋左側檔案樹＋目錄（開啟 `samples\sample.md` 所在資料夾）
2. 編輯＋即時預覽（Ctrl+E）
3. 搜尋並醒目提示（Ctrl+F）
4. 深色主題下的預覽
5. 表格與程式碼區塊的呈現

注意檔案樹與標題列不要出現工作或私人檔名、使用者名稱。每張截圖可加簡短說明文字（例如「編輯與即時預覽並排」）。

**Store logos**：1:1 App tile icon（300×300）上傳 `msix\store_listing\qMDView_AppTileIcon_300x300.png`。Box art、Poster art 是給遊戲／Xbox 用的，不用上傳。

**Trailers／16:9 Super hero art**：選填，可之後再補（Super hero art 為 1920×1080 PNG，畫面中不可有產品名稱文字）。

**Supplemental fields（補充欄位）**：

| 欄位 | 建議內容 |
|---|---|
| Short description | 輕巧快速的 Markdown 檢視／編輯器：GitHub 風格預覽、編輯即時預覽、目錄、資料夾檔案樹、檔案變更自動重新載入。介面提供繁體中文與英文。 |
| Search terms（最多 7 組） | Markdown、Markdown 檢視器、Markdown 編輯器、MD 閱讀器、README、筆記、Markdown viewer |
| Copyright and trademark info | © 2026 SAISAI.Dev 版權所有 |
| Additional license terms | 見下方 |
| Developed by | SAISAI.Dev |

**Additional license terms**（Microsoft Store 標準應用程式授權條款禁止逆向工程，與 Qt 的 LGPL v3 義務有衝突；標準條款本身寫明「應用程式附有個別條款時，以該條款為準」，因此在這裡把 LGPL 權利寫清楚）：

```
本軟體包含依 GNU 寬通用公共授權條款第 3 版（LGPL v3）授權的 Qt 程式庫及其他開放原始碼元件，完整清單與授權全文見程式內「說明 → 關於 → 授權資訊」。這些元件依其各自的授權條款提供。LGPL v3 賦予你的權利，包括修改 Qt 程式庫，以及為了對這些修改除錯而進行的逆向工程，不受 Microsoft Store 標準應用程式授權條款或本軟體其他授權條款的限制。如需可自行替換 Qt 程式庫的同版本一般安裝版，請來信 aisais.dev@gmail.com，將免費提供。

This software includes the Qt libraries and other open-source components licensed under the GNU Lesser General Public License version 3 (LGPL v3). See Help > About > Licenses in the app for the full list and license texts. These components are provided under their own licenses. Your rights under the LGPL v3, including modifying the Qt libraries and reverse engineering for debugging such modifications, are not restricted by the Microsoft Store Standard Application License Terms or any other license terms of this software. To obtain the same version as a regular installation in which the Qt libraries can be replaced, contact aisais.dev@gmail.com (free of charge).
```

### 3.7a Store listings（商店清單）— 英文（en-us，1.1.0 起）

套件宣告了 en-US，Partner Center 會列出 English (United States)。以下內容可直接貼上；Additional license terms 用 3.7 的同一段（已含英文）。

注意：英文版的編碼說明與中文版不同——程式遇到非 UTF-8 的舊檔案時使用 Windows 系統字碼頁，在繁中 Windows 是 Big5，在英文 Windows 是 Windows-1252，所以英文清單不寫 Big5。

**Product name**：qMDView

**Description**：

```
qMDView is a lightweight, fast Markdown viewer and editor for Windows. It does not use a browser engine, so even long documents open smoothly.

The user interface is available in English and Traditional Chinese. It follows your Windows display language and can be changed in View > Language.

Key features
• Live preview: headings, lists, task lists, tables (with alignment), code blocks, block quotes, images and horizontal rules, with a GitHub-style look that follows the Windows dark or light theme
• Edit with live preview: press Ctrl+E for a side-by-side editor with Markdown syntax highlighting and synchronized scrolling
• Table of contents: an outline built from the headings; click to jump, and the current section is highlighted as you scroll
• Folder tree: open a folder to list all its Markdown files and switch between them with one click
• File watching: reloads automatically when another program changes the file, and asks first if you have unsaved edits
• Links: heading anchors (including non-English headings), relative links to other .md files, and web links opened in your browser
• Encodings: detects UTF-8, UTF-8 with BOM and UTF-16, falls back to the Windows code page for legacy files, and keeps the original encoding and line endings (CRLF/LF) when saving
• More: find with all matches highlighted, zoom, recent files, drag and drop, and it remembers the window layout and the last opened file
• Appears in "Open with" for .md and .markdown files

Great for
Reading project READMEs and technical documents, writing notes, and viewing Markdown reports generated by other tools.

Privacy
qMDView only reads and writes the local files you open. No account, no server connection, no data collection.

Known limitations
• The preview does not support Mermaid diagrams or LaTeX math
• Web images are not downloaded; only local images are shown
• Code blocks in the preview are not syntax-highlighted
• Embedded HTML is only partially supported

Open-source components
This software uses Qt (GNU LGPL v3) and other open-source components. License information is available in the app under Help > About > Licenses.
```

**Product features**：

```
GitHub-style Markdown preview with tables, task lists, code blocks and images
Side-by-side editor and live preview with synchronized scrolling
Table of contents built from headings; click to jump
Folder tree for quick switching between documents
Automatic reload when another program changes the file
Heading anchors and relative links between Markdown files
Keeps the original encoding and line endings when saving
Find with all matches highlighted
Follows the Windows dark or light theme
English and Traditional Chinese user interface
No account, no network connection, no data collection
```

**Screenshots**：同 3.7 的建議畫面，但用英文介面與 `samples\sample-en.md` 拍攝（見 2.5）。

**Store logos**：與繁中清單相同的 300×300 圖示。

**Supplemental fields**：

| 欄位 | 內容 |
|---|---|
| Short description | A lightweight, fast Markdown viewer and editor: GitHub-style preview, side-by-side editing with live preview, table of contents, folder tree, and automatic reload when files change. |
| Search terms | Markdown, Markdown viewer, Markdown editor, MD reader, README, notes, Markdown preview |
| Copyright and trademark info | © 2026 SAISAI.Dev. All rights reserved. |
| Additional license terms | 與 3.7 相同（中英對照那一段） |
| Developed by | SAISAI.Dev |

### 3.8 Submission options（提交選項）

**Publishing hold options**：第一次上架建議選「通過認證後不要自動發布，等我按 Publish now」，可以先確認商店頁面再上線。

**Notes for certification**（給審核人員看，用英文）：

```
qMDView is a Markdown viewer/editor built with Qt 6 (C++) and packaged as a full-trust Win32 MSIX app.
No account, sign-in, network connection or test data is required.
The user interface is available in English and Traditional Chinese. It follows the Windows display language and can be changed in View > Language (applied after a restart).

How to test:
1. Launch qMDView from Start.
2. Press Ctrl+O and open any .md file: a formatted preview is shown. The left panels show the table of contents.
3. Press Ctrl+Shift+O and choose a folder: all Markdown files in it are listed in the file tree.
4. Press Ctrl+E to toggle the editor with live preview; Ctrl+S saves.
5. Press Ctrl+F to search; Ctrl+Plus / Ctrl+Minus to zoom.
6. .md and .markdown files can also be opened via File Explorer > Open with > qMDView.
7. The last menu (Help) > About shows version, publisher and third-party license information (Qt, LGPL v3).
```

**Restricted capabilities**（宣告了 `runFullTrust`，此欄必填）：

```
runFullTrust is required because qMDView is a classic Win32 desktop application (Qt 6, C++) that runs with EntryPoint Windows.FullTrustApplication; it cannot run without this capability.
The app only reads and writes the Markdown files that the user explicitly opens or saves, and stores its own window layout and recent-file list in the user's settings. It does not install drivers or services, does not run background tasks, and does not collect or transmit personal data.
```

**Submission notification audience**：保留預設。

### 3.9 送出

所有區塊顯示 Complete 後，按 **Submit for certification**（提交認證）。認證通常需要數個工作天，第一次可能較久；結果會寄到帳戶信箱。若選了手動發布，通過後回到提交頁按 **Publish now**。

認證失敗時，報告會列出違反的政策編號與說明，修正後重新提交即可。

---

## 附錄 A：隱私權政策範本

放在任何可公開存取的 HTTPS 網址，例如 GitHub Pages、Google 協作平台、Notion 公開頁面。網址需長期有效。

```
qMDView 隱私權政策
最後更新：2026 年 10 月 20 日

qMDView（以下稱「本軟體」）由 SAISAI.Dev 開發。

1. 我們蒐集的資料
本軟體不蒐集、不儲存於我方伺服器、也不傳送任何個人資料或使用資料。本軟體沒有帳號功能、不含廣告、不含分析或追蹤程式，也不會主動連線到網際網路。

2. 本機資料
本軟體只讀取與寫入你自行開啟或儲存的檔案。為了方便使用，本軟體會在你的電腦上記錄視窗配置、最近開啟的檔案清單與上次開啟的資料夾；這些資料只存在你的電腦中，解除安裝本軟體時會一併移除。

3. 外部連結
當你點選文件中的網址時，本軟體會交由系統預設瀏覽器開啟，之後的資料處理適用該網站的隱私權政策。

4. 第三方
本軟體使用的開放原始碼元件（Qt 等）不會傳送任何資料。透過 Microsoft Store 購買時，交易由 Microsoft 處理，適用 Microsoft 隱私權聲明。

5. 政策變更
若本政策有所變更，將更新本頁面並修改上方日期。

6. 聯絡方式
aisais.dev@gmail.com


qMDView Privacy Policy
Last updated: October 20, 2026

qMDView ("the app") is developed by SAISAI.Dev.

1. Data we collect
The app does not collect, store on our servers, or transmit any personal or usage data. It has no accounts, no ads, no analytics or tracking, and does not connect to the internet on its own.

2. Data on your device
The app only reads and writes files that you open or save. For convenience it keeps the window layout, the recent-files list and the last opened folder on your computer only. This data is removed when you uninstall the app.

3. External links
When you click a web link in a document, it is opened in your default browser; the privacy policy of that website applies.

4. Third parties
The open-source components used by the app (such as Qt) do not transmit any data. Purchases through the Microsoft Store are processed by Microsoft under the Microsoft Privacy Statement.

5. Changes
If this policy changes, this page and the date above will be updated.

6. Contact
aisais.dev@gmail.com
```

---

## 附錄 B：參考資料

- 選擇發布方式（MSIX 與 EXE／MSI 的差異）：<https://learn.microsoft.com/windows/apps/package-and-deploy/choose-distribution-path>
- MSIX App 提交欄位一覽：<https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/create-app-submission>
- 商店清單文字欄位限制：<https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/add-and-edit-store-listing-info>
- 截圖與圖示規格：<https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/screenshots-and-images>
- 類別與子類別：<https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/categories-and-subcategories>
- 產品聲明：<https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/product-declarations>
- Microsoft Store Policies（10.1.1 清單描述、10.5.1 隱私權政策）：<https://learn.microsoft.com/windows/uwp/publish/store-policies>
- 套件化桌面程式與檔案總管整合（資料夾右鍵選單需 IExplorerCommand）：<https://learn.microsoft.com/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer>
- Microsoft Store 標準應用程式授權條款：<https://www.microsoft.com/store/standard-application-license-terms>
