#include "networkmanager.h"
#include "message_protocol.h"
#include <QSslError>
#include <QFile>
#include <QDebug>
#include <QDataStream>
#include <QNetworkProxy>
#include <QDateTime>
#include <QTimer>
#include <QSslCipher>
#include <QHostAddress>
#include <QtEndian>
#include <QStandardPaths>
// Replace the existing QFileDialog include with this
#include <QtWidgets/QFileDialog>
#include <QtCore/QStandardPaths>

// 在NetworkManager构造函数中添加连接
NetworkManager::NetworkManager(QObject *parent, ConfigManager *configManager)
    : QObject(parent), m_socket(nullptr), m_connected(false), m_connectionStatus("Disconnected"), m_ignoreSslErrors(false), m_latencyChecker(nullptr), m_autoStartLatencyCheck(true)
{
    m_configManager = configManager; // 添加这行
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
    // 初始化LatencyChecker
    m_latencyChecker = new LatencyChecker(this);
    connect(m_latencyChecker, &LatencyChecker::checkingFinished, this, &NetworkManager::onLatencyCheckFinished);
    connect(m_latencyChecker, &LatencyChecker::latencyResult, this, &NetworkManager::onLatencyResult);
    connect(m_latencyChecker, &LatencyChecker::runningChanged, this, &NetworkManager::latencyCheckRunningChanged);
    connect(m_latencyChecker, &LatencyChecker::progressChanged, this, [this]()
            { emit latencyCheckProgress(m_latencyChecker->progress(), m_latencyChecker->totalIps()); });
    connect(m_latencyChecker, &LatencyChecker::logMessage, this, [this](const QString &message)
            { emit errorOccurred(message); }); // 移除多余括号
}

NetworkManager::~NetworkManager()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
    {
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

// 在connectToServer方法中添加CA证书配置
void NetworkManager::connectToServer(const QString &host, int port, bool ignoreSslErrors)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();
    }

    // 设置连接选项
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    // Store SSL error ignore setting
    m_ignoreSslErrors = ignoreSslErrors;

    // 添加这两行代码保存主机和端口
    m_currentHost = host;
    m_currentPort = port;

    // 使用通用函数配置SSL
    QSslConfiguration sslConfig = configureSslSocket(m_socket, ignoreSslErrors);

    // 记录TLS协议强制设置（不是错误）
    emit errorOccurred("Forcing TLS 1.2 or later protocol");

    // Connect to server
    m_socket->connectToHostEncrypted(host, port);

    // Wait for connection with timeout
    if (!m_socket->waitForConnected(5000))
    {
        QString errorMsg = QString("Connection test failed: %1").arg(m_socket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(errorMsg);
        emit testConnectionResult("Server is unreachable!", false);
        return;
    }

    // Wait for SSL handshake completion
    if (!m_socket->waitForEncrypted(5000))
    {
        QString errorMsg = QString("SSL handshake failed: %1").arg(m_socket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(errorMsg);
        return;
    }
}

void NetworkManager::disconnectFromServer()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();
    }
}

