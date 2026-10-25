#pragma once

#include <QString>
#include <QStringList>

// 介面語言
// - 原始字串為繁體中文（zh_TW），其他語言由 :/i18n/qMDView_<代碼>.qm 提供
// - 使用者設定存在 QSettings 的 ui/language；空字串 = 跟隨系統
// - 系統語系沒有對應翻譯時使用英文
namespace UiLanguage {

QStringList available();                     // 支援的語言代碼，例如 { "zh_TW", "en" }
QString nativeName(const QString &code);     // 以該語言本身的文字顯示的名稱（不翻譯）

QString preference();                        // 使用者設定；空字串 = 跟隨系統
void setPreference(const QString &code);

QString resolve(const QString &preference);  // 依設定與系統語系決定實際使用的語言
void install(const QString &code);           // 載入翻譯；須在建立任何視窗之前呼叫
QString current();                           // 目前生效的語言

} // namespace UiLanguage
