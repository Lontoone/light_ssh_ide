#include "ui/terminal_widget.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextCursor>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

namespace {
// 當 ssh.exe 觸發 SSH_ASKPASS 時，在進入 main() 前攔截並與 GUI 交換密碼
struct WinSshAskPassInterceptor {
    WinSshAskPassInterceptor()
    {
        wchar_t mode[16] = {0};
        if (GetEnvironmentVariableW(L"LIGHTSSH_ASKPASS_MODE", mode, 16) == 0 || wcscmp(mode, L"1") != 0) {
            return;
        }

        wchar_t ipcDir[MAX_PATH] = {0};
        if (GetEnvironmentVariableW(L"LIGHTSSH_ASKPASS_DIR", ipcDir, MAX_PATH) == 0) {
            ExitProcess(1);
        }

        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring prompt = L"Password: ";
        if (argv && argc >= 2) {
            prompt = argv[1];
        }
        if (argv) {
            LocalFree(argv);
        }

        std::wstring promptFile = std::wstring(ipcDir) + L"\\askpass_prompt.txt";
        std::wstring replyFile  = std::wstring(ipcDir) + L"\\askpass_reply.txt";

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, prompt.c_str(), static_cast<int>(prompt.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8Prompt(utf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, prompt.c_str(), static_cast<int>(prompt.size()), &utf8Prompt[0], utf8Len, nullptr, nullptr);

        HANDLE hPrompt = CreateFileW(promptFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hPrompt != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hPrompt, utf8Prompt.data(), static_cast<DWORD>(utf8Prompt.size()), &written, nullptr);
            CloseHandle(hPrompt);
        }

        for (int i = 0; i < 4000; ++i) {
            if (GetFileAttributesW(replyFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
                Sleep(20);
                HANDLE hReply = CreateFileW(replyFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hReply != INVALID_HANDLE_VALUE) {
                    char buf[1024] = {0};
                    DWORD bytesRead = 0;
                    if (ReadFile(hReply, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                        CloseHandle(hReply);
                        DeleteFileW(replyFile.c_str());

                        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                        if (hOut && hOut != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            WriteFile(hOut, buf, bytesRead, &written, nullptr);
                            FlushFileBuffers(hOut);
                        }
                        ExitProcess(0);
                    }
                    CloseHandle(hReply);
                }
            }
            Sleep(30);
        }
        ExitProcess(1);
    }
};

static WinSshAskPassInterceptor s_askPassInterceptor;
} // namespace
#endif

namespace {
struct SshSessionState {
    bool waitingForAskPass = false;
    QString ipcDir;
};

static SshSessionState g_sshState;

// 自動判斷輸出為 UTF-8 (Linux / 現代工具) 或 Local 8-bit (Windows Big5 cmd.exe)
QString decodeTerminalOutput(const QByteArray &data)
{
    QStringDecoder utf8Decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString utf8Text = utf8Decoder(data);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }
    return QString::fromLocal8Bit(data);
}

// 過濾基礎 ANSI 跳脫序列 (避免 Windows OpenSSH 或 Linux 回傳控制碼造成版面混亂)
QString stripAnsiCodes(const QString &text)
{
    static const QRegularExpression ansiRegex(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]|\][^\a\x1B]*(?:\a|\x1B\\)))");
    QString cleaned = text;
    cleaned.remove(ansiRegex);
    return cleaned;
}
} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setPlaceholderText("Terminal ready");
    setMaximumBlockCount(2000);
    setUndoRedoEnabled(false);
    setAcceptDrops(true);

    g_sshState.ipcDir = QDir::toNativeSeparators(QDir::tempPath() + "/lightssh_ipc_" + QString::number(QCoreApplication::applicationPid()));
    QDir().mkpath(g_sshState.ipcDir);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readProcessOutput);
    connect(m_process, &QProcess::finished, this, &TerminalWidget::onProcessFinished);

    // 定期檢查 SSH_ASKPASS 是否傳來密碼提示
    QTimer *askPassTimer = new QTimer(this);
    connect(askPassTimer, &QTimer::timeout, this, [this]() {
        const QString promptPath = g_sshState.ipcDir + "/askpass_prompt.txt";
        if (QFile::exists(promptPath)) {
            QFile f(promptPath);
            if (f.open(QIODevice::ReadOnly)) {
                const QString prompt = QString::fromUtf8(f.readAll());
                f.close();
                QFile::remove(promptPath);

                g_sshState.waitingForAskPass = true;
                appendTerminalText(prompt.endsWith(' ') ? prompt : (prompt + " "));
            }
        }
    });
    askPassTimer->start(50);

    startLocalShell();
}

