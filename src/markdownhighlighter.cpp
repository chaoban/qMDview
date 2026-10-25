#include "markdownhighlighter.h"

#include <QApplication>
#include <QPalette>

namespace {
enum BlockState { Normal = 0, InBacktickFence = 1, InTildeFence = 2 };

QColor pick(bool dark, const char *light, const char *darkColor)
{
    return QColor(QLatin1String(dark ? darkColor : light));
}
} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    updateColors(QApplication::palette());
}

void MarkdownHighlighter::updateColors(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    const bool dark = base.lightness() < 128;

    m_rules.clear();
    auto addRule = [this](const QString &pattern, const QTextCharFormat &fmt, int group = 0) {
        m_rules.append({ QRegularExpression(pattern), fmt, group });
    };

    m_headingFormat = QTextCharFormat();
    m_headingFormat.setForeground(pick(dark, "#0550ae", "#79c0ff"));
    m_headingFormat.setFontWeight(QFont::Bold);

    m_codeBlockFormat = QTextCharFormat();
    m_codeBlockFormat.setForeground(pick(dark, "#116329", "#7ee787"));
    m_codeBlockFormat.setBackground(pick(dark, "#f3f5f7", "#1c2128"));

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(pick(dark, "#57606a", "#8b949e"));

    m_ruleFormat = QTextCharFormat();
    m_ruleFormat.setForeground(pick(dark, "#8c959f", "#6e7681"));

    QTextCharFormat listMarker;
    listMarker.setForeground(pick(dark, "#cf222e", "#ff7b72"));
    listMarker.setFontWeight(QFont::Bold);

    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);

    QTextCharFormat italic;
    italic.setFontItalic(true);

    QTextCharFormat strike;
    strike.setFontStrikeOut(true);

    QTextCharFormat inlineCode;
    inlineCode.setForeground(pick(dark, "#116329", "#7ee787"));

    QTextCharFormat link;
    link.setForeground(pick(dark, "#0969da", "#58a6ff"));

    QTextCharFormat html;
    html.setForeground(pick(dark, "#8250df", "#d2a8ff"));

    // 順序有意義：後面的規則會覆蓋前面的（例如行內碼裡的 * 不該被當成斜體）
    addRule(QStringLiteral("^\\s*([-*+]|\\d+[.)])\\s(\\[[ xX]\\]\\s)?"), listMarker);
    addRule(QStringLiteral("\\*\\*[^*\\n]+\\*\\*|__[^_\\n]+__"), bold);
    addRule(QStringLiteral("(?<![*\\w])\\*[^*\\s][^*\\n]*\\*(?!\\*)|(?<![_\\w])_[^_\\s][^_\\n]*_(?![_\\w])"), italic);
    addRule(QStringLiteral("~~[^~\\n]+~~"), strike);
    addRule(QStringLiteral("</?[A-Za-z][^>\\n]*>"), html);
    addRule(QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^)\\n]*\\)|<https?://[^>\\s]+>|https?://[^\\s)>]+"), link);
    addRule(QStringLiteral("`[^`\\n]+`"), inlineCode);

    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression heading(QStringLiteral("^ {0,3}#{1,6}(\\s|$)"));
    static const QRegularExpression quote(QStringLiteral("^ {0,3}>"));
    static const QRegularExpression hr(QStringLiteral("^ {0,3}([-*_])([ \\t]*\\1){2,}[ \\t]*$"));

    const int prev = previousBlockState();
    const QRegularExpressionMatch fm = fence.match(text);

    // 程式碼區塊內：整行套用程式碼格式，遇到相同圍欄字元才結束
    if (prev == InBacktickFence || prev == InTildeFence) {
        setFormat(0, int(text.size()), m_codeBlockFormat);
        const QChar want = prev == InBacktickFence ? QLatin1Char('`') : QLatin1Char('~');
        setCurrentBlockState(fm.hasMatch() && fm.captured(1).at(0) == want ? Normal : prev);
        return;
    }
    if (fm.hasMatch()) {
        setFormat(0, int(text.size()), m_codeBlockFormat);
        setCurrentBlockState(fm.captured(1).at(0) == QLatin1Char('`') ? InBacktickFence : InTildeFence);
        return;
    }
    setCurrentBlockState(Normal);

    if (hr.match(text).hasMatch()) {
        setFormat(0, int(text.size()), m_ruleFormat);
        return;
    }
    if (heading.match(text).hasMatch())
        setFormat(0, int(text.size()), m_headingFormat);
    else if (quote.match(text).hasMatch())
        setFormat(0, int(text.size()), m_quoteFormat);

    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QTextCharFormat merged = format(int(m.capturedStart(rule.group)));
            merged.merge(rule.format);
            setFormat(int(m.capturedStart(rule.group)), int(m.capturedLength(rule.group)), merged);
        }
    }
}
