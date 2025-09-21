#include "networkmanager.h"
#include <QSslError>
#include <QFile>
#include <QDebug>
#include <QDataStream>
#include <QNetworkProxy>
#include <QDateTime>
#include <QTimer>
#include <QSslCipher>
#include <QHostAddress>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_connected(false)
    , m_connectionStatus("Disconnected")
    , m_ignoreSslErrors(false)
{
    m_socket = new QSslSocket(this);
    
    // 禁用代理
    m_socket->setProxy(QNetworkProxy::NoProxy);
    
    connect(m_socket, &QSslSocket::connected, this, &NetworkManager::onConnected);
    connect(m_socket, &QSslSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, &NetworkManager::onSslErrors);
    connect(m_socket, &QSslSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::errorOccurred),
            this, &NetworkManager::onSocketError);
    // 添加SSL握手完成信号的连接
    connect(m_socket, &QSslSocket::encrypted, this, &NetworkManager::onEncrypted);
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

// 添加格式化日志消息的私有方法
QString NetworkManager::formatLogMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate).replace('T', ' ');
    return QString("[%1] %2").arg(timestamp, message);
}

void NetworkManager::connectToServer(const QString &host, int port, 
                                   const QString &certPath, const QString &keyPath, bool ignoreSslErrors)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }

    // Store SSL error ignore setting
    m_ignoreSslErrors = ignoreSslErrors;
    
    // Add debug log
    emit errorOccurred(formatLogMessage(QString("SSL Error Handling: %1").arg(m_ignoreSslErrors ? "IGNORE" : "VALIDATE")));
    
    // Pre-configure socket if SSL errors should be ignored
    if (m_ignoreSslErrors) {
        m_socket->ignoreSslErrors();
        QSslConfiguration sslConfig = m_socket->sslConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        m_socket->setSslConfiguration(sslConfig);
    }
    
    // Store connection info
    m_currentHost = host;
    m_currentPort = port;
    
    // Output connection attempt log
    QString connectMsg = QString("Attempting to connect to %1:%2").arg(host).arg(port);
    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        connectMsg += " with client certificates";
    } else {
        connectMsg += " using standard TLS";
    }
    emit errorOccurred(formatLogMessage(connectMsg));
    
    setConnectionStatus("Connecting...");
    
    // Set connection options
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    // Load client certificates if provided
    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        if (!loadCertificates(certPath, keyPath)) {
            setConnectionStatus("Certificate loading failed");
            emit errorOccurred(formatLogMessage("Failed to load client certificates"));
            return;
        }
    }
    
    // Connect to server
    m_socket->connectToHostEncrypted(host, port);
    
    // Wait for connection with timeout
    if (!m_socket->waitForConnected(5000)) {
        QString errorMsg = QString("Connection test failed: %1").arg(m_socket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(formatLogMessage(errorMsg));
        emit testConnectionResult("Server is unreachable!", false);
        return;
    }
    
    // Wait for SSL handshake completion
    if (!m_socket->waitForEncrypted(5000)) {
        QString errorMsg = QString("SSL handshake test failed: %1").arg(m_socket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(formatLogMessage(errorMsg));
        return;
    }
}

void NetworkManager::disconnectFromServer()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void NetworkManager::testConnection(const QString &host, int port, 
                                  const QString &certPath, const QString &keyPath, bool ignoreSslErrors)
{
    // 创建临时socket进行测试，不影响主连接状态
    QSslSocket *testSocket = new QSslSocket(this);
    testSocket->setProxy(QNetworkProxy::NoProxy);
    
    // Store SSL error ignore setting
    m_ignoreSslErrors = ignoreSslErrors;
    
    // Add debug log
    emit errorOccurred(formatLogMessage(QString("Testing connection to %1:%2").arg(host).arg(port)));
    
    // Pre-configure socket if SSL errors should be ignored
    if (m_ignoreSslErrors) {
        testSocket->ignoreSslErrors();
        QSslConfiguration sslConfig = testSocket->sslConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        testSocket->setSslConfiguration(sslConfig);
    }
    
    setConnectionStatus("Testing connection...");
    
    // Set connection options
    testSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    testSocket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    // Load client certificates if provided
    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        // Load certificates for test socket
        QFile certFile(certPath);
        if (!certFile.open(QIODevice::ReadOnly)) {
            setConnectionStatus("Certificate loading failed");
            emit errorOccurred(formatLogMessage("Failed to load client certificates"));
            emit testConnectionResult("Server is unreachable!", false);
            testSocket->deleteLater();
            return;
        }

        QFile keyFile(keyPath);
        if (!keyFile.open(QIODevice::ReadOnly)) {
            setConnectionStatus("Certificate loading failed");
            emit errorOccurred(formatLogMessage("Failed to load client certificates"));
            emit testConnectionResult("Server is unreachable!", false);
            testSocket->deleteLater();
            return;
        }

        QSslCertificate certificate(certFile.readAll());
        QSslKey privateKey(keyFile.readAll(), QSsl::Rsa);

        if (certificate.isNull() || privateKey.isNull()) {
            setConnectionStatus("Invalid certificates");
            emit errorOccurred(formatLogMessage("Invalid client certificates"));
            emit testConnectionResult("Server is unreachable!", false);
            testSocket->deleteLater();
            return;
        }

        testSocket->setLocalCertificate(certificate);
        testSocket->setPrivateKey(privateKey);
    }
    
    // Connect to server
    testSocket->connectToHostEncrypted(host, port);
    
    // Wait for connection with timeout
    if (!testSocket->waitForConnected(5000)) {
        QString errorMsg = QString("Connection test failed: %1").arg(testSocket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(formatLogMessage(errorMsg));
        emit testConnectionResult("Server is unreachable!", false);
        testSocket->deleteLater();
        return;
    }
    
    // Wait for SSL handshake completion
    if (!testSocket->waitForEncrypted(5000)) {
        QString errorMsg = QString("SSL handshake test failed: %1").arg(testSocket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(formatLogMessage(errorMsg));
        emit testConnectionResult("Server is unreachable!", false);
        testSocket->deleteLater();
        return;
    }
    
    // Connection test successful, now disconnect
    emit errorOccurred(formatLogMessage("Connection test successful"));
    setConnectionStatus("Connection test successful");
    
    // 简化测试连接结果消息，不包含TLS详细信息
    emit testConnectionResult("Server is available", true);
    testSocket->disconnectFromHost();
    testSocket->deleteLater();
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

void NetworkManager::onEncrypted()
{
    // SSL握手完成后获取TLS信息
    QString protocolVersion = getTlsProtocolVersion();
    QString cipherInfo = getCipherSuiteInfo();
    
    QString tlsInfo = QString("TLS Protocol: %1").arg(protocolVersion);
    QString cipherSuiteInfo = QString("Cipher Suite: %1").arg(cipherInfo);
    
    emit errorOccurred(formatLogMessage(tlsInfo));
    emit errorOccurred(formatLogMessage(cipherSuiteInfo));
    
    // 发送TLS版本信号供外部使用
    emit tlsVersionDetected(protocolVersion);
}

QString NetworkManager::getTlsProtocolVersion()
{
    QSslConfiguration sslConfig = m_socket->sslConfiguration();
    QSslCipher cipher = sslConfig.sessionCipher();
    
    // 尝试从密码套件获取协议版本
    if (!cipher.isNull()) {
        QString protocolStr = cipher.protocolString();
        if (!protocolStr.isEmpty()) {
            // 标准化协议名称
            if (protocolStr.contains("TLSv1.3")) return "TLS 1.3";
            if (protocolStr.contains("TLSv1.2")) return "TLS 1.2";
            if (protocolStr.contains("TLSv1.1")) return "TLS 1.1";
            if (protocolStr.contains("TLSv1")) return "TLS 1.0";
            if (protocolStr.contains("SSLv3")) return "SSL 3.0";
            return protocolStr;
        }
    }
    
    // 尝试从SSL配置获取
    QSsl::SslProtocol protocol = sslConfig.protocol();
    switch (protocol) {
        case QSsl::TlsV1_2: return "TLS 1.2";
        case QSsl::TlsV1_3: return "TLS 1.3";
        case QSsl::DtlsV1_2: return "DTLS 1.2";
        default: {
            // 对于自动协商的情况，尝试更深入的检测
            QByteArray sslLibraryVersion = QSslSocket::sslLibraryVersionString().toUtf8();
            if (sslLibraryVersion.contains("TLSv1.3")) return "TLS 1.3";
            if (sslLibraryVersion.contains("TLSv1.2")) return "TLS 1.2";
            return "Unknown (Negotiated)";
        }
    }
}

QString NetworkManager::getCipherSuiteInfo()
{
    QSslConfiguration sslConfig = m_socket->sslConfiguration();
    QSslCipher cipher = sslConfig.sessionCipher();
    
    if (cipher.isNull()) {
        return "No cipher suite negotiated";
    }
    
    // 修复macMethod调用 - QSslCipher没有macMethod方法
    return QString("%1 (Key Exchange: %2, Authentication: %3, Encryption: %4)")
           .arg(cipher.name())
           .arg(cipher.keyExchangeMethod())
           .arg(cipher.authenticationMethod())
           .arg(cipher.encryptionMethod());
}

// 修改onConnected方法，移除TLS检测代码
void NetworkManager::onConnected()
{
    setConnected(true);
    
    QString successMsg = QString("Successfully connected to %1:%2")
                        .arg(m_currentHost).arg(m_currentPort);
    
    setConnectionStatus(successMsg);
    emit errorOccurred(formatLogMessage(successMsg));
}

void NetworkManager::onDisconnected()
{
    setConnected(false);
    QString disconnectMsg = QString("Disconnected from %1:%2").arg(m_currentHost).arg(m_currentPort);
    setConnectionStatus(disconnectMsg);
    emit errorOccurred(formatLogMessage(disconnectMsg));  // 使用格式化的日志
}

void NetworkManager::onSslErrors(const QList<QSslError> &errors)
{
    QString errorString = "SSL Errors: ";
    for (const QSslError &error : errors) {
        errorString += error.errorString() + "; ";
    }
    
    // 只在第一次SSL错误时输出设置信息
    static bool firstSslError = true;
    if (firstSslError) {
        emit errorOccurred(formatLogMessage(QString("Ignore SSL Errors setting: %1").arg(m_ignoreSslErrors ? "true" : "false")));
        firstSslError = false;
    }
    
    // 如果配置为忽略SSL错误，则忽略它们
    if (m_ignoreSslErrors) {
        emit errorOccurred(formatLogMessage("Ignoring SSL errors as configured"));
        m_socket->ignoreSslErrors();
        return; // 不设置错误状态，不发射错误信号
    }
    
    // 只有在不忽略SSL错误时才报告错误
    setConnectionStatus(errorString);
    emit errorOccurred(formatLogMessage(errorString));
}

QByteArray NetworkManager::createMessageHeader(quint32 msgType, quint32 dataLength)
{
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << msgType << dataLength;
    return header;
}

QByteArray NetworkManager::createLoginRequestData(const QString &username, const QString &passwordHash)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    
    // 用户名32字节
    QByteArray usernameBytes = username.toUtf8();
    usernameBytes.resize(32, '\0');
    stream.writeRawData(usernameBytes.constData(), 32);
    
    // 密码哈希32字节
    QByteArray passwordBytes = passwordHash.toUtf8();
    passwordBytes.resize(32, '\0');
    stream.writeRawData(passwordBytes.constData(), 32);
    
    return data;
}

bool NetworkManager::sendLoginRequest(const QString &username, const QString &passwordHash)
{
    if (!m_connected) {
        return false;
    }
    
    QByteArray loginData = createLoginRequestData(username, passwordHash);
    QByteArray header = createMessageHeader(LOGIN_REQUEST, loginData.size());
    
    m_socket->write(header + loginData);
    m_socket->flush();
    
    emit errorOccurred(formatLogMessage("Login request sent"));
    return true;
}

bool NetworkManager::sendListRequest()
{
    if (!m_connected) {
        return false;
    }
    
    QByteArray header = createMessageHeader(LIST_REQUEST, 0);
    m_socket->write(header);
    m_socket->flush();
    
    emit errorOccurred(formatLogMessage("Server list request sent"));
    return true;
}

QByteArray NetworkManager::createReportRequestData(const QString &location, const QVariantList &results)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    // location_length(4字节) + location(变长)
    QByteArray locationBytes = location.toUtf8();
    stream << static_cast<quint32>(locationBytes.size());
    stream.writeRawData(locationBytes.constData(), locationBytes.size());
    
    // record_count(4字节)
    stream << static_cast<quint32>(results.size());
    
    // [server_id(4字节) + latency(4字节)] * record_count
    for (const QVariant &result : results) {
        QVariantMap resultMap = result.toMap();
        quint32 serverId = resultMap["server_id"].toUInt();
        quint32 latency = resultMap["latency"].toUInt();
        stream << serverId << latency;
    }
    
    return data;
}

bool NetworkManager::sendReportRequest(const QString &location, const QVariantList &results)
{
    if (!m_connected) {
        return false;
    }
    
    QByteArray reportData = createReportRequestData(location, results);
    QByteArray header = createMessageHeader(REPORT_REQUEST, reportData.size());
    
    m_socket->write(header + reportData);
    m_socket->flush();
    
    emit errorOccurred(formatLogMessage(QString("Report uploaded: %1 results from %2")
                                       .arg(results.size()).arg(location)));
    return true;
}

void NetworkManager::processIncomingMessage(const QByteArray &data)
{
    if (data.size() < 8) {
        return; // 消息头不完整
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    quint32 msgType, dataLength;
    stream >> msgType >> dataLength;
    
    if (data.size() < 8 + dataLength) {
        return; // 数据不完整
    }
    
    QByteArray messageData = data.mid(8, dataLength);
    
    switch (msgType) {
    case LOGIN_OK:
        emit loginResult(true, "Login successful");
        emit errorOccurred(formatLogMessage("Login successful"));
        break;
    case LOGIN_FAIL:
        emit loginResult(false, "Login failed");
        emit errorOccurred(formatLogMessage("Login failed"));
        break;
    case LIST_RESPONSE:
        processServerListResponse(messageData);
        break;
    case REPORT_OK:
        emit errorOccurred(formatLogMessage("Report upload successful"));
        break;
    case REPORT_FAIL:
        emit errorOccurred(formatLogMessage("Report upload failed"));
        break;
    default:
        emit errorOccurred(formatLogMessage(QString("Unknown message type: 0x%1")
                                           .arg(msgType, 4, 16, QChar('0'))));
        break;
    }
}

void NetworkManager::processServerListResponse(const QByteArray &data)
{
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    quint32 serverCount;
    stream >> serverCount;
    
    QVariantList serverList;
    for (quint32 i = 0; i < serverCount; ++i) {
        quint32 serverId, ipAddr;
        stream >> serverId >> ipAddr;
        
        // 将IP地址从整数转换为字符串
        QString ipString = QString("%1.%2.%3.%4")
                          .arg((ipAddr >> 24) & 0xFF)
                          .arg((ipAddr >> 16) & 0xFF)
                          .arg((ipAddr >> 8) & 0xFF)
                          .arg(ipAddr & 0xFF);
        
        QVariantMap server;
        server["server_id"] = serverId;
        server["ip_address"] = ipString;
        serverList.append(server);
    }
    
    emit ipListReceived(serverList);
    emit errorOccurred(formatLogMessage(QString("Received %1 servers from server")
                                       .arg(serverCount)));
}

void NetworkManager::onReadyRead()
{
    m_receivedData.append(m_socket->readAll());
    
    // 处理完整的消息
    while (m_receivedData.size() >= 8) {
        QDataStream stream(m_receivedData);
        stream.setByteOrder(QDataStream::BigEndian);
        
        quint32 msgType, dataLength;
        stream >> msgType >> dataLength;
        
        if (m_receivedData.size() < 8 + dataLength) {
            break; // 等待更多数据
        }
        
        // 处理完整消息
        QByteArray completeMessage = m_receivedData.left(8 + dataLength);
        processIncomingMessage(completeMessage);
        
        // 移除已处理的数据
        m_receivedData = m_receivedData.mid(8 + dataLength);
    }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        errorString += "Connection refused (server not running or port blocked)";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorString += "Remote host closed the connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorString += "Host not found (check server address)";
        break;
    case QAbstractSocket::SocketAccessError:
        errorString += "Socket access error (insufficient permissions)";
        break;
    case QAbstractSocket::SocketResourceError:
        errorString += "Socket resource error (too many connections)";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorString += "Connection timeout (server not responding)";
        break;
    case QAbstractSocket::NetworkError:
        errorString += "Network error (check internet connection)";
        break;
    case QAbstractSocket::UnsupportedSocketOperationError:
        errorString += "Unsupported socket operation (proxy type invalid)";
        break;
    case QAbstractSocket::ProxyAuthenticationRequiredError:
        errorString += "Proxy authentication required";
        break;
    case QAbstractSocket::ProxyConnectionRefusedError:
        errorString += "Proxy connection refused";
        break;
    case QAbstractSocket::ProxyConnectionClosedError:
        errorString += "Proxy connection closed";
        break;
    case QAbstractSocket::ProxyConnectionTimeoutError:
        errorString += "Proxy connection timeout";
        break;
    case QAbstractSocket::ProxyNotFoundError:
        errorString += "Proxy not found";
        break;
    case QAbstractSocket::ProxyProtocolError:
        errorString += "Proxy protocol error";
        break;
    default:
        errorString += QString("Unknown error (%1)").arg(m_socket->errorString());
        break;
    }
    
    setConnectionStatus(errorString);
    emit errorOccurred(formatLogMessage(errorString));  // 使用格式化的日志
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
    // 如果路径为空，返回false但不是错误状态
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