void NetworkManager::testConnection(const QString &host, int port, bool ignoreSslErrors)
{
    // 创建新的测试套接字
    QSslSocket *testSocket = new QSslSocket(this);

    // 禁用代理
    testSocket->setProxy(QNetworkProxy::NoProxy);

    // 设置忽略SSL错误标志
    testSocket->setObjectName("TestSocket");

    // 连接sslErrors信号到onSslErrors槽函数
    connect(testSocket, &QSslSocket::sslErrors, this, &NetworkManager::onSslErrors);

    // 使用通用函数配置SSL
    QSslConfiguration sslConfig = configureSslSocket(testSocket, ignoreSslErrors);

    // 连接到服务器
    setConnectionStatus("Testing connection...");
    emit errorOccurred(QString("Testing connection to %1:%2").arg(host).arg(port));

    testSocket->connectToHostEncrypted(host, port);

    // 等待连接建立
    if (!testSocket->waitForConnected(5000))
    {
        QString errorMsg = QString("Connection test failed: %1").arg(testSocket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(errorMsg);
        emit testConnectionResult("Server is unreachable!", false);
        testSocket->deleteLater();
        return;
    }

    // 等待SSL握手完成
    if (!testSocket->waitForEncrypted(5000))
    {
        QString errorMsg = QString("SSL handshake test failed: %1").arg(testSocket->errorString());
        setConnectionStatus(errorMsg);
        emit errorOccurred(errorMsg);
        emit testConnectionResult("Server is unreachable!", false);
        testSocket->deleteLater();
        return;
    }

    // Connection test successful, now disconnect
    emit errorOccurred("Connection test successful");
    setConnectionStatus("Connection test successful");

    // 简化测试连接结果消息，不包含TLS详细信息
    emit testConnectionResult("Server is available", true);
    testSocket->disconnectFromHost();
    testSocket->deleteLater();
}

bool NetworkManager::login(const QString &username, const QString &password)
{
    qDebug() << "Starting login process for user:" << username;
    emit errorOccurred(QString("Starting login process for user: %1").arg(username));

    if (!m_connected)
    {
        qDebug() << "Not connected, attempting auto-connect...";
        // emit errorOccurred("Connecting to server...");
        if (m_configManager)
        {
            QString serverIp = m_configManager->serverIp();
            int serverPort = m_configManager->serverPort();

            if (!serverIp.isEmpty() && serverPort > 0)
            {
                qDebug() << "Auto-connecting to server:" << serverIp << ":" << serverPort;
                emit errorOccurred(QString("Connecting to server: %1:%2").arg(serverIp).arg(serverPort));

                // 存储登录信息，等待连接完成后再发送
                m_pendingUsername = username;
                m_pendingPassword = password;
                m_hasPendingLogin = true;
                // emit errorOccurred(QString("Set pending login: %1, Username: %2").arg(m_hasPendingLogin ? "true" : "false").arg(username));

                connectToServer(serverIp, serverPort, false);
                return true; // 返回true表示登录流程已启动
            }
        }
        emit errorOccurred("Failed connecting to server");
        return false;
    }

    emit errorOccurred(QString("Already connected, sending login request directly for user: %1").arg(username));
    return sendLoginRequest(username, password);
}

// 修改现有的changePassword方法
void NetworkManager::changePassword(const QString &username, const QString &oldPassword, const QString &newPassword)
{
    // 使用sendChangePasswordRequest方法发送请求
    if (!sendChangePasswordRequest(username, oldPassword, newPassword))
    {
        emit changePasswordResult(false, QString("Failed to send password change request for user: ") + username);
    }
}

// 修改现有的sendChangePasswordRequest函数
bool NetworkManager::sendChangePasswordRequest(const QString &username, const QString &oldPassword, const QString &newPassword)
{
    if (!m_connected)
    {
        qDebug() << "Not connected, attempting auto-connect for password change...";
        if (m_configManager)
        {
            QString serverIp = m_configManager->serverIp();
            int serverPort = m_configManager->serverPort();

            if (!serverIp.isEmpty() && serverPort > 0)
            {
                qDebug() << "Auto-connecting to server for password change:" << serverIp << ":" << serverPort;
                emit errorOccurred(QString("Connecting to server for password change: %1:%2").arg(serverIp).arg(serverPort));

                // 存储密码修改信息，等待连接完成后再发送
                m_pendingPasswordChangeUsername = username;
                m_pendingOldPassword = oldPassword;
                m_pendingNewPassword = newPassword;
                m_hasPendingPasswordChange = true;

                connectToServer(serverIp, serverPort, false);
                return true; // 返回true表示密码修改流程已启动
            }
        }
        emit errorOccurred("Failed to connect to server for password change");
        return false;
    }

    // 创建并发送密码修改请求
    QByteArray passwordData = MessageProtocol::serializeChangePasswordRequest(username, oldPassword, newPassword);

    MessageHeader header(MessageType::CHANGE_PASSWORD_REQUEST, static_cast<quint32>(passwordData.size()));
    QByteArray headerData = MessageProtocol::serializeHeader(header);

    m_socket->write(headerData + passwordData);
    m_socket->flush();

    emit errorOccurred("Password change request sent, waiting for response...");
    return true;
}

QVariantList NetworkManager::requestIpList()
{
    if (!m_connected)
    {
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

    emit errorOccurred(tlsInfo);
    emit errorOccurred(cipherSuiteInfo);

    // 发送TLS版本信号供外部使用
    emit tlsVersionDetected(protocolVersion);
}

QString NetworkManager::getTlsProtocolVersion()
{
    QSslConfiguration sslConfig = m_socket->sslConfiguration();
    QSslCipher cipher = sslConfig.sessionCipher();

    // 尝试从密码套件获取协议版本
    if (!cipher.isNull())
    {
        QString protocolStr = cipher.protocolString();
        if (!protocolStr.isEmpty())
        {
            // 标准化协议名称
            if (protocolStr.contains("TLSv1.3"))
                return "TLS 1.3";
            if (protocolStr.contains("TLSv1.2"))
                return "TLS 1.2";
            if (protocolStr.contains("TLSv1.1"))
                return "TLS 1.1";
            if (protocolStr.contains("TLSv1"))
                return "TLS 1.0";
            if (protocolStr.contains("SSLv3"))
                return "SSL 3.0";
            return protocolStr;
        }
    }

    // 尝试从SSL配置获取
    QSsl::SslProtocol protocol = sslConfig.protocol();
    switch (protocol)
    {
    case QSsl::TlsV1_2:
        return "TLS 1.2";
    case QSsl::TlsV1_3:
        return "TLS 1.3";
    case QSsl::DtlsV1_2:
        return "DTLS 1.2";
    default:
    {
        // 对于自动协商的情况，尝试更深入的检测
        QByteArray sslLibraryVersion = QSslSocket::sslLibraryVersionString().toUtf8();
        if (sslLibraryVersion.contains("TLSv1.3"))
            return "TLS 1.3";
        if (sslLibraryVersion.contains("TLSv1.2"))
            return "TLS 1.2";
        return "Unknown (Negotiated)";
    }
    }
}

QString NetworkManager::getCipherSuiteInfo()
{
    QSslConfiguration sslConfig = m_socket->sslConfiguration();
    QSslCipher cipher = sslConfig.sessionCipher();

    if (cipher.isNull())
    {
        return "No cipher suite negotiated";
    }

    // 修复macMethod调用 - QSslCipher没有macMethod方法
    return QString("%1 (Key Exchange: %2, Authentication: %3, Encryption: %4)")
        .arg(cipher.name())
        .arg(cipher.keyExchangeMethod())
        .arg(cipher.authenticationMethod())
        .arg(cipher.encryptionMethod());
}

// 修改onConnected函数，添加密码修改请求处理
void NetworkManager::onConnected()
{
    setConnected(true);

    QString successMsg = QString("Successfully connected to %1:%2").arg(m_currentHost).arg(m_currentPort);
    setConnectionStatus(successMsg);
    emit errorOccurred(successMsg);

    // 如果有待发送的登录请求，现在发送
    if (m_hasPendingLogin)
    {
        emit errorOccurred("Sending pending login request");
        sendLoginRequest(m_pendingUsername, m_pendingPassword);
        m_hasPendingLogin = false;
        m_pendingUsername.clear();
        m_pendingPassword.clear();
    }
    // 如果有待发送的密码修改请求，现在发送
    else if (m_hasPendingPasswordChange)
    {
        emit errorOccurred("Sending pending password change request");
        sendChangePasswordRequest(m_pendingPasswordChangeUsername, m_pendingOldPassword, m_pendingNewPassword);
        m_hasPendingPasswordChange = false;
        m_pendingPasswordChangeUsername.clear();
        m_pendingOldPassword.clear();
        m_pendingNewPassword.clear();
    }
    else
    {
        emit errorOccurred("No pending requests to send");
    }
}

void NetworkManager::onDisconnected()
{
    setConnected(false);
    QString disconnectMsg = QString("Disconnected from %1:%2").arg(m_currentHost).arg(m_currentPort);
    setConnectionStatus(disconnectMsg);
    emit errorOccurred(disconnectMsg); // 使用格式化的日志
}

// 修改onSslErrors槽函数以支持测试套接字并处理颁发者证书错误
void NetworkManager::onSslErrors(const QList<QSslError> &errors)
{
    // 获取发送信号的套接字
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
        return;

    // 确定是否是测试套接字
    bool isTestSocket = (socket->objectName() == "TestSocket");
    if (isTestSocket)
    {
        socket->ignoreSslErrors(errors);
        return;
    }

    // 对于测试套接字或设置了忽略SSL错误的情况
    if (m_ignoreSslErrors || isTestSocket)
    {
        // 忽略以下类型的错误：
        QList<QSslError> errorsToIgnore;
        for (const QSslError &error : errors)
        {
            if (error.error() == QSslError::HostNameMismatch ||
                error.error() == QSslError::SelfSignedCertificateInChain)
            {
                errorsToIgnore.append(error);
                QString errorMsg = QString("Ignoring SSL error: %1(%2)").arg(error.error()).arg(error.errorString());
                emit errorOccurred(errorMsg);
            }
        }

        if (!errorsToIgnore.isEmpty())
        {
            socket->ignoreSslErrors(errorsToIgnore);
        }

        // 如果还有其他错误没有被忽略，记录下来
        if (errors.size() > errorsToIgnore.size())
        {
            QString errorMsg = "SSL Errors: ";
            for (const QSslError &error : errors)
            {
                if (!errorsToIgnore.contains(error))
                {
                    errorMsg += error.errorString() + "; ";
                }
            }
            emit errorOccurred(errorMsg);
        }
    }
    else
    {
        // 标准行为：只忽略主机名不匹配的错误
        QList<QSslError> errorsToIgnore;
        for (const QSslError &error : errors)
        {
            if (error.error() == QSslError::HostNameMismatch)
            {
                errorsToIgnore.append(error);
            }
        }

        if (!errorsToIgnore.isEmpty())
        {
            socket->ignoreSslErrors(errorsToIgnore);
        }

        // 如果还有其他错误没有被忽略，记录下来
        if (errors.size() > errorsToIgnore.size())
        {
            QString errorMsg = "SSL Errors: ";
            for (const QSslError &error : errors)
            {
                if (!errorsToIgnore.contains(error))
                {
                    errorMsg += error.errorString() + "; ";
                }
            }
            emit errorOccurred(errorMsg);
        }
    }
}

QByteArray NetworkManager::createMessageHeader(quint32 msgType, quint32 dataLength)
{
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << msgType << dataLength;
    return header;
}

QByteArray NetworkManager::createLoginRequestData(const QString &username, const QString &password)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // 用户名32字节
    QByteArray usernameBytes = username.toUtf8();
    usernameBytes.resize(32, '\0');
    stream.writeRawData(usernameBytes.constData(), 32);

    // 明文密码32字节（通过TLS安全传输）
    QByteArray passwordBytes = password.toUtf8();
    passwordBytes.resize(32, '\0');
    stream.writeRawData(passwordBytes.constData(), 32);

    return data;
}

bool NetworkManager::sendLoginRequest(const QString &username, const QString &password)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
    {
        emit errorOccurred("Not connected to server");
        return false;
    }

    // Use MessageProtocol's serialization methods
    QByteArray loginData = MessageProtocol::serializeLoginRequest(username, password);
    MessageHeader header(MessageType::LOGIN_REQUEST, loginData.size());
    QByteArray headerData = MessageProtocol::serializeHeader(header);

    // Send complete message
    QByteArray completeMessage = headerData + loginData;
    m_socket->write(completeMessage);

    emit errorOccurred(QString("Sent login request (%1 bytes)").arg(completeMessage.size()));
    return true;
}
bool NetworkManager::sendListRequest()
{
    if (!m_connected)
    {
        emit errorOccurred("Not connected to server");
        return false;
    }

    // Create and send list request
    MessageHeader header(MessageType::LIST_REQUEST, 0);
    QByteArray headerData = MessageProtocol::serializeHeader(header);

    m_socket->write(headerData);
    m_socket->flush();

    emit errorOccurred("→ Server list request sent, waiting for response...");
    return true;
}

