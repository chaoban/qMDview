#include "markdownstyler.h"

#include <QFont>
#include <QHash>
#include <QPalette>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QVector>

namespace {

// 標記「這個表格其實是引用框」，避免被當成一般表格套用框線
constexpr int kQuoteBoxProperty = QTextFormat::UserProperty + 1;

struct Colors
{
    QColor codeBackground;
    QColor codeBorder;
    QColor inlineCodeBackground;
    QColor quoteText;
    QColor quoteBar;
    QColor tableBorder;
    QColor tableHeaderBackground;
};

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF()   + (b.redF()   - a.redF())   * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF()  + (b.blueF()  - a.blueF())  * t));
}

// 顏色以目前調色盤的底色／文字色混合而成，深色與淺色主題都能自動適應
Colors colorsFor(const QPalette &pal)
{
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const bool dark = base.lightness() < 128;

    Colors c;
    c.codeBackground        = mix(base, text, dark ? 0.07 : 0.035);
    c.codeBorder            = mix(base, text, dark ? 0.20 : 0.16);
    c.inlineCodeBackground  = mix(base, text, dark ? 0.14 : 0.08);
    c.quoteText             = mix(text, base, 0.38);
    c.quoteBar              = mix(base, text, dark ? 0.30 : 0.22);
    c.tableBorder           = c.codeBorder;
    c.tableHeaderBackground = c.codeBackground;
    return c;
}

bool isCodeBlock(const QTextBlockFormat &f)
{
    return f.hasProperty(QTextFormat::BlockCodeLanguage)
        || f.hasProperty(QTextFormat::BlockCodeFence);
}

QTextTable *tableOf(const QTextBlock &block)
{
    return QTextCursor(block).currentTable();
}

bool isQuoteBox(const QTextTable *t)
{
    return t && t->format().hasProperty(kQuoteBoxProperty);
}

bool hasImage(const QTextBlock &block)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        if (it.fragment().isValid() && it.fragment().charFormat().isImageFormat())
            return true;
    }
    return false;
}

struct Range { int start; int end; };

// 找出連續、且符合條件的區塊範圍（同一個 frame 內）
template <typename Pred>
QVector<Range> collectRanges(QTextDocument *doc, Pred pred)
{
    QVector<Range> ranges;
    QTextBlock b = doc->begin();
    while (b.isValid()) {
        if (!pred(b) || tableOf(b)) {
            b = b.next();
            continue;
        }
        QTextFrame *frame = QTextCursor(b).currentFrame();
        QTextBlock last = b;
        QTextBlock n = b.next();
        while (n.isValid() && pred(n) && QTextCursor(n).currentFrame() == frame) {
            last = n;
            n = n.next();
        }
        ranges.append({ b.position(), last.position() + last.length() - 1 });
        b = n;
    }
    return ranges;
}

// 1) 引用：搬進只有左框線的 1x1 表格，做出 GitHub 式的左側色條
void wrapQuotes(QTextDocument *doc, const Colors &c)
{
    const QVector<Range> ranges = collectRanges(doc, [](const QTextBlock &b) {
        return b.blockFormat().property(QTextFormat::BlockQuoteLevel).toInt() > 0;
    });

    QTextTableFormat tf;
    tf.setProperty(kQuoteBoxProperty, true);
    tf.setBorder(0);
    tf.setBorderCollapse(true);
    tf.setCellSpacing(0);
    tf.setCellPadding(0);
    tf.setTopMargin(2);
    tf.setBottomMargin(10);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));

    QTextTableCellFormat cf;
    cf.setLeftBorder(4);
    cf.setLeftBorderBrush(c.quoteBar);
    cf.setLeftBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    cf.setTopBorder(0);
    cf.setRightBorder(0);
    cf.setBottomBorder(0);
    cf.setLeftPadding(14);
    cf.setTopPadding(2);
    cf.setBottomPadding(2);

    QTextCursor cur(doc);
    for (int i = int(ranges.size()) - 1; i >= 0; --i) {
        cur.setPosition(ranges[i].start);
        cur.setPosition(ranges[i].end, QTextCursor::KeepAnchor);
        const QTextDocumentFragment content = cur.selection();
        cur.removeSelectedText();

        QTextTable *table = cur.insertTable(1, 1, tf);
        QTextTableCell cell = table->cellAt(0, 0);
        cell.setFormat(cf);

        QTextCursor in = cell.firstCursorPosition();
        in.insertFragment(content);

        // 引用框本身已提供縮排，去掉原本的左邊界；文字改為次要色
        for (QTextBlock b = cell.firstCursorPosition().block();
             b.isValid() && b.position() <= cell.lastCursorPosition().position(); b = b.next()) {
            QTextCursor bc(b);
            QTextBlockFormat bf = b.blockFormat();
            if (!b.textList())
                bf.setLeftMargin(0);
            bf.setIndent(0);
            bf.setProperty(QTextFormat::BlockQuoteLevel, 1);
            bc.setBlockFormat(bf);
        }
    }
}

