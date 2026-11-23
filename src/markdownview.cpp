#include "markdownview.h"
#include "markdownstyler.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QStyle>
#include <QWheelEvent>

namespace {

// YAML front matter（檔案開頭 --- 到 --- 之間）不顯示；回傳其長度，沒有則為 0
int frontMatterLength(const QString &md)
{
    static const QRegularExpression start(QStringLiteral("\\A---[ \\t]*\\n[A-Za-z0-9_-]+[ \\t]*:"));
    if (!start.match(md).hasMatch())
        return 0;
    static const QRegularExpression end(QStringLiteral("\\n(---|\\.\\.\\.)[ \\t]*(\\n|\\z)"));
    const QRegularExpressionMatch m = end.match(md, 3);
    return m.hasMatch() ? int(m.capturedEnd()) : 0;
}

// ── HTML 空元素補斜線 ────────────────────────────────────────────────
// Qt 的 Markdown 匯入器（QTextMarkdownImporter）遇到行內 HTML 時，以「開啟標籤數 − 關閉標籤數」
// 判斷 HTML 是否結束，只有寫成 <br/> 才算自我關閉。HTML 空元素 <br>、<hr>、<img ...> 不寫斜線時
// 會被當成「開了沒關」，計數永遠不歸零，之後所有一般文字都被收進 HTML 暫存而不顯示
// （表格儲存格全空、後面的段落與標題消失）。解析前把這些標籤補成 <br /> 形式；
// 屬性值內的 < > 改成 &lt; &gt;，否則 Qt 會把引號內的 > 當成標籤結尾。
// 程式碼區塊（圍欄、縮排）與行內程式碼要原樣顯示，不處理；\<br> 是跳脫的字面文字，也不處理。

QString closeVoidTagsInText(const QString &text)
{
    static const QRegularExpression voidTag(
        QStringLiteral("(?<!\\\\)<(area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr)"
                       "((?:\\s+[A-Za-z_:][-A-Za-z0-9_.:]*"
                       "(?:\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s\"'=<>`]+))?)*)\\s*>"),
        QRegularExpression::CaseInsensitiveOption);

    if (!text.contains(QLatin1Char('<')))
        return text;

    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = voidTag.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString attrs = m.captured(2);
        attrs.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        attrs.replace(QLatin1Char('>'), QLatin1String("&gt;"));
        out += QStringView(text).mid(last, m.capturedStart() - last);
        out += QLatin1Char('<');
        out += m.captured(1);
        out += attrs;
        out += QLatin1String(" />");
        last = m.capturedEnd();
    }
    if (last == 0) // 沒有需要修改的標籤
        return text;
    out += QStringView(text).mid(last);
    return out;
}

// 處理一行：行內程式碼（成對、長度相同的反引號之間）保持原樣，其餘部分補斜線
QString closeVoidTagsInLine(const QString &row)
{
    const QChar tick(QLatin1Char('`'));
    const qsizetype n = row.size();
    QString out;
    qsizetype seg = 0;
    qsizetype pos = 0;
    while (pos < n) {
        if (row.at(pos) != tick) {
            ++pos;
            continue;
        }
        qsizetype runEnd = pos;
        while (runEnd < n && row.at(runEnd) == tick)
            ++runEnd;
        const qsizetype len = runEnd - pos;

        qsizetype close = -1;
        for (qsizetype s = row.indexOf(tick, runEnd); s >= 0;) {
            qsizetype e = s;
            while (e < n && row.at(e) == tick)
                ++e;
            if (e - s == len) {
                close = s;
                break;
            }
            s = row.indexOf(tick, e);
        }
        if (close < 0) { // 沒有配對的反引號是一般文字
            pos = runEnd;
            continue;
        }
        out += closeVoidTagsInText(row.mid(seg, pos - seg));
        out += QStringView(row).mid(pos, close + len - pos);
        seg = pos = close + len;
    }
    out += closeVoidTagsInText(row.mid(seg));
    return out;
}

