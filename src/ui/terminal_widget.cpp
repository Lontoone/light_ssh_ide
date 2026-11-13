#include "ui/terminal_widget.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTextCursor>

TerminalWidget::TerminalWidget(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setPlaceholderText("Terminal ready");
    setMaximumBlockCount(2000);
    setUndoRedoEnabled(false);
    setAcceptDrops(true);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readProcessOutput);
    connect(m_process, &QProcess::finished, this, &TerminalWidget::onProcessFinished);

    startLocalShell();
}

TerminalWidget::~TerminalWidget()
{
    closeTerminal();
}

void TerminalWidget::startLocalShell()
{
    if (m_process->state() == QProcess::Running) {
        return;
    }

    QString shellProgram;
    QStringList arguments;

#ifdef Q_OS_WIN
    shellProgram = QProcessEnvironment::systemEnvironment().value("ComSpec", "cmd.exe");
    arguments << "/K";
#else
    shellProgram = QProcessEnvironment::systemEnvironment().value("SHELL", "/bin/bash");
    arguments << "--noprofile" << "--norc" << "-i";
#endif

    appendTerminalText(QString("Starting shell: %1\n").arg(shellProgram));
    m_process->start(shellProgram, arguments);
    if (!m_process->waitForStarted(2000)) {
        appendTerminalText(QString("Failed to start shell process: %1\n").arg(m_process->errorString()));
    }

    if (!m_promptText.isEmpty()) {
        appendTerminalText(m_promptText);
    }
}

void TerminalWidget::closeTerminal()
{
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->terminate();
        m_process->waitForFinished(1000);
        if (m_process->state() == QProcess::Running) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    emit terminalClosed();
}

bool TerminalWidget::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void TerminalWidget::setPromptText(const QString &prompt)
{
    m_promptText = prompt;
    if (!m_promptText.isEmpty()) {
        appendTerminalText(m_promptText);
    }
}

void TerminalWidget::appendTerminalText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    insertPlainText(text);
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void TerminalWidget::writeToShell(const QByteArray &data)
{
    if (m_process && m_process->state() == QProcess::Running && !data.isEmpty()) {
        m_process->write(data);
    }
}

void TerminalWidget::updateInputDisplay()
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    if (!m_inputBuffer.isEmpty()) {
        appendTerminalText(m_inputBuffer);
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!event) {
        return;
    }

    const int key = event->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!m_inputBuffer.isEmpty()) {
            writeToShell(m_inputBuffer.toUtf8() + "\r\n");
            m_inputBuffer.clear();            
        }
        appendTerminalText("\n");
        if (!m_promptText.isEmpty()) {
            appendTerminalText(m_promptText);
        }
        return;
    }

    if (key == Qt::Key_Backspace) {
        if (!m_inputBuffer.isEmpty()) {
            m_inputBuffer.chop(1);
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            setTextCursor(cursor);
            cursor.deletePreviousChar();
            setTextCursor(cursor);
            writeToShell("\x7f");
        }
        return;
    }

    if (key == Qt::Key_Delete) {
        QTextCursor cursor = textCursor();
        if (!cursor.atEnd()) {
            cursor.deleteChar();
            setTextCursor(cursor);
        }
        return;
    }

    if (key == Qt::Key_Tab) {
        m_inputBuffer += '\t';
        appendTerminalText("\t");
        writeToShell("\t");
        return;
    }

    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }

    if (event->matches(QKeySequence::Paste)) {
        const QString text = QGuiApplication::clipboard()->text();
        m_inputBuffer += text;
        appendTerminalText(text);
        writeToShell(text.toUtf8());
        return;
    }

    if (event->text().isEmpty()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    
    m_inputBuffer += event->text();
    appendTerminalText(event->text());
    //writeToShell(event->text().toUtf8());
}

void TerminalWidget::readProcessOutput()
{
    if (!m_process) {
        return;
    }

    const QByteArray data = m_process->readAllStandardOutput();
    if (!data.isEmpty()) {
        appendTerminalText(QString::fromLocal8Bit(data));
    }
}

void TerminalWidget::readProcessError()
{
    if (!m_process) {
        return;
    }

    const QByteArray data = m_process->readAllStandardError();
    if (!data.isEmpty()) {
        appendTerminalText(QString::fromLocal8Bit(data));
    }
}

void TerminalWidget::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    appendTerminalText(QString("\n[Process exited with code %1]\n").arg(exitCode));
    emit terminalClosed();
}