// 2) 程式碼區塊：包進有底色、框線、內距的 QTextFrame
void wrapCodeBlocks(QTextDocument *doc, const Colors &c)
{
    const QVector<Range> ranges = collectRanges(doc, [](const QTextBlock &b) {
        return isCodeBlock(b.blockFormat());
    });

    QTextFrameFormat ff;
    ff.setBackground(c.codeBackground);
    ff.setBorder(1);
    ff.setBorderBrush(c.codeBorder);
    ff.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    ff.setPadding(10);
    ff.setTopMargin(4);
    ff.setBottomMargin(10);
    ff.setWidth(QTextLength(QTextLength::PercentageLength, 100));

    QTextCursor cur(doc);
    // 由後往前插入，前面的位置才不會位移
    for (int i = int(ranges.size()) - 1; i >= 0; --i) {
        const QTextBlockFormat firstFormat = doc->findBlock(ranges[i].start).blockFormat();
        cur.setPosition(ranges[i].start);
        cur.setPosition(ranges[i].end, QTextCursor::KeepAnchor);
        QTextFrame *frame = cur.insertFrame(ff);
        // insertFrame 會讓框內第一個區塊遺失原本的區塊格式，補回去
        if (frame) {
            QTextCursor first = frame->firstCursorPosition();
            first.setBlockFormat(firstFormat);
        }
    }
}

// 依 frame 樹狀結構建立「區塊編號 → 所屬 frame」對照表。
// 不能用 QTextCursor(block).currentFrame()：緊接在 frame 前的空區塊，
// 其位置正好是 frame 起始字元，會被誤判成屬於後面那個 frame。
void mapBlockFrames(QTextFrame *frame, QHash<int, QTextFrame *> &map)
{
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame())
            mapBlockFrames(child, map);
        else if (it.currentBlock().isValid())
            map.insert(it.currentBlock().blockNumber(), frame);
    }
}

// 3) 插入 frame / table 時 Qt 會在前後留下空白區塊，把它們壓到幾乎沒有高度
void collapseFrameSpacers(QTextDocument *doc)
{
    QHash<int, QTextFrame *> frames;
    mapBlockFrames(doc->rootFrame(), frames);

    QTextCursor cur(doc);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.length() != 1)
            continue;
        const QTextBlockFormat bf = b.blockFormat();
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            continue; // 水平分隔線本身就是空區塊
        QTextFrame *frame = frames.value(b.blockNumber());
        if (qobject_cast<QTextTable *>(frame))
            continue; // 空的表格儲存格不處理
        const bool nextDiffers = b.next().isValid() && frames.value(b.next().blockNumber()) != frame;
        const bool prevDiffers = b.previous().isValid() && frames.value(b.previous().blockNumber()) != frame;
        if (!nextDiffers && !prevDiffers)
            continue;

        QTextBlockFormat spacer;
        spacer.setLineHeight(1, QTextBlockFormat::FixedHeight);
        spacer.setTopMargin(0);
        spacer.setBottomMargin(0);
        cur.setPosition(b.position());
        cur.setBlockFormat(spacer);
    }
}

