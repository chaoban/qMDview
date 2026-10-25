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
    doc->setMarkdown(m_markdown.mid(frontMatterLength(m_markdown)),
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
    QImage image;
    const auto it = m_imageCache.constFind(fi.absoluteFilePath());
    if (it != m_imageCache.constEnd() && it->modified == fi.lastModified()) {
        image = it->image;
    } else {
        if (!image.load(fi.absoluteFilePath()))
            return QVariant();
        m_imageCache.insert(fi.absoluteFilePath(), { fi.lastModified(), image });
    }

    // 太寬的圖縮到視窗寬度（依螢幕 DPI 保留清晰度）。
    // 一律預留垂直捲軸寬度，結果才不會隨捲軸出現與否而變動
    const int scrollBarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    const int maxWidth = width() - 2 * frameWidth() - scrollBarWidth
                         - int(2 * document()->documentMargin()) - 8;
    if (maxWidth > 64 && image.width() > maxWidth) {
        const qreal dpr = devicePixelRatioF();
        image = image.scaledToWidth(qRound(maxWidth * dpr), Qt::SmoothTransformation);
        image.setDevicePixelRatio(dpr);
    }
    return image;
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
