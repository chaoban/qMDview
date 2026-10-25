cmake_minimum_required(VERSION 3.16)

project(qMDView VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自動處理 moc / uic / rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# .ui 檔與 .cpp 放在同一個 src 資料夾
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 發行者資訊：顯示在「關於」對話框、exe 檔案內容（版本資訊）與安裝程式
set(APP_PUBLISHER "SAISAI.Dev")
set(APP_COPYRIGHT_YEAR "2026")
set(APP_CONTACT_EMAIL "aisais.dev@gmail.com")
set(APP_COPYRIGHT "Copyright (C) ${APP_COPYRIGHT_YEAR} ${APP_PUBLISHER}")

# QTextDocument::setMarkdown 需要 Qt 5.14+；本專案使用 Qt 6 API（QStringDecoder 等）
# LinguistTools：介面翻譯工具（lupdate / lrelease），Qt 安裝時已附帶
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets LinguistTools)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mainwindow.ui
    src/markdownview.h
    src/markdownview.cpp
    src/markdownstyler.h
    src/markdownstyler.cpp
    src/markdownhighlighter.h
    src/markdownhighlighter.cpp
    src/tocwidget.h
    src/tocwidget.cpp
    src/findbar.h
    src/findbar.cpp
    src/aboutdialog.h
    src/aboutdialog.cpp
    src/textfileio.h
    src/textfileio.cpp
    src/uilanguage.h
    src/uilanguage.cpp
    resources/resources.qrc
)

if(WIN32)
    # 圖示＋版本資訊（檔案總管 → 內容 → 詳細資料）；版本號與發行者由上方變數帶入
    set(APP_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND PROJECT_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

qt_add_executable(qMDView ${PROJECT_SOURCES})

target_include_directories(qMDView PRIVATE src)

# 介面多國語系：原始字串為繁體中文，其他語言放在 translations/qMDView_<語言>.ts。
# 建置時 lrelease 把 .ts 編成 .qm 並嵌入 exe 的 :/i18n/（不需另外部署）。
# 新增或修改介面字串後：建置 update_translations 目標更新 .ts，再用 Qt Linguist 補翻譯。
# 新增語言：在 TS_FILES 加一個 .ts，並在 src/uilanguage.cpp 的 available() 加上語言代碼。
qt_add_translations(qMDView
    TS_FILES translations/qMDView_en.ts
    RESOURCE_PREFIX "/i18n"
)
target_link_libraries(qMDView PRIVATE Qt6::Widgets)

target_compile_definitions(qMDView PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    APP_PUBLISHER="${APP_PUBLISHER}"
    APP_COPYRIGHT_YEAR="${APP_COPYRIGHT_YEAR}"
    APP_CONTACT_EMAIL="${APP_CONTACT_EMAIL}"
)

# 原始碼含中文字串：MSVC 預設以系統字碼頁（CP950）解讀，必須加 /utf-8
if(MSVC)
    target_compile_options(qMDView PRIVATE /utf-8 /W3)
else()
    target_compile_options(qMDView PRIVATE -Wall -Wextra)
endif()

set_target_properties(qMDView PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

include(GNUInstallDirs)
install(TARGETS qMDView
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
