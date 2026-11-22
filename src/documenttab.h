#pragma once

#include "textfileio.h"

#include <QStringList>
#include <QTimer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QMimeData;
class QPlainTextEdit;
class QSplitter;
QT_END_NAMESPACE

class MarkdownHighlighter;
class MarkdownView;

// 一個分頁＝一份文件：左邊原始碼編輯器、右邊即時預覽，
// 加上這份文件自己的檔案狀態（路徑、編碼／BOM／換行、是否已修改）
class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);
    ~DocumentTab() override;

    QPlainTextEdit *editor() const { return m_editor; }
    MarkdownView *preview() const { return m_preview; }
    QSplitter *splitter() const { return m_splitter; }

    QString filePath() const { return m_filePath; }   // 空字串表示未命名
    void setFilePath(const QString &path) { m_filePath = path; }
    const TextFormat &format() const { return m_format; }
    void setFormat(const TextFormat &format) { m_format = format; }

    bool isModified() const;
    void setModified(bool modified);
    bool hasDocument() const;   // 有檔案，或編輯器中有內容／已修改
    bool isBlank() const { return !hasDocument(); }
    QString displayName() const;

    // 背景分頁的檔案在外部被修改、而分頁本身有未儲存的變更：切換到這個分頁時再詢問
    bool reloadPending() const { return m_reloadPending; }
    void setReloadPending(bool pending) { m_reloadPending = pending; }

    // 換上新內容（開檔、重新載入、清空），不算修改
    void setContent(const QString &text, bool keepScroll);
    void renderPreview(bool keepScroll);
    // 未命名文件的相對路徑基準（資料夾樹的根）
    void setFallbackBaseDir(const QString &dir) { m_fallbackBaseDir = dir; }
    QString previewBaseDir() const;

    void setEditorVisible(bool visible);
    bool isEditorShown() const { return m_editorVisible; }
    void setSyncScroll(bool on);
    void syncPreviewToEditor();
    void setZoom(int step, qreal editorBasePointSize);
    void updateColors(const QPalette &palette);

    // 拖放進來的檔案／資料夾（只收 Markdown、文字檔與資料夾）
    static QStringList droppedPaths(const QMimeData *mime);
    static bool isMarkdownFile(const QString &path);

signals:
    void modificationChanged(bool modified);
    void pathsDropped(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QSplitter *m_splitter;
    QPlainTextEdit *m_editor;
    MarkdownView *m_preview;
    MarkdownHighlighter *m_highlighter;

    QTimer m_renderTimer;   // 編輯時延遲重繪，避免每打一個字就重排

    QString m_filePath;
    QString m_fallbackBaseDir;
    TextFormat m_format;

    bool m_loading = false;
    bool m_editorVisible = false;
    bool m_syncScroll = true;
    bool m_reloadPending = false;
};