TerminalWidget::~TerminalWidget()
{
    closeTerminal();
    QDir(g_sshState.ipcDir).removeRecursively();
}

void TerminalWidget::startLocalShell()
{
    if (m_process->state() == QProcess::Running) {
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    env.insert("DISPLAY", "localhost:0.0");
    env.insert("SSH_ASKPASS", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    env.insert("SSH_ASKPASS_REQUIRE", "force");
    env.insert("LIGHTSSH_ASKPASS_MODE", "1");
    env.insert("LIGHTSSH_ASKPASS_DIR", g_sshState.ipcDir);
    m_process->setProcessEnvironment(env);

    QString shellProgram;
    QStringList arguments;

#ifdef Q_OS_WIN
    shellProgram = env.value("ComSpec", "cmd.exe");
    arguments << "/Q" << "/K";
#else
    shellProgram = env.value("SHELL", "/bin/bash");
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

    // Ctrl+C 處理
    if ((event->modifiers() & Qt::ControlModifier) && key == Qt::Key_C) {
        if (textCursor().hasSelection()) {
            copy();
        } else {
            m_inputBuffer.clear();
            g_sshState.waitingForAskPass = false;
            appendTerminalText("^C\n");
            writeToShell("\x03\r\n");
        }
        return;
    }

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        QString cmd = m_inputBuffer;
        m_inputBuffer.clear();
        appendTerminalText("\n");

        // 1. 若目前正在等待輸入 SSH 密碼，將密碼寫入 IPC 檔案交給 SSH_ASKPASS
        if (g_sshState.waitingForAskPass) {
            g_sshState.waitingForAskPass = false;

            QFile replyFile(g_sshState.ipcDir + "/askpass_reply.txt");
            if (replyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                replyFile.write(cmd.toUtf8() + "\r\n");
                replyFile.close();
            }
            return;
        }

        // 2. 若輸入為 ssh 連線指令，自動加上 -T 與 StrictHostKeyChecking=accept-new
        static const QRegularExpression sshRegex(R"(^\s*ssh(\.exe)?\s+(?!.*(^|\s)-[tT]))");
        if (sshRegex.match(cmd).hasMatch()) {
            QFile::remove(g_sshState.ipcDir + "/askpass_prompt.txt");
            QFile::remove(g_sshState.ipcDir + "/askpass_reply.txt");
            cmd.replace(QRegularExpression(R"(^(\s*ssh(?:\.exe)?)\s+)"), "\\1 -T -o StrictHostKeyChecking=accept-new ");
        }

        // 3. 將指令原封不動送往 Shell (不再強行附加 Linux 專用的 printf)
        writeToShell(cmd.toUtf8() + "\r\n");
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
        }
        return;
    }

    if (key == Qt::Key_Delete) {
        return;
    }

    if (key == Qt::Key_Tab) {
        m_inputBuffer += '\t';
        appendTerminalText("\t");
        return;
    }

    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }

    if (event->matches(QKeySequence::Paste)) {
        const QString text = QGuiApplication::clipboard()->text();
        m_inputBuffer += text;
        if (g_sshState.waitingForAskPass) {
            appendTerminalText(QString(text.size(), '*'));
        } else {
            appendTerminalText(text);
        }
        return;
    }

    if (event->text().isEmpty()) {
        return;
    }

    m_inputBuffer += event->text();
    if (g_sshState.waitingForAskPass) {
        appendTerminalText("*");
    } else {
        appendTerminalText(event->text());
    }
}

void TerminalWidget::readProcessOutput()
{
    if (!m_process) {
        return;
    }

    const QByteArray data = m_process->readAllStandardOutput();
    if (!data.isEmpty()) {
        appendTerminalText(stripAnsiCodes(decodeTerminalOutput(data)));
    }
}

void TerminalWidget::readProcessError()
{
    if (!m_process) {
        return;
    }

    const QByteArray data = m_process->readAllStandardError();
    if (!data.isEmpty()) {
        appendTerminalText(stripAnsiCodes(decodeTerminalOutput(data)));
    }
}

void TerminalWidget::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    g_sshState.waitingForAskPass = false;
    appendTerminalText(QString("\n[Process exited with code %1]\n").arg(exitCode));
    emit terminalClosed();
}