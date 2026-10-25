#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"
#include "findbar.h"
#include "markdownhighlighter.h"
#include "markdownview.h"
#include "tocwidget.h"
#include "uilanguage.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTextBlock>
#include <QUrl>

namespace {

constexpr int kMaxRecentFiles = 10;

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

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ---- 編輯器 ----
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_editorBasePointSize = qMax<qreal>(11.0, mono.pointSizeF());
    mono.setPointSizeF(m_editorBasePointSize);
    ui->editor->setFont(mono);
    ui->editor->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
    ui->editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ui->editor->viewport()->installEventFilter(this); // 攔截拖放進來的檔案
    m_highlighter = new MarkdownHighlighter(ui->editor->document());
    ui->editor->hide();

    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 1);

    // 左側兩個面板上下排列
    splitDockWidget(ui->dockFiles, ui->dockToc, Qt::Vertical);
    resizeDocks({ ui->dockFiles }, { 280 }, Qt::Horizontal);

    // ---- 計時器 ----
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(250);
    connect(&m_renderTimer, &QTimer::timeout, this, [this] { renderPreview(true); });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &MainWindow::onFileChangedOnDisk);

    // ---- 檔案監看 ----
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (path == m_currentFile)
            m_reloadTimer.start();
    });

    // ---- 編輯器 ↔ 預覽 ----
    connect(ui->editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading)
            m_renderTimer.start();
    });
    connect(ui->editor->document(), &QTextDocument::modificationChanged,
            this, &QWidget::setWindowModified);
    connect(ui->editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::syncPreviewToEditor);

    // ---- 預覽 ↔ 目錄 ----
    connect(ui->preview, &MarkdownView::headingsChanged, this, [this] {
        ui->tocTree->setHeadings(ui->preview->headings());
    });
    connect(ui->preview, &MarkdownView::rebuilt, this, [this] {
        if (ui->findBar->isVisible())
            updateFindHighlights(); // 重建後舊的醒目提示位置已失效
    });
    connect(ui->preview, &MarkdownView::currentHeadingChanged, ui->tocTree, &TocWidget::setCurrentHeading);
    connect(ui->tocTree, &TocWidget::headingActivated, this, &MainWindow::onHeadingActivated);
    connect(ui->preview, &MarkdownView::markdownLinkActivated, this,
            [this](const QString &path, const QString &fragment) { openFile(path, fragment); });
    connect(ui->preview, &MarkdownView::zoomRequested, this, &MainWindow::zoomBy);
    connect(ui->preview, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
        if (url.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(url.toString());
    });

    // ---- 搜尋列 ----
    connect(ui->findBar, &FindBar::findNext, this, [this] { find(false); });
    connect(ui->findBar, &FindBar::findPrevious, this, [this] { find(true); });
    connect(ui->findBar, &FindBar::searchChanged, this, [this] { find(false, true); });
    connect(ui->findBar, &FindBar::closed, this, [this] {
        clearFindHighlights();
        if (findTargetIsEditor())
            ui->editor->setFocus();
        else
            ui->preview->setFocus();
    });

    m_formatLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_formatLabel);

    setupActions();
    setupLanguageMenu();
    setupFileTree();
    readSettings();
    updateTitle();
    updateStatus();
}

