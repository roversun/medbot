#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>
#include <QHostAddress>

class NetworkManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();

    bool connected() const;
    QString connectionStatus() const;

    Q_INVOKABLE void connectToServer(const QString &host, int port, 
                                   const QString &certPath, const QString &keyPath);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE bool login(const QString &username, const QString &passwordHash);
    Q_INVOKABLE QVariantList requestIpList();

signals:
    void connectedChanged();
    void connectionStatusChanged();
    void loginResult(bool success, const QString &message);
    void ipListReceived(const QVariantList &ipList);
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();
    void onSslErrors(const QList<QSslError> &errors);
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QSslSocket *m_socket;
    bool m_connected;
    QString m_connectionStatus;
    QByteArray m_receivedData;
    
    void setConnected(bool connected);
    void setConnectionStatus(const QString &status);
    bool loadCertificates(const QString &certPath, const QString &keyPath);
    void sendRequest(const QString &request, const QByteArray &data = QByteArray());
    QVariantList parseIpList(const QByteArray &data);
};

#endif // NETWORKMANAGER_H