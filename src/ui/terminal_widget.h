#pragma once

#include <QPlainTextEdit>
#include <QProcess>

class TerminalWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void startLocalShell();
    void closeTerminal();
    bool isRunning() const;

    void appendTerminalText(const QString &text);
    void setPromptText(const QString &prompt);

signals:
    void terminalClosed();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void readProcessOutput();
    void readProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess *m_process = nullptr;
    QString m_promptText;
    QString m_inputBuffer;
    void updateInputDisplay();
    void writeToShell(const QByteArray &data);
};
