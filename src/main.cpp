#include "mainwindow.h"
#include "uilanguage.h"

#include <QApplication>
#include <QCommandLineParser>
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
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QCoreApplication::translate("main", "要開啟的 Markdown 檔案或資料夾"),
                                 QStringLiteral("[path]"));
    parser.process(app);

    MainWindow window;
    window.show();

    // 有指定檔案（例如從檔案總管「開啟檔案」）就開它，否則回到上次的資料夾與檔案
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        window.openPath(args.first());
    else
        window.restoreLastSession();

    return app.exec();
}
