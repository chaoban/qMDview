#pragma once

class QFont;
class QPalette;
class QTextDocument;

// QTextDocument::setMarkdown() 產生的文件只有最基本的格式。
// 這裡在匯入後逐一調整區塊／字元格式，讓呈現接近 GitHub 風格：
//   - 程式碼區塊包進有底色、框線、內距的 QTextFrame
//   - 行內程式碼加底色
//   - 表格加框線、儲存格內距、表頭底色
//   - 引用文字改為次要色
//   - 段落行高 150%（中文較易閱讀）
//   - H1/H2 下方加分隔線
//
// 想調整外觀時，從 markdownstyler.cpp 的 colorsFor() 與 apply() 改起。
namespace MarkdownStyler {

void apply(QTextDocument *doc, const QPalette &palette, const QFont &codeFont);

} // namespace MarkdownStyler
