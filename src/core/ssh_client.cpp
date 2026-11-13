#include "core/ssh_client.h"

#include <QByteArray>
#include <QThread>

SSHClient::SSHClient(QObject *parent)
    : QObject(parent)
{
    resetSocket();
}

SSHClient::~SSHClient()
{
    disconnect();
}

bool SSHClient::connect(const ConnectionSettings &settings)
{
    m_settings = settings;
    m_lastError.clear();

    if (m_socket) {
        m_socket->disconnectFromHost();
    }

    resetSocket();
    m_socket->connectToHost(m_settings.host, m_settings.port);
    if (!m_socket->waitForConnected(20000)) {
        m_lastError = m_socket->errorString();
        emit errorOccurred(m_lastError);
        return false;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        m_lastError = "libssh2_session_init failed.";
        emit errorOccurred(m_lastError);
        return false;
    }

    libssh2_session_set_blocking(m_session, 1);

    int rc = libssh2_session_handshake(m_session, static_cast<int>(m_socket->socketDescriptor()));
    if (rc != 0) {
        char *errorDesc = nullptr;
        libssh2_session_last_error(m_session, &errorDesc, nullptr, 0);
        m_lastError = QString("Handshake failed: %1 (%2)")
            .arg(rc)
            .arg(errorDesc ? QString::fromLocal8Bit(errorDesc) : QString("unknown libssh2 error"));
        emit errorOccurred(m_lastError);
        disconnect();
        return false;
    }

    const QByteArray username = m_settings.user.toUtf8();
    const QByteArray password = m_settings.password.toUtf8();
    rc = LIBSSH2_ERROR_EAGAIN;
    while (rc == LIBSSH2_ERROR_EAGAIN) {
        rc = libssh2_userauth_password_ex(
            m_session,
            username.constData(),
            static_cast<unsigned int>(username.size()),
            password.constData(),
            static_cast<unsigned int>(password.size()),
            nullptr);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            QThread::msleep(10);
        }
    }
    if (rc != 0) {
        m_lastError = QString("Password authentication failed: %1").arg(rc);
        emit errorOccurred(m_lastError);
        disconnect();
        return false;
    }

    if (!openShellChannel() || !openSftpSession()) {
        disconnect();
        return false;
    }

    m_connected = true;
    emit connected();
    return true;
}

void SSHClient::disconnect()
{
    if (m_shellChannel) {
        libssh2_channel_free(m_shellChannel);
        m_shellChannel = nullptr;
    }
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect_ex(m_session, 0, "bye", "");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }

    m_connected = false;
    emit disconnected();
}

bool SSHClient::isConnected() const
{
    return m_connected;
}

QString SSHClient::lastError() const
{
    return m_lastError;
}

QString SSHClient::readRemoteFile(const QString &remotePath)
{
    if (!m_connected || !m_sftp || remotePath.isEmpty()) {
        return QString();
    }

    const QByteArray encoded = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(m_sftp,
        encoded.constData(),
        static_cast<size_t>(encoded.size()),
        LIBSSH2_FXF_READ,
        0,
        LIBSSH2_SFTP_OPENFILE);
    if (!file) {
        m_lastError = "Could not open remote file.";
        emit errorOccurred(m_lastError);
        return QString();
    }

    QByteArray buffer;
    char data[4096];
    while (true) {
        const int bytesRead = libssh2_sftp_read(file, data, sizeof(data));
        if (bytesRead > 0) {
            buffer.append(data, bytesRead);
            continue;
        }
        if (bytesRead == LIBSSH2_ERROR_EAGAIN) {
            QThread::msleep(10);
            continue;
        }
        break;
    }

    libssh2_sftp_close(file);
    return QString::fromUtf8(buffer);
}

bool SSHClient::writeRemoteFile(const QString &remotePath, const QString &contents)
{
    if (!m_connected || !m_sftp || remotePath.isEmpty()) {
        return false;
    }

    const QByteArray encodedPath = remotePath.toUtf8();
    const QByteArray encodedData = contents.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(m_sftp,
        encodedPath.constData(),
        static_cast<size_t>(encodedPath.size()),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        0777,
        LIBSSH2_SFTP_OPENFILE);
    if (!file) {
        m_lastError = "Could not open remote file for writing.";
        emit errorOccurred(m_lastError);
        return false;
    }

    const int written = libssh2_sftp_write(file, encodedData.constData(), encodedData.size());
    libssh2_sftp_close(file);
    if (written < 0) {
        m_lastError = "Remote write failed.";
        emit errorOccurred(m_lastError);
        return false;
    }

    return true;
}