// 行首縮排寬度（Tab 對齊到 4 的倍數）
int indentWidth(const QString &row)
{
    int w = 0;
    for (const QChar ch : row) {
        if (ch == QLatin1Char(' '))
            ++w;
        else if (ch == QLatin1Char('\t'))
            w = (w / 4 + 1) * 4;
        else
            break;
    }
    return w;
}

QString closeVoidHtmlTags(const QString &md)
{
    if (!md.contains(QLatin1Char('<')))
        return md;

    static const QRegularExpression fence(QStringLiteral("^[ \\t]*(`{3,}|~{3,})"));
    static const QRegularExpression listItem(QStringLiteral("^ {0,3}([-*+]|\\d{1,9}[.)])([ \\t]|$)"));

    QStringList rows = md.split(QLatin1Char('\n'));
    QString openFence;           // 目前所在圍欄程式碼區塊的開頭符號（空字串＝不在區塊內）
    bool prevBlank = true;       // 縮排程式碼區塊只能接在空行（或文件開頭）之後
    bool inIndentedCode = false;
    bool inList = false;         // 清單內縮排 4 格以上是續行，不是程式碼
    for (QString &row : rows) {
        const int indent = indentWidth(row);
        const QRegularExpressionMatch fm = fence.match(row);
        const bool isFence = fm.hasMatch() && (indent <= 3 || inList);

        if (!openFence.isEmpty()) { // 圍欄程式碼區塊內：只找結束圍欄
            if (isFence && fm.capturedView(1).front() == openFence.front()
                && fm.capturedLength(1) >= openFence.size())
                openFence.clear();
            continue;
        }
        if (row.trimmed().isEmpty()) {
            prevBlank = true;
            continue;
        }
        const bool wasBlank = prevBlank;
        prevBlank = false;

        const bool indented = indent >= 4;
        if (inIndentedCode && !indented)
            inIndentedCode = false;
        if (!inIndentedCode && indented && wasBlank && !inList)
            inIndentedCode = true;
        if (inIndentedCode)
            continue;

        if (isFence) {
            openFence = fm.captured(1);
            continue;
        }
        if (!indented)
            inList = listItem.match(row).hasMatch() || (inList && !wasBlank);
        if (row.contains(QLatin1Char('<')))
            row = closeVoidTagsInLine(row);
    }
    return rows.join(QLatin1Char('\n'));
}

bool isMarkdownSuffix(const QString &suffix)
{
    static const QStringList md = { QStringLiteral("md"), QStringLiteral("markdown"),
                                    QStringLiteral("mdown"), QStringLiteral("mkd"),
                                    QStringLiteral("mkdn") };
    return md.contains(suffix.toLower());
}

} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);          // 連結一律自己處理
    setOpenExternalLinks(false);
    setAcceptDrops(false);        // 拖放交給主視窗處理
    viewport()->setAcceptDrops(false);

    QFont f = font();
    m_basePointSize = qMax<qreal>(11.0, f.pointSizeF() + 2.0);
    f.setPointSizeF(m_basePointSize);
    setFont(f);

    connect(this, &QTextBrowser::anchorClicked, this, &MarkdownView::onAnchorClicked);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownView::updateCurrentHeading);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &MarkdownView::updateCurrentHeading);

    // 圖片是依視窗寬度預先縮好的，寬度改變後（停止拖曳 200ms）重新排版
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(200);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this] {
        if (!m_hasImages || m_markdown.isEmpty() || width() == m_renderedWidth)
            return;
        const double ratio = scrollRatio();
        rebuild();
        setScrollRatio(ratio);
        updateCurrentHeading();
    });
}

void MarkdownView::renderMarkdown(const QString &markdown, const QString &baseDir, bool keepScroll)
{
    const int oldValue = verticalScrollBar()->value();

    m_markdown = markdown;
    m_baseDir = baseDir;
    rebuild();

    document()->size(); // 強制完成版面配置，捲軸範圍才會正確
    verticalScrollBar()->setValue(keepScroll ? oldValue : 0);
    updateCurrentHeading();
}

