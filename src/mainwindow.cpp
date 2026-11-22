#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"
#include "documenttab.h"
#include "findbar.h"
#include "markdownview.h"
#include "singleinstance.h"
#include "tocwidget.h"
#include "uilanguage.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHash>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTextBlock>
#include <QUrl>

#include <utility>

namespace {

constexpr int kMaxRecentFiles = 10;
const QString kOpenModeKey = QStringLiteral("ui/openMode");   // "tab"（預設）或 "window"
const QString kNewWindowArg = QStringLiteral("--new-window");

// QTextBrowser 與 QPlainTextEdit 沒有共同的 find() 基底類別，用樣板共用邏輯
template <typename Edit>
bool findInEdit(Edit *edit, const QString &text, QTextDocument::FindFlags flags, bool incremental)
{
    const QTextCursor original = edit->textCursor();
    if (incremental) { // 即時搜尋：從目前選取的開頭重新找，輸入更多字時才不會跳過
        QTextCursor c = original;
        c.setPosition(original.selectionStart());
        edit->setTextCursor(c);
    }
    if (edit->find(text, flags))
        return true;

    // 到底了就從另一端繞回來
    QTextCursor c = edit->textCursor();
    c.movePosition((flags & QTextDocument::FindBackward) ? QTextCursor::End : QTextCursor::Start);
    edit->setTextCursor(c);
    if (edit->find(text, flags))
        return true;

    edit->setTextCursor(original);
    return false;
}

bool samePath(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return QFileInfo(a) == QFileInfo(b); // Windows 上不分大小寫
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_editorBasePointSize = qMax<qreal>(11.0, mono.pointSizeF());

    // 左側兩個面板上下排列
    splitDockWidget(ui->dockFiles, ui->dockToc, Qt::Vertical);
    resizeDocks({ ui->dockFiles }, { 280 }, Qt::Horizontal);

    // ---- 檔案監看（所有分頁共用一個監看器，依路徑找到對應的分頁）----
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &MainWindow::processChangedFiles);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        m_changedFiles.insert(path);
        m_reloadTimer.start();
    });

    // ---- 目錄 ----
    connect(ui->tocTree, &TocWidget::headingActivated, this, &MainWindow::onHeadingActivated);

    // ---- 搜尋列 ----
    connect(ui->findBar, &FindBar::findNext, this, [this] { find(false); });
    connect(ui->findBar, &FindBar::findPrevious, this, [this] { find(true); });
    connect(ui->findBar, &FindBar::searchChanged, this, [this] { find(false, true); });
    connect(ui->findBar, &FindBar::closed, this, [this] {
        clearFindHighlights(currentTab());
        if (DocumentTab *tab = currentTab()) {
            if (findTargetIsEditor())
                tab->editor()->setFocus();
            else
                tab->preview()->setFocus();
        }
    });

    // 狀態列右側：游標位置（僅編輯器顯示時）、檔案編碼
    m_cursorLabel = new QLabel(this);
    m_cursorLabel->setContentsMargins(8, 0, 8, 0);
    m_cursorLabel->setMinimumWidth(
        m_cursorLabel->fontMetrics().horizontalAdvance(tr("行 %1，欄 %2").arg(99999).arg(9999)) + 16);
    m_cursorLabel->hide();
    statusBar()->addPermanentWidget(m_cursorLabel);

    m_formatLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_formatLabel);

    setupActions();
    setupLanguageMenu();
    setupOpenModeMenu();
    setupFileTree();
    setupTabs();
    readSettings();

    createTab(); // 永遠至少有一個分頁；沒有文件時就是空白分頁
    onCurrentTabChanged();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ============================================================================
// 介面初始化
// ============================================================================

void MainWindow::setupActions()
{
    auto themed = [this](const char *name, QStyle::StandardPixmap fallback) {
        return QIcon::fromTheme(QString::fromLatin1(name), style()->standardIcon(fallback));
    };
    ui->actionNew->setIcon(themed("document-new", QStyle::SP_FileIcon));
    ui->actionOpen->setIcon(themed("document-open", QStyle::SP_DialogOpenButton));
    ui->actionOpenFolder->setIcon(themed("folder-open", QStyle::SP_DirOpenIcon));
    ui->actionSave->setIcon(themed("document-save", QStyle::SP_DialogSaveButton));
    ui->actionReload->setIcon(themed("view-refresh", QStyle::SP_BrowserReload));
    ui->actionClose->setIcon(themed("document-close", QStyle::SP_DialogCloseButton));
    ui->actionToggleEditor->setIcon(themed("document-edit", QStyle::SP_FileDialogDetailedView));
    ui->actionFind->setIcon(themed("edit-find", QStyle::SP_FileDialogContentsView));

    ui->actionZoomIn->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal) });
    ui->actionZoomOut->setShortcuts(QKeySequence::ZoomOut);
    ui->actionClose->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_F4) });

    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(ui->actionOpenFolder, &QAction::triggered, this, &MainWindow::openFolderDialog);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::save);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveAs);
    connect(ui->actionReload, &QAction::triggered, this, &MainWindow::reload);
    connect(ui->actionClose, &QAction::triggered, this, [this] { closeTab(ui->tabs->currentIndex()); });
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionToggleEditor, &QAction::toggled, this, &MainWindow::toggleEditor);
    connect(ui->actionSyncScroll, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < ui->tabs->count(); ++i)
            tabAt(i)->setSyncScroll(on);
    });

    connect(ui->actionFind, &QAction::triggered, this, &MainWindow::startFind);
    connect(ui->actionFindNext, &QAction::triggered, this, [this] {
        if (ui->findBar->text().isEmpty()) startFind(); else find(false);
    });
    connect(ui->actionFindPrevious, &QAction::triggered, this, [this] {
        if (ui->findBar->text().isEmpty()) startFind(); else find(true);
    });

    connect(ui->actionZoomIn, &QAction::triggered, this, [this] { zoomBy(1); });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this] { zoomBy(-1); });
    connect(ui->actionZoomReset, &QAction::triggered, this, [this] { zoomBy(-m_zoomStep); });

    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAbout);
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // 檢視選單：面板開關放最前面
    QAction *filesToggle = ui->dockFiles->toggleViewAction();
    filesToggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    QAction *tocToggle = ui->dockToc->toggleViewAction();
    tocToggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    QAction *toolbarToggle = ui->mainToolBar->toggleViewAction();

    QAction *first = ui->menuView->actions().value(0);
    ui->menuView->insertActions(first, { filesToggle, tocToggle, toolbarToggle });
    ui->menuView->insertSeparator(first);

    // 檢視選單：切換分頁（放在縮放之後）
    QAction *beforeLanguage = ui->menuLanguage->menuAction();
    auto *nextTab = new QAction(tr("下一個分頁"), this);
    nextTab->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown) });
    connect(nextTab, &QAction::triggered, this, [this] { cycleTab(1); });
    auto *prevTab = new QAction(tr("上一個分頁"), this);
    prevTab->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                            QKeySequence(Qt::CTRL | Qt::Key_PageUp) });
    connect(prevTab, &QAction::triggered, this, [this] { cycleTab(-1); });
    ui->menuView->insertActions(beforeLanguage, { nextTab, prevTab });
    ui->menuView->insertSeparator(beforeLanguage);
}

