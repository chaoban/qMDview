#include "tocwidget.h"

#include <QHeaderView>
#include <QSignalBlocker>

TocWidget::TocWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(false);
    setIndentation(14);
    setTextElideMode(Qt::ElideRight);

    auto activate = [this](QTreeWidgetItem *item) {
        if (item)
            emit headingActivated(item->data(0, Qt::UserRole).toInt());
    };
    connect(this, &QTreeWidget::itemClicked, this, activate);
    connect(this, &QTreeWidget::itemActivated, this, activate);
}

void TocWidget::setHeadings(const QList<MarkdownHeading> &headings)
{
    // 編輯時每次重繪都會呼叫；標題結構沒變就不重建，避免收合狀態與捲動位置跳動
    QStringList signature;
    signature.reserve(headings.size());
    for (const MarkdownHeading &h : headings)
        signature.append(QString::number(h.level) + QLatin1Char('|') + h.text);
    if (signature == m_signature && m_items.size() == headings.size())
        return;
    m_signature = signature;

    const QSignalBlocker blocker(this);
    clear();
    m_items.clear();
    m_items.reserve(headings.size());

    // 以堆疊維護目前的父節點鏈，處理 H1 → H3 這類跳級的情況
    struct Node { int level; QTreeWidgetItem *item; };
    QVector<Node> stack;

    for (int i = 0; i < headings.size(); ++i) {
        const MarkdownHeading &h = headings.at(i);
        while (!stack.isEmpty() && stack.last().level >= h.level)
            stack.removeLast();

        QTreeWidgetItem *item = stack.isEmpty() ? new QTreeWidgetItem(this)
                                                : new QTreeWidgetItem(stack.last().item);
        item->setText(0, h.text.isEmpty() ? tr("(無標題)") : h.text);
        item->setToolTip(0, h.text);
        item->setData(0, Qt::UserRole, i);
        if (h.level == 1) {
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        }
        m_items.append(item);
        stack.append({ h.level, item });
    }
    expandAll();
}

void TocWidget::setCurrentHeading(int index)
{
    const QSignalBlocker blocker(this);
    if (index < 0 || index >= m_items.size()) {
        clearSelection();
        setCurrentItem(nullptr);
        return;
    }
    QTreeWidgetItem *item = m_items.at(index);
    setCurrentItem(item);
    scrollToItem(item);
}
