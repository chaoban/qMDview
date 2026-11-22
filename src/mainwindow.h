#pragma once

#include "textfileio.h"

#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QActionGroup;
class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
QT_END_NAMESPACE

class DocumentTab;
class SingleInstance;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openPath(const QString &path);          // 檔案或資料夾皆可
    void openPaths(const QStringList &paths);    // 命令列、其他程式轉交、拖放
    bool openFile(const QString &path, const QString &fragment = QString());
    void setRootFolder(const QString &dir);
    void restoreLastSession();
    void bringToFront();

    // 由 main() 交給主視窗管理（收到其他程式轉交的路徑就開成新分頁）
    void setSingleInstance(SingleInstance *instance);

    // 「從檔案總管開啟時」的設定：true＝每個檔案開新視窗（1.2.0 以前的行為）
    static bool opensInNewWindow();

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
    void setOpenMode(bool newWindow);
    void restartApplication();

    // ---- 分頁 ----
    DocumentTab *currentTab() const;
    DocumentTab *tabAt(int index) const;
    DocumentTab *findTab(const QString &path) const;
    DocumentTab *createTab();
    bool closeTab(int index, bool askSave = true);
    void closeOtherTabs(int index);
    void closeTabsToRight(int index);
    void openTabInNewWindow(int index);
    void showTabContextMenu(const QPoint &pos);
    void onCurrentTabChanged();
    void updateTabTitles();
    void cycleTab(int delta);

    // ---- 檔案 ----
    bool maybeSave(DocumentTab *tab);
    bool saveTab(DocumentTab *tab);
    bool saveTabAs(DocumentTab *tab);
    bool saveToFile(DocumentTab *tab, const QString &path);
    void applyContent(DocumentTab *tab, const TextFileContent &content, bool keepScroll);
    void watchFile(const QString &path);
    void unwatchFile(const QString &path);
    void processChangedFiles();
    void handleFileChanged(const QString &path);
    void openDropped(const QStringList &paths);

    // ---- 預覽 / 目錄 ----
    void onHeadingActivated(int index);
    void selectCurrentInTree();

    // ---- 搜尋 ----
    void startFind();
    void find(bool backward, bool incremental = false);
    void updateFindHighlights();
    void clearFindHighlights(DocumentTab *tab);
    bool findTargetIsEditor() const;

    // ---- 介面 / 設定 ----
    void setupActions();
    void setupLanguageMenu();
    void setupOpenModeMenu();
    void setupFileTree();
    void setupTabs();
    void updateTitle();
    void updateStatus();
    void updateCursorPosition();   // 狀態列：編輯器游標所在的行號與欄位
    void addRecentFile(const QString &path);
    void rebuildRecentMenu();
    void readSettings();
    void writeSettings();
    void writeSession();

    static QStringList markdownNameFilters();

    Ui::MainWindow *ui;
    QFileSystemModel *m_fsModel = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QActionGroup *m_openModeGroup = nullptr;
    SingleInstance *m_instance = nullptr;
    QPointer<DocumentTab> m_previousTab;   // 切換分頁時清除舊分頁的搜尋醒目提示

    QTimer m_reloadTimer;          // 合併短時間內多次的檔案變更通知
    QSet<QString> m_changedFiles;  // 等待處理的外部變更

    QString m_rootFolder;
    QStringList m_recentFiles;
    QByteArray m_splitterState;    // 編輯器／預覽的分隔位置，所有分頁共用
    qreal m_editorBasePointSize = 11.0;
    int m_zoomStep = 0;

    bool m_askingReload = false;
    bool m_findInEditor = false;
};