void MainWindow::setupLanguageMenu()
{
    // 「跟隨系統」＋各語言（語言名稱以該語言本身顯示，方便看不懂目前介面的人找到）
    auto *group = new QActionGroup(this);
    const QString pref = UiLanguage::preference();
    auto addItem = [&](const QString &text, const QString &code) {
        QAction *a = ui->menuLanguage->addAction(text);
        a->setCheckable(true);
        a->setChecked(code == pref);
        a->setData(code);
        group->addAction(a);
    };
    addItem(tr("跟隨系統"), QString());
    ui->menuLanguage->addSeparator();
    for (const QString &code : UiLanguage::available())
        addItem(UiLanguage::nativeName(code), code);

    connect(group, &QActionGroup::triggered, this, [this](QAction *a) {
        changeLanguage(a->data().toString());
    });
}

void MainWindow::setupOpenModeMenu()
{
    // 檢視 → 從檔案總管開啟時：新分頁（預設，只執行一個程式）／新視窗（1.2.0 以前的行為）
    auto *menu = new QMenu(tr("從檔案總管開啟時(&W)"), this);
    m_openModeGroup = new QActionGroup(this);
    QAction *tabMode = menu->addAction(tr("在同一個視窗開新分頁"));
    QAction *windowMode = menu->addAction(tr("每個檔案開新視窗"));
    for (QAction *a : { tabMode, windowMode }) {
        a->setCheckable(true);
        m_openModeGroup->addAction(a);
    }
    windowMode->setData(true);
    tabMode->setData(false);
    (opensInNewWindow() ? windowMode : tabMode)->setChecked(true);
    connect(m_openModeGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        setOpenMode(a->data().toBool());
    });
    ui->menuView->insertMenu(ui->menuLanguage->menuAction(), menu);
}

void MainWindow::setupFileTree()
{
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilters(markdownNameFilters());
    m_fsModel->setNameFilterDisables(false); // 直接隱藏非 Markdown 檔，而不是反灰
    // 模型在開啟資料夾後才掛上，避免一開始列出整台電腦

    ui->fileTree->setUniformRowHeights(true);
    ui->fileTree->setAnimated(false);

    auto openIndex = [this](const QModelIndex &index) {
        if (!index.isValid() || m_fsModel->isDir(index))
            return;
        openFile(m_fsModel->filePath(index));
    };
    connect(ui->fileTree, &QTreeView::clicked, this, openIndex);
    connect(ui->fileTree, &QTreeView::activated, this, openIndex);
}

