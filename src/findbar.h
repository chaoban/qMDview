#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;

// 視窗底部的搜尋列（Ctrl+F 開啟、Esc 關閉、Enter 下一個、Shift+Enter 上一個）
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void activate(const QString &initialText = QString());
    QString text() const;
    bool caseSensitive() const;
    void setResult(int matchCount); // -1 表示清除提示

signals:
    void findNext();
    void findPrevious();
    void searchChanged();  // 文字或選項改變（即時搜尋）
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *m_edit;
    QCheckBox *m_case;
    QLabel *m_status;
};
