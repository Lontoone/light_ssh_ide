#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextDocument>

class SimpleSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit SimpleSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setFilePath(const QString &path);
    QString filePath() const;
    void loadText(const QString &text);
    QString currentText() const;

private:
    QString m_filePath;
    SimpleSyntaxHighlighter *m_highlighter = nullptr;
};