void MainWindow::setupTabs()
{
    QTabBar *bar = ui->tabs->tabBar();
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    bar->installEventFilter(this); // 中鍵關閉分頁
    connect(bar, &QTabBar::customContextMenuRequested, this, &MainWindow::showTabContextMenu);
    connect(ui->tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    connect(ui->tabs, &QTabWidget::currentChanged, this, [this] { onCurrentTabChanged(); });
}

// ============================================================================
// 分頁
// ============================================================================

DocumentTab *MainWindow::currentTab() const
{
    return qobject_cast<DocumentTab *>(ui->tabs->currentWidget());
}

DocumentTab *MainWindow::tabAt(int index) const
{
    return qobject_cast<DocumentTab *>(ui->tabs->widget(index));
}

DocumentTab *MainWindow::findTab(const QString &path) const
{
    for (int i = 0; i < ui->tabs->count(); ++i) {
        DocumentTab *tab = tabAt(i);
        if (samePath(tab->filePath(), path))
            return tab;
    }
    return nullptr;
}

DocumentTab *MainWindow::createTab()
{
    auto *tab = new DocumentTab(ui->tabs);

    // 預覽的訊號只有目前分頁需要反映到目錄／狀態列
    MarkdownView *preview = tab->preview();
    connect(tab, &DocumentTab::modificationChanged, this, [this, tab] {
        updateTabTitles();
        if (tab == currentTab()) {
            updateTitle();
            updateStatus();
        }
    });
    connect(tab, &DocumentTab::pathsDropped, this, &MainWindow::openDropped);
    connect(preview, &MarkdownView::headingsChanged, this, [this, tab] {
        if (tab == currentTab())
            ui->tocTree->setHeadings(tab->preview()->headings());
    });
    connect(preview, &MarkdownView::rebuilt, this, [this, tab] {
        if (tab == currentTab() && ui->findBar->isVisible())
            updateFindHighlights(); // 重建後舊的醒目提示位置已失效
    });
    connect(preview, &MarkdownView::currentHeadingChanged, this, [this, tab](int index) {
        if (tab == currentTab())
            ui->tocTree->setCurrentHeading(index);
    });
    connect(preview, &MarkdownView::markdownLinkActivated, this,
            [this](const QString &path, const QString &fragment) { openFile(path, fragment); });
    connect(preview, &MarkdownView::zoomRequested, this, &MainWindow::zoomBy);
    connect(preview, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
        if (url.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(url.toString());
    });
    connect(tab->editor(), &QPlainTextEdit::cursorPositionChanged, this, [this, tab] {
        if (tab == currentTab())
            updateCursorPosition();
    });
    connect(tab->splitter(), &QSplitter::splitterMoved, this, [this, tab] {
        m_splitterState = tab->splitter()->saveState();
    });

    // 套用目前的檢視設定
    tab->setFallbackBaseDir(m_rootFolder);
    tab->setSyncScroll(ui->actionSyncScroll->isChecked());
    tab->setEditorVisible(ui->actionToggleEditor->isChecked());
    tab->setZoom(m_zoomStep, m_editorBasePointSize);
    if (!m_splitterState.isEmpty())
        tab->splitter()->restoreState(m_splitterState);

    ui->tabs->insertTab(ui->tabs->currentIndex() + 1, tab, tab->displayName());
    updateTabTitles();
    return tab;
}

bool MainWindow::closeTab(int index, bool askSave)
{
    DocumentTab *tab = tabAt(index);
    if (!tab)
        return false;
    if (askSave && tab->isModified()) {
        ui->tabs->setCurrentIndex(index); // 先切過去，讓使用者看到要存的是哪一份
        if (!maybeSave(tab))
            return false;
    }

    const QString closed = tab->filePath();
    unwatchFile(closed);

    if (ui->tabs->count() == 1) {
        // 最後一個分頁：清空成「沒有文件」的狀態，不關閉視窗
        tab->setFilePath(QString());
        tab->setFormat(TextFormat());
        tab->setReloadPending(false);
        tab->setContent(QString(), false);   // 清空編輯器與預覽；目錄隨預覽一起清空
        clearFindHighlights(tab);
        onCurrentTabChanged();
    } else {
        ui->tabs->removeTab(index);
        tab->deleteLater();
    }

    updateTabTitles();
    // 立刻記下目前開啟的檔案：下次啟動不會再自動載入已關閉的檔案
    writeSession();

    if (!closed.isEmpty())
        statusBar()->showMessage(tr("已關閉 %1").arg(QDir::toNativeSeparators(closed)), 3000);
    return true;
}

void MainWindow::closeOtherTabs(int index)
{
    DocumentTab *keep = tabAt(index);
    if (!keep)
        return;
    for (int i = ui->tabs->count() - 1; i >= 0; --i) {
        if (tabAt(i) != keep && !closeTab(i))
            break; // 使用者在「要儲存嗎」按了取消：其餘分頁保留
    }
    ui->tabs->setCurrentWidget(keep);
}

void MainWindow::closeTabsToRight(int index)
{
    DocumentTab *keep = tabAt(index);
    for (int i = ui->tabs->count() - 1; i > index; --i) {
        if (!closeTab(i))
            break;
    }
    if (keep)
        ui->tabs->setCurrentWidget(keep);
}

void MainWindow::openTabInNewWindow(int index)
{
    DocumentTab *tab = tabAt(index);
    if (!tab || tab->filePath().isEmpty())
        return;
    if (tab->isModified()) {
        ui->tabs->setCurrentIndex(index);
        if (!maybeSave(tab))
            return;
    }
    // --new-window：新程式不把檔案交回這個視窗，而是自己開一個視窗
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 { kNewWindowArg, tab->filePath() })) {
        QMessageBox::warning(this, tr("在新視窗開啟"), tr("無法開啟新視窗。"));
        return;
    }
    closeTab(ui->tabs->indexOf(tab), false); // 已詢問過是否儲存
}

void MainWindow::showTabContextMenu(const QPoint &pos)
{
    QTabBar *bar = ui->tabs->tabBar();
    const int index = bar->tabAt(pos);
    DocumentTab *tab = tabAt(index);
    if (!tab)
        return;
    const QString path = tab->filePath();
    const int count = ui->tabs->count();

    QMenu menu(this);
    connect(menu.addAction(tr("關閉分頁")), &QAction::triggered, this,
            [this, tab] { closeTab(ui->tabs->indexOf(tab)); });
    QAction *closeOthers = menu.addAction(tr("關閉其他分頁"));
    closeOthers->setEnabled(count > 1);
    connect(closeOthers, &QAction::triggered, this,
            [this, tab] { closeOtherTabs(ui->tabs->indexOf(tab)); });
    QAction *closeRight = menu.addAction(tr("關閉右側分頁"));
    closeRight->setEnabled(index < count - 1);
    connect(closeRight, &QAction::triggered, this,
            [this, tab] { closeTabsToRight(ui->tabs->indexOf(tab)); });
    menu.addSeparator();

    QAction *copyPath = menu.addAction(tr("複製路徑"));
    copyPath->setEnabled(!path.isEmpty());
    connect(copyPath, &QAction::triggered, this, [path] {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    });
    QAction *reveal = menu.addAction(tr("在檔案總管中顯示"));
    reveal->setEnabled(!path.isEmpty());
    connect(reveal, &QAction::triggered, this, [path] {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    });
    menu.addSeparator();

    QAction *newWindow = menu.addAction(tr("在新視窗開啟"));
    newWindow->setEnabled(!path.isEmpty());
    connect(newWindow, &QAction::triggered, this,
            [this, tab] { openTabInNewWindow(ui->tabs->indexOf(tab)); });

    menu.exec(bar->mapToGlobal(pos));
}

