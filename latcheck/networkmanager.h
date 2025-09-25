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
#include "latencychecker.h"

class NetworkManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool latencyCheckRunning READ latencyCheckRunning NOTIFY latencyCheckRunningChanged)

public:
    explicit NetworkManager(QObject *parent = nullptr, ConfigManager *configManager = nullptr);
    ~NetworkManager();

    // 修改connectToServer方法签名，移除证书路径参数
    Q_INVOKABLE void connectToServer(const QString &host, int port, bool ignoreSslErrors);
    // 修改testConnection方法签名，移除证书路径参数
    Q_INVOKABLE void testConnection(const QString &host, int port, bool ignoreSslErrors);
    Q_INVOKABLE bool login(const QString &username, const QString &password);
    bool connected() const;
    QString connectionStatus() const;

    // 移除loadCertificates方法声明
    // bool loadCertificates(const QString &certPath, const QString &keyPath);

    bool latencyCheckRunning() const;
    Q_INVOKABLE void startLatencyCheck(int threadCount = 4);
    Q_INVOKABLE void stopLatencyCheck();

    // 删除带有证书路径参数的connectToServer方法声明
    // Q_INVOKABLE void connectToServer(const QString &host, int port,
    //                                  const QString &certPath, const QString &keyPath, bool ignoreSslErrors = false);
    Q_INVOKABLE void disconnectFromServer();
    // 删除带有证书路径参数的testConnection方法声明
    // Q_INVOKABLE void testConnection(const QString &host, int port,
    //                                const QString &certPath, const QString &keyPath, bool ignoreSslErrors = false);
    // 删除重复的login方法声明
    // Q_INVOKABLE bool login(const QString &username, const QString &passwordHash);
    Q_INVOKABLE QVariantList requestIpList();

    // 新增方法
    Q_INVOKABLE bool sendLoginRequest(const QString &username, const QString &passwordHash);
    Q_INVOKABLE bool sendListRequest();
    Q_INVOKABLE bool sendReportRequest(const QString &location, const QVariantList &results);
    Q_INVOKABLE bool saveIpListToFile(const QString &filePath, const QVariantList &ipList);
    // 添加新的重载方法，直接使用内部保存的IP列表并提供文件浏览功能
    Q_INVOKABLE bool saveIpListToFile();

signals:
    void connectedChanged();
    void connectionStatusChanged();
    void loginResult(bool success, const QString &message);
    void ipListReceived(const QVariantList &ipList);
    void errorOccurred(const QString &error);
    void testConnectionResult(const QString &message, bool success);
    void tlsVersionDetected(const QString &version);
    void reportUploadResult(bool success, const QString &reportId, const QString &message);
    void latencyCheckRunningChanged();
    void latencyCheckProgress(int current, int total);
    void latencyCheckFinished(const QVariantList &results);

private slots:
    void onConnected();
    void onDisconnected();
    void onSslErrors(const QList<QSslError> &errors);
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onEncrypted();
    void onLatencyCheckFinished(const QVariantList &results);
    void onLatencyResult(quint32 serverId, quint32 ipAddr, int latency);

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
    LatencyChecker *m_latencyChecker;
    QVariantList m_currentServerList;
    bool m_autoStartLatencyCheck;

    void setConnected(bool connected);
    void setConnectionStatus(const QString &status);
    // 移除未使用的loadCertificates方法
    void sendRequest(const QString &request, const QByteArray &data = QByteArray());
    QVariantList parseIpList(const QByteArray &data);
    // 添加通用SSL配置函数声明
    QSslConfiguration configureSslSocket(QSslSocket *socket, bool ignoreSslErrors);

    // 消息处理相关方法
    bool sendMessage(MessageType msgType, const QByteArray &data); // 添加这个声明
    QByteArray createMessageHeader(quint32 msgType, quint32 dataLength);
    QByteArray createLoginRequestData(const QString &username, const QString &passwordHash);
    QByteArray createReportRequestData(const QString &location, const QVariantList &results);
    void processIncomingMessage();                                          // 修改为无参数版本
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