void NetworkManager::processIncomingMessage()
{
    // 处理缓冲区中的所有完整消息
    while (m_receivedData.size() >= sizeof(MessageHeader))
    {
        // 直接使用指针转换解析消息头（高效）
        const char *rawData = m_receivedData.constData();
        const MessageHeader *headerPtr = reinterpret_cast<const MessageHeader *>(rawData);

        // 处理字节序
        MessageHeader header;
        header.msgType = qFromBigEndian(headerPtr->msgType);
        header.dataLength = qFromBigEndian(headerPtr->dataLength);

        // 检查是否有完整消息
        const int totalMessageSize = sizeof(MessageHeader) + header.dataLength;
        if (m_receivedData.size() < totalMessageSize)
        {
            emit errorOccurred(QString("Waiting for more data - Need: %1, Have: %2")
                                   .arg(totalMessageSize)
                                   .arg(m_receivedData.size()));
            break;
        }

        // 验证数据长度合理性
        if (header.dataLength > 1024 * 1024) // 1MB 限制
        {
            emit errorOccurred(QString("Message too large: %1 bytes").arg(header.dataLength));
            m_receivedData.clear(); // 清空缓冲区，避免恶意数据
            break;
        }

        // 提取消息数据（跳过消息头）
        QByteArray messageData;
        if (header.dataLength > 0)
        {
            messageData = m_receivedData.mid(sizeof(MessageHeader), header.dataLength);
        }

        // 处理具体消息类型
        handleMessage(static_cast<MessageType>(header.msgType), messageData);

        // 从缓冲区移除已处理的数据
        m_receivedData = m_receivedData.mid(totalMessageSize);
    }
}