void MainWindow::onCurrentTabChanged()
{
    DocumentTab *tab = currentTab();
    if (m_previousTab && m_previousTab != tab)
        clearFindHighlights(m_previousTab);
    m_previousTab = tab;
    if (!tab)
        return;

    if (!m_splitterState.isEmpty())
        tab->splitter()->restoreState(m_splitterState);
    ui->tocTree->setHeadings(tab->preview()->headings());
    ui->tocTree->setCurrentHeading(tab->preview()->currentHeading());
    if (ui->findBar->isVisible())
        updateFindHighlights();

    updateTitle();
    updateStatus();
    selectCurrentInTree();

    // 在背景時檔案被外部修改、且分頁有未儲存的變更：現在才詢問
    if (tab->reloadPending()) {
        const QString path = tab->filePath();
        QTimer::singleShot(0, this, [this, path] { handleFileChanged(path); });
    }
}

void MainWindow::updateTabTitles()
{
    QTabWidget *tabs = ui->tabs;

    // 不同資料夾的同名檔案（例如好幾個 README.md）：標題後面加上資料夾名稱區分
    QHash<QString, int> nameCount;
    for (int i = 0; i < tabs->count(); ++i) {
        const QString path = tabAt(i)->filePath();
        if (!path.isEmpty())
            ++nameCount[QFileInfo(path).fileName().toLower()];
    }

    for (int i = 0; i < tabs->count(); ++i) {
        const DocumentTab *tab = tabAt(i);
        const QString path = tab->filePath();
        QString title = tab->displayName();
        if (!path.isEmpty() && nameCount.value(QFileInfo(path).fileName().toLower()) > 1)
            title += QStringLiteral(" — ") + QFileInfo(path).dir().dirName();
        if (tab->isModified())
            title += QStringLiteral(" ●");
        title.replace(QLatin1Char('&'), QLatin1String("&&")); // 分頁標題的 & 會被當成助記鍵
        tabs->setTabText(i, title);

        QString tip = path.isEmpty() ? tab->displayName() : QDir::toNativeSeparators(path);
        if (tab->reloadPending())
            tip += QLatin1Char('\n') + tr("（已在外部被修改，切換到這個分頁時會詢問是否重新載入）");
        tabs->setTabToolTip(i, tip);
        tabs->setTabIcon(i, tab->reloadPending() ? style()->standardIcon(QStyle::SP_MessageBoxWarning)
                                                 : QIcon());
    }

    // 只有一個空白分頁時不顯示分頁列，畫面和沒有分頁時一樣
    DocumentTab *cur = currentTab();
    tabs->tabBar()->setVisible(tabs->count() > 1 || (cur && cur->hasDocument()));
}

void MainWindow::cycleTab(int delta)
{
    const int count = ui->tabs->count();
    if (count < 2)
        return;
    ui->tabs->setCurrentIndex((ui->tabs->currentIndex() + delta + count) % count);
}

// ============================================================================
// 開檔 / 存檔
// ============================================================================

bool MainWindow::openPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        setRootFolder(fi.absoluteFilePath());
        return true;
    }
    return openFile(fi.absoluteFilePath());
}

void MainWindow::openPaths(const QStringList &paths)
{
    for (const QString &path : paths)
        openPath(path);
}

bool MainWindow::openFile(const QString &path, const QString &fragment)
{
    const QFileInfo fi(path);
    const QString absPath = fi.absoluteFilePath();

    // 已經開著：切換到那個分頁，只處理錨點
    if (DocumentTab *existing = findTab(absPath)) {
        ui->tabs->setCurrentWidget(existing);
        if (!fragment.isEmpty())
            existing->preview()->scrollToFragment(fragment);
        return true;
    }

    if (!fi.isFile()) {
        QMessageBox::warning(this, tr("開啟檔案"),
                             tr("找不到檔案：\n%1").arg(QDir::toNativeSeparators(absPath)));
        if (m_recentFiles.removeAll(absPath) > 0)
            rebuildRecentMenu();
        return false;
    }

    const TextFileContent content = TextFileIO::read(absPath);
    if (!content.ok) {
        QMessageBox::warning(this, tr("開啟檔案"),
                             tr("無法讀取 %1：\n%2").arg(QDir::toNativeSeparators(absPath), content.error));
        return false;
    }

    // 目前是空白分頁就直接用它，否則在目前分頁右邊開新分頁
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isBlank())
        tab = createTab();
    ui->tabs->setCurrentWidget(tab);

    tab->setFilePath(absPath);
    watchFile(absPath);
    applyContent(tab, content, false);
    addRecentFile(absPath);
    updateTabTitles();
    updateTitle();

    if (m_rootFolder.isEmpty())
        setRootFolder(fi.absolutePath());
    else
        selectCurrentInTree();

    if (!fragment.isEmpty())
        tab->preview()->scrollToFragment(fragment);

    statusBar()->showMessage(tr("已開啟 %1").arg(QDir::toNativeSeparators(absPath)), 3000);
    return true;
}

void MainWindow::applyContent(DocumentTab *tab, const TextFileContent &content, bool keepScroll)
{
    tab->setFormat(content.format);
    tab->setContent(content.text, keepScroll);
    if (tab == currentTab()) {
        updateTitle();
        updateStatus();
    }
}

void MainWindow::watchFile(const QString &path)
{
    if (!path.isEmpty() && QFileInfo::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
}

void MainWindow::unwatchFile(const QString &path)
{
    if (!path.isEmpty() && m_watcher->files().contains(path))
        m_watcher->removePath(path);
    m_changedFiles.remove(path);
}

void MainWindow::newFile()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isBlank())
        tab = createTab();
    ui->tabs->setCurrentWidget(tab);
    updateTabTitles();
    ui->actionToggleEditor->setChecked(true);
    tab->editor()->setFocus();
}

void MainWindow::openFileDialog()
{
    const DocumentTab *tab = currentTab();
    QString dir = tab && !tab->filePath().isEmpty() ? QFileInfo(tab->filePath()).absolutePath() : m_rootFolder;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    // 可一次選多個檔案，每個檔案開一個分頁
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("開啟 Markdown 檔案"), dir,
        tr("Markdown 檔案 (*.md *.markdown *.mdown *.mkd *.mkdn);;文字檔 (*.txt);;所有檔案 (*)"));
    for (const QString &path : paths)
        openFile(path);
}

