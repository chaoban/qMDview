#pragma once

#include "textfileio.h"

#include <QMainWindow>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
class QMimeData;
QT_END_NAMESPACE

class MarkdownHighlighter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openPath(const QString &path);   // 檔案或資料夾皆可
    bool openFile(const QString &path, const QString &fragment = QString());
    void setRootFolder(const QString &dir);
    void restoreLastSession();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // ---- 選單動作 ----
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    bool save();
    bool saveAs();
    void reload();
    void toggleEditor(bool visible);
    void zoomBy(int delta);
    void showAbout();
    void changeLanguage(const QString &preference);
    void restartApplication();

    // ---- 檔案 ----
    bool maybeSave();
    bool saveToFile(const QString &path);
    void applyContent(const TextFileContent &content, bool keepScroll);
    void setCurrentFile(const QString &path);
    void watchFile(const QString &path);
    void onFileChangedOnDisk();
    QString displayName() const;

    // ---- 預覽 / 目錄 ----
    void renderPreview(bool keepScroll);
    QString previewBaseDir() const;
    void syncPreviewToEditor();
    void onHeadingActivated(int index);
    void selectCurrentInTree();

    // ---- 搜尋 ----
    void startFind();
    void find(bool backward, bool incremental = false);
    void updateFindHighlights();
    void clearFindHighlights();
    bool findTargetIsEditor() const;

    // ---- 介面 / 設定 ----
    void setupActions();
    void setupLanguageMenu();
    void setupFileTree();
    void updateTitle();
    void updateStatus();
    void addRecentFile(const QString &path);
    void rebuildRecentMenu();
    void readSettings();
    void writeSettings();

    static QStringList markdownNameFilters();
    static bool isMarkdownFile(const QString &path);
    static QString dropTarget(const QMimeData *mime);

    Ui::MainWindow *ui;
    QFileSystemModel *m_fsModel = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    MarkdownHighlighter *m_highlighter = nullptr;
    QLabel *m_formatLabel = nullptr;

    QTimer m_renderTimer;   // 編輯時延遲重繪，避免每打一個字就重排
    QTimer m_reloadTimer;   // 合併短時間內多次的檔案變更通知

    QString m_currentFile;  // 空字串表示未命名
    QString m_rootFolder;
    TextFormat m_format;
    QStringList m_recentFiles;
    qreal m_editorBasePointSize = 11.0;

    bool m_loading = false;
    bool m_askingReload = false;
    bool m_findInEditor = false;
};