// 新增：专门处理消息类型的函数
void NetworkManager::handleMessage(MessageType msgType, const QByteArray &messageData)
{
    switch (msgType)
    {
    case MessageType::LOGIN_OK:
        emit errorOccurred("✅ Login successful");
        emit loginResult(true, "");
        break;
    case MessageType::LOGIN_FAIL:
        emit errorOccurred("❌ Login failed");
        emit loginResult(false, "Login failed");
        disconnectFromServer();
        break;
    case MessageType::LIST_RESPONSE:
        processServerListResponse(messageData);
        break;
    case MessageType::REPORT_OK:
        // emit errorOccurred("📤 Latency report uploaded successfully");
        emit reportUploadResult(true, "", "");
        break;
    case MessageType::REPORT_FAIL:
        // emit errorOccurred("❌ Latency report upload failed");
        emit reportUploadResult(false, "", "Upload failed");
        break;
    case MessageType::CHANGE_PASSWORD_RESPONSE:
    {
        ChangePasswordResponseData response = MessageProtocol::deserializeChangePasswordResponse(messageData);
        if (response.resultCode == 0)
        {
            // emit errorOccurred("✅ Password changed successfully");
            emit changePasswordResult(true, "Password changed successfully");
        }
        else
        {
            // emit errorOccurred("❌ Password change failed");
            emit changePasswordResult(false, QString("error code:%1").arg(response.resultCode));
        }
        break;
    }
    default:
        emit errorOccurred(QString("Unknown message type: 0x%1").arg(static_cast<quint32>(msgType), 4, 16, QChar('0')));
        break;
    }
}

