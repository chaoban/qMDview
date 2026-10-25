#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QTextBrowser>
#include <QTimer>

struct MarkdownHeading
{
    int level = 0;        // 1 ~ 6
    QString text;
    QString anchor;       // GitHub 風格 slug，供 [連結](#標題) 使用
    int blockNumber = -1; // 在渲染後文件中的區塊編號
};

// 以 QTextBrowser 為基礎的 Markdown 預覽元件
//   - 使用 Qt 內建 Markdown 解析器（md4c，GitHub 方言）
//   - 相對路徑圖片以 Markdown 檔所在資料夾為基準
//   - 處理 #錨點、相對 .md 連結、外部網址
//   - 擷取標題清單並回報目前閱讀到的標題（給 TOC 用）
class MarkdownView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    // 渲染 Markdown；baseDir 用來解析相對路徑的圖片與連結
    void renderMarkdown(const QString &markdown, const QString &baseDir, bool keepScroll);
    QString markdown() const { return m_markdown; }
    QString baseDir() const { return m_baseDir; }

    const QList<MarkdownHeading> &headings() const { return m_headings; }
    void scrollToHeading(int index);
    bool scrollToFragment(const QString &fragment);

    // 0.0 ~ 1.0，用於和編輯器同步捲動
    double scrollRatio() const;
    void setScrollRatio(double ratio);

    void setZoomStep(int step);
    int zoomStep() const { return m_zoomStep; }

    // 掃描 Markdown 原始碼，回傳每個標題所在的行號（0 起算），順序與 headings() 一致
    static QList<int> headingSourceLines(const QString &markdown);
    static QString makeAnchor(const QString &headingText);

signals:
    void headingsChanged();
    void rebuilt();            // 文件內容重建（重新渲染、視窗寬度改變、主題切換）
    void currentHeadingChanged(int index);
    void markdownLinkActivated(const QString &filePath, const QString &fragment);
    void zoomRequested(int delta);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void changeEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void collectHeadings();
    void updateCurrentHeading();
    void onAnchorClicked(const QUrl &url);
    qreal headingTop(int index) const;
    QString resolveLocalPath(const QUrl &url) const;

    struct CachedImage { QDateTime modified; QImage image; };

    QString m_markdown;
    QString m_baseDir;
    QList<MarkdownHeading> m_headings;
    int m_currentHeading = -1;
    int m_zoomStep = 0;
    qreal m_basePointSize = 11.0;
    QHash<QString, CachedImage> m_imageCache;
    bool m_rebuilding = false;
    bool m_hasImages = false;     // 文件含本機圖片：視窗變寬變窄時需重新縮放
    QTimer m_resizeTimer;
    int m_layoutWidth = -1;
    int m_renderedWidth = -1;
};