MainWindow::~MainWindow()
{
    ui->editor->viewport()->removeEventFilter(this);
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
    ui->actionToggleEditor->setIcon(themed("document-edit", QStyle::SP_FileDialogDetailedView));
    ui->actionFind->setIcon(themed("edit-find", QStyle::SP_FileDialogContentsView));

    ui->actionZoomIn->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal) });
    ui->actionZoomOut->setShortcuts(QKeySequence::ZoomOut);

    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(ui->actionOpenFolder, &QAction::triggered, this, &MainWindow::openFolderDialog);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::save);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveAs);
    connect(ui->actionReload, &QAction::triggered, this, &MainWindow::reload);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionToggleEditor, &QAction::toggled, this, &MainWindow::toggleEditor);
    connect(ui->actionSyncScroll, &QAction::toggled, this, [this](bool on) {
        if (on)
            syncPreviewToEditor();
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
    connect(ui->actionZoomReset, &QAction::triggered, this, [this] { zoomBy(-ui->preview->zoomStep()); });

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

bool MainWindow::openFile(const QString &path, const QString &fragment)
{
    const QFileInfo fi(path);
    const QString absPath = fi.absoluteFilePath();

    // 已經是目前的檔案：只處理錨點
    if (!m_currentFile.isEmpty() && QFileInfo(m_currentFile) == fi) {
        if (!fragment.isEmpty())
            ui->preview->scrollToFragment(fragment);
        return true;
    }

    if (!fi.isFile()) {
        QMessageBox::warning(this, tr("開啟檔案"),
                             tr("找不到檔案：\n%1").arg(QDir::toNativeSeparators(absPath)));
        if (m_recentFiles.removeAll(absPath) > 0)
            rebuildRecentMenu();
        return false;
    }
    if (!maybeSave())
        return false;

    const TextFileContent content = TextFileIO::read(absPath);
    if (!content.ok) {
        QMessageBox::warning(this, tr("開啟檔案"),
                             tr("無法讀取 %1：\n%2").arg(QDir::toNativeSeparators(absPath), content.error));
        return false;
    }

    m_format = content.format;
    setCurrentFile(absPath);
    applyContent(content, false);
    addRecentFile(absPath);

    if (m_rootFolder.isEmpty())
        setRootFolder(fi.absolutePath());
    else
        selectCurrentInTree();

    if (!fragment.isEmpty())
        ui->preview->scrollToFragment(fragment);

    statusBar()->showMessage(tr("已開啟 %1").arg(QDir::toNativeSeparators(absPath)), 3000);
    return true;
}

void MainWindow::applyContent(const TextFileContent &content, bool keepScroll)
{
    m_loading = true;
    const int editorScroll = ui->editor->verticalScrollBar()->value();
    const int cursorPos = ui->editor->textCursor().position();

    ui->editor->setPlainText(content.text);
    ui->editor->document()->setModified(false);

    if (keepScroll) {
        QTextCursor c = ui->editor->textCursor();
        c.setPosition(qMin(cursorPos, ui->editor->document()->characterCount() - 1));
        ui->editor->setTextCursor(c);
        ui->editor->verticalScrollBar()->setValue(editorScroll);
    }
    m_loading = false;

    m_renderTimer.stop();
    renderPreview(keepScroll);
    updateTitle();
    updateStatus();
}

void MainWindow::setCurrentFile(const QString &path)
{
    if (!m_currentFile.isEmpty() && m_watcher->files().contains(m_currentFile))
        m_watcher->removePath(m_currentFile);

    m_currentFile = path;
    watchFile(path);
    updateTitle();
}

void MainWindow::watchFile(const QString &path)
{
    if (!path.isEmpty() && QFileInfo::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_format = TextFormat();
    setCurrentFile(QString());
    TextFileContent empty;
    empty.ok = true;
    applyContent(empty, false);
    ui->actionToggleEditor->setChecked(true);
    ui->editor->setFocus();
}

void MainWindow::openFileDialog()
{
    QString dir = !m_currentFile.isEmpty() ? QFileInfo(m_currentFile).absolutePath() : m_rootFolder;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString path = QFileDialog::getOpenFileName(
        this, tr("開啟 Markdown 檔案"), dir,
        tr("Markdown 檔案 (*.md *.markdown *.mdown *.mkd *.mkdn);;文字檔 (*.txt);;所有檔案 (*)"));
    if (!path.isEmpty())
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
    if (m_currentFile.isEmpty())
        return saveAs();
    return saveToFile(m_currentFile);
}

bool MainWindow::saveAs()
{
    QString initial = m_currentFile;
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
    return saveToFile(path);
}

bool MainWindow::saveToFile(const QString &path)
{
    TextFormat fmt = m_format;
    QString error;
    bool unencodable = false;

    if (!TextFileIO::write(path, ui->editor->toPlainText(), fmt, &error, &unencodable)) {
        if (!unencodable) {
            QMessageBox::warning(this, tr("儲存"), tr("無法儲存檔案：\n%1").arg(error));
            return false;
        }
        if (QMessageBox::question(this, tr("儲存"), tr("%1\n要改用 UTF-8 儲存嗎？").arg(error))
            != QMessageBox::Yes)
            return false;
        fmt.encoding = QStringConverter::Utf8;
        fmt.bom = false;
        if (!TextFileIO::write(path, ui->editor->toPlainText(), fmt, &error)) {
            QMessageBox::warning(this, tr("儲存"), tr("無法儲存檔案：\n%1").arg(error));
            return false;
        }
    }

    m_format = fmt;
    const QFileInfo fi(path);
    const QString absPath = fi.absoluteFilePath();
    const bool isNewPath = m_currentFile.isEmpty() || QFileInfo(m_currentFile) != fi;

    if (isNewPath)
        setCurrentFile(absPath);
    else
        watchFile(absPath); // QSaveFile 以取代方式寫入，監看可能被移除

    ui->editor->document()->setModified(false);
    addRecentFile(absPath);

    if (isNewPath) {
        renderPreview(true); // 相對路徑的基準資料夾可能改變
        if (m_rootFolder.isEmpty())
            setRootFolder(fi.absolutePath());
        else
            selectCurrentInTree();
    }
    updateTitle();
    updateStatus();
    statusBar()->showMessage(tr("已儲存 %1").arg(QDir::toNativeSeparators(absPath)), 3000);
    return true;
}

void MainWindow::reload()
{
    if (m_currentFile.isEmpty())
        return;
    if (ui->editor->document()->isModified()
        && QMessageBox::question(this, tr("重新載入"), tr("放棄未儲存的變更並重新載入？"))
               != QMessageBox::Yes)
        return;

    const TextFileContent content = TextFileIO::read(m_currentFile);
    if (!content.ok) {
        QMessageBox::warning(this, tr("重新載入"), content.error);
        return;
    }
    m_format = content.format;
    applyContent(content, true);
    watchFile(m_currentFile);
    statusBar()->showMessage(tr("已重新載入"), 2000);
}

bool MainWindow::maybeSave()
{
    if (!ui->editor->document()->isModified())
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("尚未儲存"), tr("「%1」有未儲存的變更，要儲存嗎？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (ret == QMessageBox::Save)
        return save();
    return ret == QMessageBox::Discard;
}

void MainWindow::onFileChangedOnDisk()
{
    if (m_currentFile.isEmpty() || m_askingReload)
        return;

    const QFileInfo fi(m_currentFile);
    if (!fi.exists()) {
        // 有些編輯器存檔時是「刪除 → 重建」，稍等一下再確認一次
        QTimer::singleShot(1000, this, [this, path = m_currentFile] {
            if (path != m_currentFile)
                return;
            if (QFileInfo::exists(path))
                m_reloadTimer.start();
            else
                statusBar()->showMessage(tr("檔案已被刪除或移動：%1").arg(QDir::toNativeSeparators(path)));
        });
        return;
    }
    watchFile(m_currentFile); // 被取代的檔案要重新加入監看

    TextFileContent content = TextFileIO::read(m_currentFile);
    if (!content.ok || content.text == ui->editor->toPlainText())
        return; // 內容沒變（例如自己存檔觸發的通知）

    if (ui->editor->document()->isModified()) {
        m_askingReload = true;
        const auto ret = QMessageBox::question(
            this, tr("檔案已變更"),
            tr("「%1」已在外部被修改。\n要重新載入嗎？編輯器中未儲存的變更將會遺失。").arg(fi.fileName()));
        m_askingReload = false;
        if (ret != QMessageBox::Yes)
            return;
        content = TextFileIO::read(m_currentFile); // 對話框開著時檔案可能又變了
        if (!content.ok)
            return;
    }

    m_format = content.format;
    applyContent(content, true);
    statusBar()->showMessage(tr("檔案已在外部更新，已自動重新載入"), 3000);
}

QString MainWindow::displayName() const
{
    return m_currentFile.isEmpty() ? tr("未命名") : QFileInfo(m_currentFile).fileName();
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
    if (m_currentFile.isEmpty() || m_rootFolder.isEmpty() || ui->fileTree->model() != m_fsModel)
        return;
    const QString rel = QDir(m_rootFolder).relativeFilePath(m_currentFile);
    if (rel.startsWith(QLatin1String("..")))
        return; // 不在目前資料夾內
    const QModelIndex index = m_fsModel->index(m_currentFile);
    if (index.isValid()) {
        ui->fileTree->setCurrentIndex(index);
        ui->fileTree->scrollTo(index);
    }
}

// ============================================================================
// 預覽 / 目錄
// ============================================================================

QString MainWindow::previewBaseDir() const
{
    if (!m_currentFile.isEmpty())
        return QFileInfo(m_currentFile).absolutePath();
    return m_rootFolder;
}

void MainWindow::renderPreview(bool keepScroll)
{
    ui->preview->renderMarkdown(ui->editor->toPlainText(), previewBaseDir(), keepScroll);
    syncPreviewToEditor();
    if (ui->findBar->isVisible())
        updateFindHighlights();
}

void MainWindow::syncPreviewToEditor()
{
    if (!ui->actionToggleEditor->isChecked() || !ui->actionSyncScroll->isChecked())
        return;
    const QScrollBar *sb = ui->editor->verticalScrollBar();
    ui->preview->setScrollRatio(sb->maximum() > 0 ? double(sb->value()) / sb->maximum() : 0.0);
}

void MainWindow::onHeadingActivated(int index)
{
    // 編輯模式下，編輯器也跳到對應的標題行
    if (ui->actionToggleEditor->isChecked()) {
        const QList<int> lines = MarkdownView::headingSourceLines(ui->editor->toPlainText());
        if (index >= 0 && index < lines.size()) {
            const QTextBlock block = ui->editor->document()->findBlockByNumber(lines.at(index));
            if (block.isValid()) {
                ui->editor->setTextCursor(QTextCursor(block));
                ui->editor->verticalScrollBar()->setValue(block.firstLineNumber());
            }
        }
    }
    ui->preview->scrollToHeading(index); // 放在後面，覆蓋同步捲動的比例位置
}

void MainWindow::toggleEditor(bool visible)
{
    const double ratio = ui->preview->scrollRatio();
    ui->editor->setVisible(visible);
    if (!visible) {
        ui->preview->setFocus();
        return;
    }

    const QList<int> sizes = ui->splitter->sizes();
    if (sizes.value(0) < 80) {
        const int w = qMax(ui->splitter->width(), 400);
        ui->splitter->setSizes({ w / 2, w - w / 2 });
    }
    ui->editor->setFocus();
    // 編輯器版面要等顯示後才算得出捲軸範圍，延後一個事件迴圈再對齊位置
    QTimer::singleShot(0, this, [this, ratio] {
        QScrollBar *sb = ui->editor->verticalScrollBar();
        sb->setValue(qRound(ratio * sb->maximum()));
        syncPreviewToEditor();
    });
}

void MainWindow::zoomBy(int delta)
{
    ui->preview->setZoomStep(ui->preview->zoomStep() + delta);
    QFont f = ui->editor->font();
    f.setPointSizeF(qMax<qreal>(6.0, m_editorBasePointSize + ui->preview->zoomStep()));
    ui->editor->setFont(f);
    ui->editor->setTabStopDistance(4 * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));
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
    m_findInEditor = ui->actionToggleEditor->isChecked() && ui->editor->hasFocus();
    const QString selected = m_findInEditor ? ui->editor->textCursor().selectedText()
                                            : ui->preview->textCursor().selectedText();
    ui->findBar->activate(selected);
    updateFindHighlights();
}

void MainWindow::find(bool backward, bool incremental)
{
    const QString text = ui->findBar->text();
    if (text.isEmpty()) {
        clearFindHighlights();
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (ui->findBar->caseSensitive())
        flags |= QTextDocument::FindCaseSensitively;

    if (findTargetIsEditor())
        findInEdit(ui->editor, text, flags, incremental);
    else
        findInEdit(ui->preview, text, flags, incremental);
    updateFindHighlights();
}

void MainWindow::updateFindHighlights()
{
    const QString text = ui->findBar->text();
    const bool inEditor = findTargetIsEditor();
    QTextDocument *doc = inEditor ? ui->editor->document() : ui->preview->document();

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
        ui->editor->setExtraSelections(selections);
        ui->preview->setExtraSelections({});
    } else {
        ui->preview->setExtraSelections(selections);
        ui->editor->setExtraSelections({});
    }
    ui->findBar->setResult(text.isEmpty() ? -1 : count);
}

void MainWindow::clearFindHighlights()
{
    ui->editor->setExtraSelections({});
    ui->preview->setExtraSelections({});
    ui->findBar->setResult(-1);
}

// ============================================================================
// 標題列 / 狀態列 / 最近檔案 / 設定
// ============================================================================

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1[*] — qMDView").arg(displayName()));
    setWindowModified(ui->editor->document()->isModified());
}

void MainWindow::updateStatus()
{
    m_formatLabel->setText(m_format.displayName());
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
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1200, 800);
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    ui->splitter->restoreState(s.value(QStringLiteral("window/splitter")).toByteArray());

    m_recentFiles = s.value(QStringLiteral("recentFiles")).toStringList();
    rebuildRecentMenu();

    ui->actionSyncScroll->setChecked(s.value(QStringLiteral("view/syncScroll"), true).toBool());
    const int zoom = s.value(QStringLiteral("view/zoom"), 0).toInt();
    if (zoom != 0)
        zoomBy(zoom);
    ui->actionToggleEditor->setChecked(s.value(QStringLiteral("view/editor"), false).toBool());
}

void MainWindow::writeSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("window/splitter"), ui->splitter->saveState());
    s.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    s.setValue(QStringLiteral("view/syncScroll"), ui->actionSyncScroll->isChecked());
    s.setValue(QStringLiteral("view/zoom"), ui->preview->zoomStep());
    s.setValue(QStringLiteral("view/editor"), ui->actionToggleEditor->isChecked());
    s.setValue(QStringLiteral("session/folder"), m_rootFolder);
    s.setValue(QStringLiteral("session/file"), m_currentFile);
}

