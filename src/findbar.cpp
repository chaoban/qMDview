#include "findbar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_case(new QCheckBox(tr("大小寫"), this))
    , m_status(new QLabel(this))
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // 只佔一列高度

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    m_edit->setPlaceholderText(tr("搜尋…"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setMinimumWidth(240);
    m_edit->installEventFilter(this);

    auto makeButton = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton *prev  = makeButton(QStringLiteral("▲"), tr("上一個 (Shift+Enter)"));
    QToolButton *next  = makeButton(QStringLiteral("▼"), tr("下一個 (Enter)"));
    QToolButton *close = makeButton(QStringLiteral("✕"), tr("關閉 (Esc)"));

    layout->addWidget(new QLabel(tr("搜尋"), this));
    layout->addWidget(m_edit);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(m_case);
    layout->addWidget(m_status);
    layout->addStretch(1);
    layout->addWidget(close);

    connect(prev, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(close, &QToolButton::clicked, this, [this] { hide(); emit closed(); });
    connect(m_edit, &QLineEdit::textChanged, this, &FindBar::searchChanged);
    connect(m_case, &QCheckBox::toggled, this, &FindBar::searchChanged);

    hide();
}

void FindBar::activate(const QString &initialText)
{
    show();
    if (!initialText.isEmpty() && !initialText.contains(QChar::ParagraphSeparator))
        m_edit->setText(initialText);
    m_edit->setFocus();
    m_edit->selectAll();
}

QString FindBar::text() const { return m_edit->text(); }

bool FindBar::caseSensitive() const { return m_case->isChecked(); }

void FindBar::setResult(int matchCount)
{
    if (matchCount < 0 || m_edit->text().isEmpty()) {
        m_status->clear();
        m_edit->setStyleSheet(QString());
    } else if (matchCount == 0) {
        m_status->setText(tr("找不到"));
        m_edit->setStyleSheet(QStringLiteral("QLineEdit { background: rgba(255, 80, 80, 60); }"));
    } else {
        m_status->setText(tr("%n 筆", nullptr, matchCount));
        m_edit->setStyleSheet(QString());
    }
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (ke->modifiers() & Qt::ShiftModifier)
                emit findPrevious();
            else
                emit findNext();
            return true;
        case Qt::Key_Escape:
            hide();
            emit closed();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
