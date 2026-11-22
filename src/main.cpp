#include "mainwindow.h"
#include "singleinstance.h"
#include "uilanguage.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qMDView"));
    QApplication::setOrganizationName(QStringLiteral(APP_PUBLISHER));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // 介面語言：「檢視→語言」的設定，未設定時跟隨系統語系（沒有對應翻譯就用英文）。
    // 同時載入 Qt 內建字串的翻譯（對話框的「是／否／取消」等按鈕）
    UiLanguage::install(UiLanguage::resolve(UiLanguage::preference()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Markdown 檢視／編輯器"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption newWindowOption(
        QStringLiteral("new-window"),
        QCoreApplication::translate("main", "開新視窗，不交給已在執行的 qMDView"));
    parser.addOption(newWindowOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QCoreApplication::translate("main", "要開啟的 Markdown 檔案或資料夾"),
                                 QStringLiteral("[path...]"));
    parser.process(app);

    // 先轉成絕對路徑：轉交給另一個程式時，對方的工作目錄不一樣
    QStringList paths;
    const QStringList args = parser.positionalArguments();
    for (const QString &arg : args)
        paths.append(QFileInfo(arg).absoluteFilePath());

    // 只執行一個程式：已經有 qMDView 在執行，就把檔案交給它（開成新分頁）後結束。
    // 「每個檔案開新視窗」或 --new-window（分頁右鍵「在新視窗開啟」）時照常開新視窗
    const bool newWindow = parser.isSet(newWindowOption);
    SingleInstance *instance = nullptr;
    if (!newWindow && !MainWindow::opensInNewWindow()) {
        instance = new SingleInstance;
        const SingleInstance::Result result = instance->start(paths);
        if (result == SingleInstance::Result::Forwarded) {
            delete instance;
            return 0;
        }
        if (result != SingleInstance::Result::Primary) {
            delete instance;
            instance = nullptr;
        }
    }

    MainWindow window;
    window.setSingleInstance(instance);   // 交給主視窗管理（含刪除）
    window.show();

    // 有指定檔案（例如從檔案總管「開啟檔案」）就開它們，否則回到上次的資料夾與分頁。
    // --new-window 沒有指定檔案時開空白視窗，不重複開啟上次的分頁
    if (!paths.isEmpty())
        window.openPaths(paths);
    else if (!newWindow)
        window.restoreLastSession();

    return app.exec();
}
