#include "aboutdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// logo 圖檔本身的底色；外框用同色，讓圖片與內距融為一張圓角卡片
const char *const kLogoBackground = "#F5ECDC";
const char *const kQtSourceUrl = "https://www.qt.io/download-open-source";

QString readResource(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("關於 qMDView"));

    // ---- SAISAI.Dev logo ----
    auto *logo = new QLabel(this);
    logo->setPixmap(QPixmap(QStringLiteral(":/images/saisai_logo.png")));
    logo->setAlignment(Qt::AlignCenter);
    logo->setToolTip(QStringLiteral(APP_PUBLISHER));
    logo->setStyleSheet(QStringLiteral("QLabel { background: %1; border-radius: 10px; padding: 6px 18px; }")
                            .arg(QLatin1String(kLogoBackground)));
    logo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    // ---- 程式名稱與版本 ----
    auto *appIcon = new QLabel(this);
    appIcon->setPixmap(QIcon(QStringLiteral(":/icons/app.png")).pixmap(64, 64));
    appIcon->setAlignment(Qt::AlignTop);

    auto *title = new QLabel(this);
    title->setTextFormat(Qt::RichText);
    title->setText(QStringLiteral("<div style='font-size:18pt; font-weight:600;'>qMDView</div>"
                                  "<div>%1 %2</div><div>%3</div>")
                       .arg(tr("版本"), QStringLiteral(APP_VERSION), tr("Markdown 檢視／編輯器")));

    auto *header = new QHBoxLayout;
    header->setSpacing(14);
    header->addWidget(appIcon);
    header->addWidget(title, 1);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    // ---- 著作權與授權聲明 ----
    auto *copyright = new QLabel(
        tr("<b>© %1 %2</b>　版權所有")
            .arg(QStringLiteral(APP_COPYRIGHT_YEAR), QStringLiteral(APP_PUBLISHER)),
        this);
    copyright->setTextFormat(Qt::RichText);

    auto *contact = new QLabel(
        tr("聯絡信箱：<a href=\"mailto:%1\">%1</a>").arg(QStringLiteral(APP_CONTACT_EMAIL)), this);
    contact->setTextFormat(Qt::RichText);
    contact->setOpenExternalLinks(true); // 點擊開啟預設郵件程式
    contact->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *notice = new QLabel(this);
    notice->setTextFormat(Qt::RichText);
    notice->setWordWrap(true);
    notice->setOpenExternalLinks(true);
    notice->setTextInteractionFlags(Qt::TextBrowserInteraction);
    notice->setMinimumWidth(440); // 對話框為固定大小，給文字足夠寬度避免斷行太碎
    notice->setText(tr("本軟體使用 Qt %1 開發，Qt 依 GNU LGPL v3 授權使用。<br>"
                       "Qt 原始碼：<a href=\"%2\">%2</a><br>"
                       "其他第三方元件與授權全文請見「授權資訊」。")
                        .arg(QString::fromLatin1(qVersion()), QLatin1String(kQtSourceUrl)));

    // ---- 按鈕 ----
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *licenseButton = buttons->addButton(tr("授權資訊…"), QDialogButtonBox::ActionRole);
    QPushButton *aboutQtButton = buttons->addButton(tr("關於 Qt…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(licenseButton, &QPushButton::clicked, this, &AboutDialog::showLicenses);
    connect(aboutQtButton, &QPushButton::clicked, qApp, &QApplication::aboutQt);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(4);
    layout->addLayout(header);
    layout->addWidget(line);
    layout->addWidget(copyright);
    layout->addWidget(contact);
    layout->addWidget(notice);
    layout->addSpacing(4);
    layout->addWidget(buttons);

    layout->setSizeConstraint(QLayout::SetFixedSize);
}

void AboutDialog::showLicenses()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("授權資訊"));
    dialog.resize(760, 560);

    auto *tabs = new QTabWidget(&dialog);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto addTab = [&](const QString &title, const QString &resource) {
        auto *view = new QPlainTextEdit(tabs);
        view->setReadOnly(true);
        view->setFont(mono);
        view->setPlainText(readResource(resource));
        tabs->addTab(view, title);
    };
    addTab(tr("第三方元件"), QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.txt"));
    addTab(QStringLiteral("GNU LGPL v3"), QStringLiteral(":/licenses/LGPL-3.0.txt"));
    addTab(QStringLiteral("GNU GPL v3"), QStringLiteral(":/licenses/GPL-3.0.txt"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    dialog.exec();
}