void MainWindow::openFolderDialog()
{
    const QString start = !m_rootFolder.isEmpty()
                              ? m_rootFolder
                              : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = QFileDialog::getExistingDirectory(this, tr("開啟資料夾"), start);
    if (!dir.isEmpty())
        setRootFolder(dir);
}

bool MainWindow::save()
{
    DocumentTab *tab = currentTab();
    return tab && saveTab(tab);
}

bool MainWindow::saveAs()
{
    DocumentTab *tab = currentTab();
    return tab && saveTabAs(tab);
}

bool MainWindow::saveTab(DocumentTab *tab)
{
    if (tab->filePath().isEmpty())
        return saveTabAs(tab);
    return saveToFile(tab, tab->filePath());
}

bool MainWindow::saveTabAs(DocumentTab *tab)
{
    QString initial = tab->filePath();
    if (initial.isEmpty())
        initial = QDir(m_rootFolder.isEmpty()
                           ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                           : m_rootFolder).filePath(QStringLiteral("untitled.md"));

    QString path = QFileDialog::getSaveFileName(this, tr("另存新檔"), initial,
                                                tr("Markdown 檔案 (*.md *.markdown);;所有檔案 (*)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".md");
    return saveToFile(tab, path);
}

bool MainWindow::saveToFile(DocumentTab *tab, const QString &path)
{
    // 同一個檔案不能同時在兩個分頁開啟（監看與重新載入都以路徑對應分頁）
    DocumentTab *other = findTab(path);
    if (other && other != tab) {
        QMessageBox::warning(this, tr("儲存"),
                             tr("這個檔案已在另一個分頁開啟，請先關閉該分頁：\n%1")
                                 .arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())));
        return false;
    }

    TextFormat fmt = tab->format();
    QString error;
    bool unencodable = false;
    const QString text = tab->editor()->toPlainText();

    if (!TextFileIO::write(path, text, fmt, &error, &unencodable)) {
        if (!unencodable) {
            QMessageBox::warning(this, tr("儲存"), tr("無法儲存檔案：\n%1").arg(error));
            return false;
        }
        if (QMessageBox::question(this, tr("儲存"), tr("%1\n要改用 UTF-8 儲存嗎？").arg(error))
            != QMessageBox::Yes)
            return false;
        fmt.encoding = QStringConverter::Utf8;
        fmt.bom = false;
        if (!TextFileIO::write(path, text, fmt, &error)) {
            QMessageBox::warning(this, tr("儲存"), tr("無法儲存檔案：\n%1").arg(error));
            return false;
        }
    }

    tab->setFormat(fmt);
    const QFileInfo fi(path);
    const QString absPath = fi.absoluteFilePath();
    const bool isNewPath = !samePath(tab->filePath(), absPath);

    if (isNewPath) {
        unwatchFile(tab->filePath());
        tab->setFilePath(absPath);
    }
    watchFile(absPath); // QSaveFile 以取代方式寫入，監看可能被移除

    tab->setModified(false);
    addRecentFile(absPath);

    if (isNewPath) {
        tab->renderPreview(true); // 相對路徑的基準資料夾可能改變
        if (m_rootFolder.isEmpty())
            setRootFolder(fi.absolutePath());
        else
            selectCurrentInTree();
    }
    updateTabTitles();
    updateTitle();
    updateStatus();
    statusBar()->showMessage(tr("已儲存 %1").arg(QDir::toNativeSeparators(absPath)), 3000);
    return true;
}

void MainWindow::reload()
{
    DocumentTab *tab = currentTab();
    if (!tab || tab->filePath().isEmpty())
        return;
    if (tab->isModified()
        && QMessageBox::question(this, tr("重新載入"), tr("放棄未儲存的變更並重新載入？"))
               != QMessageBox::Yes)
        return;

    const TextFileContent content = TextFileIO::read(tab->filePath());
    if (!content.ok) {
        QMessageBox::warning(this, tr("重新載入"), content.error);
        return;
    }
    tab->setReloadPending(false);
    applyContent(tab, content, true);
    watchFile(tab->filePath());
    updateTabTitles();
    statusBar()->showMessage(tr("已重新載入"), 2000);
}