// 4) 逐區塊調整段落與字元格式
void styleBlocks(QTextDocument *doc, const Colors &c, const QFont &codeFont)
{
    struct Span { int pos; int len; QTextCharFormat fmt; };

    QTextCursor cur(doc);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (bf.lineHeightType() == QTextBlockFormat::FixedHeight)
            continue; // collapseFrameSpacers 處理過的空白區塊

        const bool code    = isCodeBlock(bf);
        const bool quote   = bf.property(QTextFormat::BlockQuoteLevel).toInt() > 0;
        const int  heading = bf.headingLevel();
        const QTextTable *table = tableOf(b);
        const bool inDataTable = table && !isQuoteBox(table);

        if (code) {
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
            bf.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
            // 長行自動換行；否則整份文件會出現水平捲軸（預覽窗格常只有半個螢幕寬）
            bf.setNonBreakableLines(false);
        } else if (heading > 0) {
            bf.setTopMargin(heading <= 2 ? 20 : 14);
            bf.setBottomMargin(heading <= 2 ? 12 : 6);
            if (heading <= 2) {
                // H1/H2 底線（Qt 會在區塊底部畫一條水平線）
                bf.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth,
                               QTextLength(QTextLength::PercentageLength, 100));
            }
        } else if (!inDataTable && !hasImage(b)) {
            // 含圖片的段落不能放大行高，否則圖片下方會多出一大段空白
            if (b.textList()) {
                bf.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
                bf.setTopMargin(2);
                bf.setBottomMargin(2);
            } else {
                bf.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
            }
        }
        cur.setPosition(b.position());
        cur.setBlockFormat(bf);

        // 先收集再修改，避免修改格式時片段分裂導致迭代器失效
        QVector<Span> spans;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid())
                spans.append({ frag.position(), frag.length(), frag.charFormat() });
        }

        for (const Span &s : spans) {
            if (s.fmt.isImageFormat())
                continue;
            QTextCharFormat f = s.fmt;
            bool changed = false;

            const bool mono = code || f.fontFixedPitch();
            if (mono) {
                // 匯入器把程式碼字級寫死，清掉後才會跟著縮放
                f.clearProperty(QTextFormat::FontPointSize);
                f.clearProperty(QTextFormat::FontPixelSize);
                f.setFontFamilies(QStringList{ codeFont.family() });
                f.setFontFixedPitch(true);
                f.setFontStyleHint(QFont::TypeWriter);
                if (!code)
                    f.setBackground(c.inlineCodeBackground);
                changed = true;
            }
            if (quote && !f.isAnchor()) {
                f.setForeground(c.quoteText);
                changed = true;
            }
            if (changed) {
                cur.setPosition(s.pos);
                cur.setPosition(s.pos + s.len, QTextCursor::KeepAnchor);
                cur.setCharFormat(f);
            }
        }
    }
}

// 5) 表格：框線、內距、表頭底色與粗體
void styleTables(QTextFrame *frame, const Colors &c)
{
    const auto children = frame->childFrames();
    for (QTextFrame *child : children) {
        auto *table = qobject_cast<QTextTable *>(child);
        if (table && !isQuoteBox(table)) {
            QTextTableFormat tf = table->format();
            tf.setBorder(1);
            tf.setBorderBrush(c.tableBorder);
            tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tf.setBorderCollapse(true);
            tf.setCellSpacing(0);
            tf.setCellPadding(6);
            tf.setTopMargin(4);
            tf.setBottomMargin(10);
            table->setFormat(tf);

            if (table->rows() > 0) {
                QTextCharFormat bold;
                bold.setFontWeight(QFont::Bold);
                for (int col = 0; col < table->columns(); ++col) {
                    QTextTableCell cell = table->cellAt(0, col);
                    QTextTableCellFormat cf = cell.format().toTableCellFormat();
                    cf.setBackground(c.tableHeaderBackground);
                    cell.setFormat(cf);

                    QTextCursor cc = cell.firstCursorPosition();
                    cc.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                    cc.mergeCharFormat(bold);

                    // 表頭沿用該欄資料列的對齊方式（:---: / ---:）
                    if (table->rows() > 1) {
                        const Qt::Alignment align =
                            table->cellAt(1, col).firstCursorPosition().blockFormat().alignment();
                        QTextBlockFormat hb = cc.blockFormat();
                        hb.setAlignment(align);
                        cc.mergeBlockFormat(hb);
                    }
                }
            }
        }
        styleTables(child, c);
    }
}

} // namespace

void MarkdownStyler::apply(QTextDocument *doc, const QPalette &palette, const QFont &codeFont)
{
    const Colors c = colorsFor(palette);

    QTextCursor batch(doc);
    batch.beginEditBlock();   // 合併成一次版面重排
    wrapQuotes(doc, c);
    wrapCodeBlocks(doc, c);
    collapseFrameSpacers(doc);
    styleBlocks(doc, c, codeFont);
    styleTables(doc->rootFrame(), c);
    batch.endEditBlock();
}