void MainWindow::restoreLastSession()
{
    QSettings s;
    const QString folder = s.value(QStringLiteral("session/folder")).toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        setRootFolder(folder);
    const QString file = s.value(QStringLiteral("session/file")).toString();
    if (!file.isEmpty() && QFileInfo(file).isFile())
        openFile(file);
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

void MainWindow::restartApplication()
{
    // close() 會經過 closeEvent：有未儲存的變更時先詢問，使用者按「取消」就不重新啟動。
    // 關閉時已存下目前的資料夾與檔案，新的執行個體會自動開回來
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
    if (event->type() == QEvent::PaletteChange && m_highlighter)
        m_highlighter->updateColors(palette());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    writeSettings();
    event->accept();
}

QStringList MainWindow::markdownNameFilters()
{
    return { QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mdown"),
             QStringLiteral("*.mkd"), QStringLiteral("*.mkdn") };
}

bool MainWindow::isMarkdownFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList accepted = { QStringLiteral("md"), QStringLiteral("markdown"),
                                          QStringLiteral("mdown"), QStringLiteral("mkd"),
                                          QStringLiteral("mkdn"), QStringLiteral("txt") };
    return accepted.contains(suffix);
}

QString MainWindow::dropTarget(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return QString();
    const auto urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        const QFileInfo fi(path);
        if (fi.isDir() || isMarkdownFile(path))
            return path;
    }
    return QString();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!dropTarget(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!dropTarget(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString path = dropTarget(event->mimeData());
    if (path.isEmpty())
        return;
    event->acceptProposedAction();
    // 延後處理：在拖放事件中跳出對話框（例如詢問是否存檔）在 Windows 上會卡住拖放來源
    QTimer::singleShot(0, this, [this, path] { openPath(path); });
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 編輯器本身接受文字拖放；拖進來的是檔案時改成開檔
    if (watched == ui->editor->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (!dropTarget(e->mimeData()).isEmpty()) {
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto *e = static_cast<QDropEvent *>(event);
            const QString path = dropTarget(e->mimeData());
            if (!path.isEmpty()) {
                e->acceptProposedAction();
                QTimer::singleShot(0, this, [this, path] { openPath(path); });
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
