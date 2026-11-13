#include "ui/code_editor.h"

#include <QBrush>
#include <QColor>
#include <QRegularExpression>

SimpleSyntaxHighlighter::SimpleSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

void SimpleSyntaxHighlighter::highlightBlock(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith("#") || trimmed.startsWith("//")) {
        setFormat(0, text.length(), QColor(126, 140, 150));
        return;
    }

    static const QRegularExpression keywordPattern(
        QStringLiteral("\\b(class|struct|if|else|for|while|return|true|false|switch|case|break|continue|using|namespace|int|float|double|bool|char|void|const|auto|static|public|private|protected)\\b"));
    static const QRegularExpression stringPattern(QStringLiteral("\"([^\\\\\"]|\\\\.)*\"|'([^\\\\']|\\\\.)*'"));
    static const QRegularExpression numberPattern(QStringLiteral("\\b[0-9]+\\b"));

    auto highlightPattern = [&](const QRegularExpression &pattern, const QColor &color) {
        auto matches = pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), color);
        }
    };

    highlightPattern(keywordPattern, QColor(111, 168, 255));
    highlightPattern(stringPattern, QColor(191, 223, 127));
    highlightPattern(numberPattern, QColor(234, 174, 103));

    if (text.contains("//")) {
        const int commentIndex = text.indexOf(QStringLiteral("//"));
        setFormat(commentIndex, text.length() - commentIndex, QColor(126, 140, 150));
    }
}

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    m_highlighter = new SimpleSyntaxHighlighter(document());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(4 * QFontMetrics(font()).horizontalAdvance(' '));
}

void CodeEditor::setFilePath(const QString &path)
{
    m_filePath = path;
}

QString CodeEditor::filePath() const
{
    return m_filePath;
}

void CodeEditor::loadText(const QString &text)
{
    setPlainText(text);
}

QString CodeEditor::currentText() const
{
    return toPlainText();
}