void MarkdownView::rebuild()
{
    m_rebuilding = true;
    m_hasImages = false;
    m_renderedWidth = width();

    // 在「尚未掛到畫面上」的新文件裡解析與套用樣式，最後再一次換上。
    // 直接對顯示中的文件 setMarkdown() 會邊插入邊重排，大檔案慢 5~10 倍。
    // 文件的 parent 設為本元件，圖片載入時才會回呼本元件的 loadResource()。
    auto *doc = new QTextDocument(this);
    doc->setUndoRedoEnabled(false);
    doc->setDocumentMargin(24);
    doc->setDefaultFont(font());
    doc->setBaseUrl(m_baseDir.isEmpty()
                        ? QUrl()
                        : QUrl::fromLocalFile(QDir(m_baseDir).absolutePath() + QLatin1Char('/')));
    // closeVoidHtmlTags：<br> 等不寫斜線的 HTML 空元素會讓 Qt 吞掉之後所有文字，先補成 <br />
    doc->setMarkdown(closeVoidHtmlTags(m_markdown.mid(frontMatterLength(m_markdown))),
                     QTextDocument::MarkdownDialectGitHub);

    MarkdownStyler::apply(doc, palette(), QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // 舊文件若是 QTextEdit 內建的那份，setDocument() 會自行刪除；
    // 若是我們之前建立的（parent 為本元件），則不會被刪，要自己回收。
    // 必須在 setDocument() 之前判斷，之後內建那份已被刪除、指標失效。
    QTextDocument *old = document();
    const bool ownedByUs = old && old->parent() == this;
    setDocument(doc);
    if (ownedByUs)
        old->deleteLater();

    m_rebuilding = false;

    collectHeadings();
    m_currentHeading = -2; // 強制下一次 updateCurrentHeading() 發出訊號
    emit headingsChanged();
    emit rebuilt();
}

void MarkdownView::collectHeadings()
{
    m_headings.clear();
    QHash<QString, int> used;

    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const int level = b.blockFormat().headingLevel();
        if (level <= 0)
            continue;

        QString text = b.text();
        text.remove(QChar::ObjectReplacementCharacter); // 行內圖片
        text.replace(QChar::LineSeparator, QLatin1Char(' '));
        text = text.simplified();

        QString anchor = makeAnchor(text);
        const int n = used.value(anchor, 0);
        used.insert(anchor, n + 1);
        if (n > 0)
            anchor += QLatin1Char('-') + QString::number(n);

        m_headings.append({ level, text, anchor, b.blockNumber() });
    }
}

QString MarkdownView::makeAnchor(const QString &headingText)
{
    // 與 GitHub 相同的規則：轉小寫、空白變 -、去掉標點，保留中日韓文字
    QString out;
    const QString src = headingText.trimmed().toLower();
    out.reserve(src.size());
    for (const QChar ch : src) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_'))
            out += ch;
        else if (ch.isSpace())
            out += QLatin1Char('-');
    }
    return out;
}

qreal MarkdownView::headingTop(int index) const
{
    if (index < 0 || index >= m_headings.size())
        return 0;
    const QTextBlock b = document()->findBlockByNumber(m_headings.at(index).blockNumber);
    if (!b.isValid())
        return 0;
    return document()->documentLayout()->blockBoundingRect(b).top();
}

void MarkdownView::scrollToHeading(int index)
{
    if (index < 0 || index >= m_headings.size())
        return;
    verticalScrollBar()->setValue(qRound(headingTop(index)) - 8);
    updateCurrentHeading();
}

bool MarkdownView::scrollToFragment(const QString &fragment)
{
    const QString key = fragment.trimmed().toLower();
    if (key.isEmpty()) {
        verticalScrollBar()->setValue(0);
        return true;
    }
    const QString slug = makeAnchor(fragment);
    for (int i = 0; i < m_headings.size(); ++i) {
        if (m_headings.at(i).anchor == key || m_headings.at(i).anchor == slug) {
            scrollToHeading(i);
            return true;
        }
    }
    // 退而求其次：內嵌 HTML 的 <a name="...">
    scrollToAnchor(fragment);
    return false;
}