QList<RemoteEntry> SSHClient::listRemoteDirectory(const QString &remotePath)
{
    QList<RemoteEntry> entries;
    if (!m_connected || !m_sftp) {
        return entries;
    }

    const QByteArray path = remotePath.isEmpty() ? QByteArray("/") : remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(m_sftp, path.constData());
    if (!dir) {
        m_lastError = QString("Could not open remote directory: %1").arg(QString::fromUtf8(path));
        emit errorOccurred(m_lastError);
        return entries;
    }

    char entryName[1024];
    char longName[2048];
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    while (true) {
        const int rc = libssh2_sftp_readdir_ex(dir, entryName, sizeof(entryName), longName, sizeof(longName), &attrs);
        if (rc <= 0) {
            break;
        }

        const QString name = QString::fromUtf8(entryName);
        if (name == "." || name == "..") {
            continue;
        }

        RemoteEntry entry;
        entry.name = name;
        entry.path = path.endsWith('/') ? QString::fromUtf8(path) + name : QString::fromUtf8(path) + "/" + name;
        entry.isDir = (attrs.permissions & LIBSSH2_SFTP_S_IFDIR) != 0;
        entries.append(entry);
    }

    libssh2_sftp_closedir(dir);
    return entries;
}

QString SSHClient::pollTerminalOutput()
{
    if (!m_connected || !m_shellChannel) {
        return QString();
    }

    QByteArray output;
    char buffer[4096];
    while (true) {
        const int bytesRead = libssh2_channel_read_ex(m_shellChannel, 0, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output.append(buffer, bytesRead);
            continue;
        }
        if (bytesRead == LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        if (bytesRead < 0) {
            m_lastError = QString("Remote shell read failed: %1").arg(bytesRead);
            emit errorOccurred(m_lastError);
            break;
        }
        break;
    }

    if (!output.isEmpty()) {
        emit terminalOutputReady(QString::fromUtf8(output));
    }

    return QString::fromUtf8(output);
}

bool SSHClient::writeTerminalInput(const QByteArray &data)
{
    if (!m_connected || !m_shellChannel || data.isEmpty()) {
        return false;
    }

    int bytesWritten = 0;
    while (bytesWritten < data.size()) {
        const int rc = libssh2_channel_write_ex(m_shellChannel, 0, data.constData() + bytesWritten, data.size() - bytesWritten);
        if (rc < 0) {
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                QThread::msleep(10);
                continue;
            }
            m_lastError = QString("Remote shell write failed: %1").arg(rc);
            emit errorOccurred(m_lastError);
            return false;
        }
        bytesWritten += rc;
    }

    return true;
}

void SSHClient::resetSocket()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
    }
    m_socket = new QTcpSocket(this);
    QObject::connect(m_socket, &QTcpSocket::errorOccurred, this, &SSHClient::handleSocketError);
    QObject::connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_connected = false;
        emit disconnected();
    });
}

void SSHClient::handleSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    if (m_socket) {
        m_lastError = m_socket->errorString();
        emit errorOccurred(m_lastError);
    }
    m_connected = false;
}

bool SSHClient::openShellChannel()
{
    m_shellChannel = libssh2_channel_open_session(m_session);
    if (!m_shellChannel) {
        m_lastError = "Could not open SSH shell channel.";
        emit errorOccurred(m_lastError);
        return false;
    }

    int rc = LIBSSH2_ERROR_EAGAIN;
    while (rc == LIBSSH2_ERROR_EAGAIN) {
        rc = libssh2_channel_request_pty_ex(m_shellChannel, "xterm", 5, nullptr, 0, 80, 24, 0, 0);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            QThread::msleep(10);
        }
    }
    if (rc != 0) {
        m_lastError = QString("PTY request failed: %1").arg(rc);
        emit errorOccurred(m_lastError);
        return false;
    }

    rc = LIBSSH2_ERROR_EAGAIN;
    while (rc == LIBSSH2_ERROR_EAGAIN) {
        rc = libssh2_channel_shell(m_shellChannel);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            QThread::msleep(10);
        }
    }
    if (rc != 0) {
        m_lastError = QString("Shell launch failed: %1").arg(rc);
        emit errorOccurred(m_lastError);
        return false;
    }

    return true;
}

bool SSHClient::openSftpSession()
{
    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        m_lastError = "SFTP initialization failed.";
        emit errorOccurred(m_lastError);
        return false;
    }
    return true;
}