// 修改createReportRequestData方法以符合服务器期望的格式
QByteArray NetworkManager::createReportRequestData(const QString &location, const QVariantList &results)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 写入位置信息（32字节，不足补0）
    QByteArray locationBytes = location.toUtf8();
    locationBytes = locationBytes.leftJustified(32, '\0', true);
    stream.writeRawData(locationBytes.constData(), 32);

    // 写入结果数量
    stream << static_cast<quint32>(results.size());

    // 写入每个结果
    for (const QVariant &result : results)
    {
        QVariantMap resultMap = result.toMap();

        // 服务器ID
        stream << static_cast<quint32>(resultMap["server_id"].toUInt());

        // IP地址（转换为32位整数）
        QString ipStr = resultMap["ip_address"].toString();
        QHostAddress addr(ipStr);
        stream << addr.toIPv4Address();

        // 延迟值（毫秒，转换为double）
        stream << static_cast<double>(resultMap["latency"].toDouble());

        // 状态（成功=1，失败=0）
        stream << static_cast<quint32>(resultMap["success"].toBool() ? 1 : 0);

        // 测试时间（Unix时间戳）
        QDateTime testTime = resultMap["test_time"].toDateTime();
        stream << static_cast<quint64>(testTime.toSecsSinceEpoch());
    }

    return data;
}

bool NetworkManager::sendReportRequest(const QString &location, const QVariantList &results)
{
    if (!m_connected)
    {
        emit errorOccurred("Not connected to server");
        return false;
    }

    // 将QVariantList转换为QList<LatencyRecord>
    QList<LatencyRecord> records;
    for (const QVariant &result : results)
    {
        QVariantMap resultMap = result.toMap();
        LatencyRecord record;
        record.serverId = static_cast<quint32>(resultMap["server_id"].toUInt());
        record.latency = static_cast<quint32>(resultMap["latency"].toUInt());
        records.append(record);
    }

    // 使用MessageProtocol::serializeReportRequest序列化数据
    QByteArray reportData = MessageProtocol::serializeReportRequest(location, records);

    // 创建消息头并发送
    QByteArray header = MessageProtocol::serializeHeader(
        MessageHeader(MessageType::REPORT_REQUEST, static_cast<quint32>(reportData.size())));

    m_socket->write(header + reportData);
    m_socket->flush();

    emit errorOccurred("Report request sent");
    return true;
}