void MarkdownView::updateCurrentHeading()
{
    if (m_rebuilding)
        return;

    int index = -1;
    if (!m_headings.isEmpty()) {
        const QScrollBar *sb = verticalScrollBar();
        qreal threshold = sb->value() + 16;
        // 捲到底時，最後幾節的標題可能到不了頂端，改以畫面內最後一個標題為準
        if (sb->maximum() > 0 && sb->value() >= sb->maximum())
            threshold = sb->value() + viewport()->height();
        for (int i = 0; i < m_headings.size(); ++i) {
            if (headingTop(i) <= threshold)
                index = i;
            else
                break;
        }
        if (index < 0)
            index = 0; // 還沒捲到第一個標題時，視為在第一節
    }
    if (index != m_currentHeading) {
        m_currentHeading = index;
        emit currentHeadingChanged(index);
    }
}

double MarkdownView::scrollRatio() const
{
    const QScrollBar *sb = verticalScrollBar();
    return sb->maximum() > 0 ? double(sb->value()) / sb->maximum() : 0.0;
}

void MarkdownView::setScrollRatio(double ratio)
{
    document()->size();
    QScrollBar *sb = verticalScrollBar();
    sb->setValue(qRound(qBound(0.0, ratio, 1.0) * sb->maximum()));
}

void MarkdownView::setZoomStep(int step)
{
    m_zoomStep = qBound(-6, step, 24);
    const double ratio = scrollRatio();
    QFont f = font();
    f.setPointSizeF(m_basePointSize + m_zoomStep);
    setFont(f);
    setScrollRatio(ratio);
}

QString MarkdownView::resolveLocalPath(const QUrl &url) const
{
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());

    const QString scheme = url.scheme();
    if (scheme.size() == 1) // Windows 磁碟代號：C:/docs/a.md 會被 QUrl 當成 scheme "c"
        return QDir::cleanPath(url.toString(QUrl::FullyDecoded));

    if (scheme.isEmpty()) {
        const QString p = url.path(QUrl::FullyDecoded);
        if (p.isEmpty())
            return QString();
        const QDir base(m_baseDir.isEmpty() ? QDir::currentPath() : m_baseDir);
        return QDir::cleanPath(base.absoluteFilePath(p));
    }
    return QString(); // http(s) 等遠端資源
}

QHash<QString, MarkdownView::CachedImage> &MarkdownView::imageCache()
{
    static QHash<QString, CachedImage> cache;
    return cache;
}

void MarkdownView::trimImageCache(qint64 incomingBytes)
{
    // 圖片快取上限 128 MB（原圖＋縮圖）；超過就整批淘汰舊的，下次重繪再載入
    constexpr qint64 kMaxBytes = 128LL * 1024 * 1024;
    qint64 total = incomingBytes;
    for (const CachedImage &c : std::as_const(imageCache()))
        total += c.image.sizeInBytes() + c.scaled.sizeInBytes();
    if (total > kMaxBytes)
        imageCache().clear();
}

