#include "textfileio.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>

QString TextFormat::displayName() const
{
    QString name;
    switch (encoding) {
    case QStringConverter::Utf8:    name = QStringLiteral("UTF-8"); break;
    case QStringConverter::Utf16LE: name = QStringLiteral("UTF-16 LE"); break;
    case QStringConverter::Utf16BE: name = QStringLiteral("UTF-16 BE"); break;
    case QStringConverter::System:  name = QCoreApplication::translate("TextFileIO", "系統編碼"); break;
    default:                        name = QStringLiteral("?"); break;
    }
    if (bom)
        name += QStringLiteral(" BOM");
    name += crlf ? QStringLiteral(" · CRLF") : QStringLiteral(" · LF");
    return name;
}

TextFileContent TextFileIO::read(const QString &path)
{
    TextFileContent result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QByteArray data = file.readAll();

    QString text;
    if (data.startsWith("\xEF\xBB\xBF")) {
        result.format.encoding = QStringConverter::Utf8;
        result.format.bom = true;
        text = QString::fromUtf8(data.constData() + 3, data.size() - 3);
    } else if (data.startsWith("\xFF\xFE")) {
        result.format.encoding = QStringConverter::Utf16LE;
        result.format.bom = true;
        QStringDecoder dec(QStringConverter::Utf16LE);
        text = dec(data.mid(2));
    } else if (data.startsWith("\xFE\xFF")) {
        result.format.encoding = QStringConverter::Utf16BE;
        result.format.bom = true;
        QStringDecoder dec(QStringConverter::Utf16BE);
        text = dec(data.mid(2));
    } else {
        QStringDecoder utf8(QStringConverter::Utf8);
        text = utf8(data);
        if (utf8.hasError()) {
            // 不是合法 UTF-8：多半是舊的 Big5 文件
            QStringDecoder sys(QStringConverter::System);
            text = sys(data);
            result.format.encoding = QStringConverter::System;
        } else {
            result.format.encoding = QStringConverter::Utf8;
        }
    }

    result.format.crlf = text.contains(QLatin1String("\r\n"));
    if (result.format.crlf)
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    result.text = text;
    result.ok = true;
    return result;
}

bool TextFileIO::write(const QString &path, const QString &text, const TextFormat &format,
                       QString *error, bool *unencodable)
{
    if (unencodable)
        *unencodable = false;

    QString out = text;
    if (format.crlf)
        out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    QStringEncoder::Flags flags = QStringConverter::Flag::Default;
    if (format.bom)
        flags |= QStringConverter::Flag::WriteBom;
    QStringEncoder encoder(format.encoding, flags);
    const QByteArray bytes = encoder(out);
    if (encoder.hasError()) {
        if (error)
            *error = QCoreApplication::translate("TextFileIO", "部分字元無法以 %1 編碼儲存。")
                         .arg(format.displayName());
        if (unencodable)
            *unencodable = true;
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
