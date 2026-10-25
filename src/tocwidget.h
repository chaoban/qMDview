#pragma once

#include "markdownview.h"

#include <QTreeWidget>
#include <QVector>

// 文件大綱：依標題層級建立樹狀結構，點選後跳到該標題
class TocWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit TocWidget(QWidget *parent = nullptr);

    void setHeadings(const QList<MarkdownHeading> &headings);
    void setCurrentHeading(int index); // 只更新選取狀態，不發出 headingActivated

signals:
    void headingActivated(int index);

private:
    QVector<QTreeWidgetItem *> m_items; // 依標題索引對應的項目
    QStringList m_signature;            // 上次的「層級+標題」，沒變就不重建（保留展開狀態）
};