bool MainWindow::maybeSave(DocumentTab *tab)
{
    if (!tab || !tab->isModified())
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("尚未儲存"), tr("「%1」有未儲存的變更，要儲存嗎？").arg(tab->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (ret == QMessageBox::Save)
        return saveTab(tab);
    return ret == QMessageBox::Discard;
}

void MainWindow::processChangedFiles()
{
    const QSet<QString> paths = std::exchange(m_changedFiles, {});
    for (const QString &path : paths)
        handleFileChanged(path);
}

void MainWindow::handleFileChanged(const QString &path)
{
    DocumentTab *tab = findTab(path);
    if (!tab || m_askingReload)
        return;

    const QFileInfo fi(path);
    if (!fi.exists()) {
        // 有些編輯器存檔時是「刪除 → 重建」，稍等一下再確認一次
        QTimer::singleShot(1000, this, [this, path] {
            if (!findTab(path))
                return;
            if (QFileInfo::exists(path)) {
                m_changedFiles.insert(path);
                m_reloadTimer.start();
            } else {
                statusBar()->showMessage(tr("檔案已被刪除或移動：%1").arg(QDir::toNativeSeparators(path)));
            }
        });
        return;
    }
    watchFile(path); // 被取代的檔案要重新加入監看

    TextFileContent content = TextFileIO::read(path);
    if (!content.ok || content.text == tab->editor()->toPlainText()) {
        if (tab->reloadPending()) {
            tab->setReloadPending(false);
            updateTabTitles();
        }
        return; // 內容沒變（例如自己存檔觸發的通知）
    }

    if (tab->isModified()) {
        if (tab != currentTab()) {
            // 背景分頁有未儲存的變更：先做標記，切換過去時再詢問
            tab->setReloadPending(true);
            updateTabTitles();
            return;
        }
        m_askingReload = true;
        const auto ret = QMessageBox::question(
            this, tr("檔案已變更"),
            tr("「%1」已在外部被修改。\n要重新載入嗎？編輯器中未儲存的變更將會遺失。").arg(fi.fileName()));
        m_askingReload = false;
        tab->setReloadPending(false);
        updateTabTitles();
        if (ret != QMessageBox::Yes || !findTab(path))
            return;
        content = TextFileIO::read(path); // 對話框開著時檔案可能又變了
        if (!content.ok)
            return;
    }

    tab->setReloadPending(false);
    applyContent(tab, content, true);
    updateTabTitles();
    if (tab == currentTab())
        statusBar()->showMessage(tr("檔案已在外部更新，已自動重新載入"), 3000);
}

void MainWindow::openDropped(const QStringList &paths)
{
    // 延後處理：在拖放事件中跳出對話框（例如找不到檔案）在 Windows 上會卡住拖放來源
    QTimer::singleShot(0, this, [this, paths] { openPaths(paths); });
}

// ============================================================================
// 資料夾樹
// ============================================================================

void MainWindow::setRootFolder(const QString &dir)
{
    const QFileInfo fi(dir);
    if (!fi.isDir())
        return;
    m_rootFolder = fi.absoluteFilePath();
    for (int i = 0; i < ui->tabs->count(); ++i)
        tabAt(i)->setFallbackBaseDir(m_rootFolder);

    if (ui->fileTree->model() != m_fsModel) {
        ui->fileTree->setModel(m_fsModel);
        for (int c = 1; c < m_fsModel->columnCount(); ++c)
            ui->fileTree->hideColumn(c);
    }
    m_fsModel->setRootPath(m_rootFolder);
    ui->fileTree->setRootIndex(m_fsModel->index(m_rootFolder));
    ui->fileTree->setToolTip(QDir::toNativeSeparators(m_rootFolder));
    ui->dockFiles->setWindowTitle(tr("檔案 — %1").arg(QDir(m_rootFolder).dirName()));
    ui->dockFiles->show();

    selectCurrentInTree();
}

void MainWindow::selectCurrentInTree()
{
    if (m_rootFolder.isEmpty() || ui->fileTree->model() != m_fsModel)
        return;

    const DocumentTab *tab = currentTab();
    const QString file = tab ? tab->filePath() : QString();
    QModelIndex index;
    if (!file.isEmpty() && !QDir(m_rootFolder).relativeFilePath(file).startsWith(QLatin1String("..")))
        index = m_fsModel->index(file);

    if (index.isValid()) {
        ui->fileTree->setCurrentIndex(index);
        ui->fileTree->scrollTo(index);
    } else if (QItemSelectionModel *selection = ui->fileTree->selectionModel()) {
        // 未命名或不在目前資料夾內的檔案：清除選取，避免看起來像是選中別的檔案
        selection->clearSelection();
        selection->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }
}

// ============================================================================
// 預覽 / 目錄
// ============================================================================

void MainWindow::onHeadingActivated(int index)
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    // 編輯模式下，編輯器也跳到對應的標題行
    if (ui->actionToggleEditor->isChecked()) {
        QPlainTextEdit *editor = tab->editor();
        const QList<int> lines = MarkdownView::headingSourceLines(editor->toPlainText());
        if (index >= 0 && index < lines.size()) {
            const QTextBlock block = editor->document()->findBlockByNumber(lines.at(index));
            if (block.isValid()) {
                editor->setTextCursor(QTextCursor(block));
                editor->verticalScrollBar()->setValue(block.firstLineNumber());
            }
        }
    }
    tab->preview()->scrollToHeading(index); // 放在後面，覆蓋同步捲動的比例位置
}

void MainWindow::toggleEditor(bool visible)
{
    // 編輯模式是全域設定：所有分頁一起切換，目前分頁另外對齊捲動位置與焦點
    DocumentTab *tab = currentTab();
    const double ratio = tab ? tab->preview()->scrollRatio() : 0.0;
    for (int i = 0; i < ui->tabs->count(); ++i)
        tabAt(i)->setEditorVisible(visible);
    updateCursorPosition();
    if (!tab)
        return;
    if (!visible) {
        tab->preview()->setFocus();
        return;
    }

    QSplitter *splitter = tab->splitter();
    if (splitter->sizes().value(0) < 80) {
        const int w = qMax(splitter->width(), 400);
        splitter->setSizes({ w / 2, w - w / 2 });
        m_splitterState = splitter->saveState();
    }
    tab->editor()->setFocus();
    // 編輯器版面要等顯示後才算得出捲軸範圍，延後一個事件迴圈再對齊位置
    QPointer<DocumentTab> guard(tab);
    QTimer::singleShot(0, this, [guard, ratio] {
        if (!guard)
            return;
        QScrollBar *sb = guard->editor()->verticalScrollBar();
        sb->setValue(qRound(ratio * sb->maximum()));
        guard->syncPreviewToEditor();
    });
}

void MainWindow::zoomBy(int delta)
{
    // 縮放是全域設定，所有分頁一起套用
    DocumentTab *tab = currentTab();
    if (tab) {
        tab->setZoom(m_zoomStep + delta, m_editorBasePointSize);
        m_zoomStep = tab->preview()->zoomStep(); // MarkdownView 會限制範圍
    } else {
        m_zoomStep += delta;
    }
    for (int i = 0; i < ui->tabs->count(); ++i)
        tabAt(i)->setZoom(m_zoomStep, m_editorBasePointSize);
}

// ============================================================================
// 搜尋
// ============================================================================

bool MainWindow::findTargetIsEditor() const
{
    return m_findInEditor && ui->actionToggleEditor->isChecked();
}

