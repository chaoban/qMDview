#pragma once

#include <QString>
#include <QStringConverter>

// 記錄檔案原本的編碼格式，存檔時沿用，避免改掉使用者檔案的編碼 / 換行
struct TextFormat
{
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    bool bom = false;
    bool crlf = false;

    QString displayName() const;
};

struct TextFileContent
{
    bool ok = false;
    QString text;       // 換行已統一為 \n
    TextFormat format;
    QString error;
};

namespace TextFileIO {

// 讀取文字檔：依 BOM 判斷 UTF-8 / UTF-16；無 BOM 時先試 UTF-8，
// 解碼失敗則改用系統編碼（繁中 Windows 為 CP950 / Big5）
TextFileContent read(const QString &path);

// 以指定格式寫入（使用 QSaveFile，寫入失敗不會破壞原檔）
// 回傳 false 時 error 會說明原因；若字元無法以原編碼表示，unencodable 會設為 true
bool write(const QString &path, const QString &text, const TextFormat &format,
           QString *error = nullptr, bool *unencodable = nullptr);

} // namespace TextFileIO
