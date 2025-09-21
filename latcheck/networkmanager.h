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
#include <QDateTime>  // 添加日期时间支持

class NetworkManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();

    // 消息类型定义
    enum MessageType {
        LOGIN_REQUEST = 0x0001,
        LOGIN_OK = 0x0002,
        LOGIN_FAIL = 0x0003,
        LIST_REQUEST = 0x0004,
        LIST_RESPONSE = 0x0005,
        REPORT_REQUEST = 0x0006,
        REPORT_OK = 0x0007,
        REPORT_FAIL = 0x0008
    };

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
    void testConnectionResult(const QString &message, bool success);  // 添加测试连接结果信号
    void tlsVersionDetected(const QString &version);  // 新增：TLS版本检测信号

private slots:
    void onConnected();
    void onDisconnected();
    void onSslErrors(const QList<QSslError> &errors);
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onEncrypted();  // 新增：SSL握手完成信号处理

private:
    QString getTlsProtocolVersion();  // 新增：获取TLS协议版本
    QString getCipherSuiteInfo();     // 新增：获取密码套件信息
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
    QString formatLogMessage(const QString &message);  // 添加格式化日志方法
    
    // 消息处理方法
    QByteArray createMessageHeader(quint32 msgType, quint32 dataLength);
    QByteArray createLoginRequestData(const QString &username, const QString &passwordHash);
    QByteArray createReportRequestData(const QString &location, const QVariantList &results);
    void processIncomingMessage(const QByteArray &data);
    void processServerListResponse(const QByteArray &data);
};

#endif // NETWORKMANAGER_H