#include "uilanguage.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

const QLatin1String kSettingsKey("ui/language");
const QLatin1String kSourceLanguage("zh_TW");  // 程式碼中的原始字串
const QLatin1String kFallbackLanguage("en");   // 系統語系沒有對應翻譯時使用
const QLatin1String kQtSourceLanguage("en");   // Qt 內建字串的原始語言

QString g_current;

} // namespace

namespace UiLanguage {

QStringList available()
{
    return { QStringLiteral("zh_TW"), QStringLiteral("en") };
}

QString nativeName(const QString &code)
{
    if (code == QLatin1String("zh_TW"))
        return QStringLiteral("繁體中文");
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    return code;
}

QString preference()
{
    const QString value = QSettings().value(kSettingsKey).toString();
    return available().contains(value) ? value : QString();
}

void setPreference(const QString &code)
{
    QSettings s;
    if (code.isEmpty())
        s.remove(kSettingsKey);
    else
        s.setValue(kSettingsKey, code);
}

QString resolve(const QString &pref)
{
    if (available().contains(pref))
        return pref;

    // 依 Windows「慣用語言」清單的順序，找第一個有支援的語言：
    // 先比對語言＋書寫系統（日後加入簡體中文時可區分），再只比對語言
    // （例如簡體中文系統沒有 zh_CN 翻譯時使用繁體中文，而不是英文）
    const QStringList codes = available();
    for (const QString &tag : QLocale::system().uiLanguages()) {
        const QLocale wanted(tag);
        for (const QString &code : codes) {
            const QLocale candidate(code);
            if (candidate.language() == wanted.language() && candidate.script() == wanted.script())
                return code;
        }
        for (const QString &code : codes) {
            if (QLocale(code).language() == wanted.language())
                return code;
        }
    }
    return kFallbackLanguage;
}

void install(const QString &code)
{
    g_current = code;
    QCoreApplication *app = QCoreApplication::instance();

    // 本程式的字串；繁體中文就是原始字串，不需載入
    if (code != kSourceLanguage) {
        auto *translator = new QTranslator(app);
        if (translator->load(QStringLiteral(":/i18n/qMDView_%1.qm").arg(code)))
            QCoreApplication::installTranslator(translator);
        else
            delete translator;
    }

    // Qt 內建字串（對話框的「是／否／取消」、編輯器右鍵選單等）；英文是 Qt 的原始語言
    if (code != kQtSourceLanguage) {
        auto *qtTranslator = new QTranslator(app);
        const QLocale locale(code);
        if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                               QLibraryInfo::path(QLibraryInfo::TranslationsPath))
            || qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                                  QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
            QCoreApplication::installTranslator(qtTranslator);
        else
            delete qtTranslator;
    }
}

QString current()
{
    return g_current;
}

} // namespace UiLanguage
