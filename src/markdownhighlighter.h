#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class QPalette;

// 編輯器用的簡易 Markdown 語法上色（標題、粗斜體、行內碼、程式碼區塊、連結、引用、清單）
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *parent);

    void updateColors(const QPalette &palette);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule { QRegularExpression pattern; QTextCharFormat format; int group = 0; };

    QVector<Rule> m_rules;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_ruleFormat;
};
