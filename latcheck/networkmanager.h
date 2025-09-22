#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslCipher>
#include <QTimer>
#include <QHostAddress>
#include "configmanager.h"
#include "message_protocol.h"

class NetworkManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

public:
    explicit NetworkManager(QObject *parent = nullptr, ConfigManager *configManager = nullptr);
    ~NetworkManager();

    // 删除重复的 MessageType 枚举定义，使用 message_protocol.h 中的定义

    bool connected() const;
    QString connectionStatus() const;

    Q_INVOKABLE void connectToServer(const QString &host, int port,
                                     const QString &certPath, const QString &keyPath, bool ignoreSslErrors = false);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void testConnection(const QString &host, int port,
                                    const QString &certPath, const QString &keyPath, bool ignoreSslErrors = false);
    Q_INVOKABLE bool login(const QString &username, const QString &passwordHash);
    Q_INVOKABLE QVariantList requestIpList();

    // 新增方法
    Q_INVOKABLE bool sendLoginRequest(const QString &username, const QString &passwordHash);
    Q_INVOKABLE bool sendListRequest();
    Q_INVOKABLE bool sendReportRequest(const QString &location, const QVariantList &results);

signals:
    void connectedChanged();
    void connectionStatusChanged();
    void loginResult(bool success, const QString &message);
    void ipListReceived(const QVariantList &ipList);
    void errorOccurred(const QString &error);
    void testConnectionResult(const QString &message, bool success);
    void tlsVersionDetected(const QString &version);
    void reportUploadResult(bool success, const QString &reportId, const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onSslErrors(const QList<QSslError> &errors);
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onEncrypted(); // 新增：SSL握手完成信号处理

private:
    QString getTlsProtocolVersion();
    QString getCipherSuiteInfo();
    QSslSocket *m_socket;
    bool m_connected;
    QString m_connectionStatus;
    QByteArray m_receivedData;
    QString m_currentHost;
    int m_currentPort;
    bool m_ignoreSslErrors;

    void setConnected(bool connected);
    void setConnectionStatus(const QString &status);
    bool loadCertificates(const QString &certPath, const QString &keyPath);
    void sendRequest(const QString &request, const QByteArray &data = QByteArray());
    QVariantList parseIpList(const QByteArray &data);
    QString formatLogMessage(const QString &message);

    // 消息处理相关方法
    bool sendMessage(MessageType msgType, const QByteArray &data); // 添加这个声明
    QByteArray createMessageHeader(quint32 msgType, quint32 dataLength);
    QByteArray createLoginRequestData(const QString &username, const QString &passwordHash);
    QByteArray createReportRequestData(const QString &location, const QVariantList &results);
    void processIncomingMessage(); // 修改为无参数版本
    void handleMessage(MessageType msgType, const QByteArray &messageData); // 添加这个声明
    void processServerListResponse(const QByteArray &data);

    // 添加配置存储成员变量
    QString m_storedHost;
    int m_storedPort;
    QString m_storedCertPath;
    QString m_storedKeyPath;
    bool m_storedIgnoreSslErrors;
    ConfigManager *m_configManager;

    QString m_pendingUsername;
    QString m_pendingPassword;
    bool m_hasPendingLogin = false;
};

#endif // NETWORKMANAGER_H