void NetworkManager::onReadyRead()
{
    QByteArray newData = m_socket->readAll();
    // emit errorOccurred(QString("Received %1 bytes").arg(newData.size()));

    m_receivedData.append(newData);

    // 简单地尝试处理缓冲区中的数据
    processIncomingMessage();
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString;
    switch (error)
    {
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
    emit errorOccurred(errorString); // 使用格式化的日志
}

void NetworkManager::setConnected(bool connected)
{
    if (m_connected != connected)
    {
        m_connected = connected;
        emit connectedChanged();
    }
}

void NetworkManager::setConnectionStatus(const QString &status)
{
    if (m_connectionStatus != status)
    {
        m_connectionStatus = status;
        emit connectionStatusChanged();
    }
}

void NetworkManager::sendRequest(const QString &request, const QByteArray &data)
{
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream << request;
    if (!data.isEmpty())
    {
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

    for (quint32 i = 0; i < count; ++i)
    {
        quint32 ip;
        stream >> ip;
        QHostAddress address(ip);
        ipList.append(address.toString());
    }

    return ipList;
}

bool NetworkManager::latencyCheckRunning() const
{
    return m_latencyChecker ? m_latencyChecker->running() : false;
}

void NetworkManager::startLatencyCheck(int threadCount)
{
    if (!m_latencyChecker || m_currentServerList.isEmpty())
    {
        emit errorOccurred("No servers available for latency check");
        return;
    }

    emit errorOccurred("Starting latency check...");

    // 传递完整的服务器列表而不仅仅是IP列表
    m_latencyChecker->startChecking(m_currentServerList, threadCount);
}

void NetworkManager::stopLatencyCheck()
{
    if (m_latencyChecker)
    {
        m_latencyChecker->stopChecking();
        emit errorOccurred("Latency check stopped");
    }
}

void NetworkManager::onLatencyResult(quint32 serverId, quint32 ipAddr, int latency)
{
    QString ipString = QHostAddress(ipAddr).toString();
    QString message = QString("Server ID %1 (%2): %3ms")
                          .arg(serverId)
                          .arg(ipString)
                          .arg(latency >= 0 ? QString::number(latency) : "Failed");

    emit latencyCheckProgress(m_latencyChecker->progress(), m_latencyChecker->totalIps());

    // If you need to maintain backward compatibility with UI expecting string IP:
    // You can emit a separate signal or convert the data as needed
}

void NetworkManager::onLatencyCheckFinished(const QVariantList &results)
{
    emit errorOccurred(QString("✅ Latency check completed for %1 servers").arg(results.size()));
    emit latencyCheckFinished(results);
}

// 修改processServerListResponse函数，添加自动启动延时检测
void NetworkManager::processServerListResponse(const QByteArray &data)
{
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 serverCount;
    stream >> serverCount;

    QVariantList serverList;
    for (quint32 i = 0; i < serverCount; ++i)
    {
        quint32 serverId, ipAddr;
        stream >> serverId >> ipAddr;

        QVariantMap server;
        server["server_id"] = serverId;
        // server["ip_address"] = ipString;
        server["ip_address"] = ipAddr;
        serverList.append(server);
    }

    m_currentServerList = serverList;
    emit ipListReceived(serverList);
    emit errorOccurred(QString("✅ Received %1 servers from server")
                           .arg(serverCount));

    // 自动启动延时检测
    if (m_autoStartLatencyCheck && !serverList.isEmpty())
    {
        emit errorOccurred("Auto-starting latency check...");
        startLatencyCheck(m_configManager->threadCount());
    }
}

// 修改saveIpListToFile方法，使用函数内静态变量记住最后保存路径
bool NetworkManager::saveIpListToFile(const QString &filePath, const QVariantList &ipList)
{
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "Failed to open file for writing:" << filePath << "Error:" << file.errorString();
        emit errorOccurred(QString("Failed to open file for writing: %1, Error: %2").arg(filePath).arg(file.errorString()));
        return false;
    }

    QTextStream out(&file);
    foreach (const QVariant &ip, ipList)
    {
        out << ip.toString() << "\n";
    }

    file.close();

    qDebug() << "IP list successfully saved to:" << filePath;
    emit errorOccurred(QString("✅ IP list successfully saved to: %1").arg(filePath));
    return true;
}

// 修改saveIpListToFile()无参方法，直接保存到默认位置而不使用文件对话框
bool NetworkManager::saveIpListToFile()
{
    // 检查是否有保存的IP列表
    if (m_currentServerList.isEmpty())
    {
        emit errorOccurred("No IP list available to save.");
        return false;
    }

    // 转换IP格式
    QVariantList formattedIpList;
    foreach (const QVariant &server, m_currentServerList)
    {
        QVariantMap serverMap = server.toMap();
        if (serverMap.contains("ip_address"))
        {
            // 如果IP是整数形式，转换为点分十进制格式
            QVariant ipValue = serverMap["ip_address"];
            if (ipValue.typeId() == QMetaType::UInt)
            {
                quint32 ipAddr = ipValue.toUInt();
                QHostAddress hostAddr(ipAddr);
                formattedIpList.append(hostAddr.toString());
            }
            else
            {
                formattedIpList.append(ipValue.toString());
            }
        }
    }

    // 获取默认保存路径（我的文档目录）并设置文件名
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString filePath = documentsPath + QDir::separator() + "ip_list.txt";

    // 调用现有的保存方法
    return saveIpListToFile(filePath, formattedIpList);
}

QSslConfiguration NetworkManager::configureSslSocket(QSslSocket *socket, bool ignoreSslErrors)
{
    if (!socket)
        return QSslConfiguration();

    // 强制使用TLS 1.2或更高版本
    QSslConfiguration sslConfig = socket->sslConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);

    // 配置SSL选项
    if (m_configManager)
    {
        // 先设置SSL验证模式和CA证书
        if (ignoreSslErrors)
        {
            sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
            emit errorOccurred("SSL verification disabled");
        }
        else
        {
            // 只验证证书链是否由同一CA颁发，忽略主机名验证
            sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
            emit errorOccurred("SSL verification enabled");
        }

        // 使用内嵌的CA证书作为颁发者证书
        QSslCertificate caCert = m_configManager->getCACertificate();
        if (!caCert.isNull())
        {
            emit errorOccurred("CA certificate is configured");
            QList<QSslCertificate> caCerts = sslConfig.caCertificates();
            caCerts.append(caCert);
            sslConfig.setCaCertificates(caCerts);
        }
        else
        {
            emit errorOccurred("CA certificate is null");
        }

        // 然后设置客户端证书和私钥到sslConfig对象，而不是直接设置到套接字
        QSslCertificate clientCert = m_configManager->getPublicCert();
        QSslKey privateKey = m_configManager->getPrivateKey();

        if (!clientCert.isNull())
        {
            sslConfig.setLocalCertificate(clientCert);
            // emit errorOccurred("Client certificate set to SSL config successfully");
            // 添加调试信息显示证书信息
            emit errorOccurred(QString("Certificate subject: %1").arg(clientCert.subjectInfo(QSslCertificate::CommonName).join(", ")));
            emit errorOccurred(QString("Certificate issuer: %1").arg(clientCert.issuerInfo(QSslCertificate::CommonName).join(", ")));
        }
        else
        {
            emit errorOccurred("Client certificate is not configured");
        }

        if (!privateKey.isNull())
        {
            sslConfig.setPrivateKey(privateKey);
            emit errorOccurred("Client key set to SSL config successfully");
            // emit errorOccurred(QString("Private key algorithm: %1").arg(privateKey.algorithm()));
        }
        else
        {
            emit errorOccurred(QString("Client key is not configured"));
        }

        // 添加证书链配置到sslConfig对象
        QList<QSslCertificate> certChain;
        certChain.append(clientCert);
        certChain.append(caCert);
        sslConfig.setLocalCertificateChain(certChain);
        emit errorOccurred(QString("Certificate chain length: %1").arg(certChain.size()));
    }
    else
    {
        emit errorOccurred("Certificates are not able to load");
    }

    // 一次性将所有SSL配置应用到套接字
    socket->setSslConfiguration(sslConfig);

    // 确认证书设置
    if (socket->localCertificate().isNull())
    {
        emit errorOccurred("WARNING: Local certificate is NULL after configuration!");
    }
    else
    {
        emit errorOccurred("SUCCESS: Local certificate is correctly set after configuration!");
        emit errorOccurred(QString("Verified certificate subject: %1").arg(socket->localCertificate().subjectInfo(QSslCertificate::CommonName).join(", ")));
    }

    return sslConfig;
}
