#include "documenttab.h"

#include "markdownhighlighter.h"
#include "markdownview.h"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

DocumentTab::DocumentTab(QWidget *parent)
    : QWidget(parent)
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_editor(new QPlainTextEdit(m_splitter))
    , m_preview(new MarkdownView(m_splitter))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter);

    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_editor);
    m_splitter->addWidget(m_preview);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);

    // ---- 編輯器 ----
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(qMax<qreal>(11.0, mono.pointSizeF()));
    m_editor->setFont(mono);
    m_editor->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setPlaceholderText(tr("在此輸入 Markdown…"));
    m_editor->viewport()->installEventFilter(this); // 攔截拖放進來的檔案
    m_highlighter = new MarkdownHighlighter(m_editor->document());
    m_editor->hide();

    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(250);
    connect(&m_renderTimer, &QTimer::timeout, this, [this] { renderPreview(true); });

    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading)
            m_renderTimer.start();
    });
    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this, &DocumentTab::modificationChanged);
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &DocumentTab::syncPreviewToEditor);
}

DocumentTab::~DocumentTab()
{
    m_editor->viewport()->removeEventFilter(this);
}

bool DocumentTab::isModified() const
{
    return m_editor->document()->isModified();
}

void DocumentTab::setModified(bool modified)
{
    m_editor->document()->setModified(modified);
}

bool DocumentTab::hasDocument() const
{
    const QTextDocument *doc = m_editor->document();
    return !m_filePath.isEmpty() || doc->isModified() || !doc->isEmpty();
}

QString DocumentTab::displayName() const
{
    return m_filePath.isEmpty() ? tr("未命名") : QFileInfo(m_filePath).fileName();
}

void DocumentTab::setContent(const QString &text, bool keepScroll)
{
    m_loading = true;
    const int editorScroll = m_editor->verticalScrollBar()->value();
    const int cursorPos = m_editor->textCursor().position();

    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);

    if (keepScroll) {
        QTextCursor c = m_editor->textCursor();
        c.setPosition(qMin(cursorPos, m_editor->document()->characterCount() - 1));
        m_editor->setTextCursor(c);
        m_editor->verticalScrollBar()->setValue(editorScroll);
    }
    m_loading = false;

    m_renderTimer.stop();
    renderPreview(keepScroll);
}

QString DocumentTab::previewBaseDir() const
{
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).absolutePath();
    return m_fallbackBaseDir;
}

void DocumentTab::renderPreview(bool keepScroll)
{
    m_preview->renderMarkdown(m_editor->toPlainText(), previewBaseDir(), keepScroll);
    syncPreviewToEditor();
}

void DocumentTab::setEditorVisible(bool visible)
{
    m_editorVisible = visible;
    m_editor->setVisible(visible);
}

void DocumentTab::setSyncScroll(bool on)
{
    m_syncScroll = on;
    if (on)
        syncPreviewToEditor();
}

void DocumentTab::syncPreviewToEditor()
{
    if (!m_editorVisible || !m_syncScroll)
        return;
    const QScrollBar *sb = m_editor->verticalScrollBar();
    m_preview->setScrollRatio(sb->maximum() > 0 ? double(sb->value()) / sb->maximum() : 0.0);
}

void DocumentTab::setZoom(int step, qreal editorBasePointSize)
{
    if (m_preview->zoomStep() != step)
        m_preview->setZoomStep(step);
    QFont f = m_editor->font();
    const qreal size = qMax<qreal>(6.0, editorBasePointSize + m_preview->zoomStep());
    if (!qFuzzyCompare(f.pointSizeF(), size)) {
        f.setPointSizeF(size);
        m_editor->setFont(f);
        m_editor->setTabStopDistance(4 * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));
    }
}

void DocumentTab::updateColors(const QPalette &palette)
{
    m_highlighter->updateColors(palette);
}

bool DocumentTab::isMarkdownFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList accepted = { QStringLiteral("md"), QStringLiteral("markdown"),
                                          QStringLiteral("mdown"), QStringLiteral("mkd"),
                                          QStringLiteral("mkdn"), QStringLiteral("txt") };
    return accepted.contains(suffix);
}

QStringList DocumentTab::droppedPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    const auto urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir() || isMarkdownFile(path))
            paths.append(path);
    }
    return paths;
}

bool DocumentTab::eventFilter(QObject *watched, QEvent *event)
{
    // 編輯器本身接受文字拖放；拖進來的是檔案時改成開檔
    if (watched == m_editor->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (!droppedPaths(e->mimeData()).isEmpty()) {
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto *e = static_cast<QDropEvent *>(event);
            const QStringList paths = droppedPaths(e->mimeData());
            if (!paths.isEmpty()) {
                e->acceptProposedAction();
                emit pathsDropped(paths);
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