void MainWindow::startFind()
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    m_findInEditor = ui->actionToggleEditor->isChecked() && tab->editor()->hasFocus();
    const QString selected = m_findInEditor ? tab->editor()->textCursor().selectedText()
                                            : tab->preview()->textCursor().selectedText();
    ui->findBar->activate(selected);
    updateFindHighlights();
}

void MainWindow::find(bool backward, bool incremental)
{
    DocumentTab *tab = currentTab();
    const QString text = ui->findBar->text();
    if (!tab || text.isEmpty()) {
        clearFindHighlights(tab);
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (ui->findBar->caseSensitive())
        flags |= QTextDocument::FindCaseSensitively;

    if (findTargetIsEditor())
        findInEdit(tab->editor(), text, flags, incremental);
    else
        findInEdit(tab->preview(), text, flags, incremental);
    updateFindHighlights();
}

void MainWindow::updateFindHighlights()
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    const QString text = ui->findBar->text();
    const bool inEditor = findTargetIsEditor();
    QTextDocument *doc = inEditor ? tab->editor()->document() : tab->preview()->document();

    QList<QTextEdit::ExtraSelection> selections;
    int count = 0;
    if (!text.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(QColor(255, 200, 0, 140));
        const QTextDocument::FindFlags flags = ui->findBar->caseSensitive()
                                                   ? QTextDocument::FindCaseSensitively
                                                   : QTextDocument::FindFlags();
        QTextCursor c(doc);
        while (true) {
            c = doc->find(text, c, flags);
            if (c.isNull())
                break;
            if (++count <= 2000)
                selections.append({ c, fmt });
        }
    }

    if (inEditor) {
        tab->editor()->setExtraSelections(selections);
        tab->preview()->setExtraSelections({});
    } else {
        tab->preview()->setExtraSelections(selections);
        tab->editor()->setExtraSelections({});
    }
    ui->findBar->setResult(text.isEmpty() ? -1 : count);
}

void MainWindow::clearFindHighlights(DocumentTab *tab)
{
    if (tab) {
        tab->editor()->setExtraSelections({});
        tab->preview()->setExtraSelections({});
    }
    ui->findBar->setResult(-1);
}

// ============================================================================
// 標題列 / 狀態列 / 最近檔案 / 設定
// ============================================================================

void MainWindow::updateTitle()
{
    // 沒有開啟任何檔案（關閉檔案後、或啟動時沒有上次的檔案）只顯示程式名稱
    const DocumentTab *tab = currentTab();
    const bool open = tab && tab->hasDocument();
    setWindowTitle(open ? QStringLiteral("%1[*] — qMDView").arg(tab->displayName())
                        : QStringLiteral("qMDView[*]"));
    setWindowModified(tab && tab->isModified());
    ui->actionClose->setEnabled(open || ui->tabs->count() > 1);
}

void MainWindow::updateStatus()
{
    const DocumentTab *tab = currentTab();
    if (m_formatLabel)
        m_formatLabel->setText(tab && tab->hasDocument() ? tab->format().displayName() : QString());
    updateCursorPosition();
}

