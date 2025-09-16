#include "networkmanager.h"
#include <QSslError>
#include <QFile>
#include <QDebug>
#include <QDataStream>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_connected(false)
    , m_connectionStatus("Disconnected")
{
    m_socket = new QSslSocket(this);
    
    connect(m_socket, &QSslSocket::connected, this, &NetworkManager::onConnected);
    connect(m_socket, &QSslSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, &NetworkManager::onSslErrors);
    connect(m_socket, &QSslSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::errorOccurred),
            this, &NetworkManager::onSocketError);
}

NetworkManager::~NetworkManager()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool NetworkManager::connected() const
{
    return m_connected;
}

QString NetworkManager::connectionStatus() const
{
    return m_connectionStatus;
}

void NetworkManager::connectToServer(const QString &host, int port, 
                                   const QString &certPath, const QString &keyPath)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }

    setConnectionStatus("Connecting...");

    if (!loadCertificates(certPath, keyPath)) {
        setConnectionStatus("Certificate loading failed");
        emit errorOccurred("Failed to load client certificates");
        return;
    }

    m_socket->connectToHostEncrypted(host, port);
}

void NetworkManager::disconnectFromServer()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool NetworkManager::login(const QString &username, const QString &passwordHash)
{
    if (!m_connected) {
        return false;
    }

    QByteArray loginData;
    QDataStream stream(&loginData, QIODevice::WriteOnly);
    stream << username << passwordHash;
    
    sendRequest("LOGIN", loginData);
    return true;
}

QVariantList NetworkManager::requestIpList()
{
    if (!m_connected) {
        return QVariantList();
    }

    sendRequest("GET_LIST");
    return QVariantList(); // Will be returned via signal
}

void NetworkManager::onConnected()
{
    setConnected(true);
    setConnectionStatus("Connected (TLS)");
}

void NetworkManager::onDisconnected()
{
    setConnected(false);
    setConnectionStatus("Disconnected");
}

void NetworkManager::onSslErrors(const QList<QSslError> &errors)
{
    QString errorString = "SSL Errors: ";
    for (const QSslError &error : errors) {
        errorString += error.errorString() + "; ";
    }
    setConnectionStatus(errorString);
    emit errorOccurred(errorString);
    
    // For development, you might want to ignore SSL errors
    // m_socket->ignoreSslErrors();
}

void NetworkManager::onReadyRead()
{
    m_receivedData.append(m_socket->readAll());
    
    // Process complete messages
    while (m_receivedData.size() >= 8) { // Minimum message size
        QDataStream stream(m_receivedData);
        QString messageType;
        stream >> messageType;
        
        if (messageType == "LOGIN_RESPONSE") {
            bool success;
            QString message;
            stream >> success >> message;
            emit loginResult(success, message);
            
            // Remove processed data
            m_receivedData = m_receivedData.mid(stream.device()->pos());
        }
        else if (messageType == "IP_LIST") {
            QVariantList ipList = parseIpList(m_receivedData.mid(stream.device()->pos()));
            emit ipListReceived(ipList);
            m_receivedData.clear();
            break;
        }
        else {
            break; // Wait for more data
        }
    }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString = m_socket->errorString();
    setConnectionStatus("Error: " + errorString);
    emit errorOccurred(errorString);
}

void NetworkManager::setConnected(bool connected)
{
    if (m_connected != connected) {
        m_connected = connected;
        emit connectedChanged();
    }
}

void NetworkManager::setConnectionStatus(const QString &status)
{
    if (m_connectionStatus != status) {
        m_connectionStatus = status;
        emit connectionStatusChanged();
    }
}

bool NetworkManager::loadCertificates(const QString &certPath, const QString &keyPath)
{
    if (certPath.isEmpty() || keyPath.isEmpty()) {
        return false;
    }

    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    QSslCertificate certificate(certFile.readAll());
    QSslKey privateKey(keyFile.readAll(), QSsl::Rsa);

    if (certificate.isNull() || privateKey.isNull()) {
        return false;
    }

    QSslConfiguration sslConfig = m_socket->sslConfiguration();
    sslConfig.setLocalCertificate(certificate);
    sslConfig.setPrivateKey(privateKey);
    m_socket->setSslConfiguration(sslConfig);

    return true;
}

void NetworkManager::sendRequest(const QString &request, const QByteArray &data)
{
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream << request;
    if (!data.isEmpty()) {
        message.append(data);
    }
    
    m_socket->write(message);
}

QVariantList NetworkManager::parseIpList(const QByteArray &data)
{
    QVariantList ipList;
    QDataStream stream(data);
    
    quint32 count;
    stream >> count;
    
    for (quint32 i = 0; i < count; ++i) {
        quint32 ip;
        stream >> ip;
        QHostAddress address(ip);
        ipList.append(address.toString());
    }
    
    return ipList;
}