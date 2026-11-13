#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>

#include <libssh2.h>
#include <libssh2_sftp.h>

struct ConnectionSettings
{
    QString host;
    QString user;
    QString password;
    quint16 port = 22;
};

struct RemoteEntry
{
    QString name;
    QString path;
    bool isDir = false;
};

class SSHClient : public QObject
{
    Q_OBJECT

public:
    explicit SSHClient(QObject *parent = nullptr);
    ~SSHClient() override;

    bool connect(const ConnectionSettings &settings);
    void disconnect();
    bool isConnected() const;
    QString lastError() const;

    QString readRemoteFile(const QString &remotePath);
    bool writeRemoteFile(const QString &remotePath, const QString &contents);
    QList<RemoteEntry> listRemoteDirectory(const QString &remotePath = QString());
    QString pollTerminalOutput();
    bool writeTerminalInput(const QByteArray &data);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void terminalOutputReady(const QString &text);

private:
    QTcpSocket *m_socket = nullptr;
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_CHANNEL *m_shellChannel = nullptr;
    LIBSSH2_SFTP *m_sftp = nullptr;

    ConnectionSettings m_settings;
    bool m_connected = false;
    QString m_lastError;

    void resetSocket();
    void handleSocketError(QAbstractSocket::SocketError socketError);
    bool openShellChannel();
    bool openSftpSession();
};