void MainWindow::updateCursorPosition()
{
    if (!m_cursorLabel)
        return;
    // 只有編輯器顯示時才有游標可言；行／欄皆從 1 起算，欄位以字元計（Tab 算 1 個字元）
    const DocumentTab *tab = currentTab();
    const bool show = tab && ui->actionToggleEditor->isChecked();
    m_cursorLabel->setVisible(show);
    if (!show)
        return;
    const QTextCursor c = tab->editor()->textCursor();
    m_cursorLabel->setText(tr("行 %1，欄 %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
}

void MainWindow::addRecentFile(const QString &path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > kMaxRecentFiles)
        m_recentFiles.removeLast();
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    ui->menuRecent->clear();
    if (m_recentFiles.isEmpty()) {
        ui->menuRecent->addAction(tr("（無）"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < m_recentFiles.size(); ++i) {
        const QString path = m_recentFiles.at(i);
        const QFileInfo fi(path);
        QString name = fi.fileName();
        name.replace(QLatin1Char('&'), QLatin1String("&&"));
        const QString text = i < 9 ? QStringLiteral("&%1  %2").arg(i + 1).arg(name)
                                   : QStringLiteral("%1  %2").arg(i + 1).arg(name);
        QAction *a = ui->menuRecent->addAction(text);
        a->setToolTip(QDir::toNativeSeparators(path));
        a->setStatusTip(QDir::toNativeSeparators(path));
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
    }
    ui->menuRecent->setToolTipsVisible(true);
    ui->menuRecent->addSeparator();
    connect(ui->menuRecent->addAction(tr("清除清單")), &QAction::triggered, this, [this] {
        m_recentFiles.clear();
        rebuildRecentMenu();
    });
}

void MainWindow::readSettings()
{
    // 在建立第一個分頁之前呼叫：分頁建立時會套用這裡讀到的檢視設定
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1200, 800);
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    m_splitterState = s.value(QStringLiteral("window/splitter")).toByteArray();

    m_recentFiles = s.value(QStringLiteral("recentFiles")).toStringList();
    rebuildRecentMenu();

    ui->actionSyncScroll->setChecked(s.value(QStringLiteral("view/syncScroll"), true).toBool());
    m_zoomStep = qBound(-6, s.value(QStringLiteral("view/zoom"), 0).toInt(), 24);
    ui->actionToggleEditor->setChecked(s.value(QStringLiteral("view/editor"), false).toBool());
}

void MainWindow::writeSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    if (const DocumentTab *tab = currentTab())
        m_splitterState = tab->splitter()->saveState();
    s.setValue(QStringLiteral("window/splitter"), m_splitterState);
    s.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    s.setValue(QStringLiteral("view/syncScroll"), ui->actionSyncScroll->isChecked());
    s.setValue(QStringLiteral("view/zoom"), m_zoomStep);
    s.setValue(QStringLiteral("view/editor"), ui->actionToggleEditor->isChecked());
    writeSession();
}

void MainWindow::writeSession()
{
    // session/files：所有分頁的檔案（依分頁順序）；session/current：目前分頁在清單中的位置
    QStringList files;
    int current = 0;
    const DocumentTab *cur = currentTab();
    for (int i = 0; i < ui->tabs->count(); ++i) {
        const DocumentTab *tab = tabAt(i);
        if (tab->filePath().isEmpty())
            continue;
        if (tab == cur)
            current = files.size();
        files.append(tab->filePath());
    }
    QSettings s;
    s.setValue(QStringLiteral("session/folder"), m_rootFolder);
    s.setValue(QStringLiteral("session/files"), files);
    s.setValue(QStringLiteral("session/current"), current);
    s.remove(QStringLiteral("session/file")); // 1.2.0 以前只記一個檔案
}

void MainWindow::restoreLastSession()
{
    QSettings s;
    const QString folder = s.value(QStringLiteral("session/folder")).toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        setRootFolder(folder);

    QStringList files;
    int current = 0;
    if (s.contains(QStringLiteral("session/files"))) {
        files = s.value(QStringLiteral("session/files")).toStringList();
        current = s.value(QStringLiteral("session/current"), 0).toInt();
    } else {
        // 從 1.2.0 以前的版本升級：只有一個檔案
        const QString file = s.value(QStringLiteral("session/file")).toString();
        if (!file.isEmpty())
            files.append(file);
    }

    DocumentTab *currentDoc = nullptr;
    for (int i = 0; i < files.size(); ++i) {
        if (!QFileInfo(files.at(i)).isFile())
            continue; // 已被刪除或移動的檔案略過，不跳出警告
        if (openFile(files.at(i)) && i == current)
            currentDoc = currentTab();
    }
    if (currentDoc)
        ui->tabs->setCurrentWidget(currentDoc);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::changeLanguage(const QString &preference)
{
    UiLanguage::setPreference(preference);
    if (UiLanguage::resolve(preference) == UiLanguage::current())
        return; // 實際使用的語言沒變（例如改成「跟隨系統」而系統本來就是這個語言）

    QMessageBox box(QMessageBox::Information, tr("語言"),
                    tr("介面語言會在重新啟動 qMDView 後套用。"), QMessageBox::NoButton, this);
    QPushButton *restartButton = box.addButton(tr("立即重新啟動"), QMessageBox::AcceptRole);
    box.addButton(tr("稍後"), QMessageBox::RejectRole);
    box.setDefaultButton(restartButton);
    box.exec();
    if (box.clickedButton() == restartButton)
        restartApplication();
}

bool MainWindow::opensInNewWindow()
{
    return QSettings().value(kOpenModeKey).toString() == QLatin1String("window");
}

void MainWindow::setOpenMode(bool newWindow)
{
    QSettings().setValue(kOpenModeKey, newWindow ? QStringLiteral("window") : QStringLiteral("tab"));
    // 之後啟動的 qMDView 才會讀到這個設定。改回「新分頁」時，如果目前沒有其他 qMDView
    // 在等待轉交，就由這個視窗接手，之後雙擊的檔案會開在這裡
    if (!newWindow && !m_instance) {
        auto *instance = new SingleInstance(this);
        if (instance->start({}, false) == SingleInstance::Result::Primary)
            setSingleInstance(instance);
        else
            delete instance;
    }
}

void MainWindow::setSingleInstance(SingleInstance *instance)
{
    m_instance = instance;
    if (!instance)
        return;
    instance->setParent(this);
    connect(instance, &SingleInstance::pathsReceived, this, [this](const QStringList &paths) {
        bringToFront();
        openPaths(paths);
    });
}

void MainWindow::bringToFront()
{
    if (isMinimized())
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
}

void MainWindow::restartApplication()
{
    // close() 會經過 closeEvent：有未儲存的變更時先詢問，使用者按「取消」就不重新啟動。
    // 關閉時已存下目前的資料夾與所有分頁，並停止接收轉交，新的執行個體會自動開回來
    if (!close())
        return;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

// ============================================================================
// 事件
// ============================================================================

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // 系統切換深色／淺色主題時，編輯器的語法上色也跟著換色（預覽由 MarkdownView 自行處理）
    // （用 findChildren：建構過程中 ui 的成員可能還沒建立）
    if (event->type() == QEvent::PaletteChange) {
        const auto tabs = findChildren<DocumentTab *>();
        for (DocumentTab *tab : tabs)
            tab->updateColors(palette());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 逐一詢問有未儲存變更的分頁；任何一個按「取消」就不關閉
    const int original = ui->tabs->currentIndex();
    for (int i = 0; i < ui->tabs->count(); ++i) {
        DocumentTab *tab = tabAt(i);
        if (!tab->isModified())
            continue;
        ui->tabs->setCurrentIndex(i);
        if (!maybeSave(tab)) {
            event->ignore();
            return;
        }
    }
    ui->tabs->setCurrentIndex(original);

    writeSettings();
    // 停止接收轉交：之後雙擊的檔案（或「立即重新啟動」）會由新的程式接手
    if (m_instance)
        m_instance->stop();
    event->accept();
}

QStringList MainWindow::markdownNameFilters()
{
    return { QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mdown"),
             QStringLiteral("*.mkd"), QStringLiteral("*.mkdn") };
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!DocumentTab::droppedPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!DocumentTab::droppedPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = DocumentTab::droppedPaths(event->mimeData());
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    openDropped(paths);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 分頁列：中鍵點分頁就關閉
    if (watched == ui->tabs->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::MiddleButton) {
            const int index = ui->tabs->tabBar()->tabAt(e->position().toPoint());
            if (QPointer<DocumentTab> tab = tabAt(index)) {
                QTimer::singleShot(0, this, [this, tab] {
                    if (tab)
                        closeTab(ui->tabs->indexOf(tab));
                });
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