QVariant MarkdownView::loadResource(int type, const QUrl &name)
{
    if (type != QTextDocument::ImageResource)
        return QTextBrowser::loadResource(type, name);

    const QString path = resolveLocalPath(name);
    if (path.isEmpty())
        return QVariant(); // 不下載網路圖片

    const QFileInfo fi(path);
    if (!fi.isFile())
        return QVariant();

    m_hasImages = true;
    const QString key = fi.absoluteFilePath();
    QHash<QString, CachedImage> &cache = imageCache();
    auto it = cache.find(key);
    if (it == cache.end() || it->modified != fi.lastModified()) {
        QImage loaded;
        if (!loaded.load(key))
            return QVariant();
        trimImageCache(loaded.sizeInBytes());
        it = cache.insert(key, CachedImage{ fi.lastModified(), loaded, QImage(), 0 });
    }

    // 太寬的圖縮到視窗寬度（依螢幕 DPI 保留清晰度）。
    // 一律預留垂直捲軸寬度，結果才不會隨捲軸出現與否而變動。
    // 縮放結果會快取：編輯時每次重繪都會重新載入圖片，寬度沒變就不必再縮一次
    const int scrollBarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    const int maxWidth = width() - 2 * frameWidth() - scrollBarWidth
                         - int(2 * document()->documentMargin()) - 8;
    if (maxWidth <= 64 || it->image.width() <= maxWidth)
        return it->image;

    const qreal dpr = devicePixelRatioF();
    const int targetWidth = qRound(maxWidth * dpr);
    if (it->scaledWidth != targetWidth) {
        QImage scaled = it->image.scaledToWidth(targetWidth, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        it->scaled = scaled;
        it->scaledWidth = targetWidth;
    }
    return it->scaled;
}

void MarkdownView::onAnchorClicked(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("mailto") || scheme == QLatin1String("ftp")) {
        QDesktopServices::openUrl(url);
        return;
    }

    if (scheme.isEmpty() && url.path().isEmpty() && url.hasFragment()) {
        scrollToFragment(url.fragment(QUrl::FullyDecoded));
        return;
    }

    const QString path = resolveLocalPath(url);
    if (path.isEmpty()) {
        QDesktopServices::openUrl(url);
        return;
    }

    const QFileInfo fi(path);
    if (isMarkdownSuffix(fi.suffix())) {
        emit markdownLinkActivated(fi.absoluteFilePath(), url.fragment(QUrl::FullyDecoded));
        return;
    }
    if (fi.exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
}

void MarkdownView::changeEvent(QEvent *event)
{
    QTextBrowser::changeEvent(event);
    // 系統切換深色／淺色主題時重新套用顏色
    if (event->type() == QEvent::PaletteChange && !m_rebuilding && !m_markdown.isEmpty())
        renderMarkdown(m_markdown, m_baseDir, true);
}

void MarkdownView::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    // 注意：QAbstractScrollArea 收到的是 viewport 的 resize，捲軸出現／消失也會觸發；
    // 這裡只在元件本身寬度改變（拖曳分隔線、調整視窗）時才重排，避免重建 ↔ 捲軸切換的無窮迴圈
    if (width() != m_layoutWidth) {
        m_layoutWidth = width();
        m_resizeTimer.start();
    }
}

void MarkdownView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int dy = event->angleDelta().y();
        if (dy != 0)
            emit zoomRequested(dy > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

QList<int> MarkdownView::headingSourceLines(const QString &markdown)
{
    static const QRegularExpression atx(QStringLiteral("^ {0,3}#{1,6}(\\s|$)"));
    static const QRegularExpression setext(QStringLiteral("^ {0,3}(=+|-+)[ \\t]*$"));
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression listItem(QStringLiteral("^\\s*([-*+]|\\d+[.)])\\s"));

    QList<int> lines;
    const int fmLen = frontMatterLength(markdown);
    const int lineOffset = int(markdown.left(fmLen).count(QLatin1Char('\n')));
    const QStringList rows = markdown.mid(fmLen).split(QLatin1Char('\n'));

    QString openFence;
    bool prevIsParagraph = false;
    for (int i = 0; i < rows.size(); ++i) {
        const QString &row = rows.at(i);
        const QRegularExpressionMatch fm = fence.match(row);

        if (!openFence.isEmpty()) { // 在程式碼區塊內
            if (fm.hasMatch() && fm.captured(1).at(0) == openFence.at(0)
                && fm.captured(1).size() >= openFence.size())
                openFence.clear();
            prevIsParagraph = false;
            continue;
        }
        if (fm.hasMatch()) {
            openFence = fm.captured(1);
            prevIsParagraph = false;
            continue;
        }
        if (atx.match(row).hasMatch()) {
            lines.append(lineOffset + i);
            prevIsParagraph = false;
            continue;
        }
        if (prevIsParagraph && setext.match(row).hasMatch()) {
            lines.append(lineOffset + i - 1);
            prevIsParagraph = false;
            continue;
        }
        const QString t = row.trimmed();
        prevIsParagraph = !t.isEmpty() && !row.startsWith(QLatin1String("    "))
                          && !t.startsWith(QLatin1Char('>')) && !t.startsWith(QLatin1Char('|'))
                          && !t.startsWith(QLatin1Char('<')) && !listItem.match(row).hasMatch();
    }
    return lines;
}
