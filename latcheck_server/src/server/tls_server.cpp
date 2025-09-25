#include "server/tls_server.h"
#include "config/config_manager.h"
#include "logger/logger.h"
#include "auth/auth_manager.h"
#include "database/user_dao.h"
#include "database/report_dao.h"
#include "protocol/message_protocol.h"
#include "common/error_codes.h"
#include "auth/password_utils.h"

#include <arpa/inet.h> // 添加这一行以引入ntohl函数
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslCipher>
#include <QFile>
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QHostAddress>
#include <QTimer>

TlsServer::TlsServer(QObject *parent)
    : QTcpServer(parent), config_manager_(nullptr), user_dao_(nullptr), report_dao_(nullptr),
      cleanup_timer_(new QTimer(this)), max_connections_(100), connection_timeout_(300) // 5分钟
      ,
      auth_timeout_(30), // 30秒
      auth_manager_(nullptr),
      use_whitelist_(false),
      use_blacklist_(false)
{
    // 设置清理定时器
    cleanup_timer_->setInterval(60000); // 每分钟清理一次
    connect(cleanup_timer_, &QTimer::timeout, this, &TlsServer::onCleanupTimer);
}

TlsServer::~TlsServer()
{
    stopServer();
}

bool TlsServer::startServer(const QString &host, quint16 port)
{
    if (!initializeSsl())
    {
        Logger::instance()->error("Failed to initialize SSL configuration");
        return false;
    }

    QHostAddress address;
    if (host == "0.0.0.0" || host.isEmpty())
    {
        // 明确监听IPv4的所有地址
        address = QHostAddress::AnyIPv4;
    }
    else if (host == "::" || host == "[::]" || host.toLower() == "any")
    {
        // 监听IPv6的所有地址
        address = QHostAddress::AnyIPv6;
    }
    else if (host.toLower() == "dual" || host.toLower() == "all")
    {
        // 尝试同时监听IPv4和IPv6
        address = QHostAddress::Any;
    }
    else
    {
        // 使用指定的IP地址
        address.setAddress(host);
        if (address.isNull())
        {
            Logger::instance()->error(
                QString("Invalid host address: %1").arg(host));
            return false;
        }
    }

    if (!listen(address, port))
    {
        Logger::instance()->error(
            QString("Failed to start server on %1:%2 - %3")
                .arg(host)
                .arg(port)
                .arg(errorString()));
        return false;
    }

    cleanup_timer_->start();
    Logger::instance()->info(
        QString("TLS Server started on %1:%2 (listening on %3)")
            .arg(host)
            .arg(port)
            .arg(serverAddress().toString()));
    return true;
}

// 保持向后兼容的重载方法
bool TlsServer::startServer(quint16 port)
{
    return startServer("0.0.0.0", port);
}

void TlsServer::stopServer()
{
    if (isListening())
    {
        close();

        // 断开所有客户端连接
        QMutexLocker locker(&clients_mutex_);
        for (auto it = clients_.begin(); it != clients_.end(); ++it)
        {
            if (it.value()->socket)
            {
                it.value()->socket->disconnectFromHost();
                it.value()->socket->deleteLater();
            }
            delete it.value();
        }
        clients_.clear();

        cleanup_timer_->stop();
        Logger::instance()->info("TLS Server stopped");
    }
}

int TlsServer::getConnectionCount() const
{
    QMutexLocker locker(&clients_mutex_);
    return clients_.size();
}

int TlsServer::getAuthenticatedUserCount() const
{
    QMutexLocker locker(&clients_mutex_);
    int count = 0;
    for (const ClientSession *session : clients_)
    {
        if (session && session->state == ClientState::Authenticated)
        {
            count++;
        }
    }
    return count;
}

void TlsServer::setConfigManager(QSharedPointer<ConfigManager> config)
{
    config_manager_ = config;
}

void TlsServer::setUserDAO(QSharedPointer<UserDAO> userDAO)
{
    user_dao_ = userDAO;
}

void TlsServer::setReportDAO(QSharedPointer<ReportDAO> reportDAO)
{
    report_dao_ = reportDAO;
}

void TlsServer::setServerDAO(QSharedPointer<ServerDAO> serverDAO)
{
    server_dao_ = serverDAO;
}

void TlsServer::setAuthManager(QSharedPointer<AuthManager> authManager)
{
    auth_manager_ = authManager;
}

// 修复incomingConnection方法
void TlsServer::incomingConnection(qintptr socketDescriptor)
{
    // 检查连接数限制
    if (getConnectionCount() >= max_connections_)
    {
        QTcpSocket tempSocket;
        tempSocket.setSocketDescriptor(socketDescriptor);
        QString clientIp = tempSocket.peerAddress().toString();
        quint16 clientPort = tempSocket.peerPort();
        Logger::instance()->info(
            QString("Connection rejected - Max connections reached (%1:%2)")
                .arg(clientIp)
                .arg(clientPort));
        tempSocket.disconnectFromHost();
        return;
    }

    QSslSocket *sslSocket = new QSslSocket(this);
    if (!sslSocket->setSocketDescriptor(socketDescriptor))
    {
        Logger::instance()->error("Failed to set socket descriptor");
        delete sslSocket;
        return;
    }

    // 获取并记录客户端连接信息
    QString clientIp = sslSocket->peerAddress().toString();
    quint16 clientPort = sslSocket->peerPort();
    Logger::instance()->info(
        QString("New incoming connection: %1:%2 (total connections: %3)")
            .arg(clientIp)
            .arg(clientPort)
            .arg(getConnectionCount() + 1));

    // 创建会话
    ClientSession *session = new ClientSession();
    session->socket = sslSocket;
    session->connectTime = QDateTime::currentDateTime();
    session->lastActiveTime = session->connectTime;
    session->state = ClientState::Connected;

    // 将会话添加到客户端列表
    {
        QMutexLocker locker(&clients_mutex_);
        clients_.insert(sslSocket, session);
    }

    // 创建登录超时定时器并关联到会话
    session->loginTimer = new QTimer(this);
    session->loginTimer->setSingleShot(true);
    session->loginTimer->setInterval(10000); // 10秒

    connect(session->loginTimer, &QTimer::timeout, [this, sslSocket, session]()
            {
        if (session && session->state != ClientState::Authenticated) {
            Logger::instance()->warning("Login timeout - disconnecting session");
            if (sslSocket->isOpen()) {
                sslSocket->disconnectFromHost();
            }
        }
        if (session->loginTimer) {
            session->loginTimer->deleteLater();
            session->loginTimer = nullptr;
        } });

    // 当socket断开连接时清理登录定时器资源
    connect(sslSocket, &QSslSocket::disconnected, [session]()
            {
        if (session->loginTimer) {
            session->loginTimer->stop();
            session->loginTimer->deleteLater();
            session->loginTimer = nullptr;
        } });

    // 设置SSL连接信号和槽
    connect(sslSocket, &QSslSocket::encrypted, this, &TlsServer::onSslReady);
    connect(sslSocket, &QSslSocket::readyRead, this, &TlsServer::onDataReceived);
    connect(sslSocket, &QSslSocket::disconnected, this, &TlsServer::onClientDisconnected);
    connect(sslSocket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, &TlsServer::onSslErrors);

    // 配置SSL并启动握手
    sslSocket->setSslConfiguration(ssl_config_);
    sslSocket->startServerEncryption(); // 关键：启动SSL服务器端握手

    // 启动登录超时定时器
    session->loginTimer->start();

    Logger::instance()->debug(
        QString("SSL handshake initiated for session: %1:%2")
            .arg(clientIp)
            .arg(clientPort));
}

void TlsServer::onClientDisconnected()
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
        return;

    QMutexLocker locker(&clients_mutex_);
    ClientSession *session = clients_.value(socket);

    if (session)
    {
        QString clientIp = socket->peerAddress().toString();
        quint16 clientPort = socket->peerPort();
        QString userName = session->userName.isEmpty() ? "(unauthenticated)" : session->userName;

        Logger::instance()->info(
            QString("Client disconnected: %1:%2 (user: %3, total connections: %4)")
                .arg(clientIp)
                .arg(clientPort)
                .arg(userName)
                .arg(clients_.size() - 1));

        delete session;
    }

    clients_.remove(socket);
    socket->deleteLater();
}

void TlsServer::onCleanupTimer()
{
    QDateTime now = QDateTime::currentDateTime();
    QMutexLocker locker(&clients_mutex_);

    for (auto it = clients_.begin(); it != clients_.end();)
    {
        ClientSession *session = it.value();

        // 清理未认证超时的连接
        if (session->state == ClientState::Connected &&
            session->connectTime.secsTo(now) > auth_timeout_)
        {

            Logger::instance()->warning(
                QString("Authentication timeout for session from %1")
                    .arg(session->socket->peerAddress().toString()));

            session->socket->disconnectFromHost();
            delete session;
            it = clients_.erase(it);
            continue;
        }

        // 清理长时间不活跃的连接
        if (session->lastActiveTime.secsTo(now) > connection_timeout_)
        {
            Logger::instance()->info(
                QString("Connection timeout for user: %1").arg(session->userName));

            session->socket->disconnectFromHost();
            delete session;
            it = clients_.erase(it);
            continue;
        }

        ++it;
    }
}

// 修改initializeSsl方法，使用正确的SSL验证配置
bool TlsServer::initializeSsl()
{
    if (!config_manager_)
    {
        return false;
    }

    QString certPath = config_manager_->getCertificatePath();
    QString keyPath = config_manager_->getPrivateKeyPath();
    QString caCertPath = config_manager_->getCaCertificatePath();
    bool requireClientCert = config_manager_->getRequireClientCert();
    use_whitelist_ = config_manager_->getUseWhitelist();
    use_blacklist_ = config_manager_->getUseBlacklist();
    whitelist_path_ = config_manager_->getWhitelistPath();
    blacklist_path_ = config_manager_->getBlacklistPath();

    // 加载证书
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly))
    {
        Logger::instance()->error(
            QString("Failed to open certificate file: %1").arg(certPath));
        return false;
    }

    QSslCertificate cert(&certFile, QSsl::Pem);
    if (cert.isNull())
    {
        Logger::instance()->error("Invalid certificate");
        return false;
    }

    // 加载私钥
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly))
    {
        Logger::instance()->error(
            QString("Failed to open private key file: %1").arg(keyPath));
        return false;
    }

    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (key.isNull())
    {
        Logger::instance()->error("Invalid private key");
        return false;
    }

    // 加载CA证书
    if (requireClientCert && !caCertPath.isEmpty())
    {
        if (!loadCaCertificates())
        {
            Logger::instance()->error("Failed to load CA certificates");
            return false;
        }
    }

    // 加载白名单和黑名单
    if (use_whitelist_ && !whitelist_path_.isEmpty())
    {
        if (!loadSubjectList(whitelist_path_, whitelisted_subjects_))
        {
            Logger::instance()->warning(QString("Failed to load whitelist: %1").arg(whitelist_path_));
        }
        else
        {
            Logger::instance()->info("Whitelist is used");
        }
    }

    if (use_blacklist_ && !blacklist_path_.isEmpty())
    {
        if (!loadSubjectList(blacklist_path_, blacklisted_subjects_))
        {
            Logger::instance()->warning(QString("Failed to load blacklist: %1").arg(blacklist_path_));
        }
        else
        {
            Logger::instance()->info("blacklist is used");
        }
    }

    // 配置SSL
    ssl_config_.setLocalCertificate(cert);
    ssl_config_.setPrivateKey(key);

    if (requireClientCert)
    {
        Logger::instance()->warning("Client certificate is required");
        // 使用更严格的验证模式，强制要求客户端提供证书
        ssl_config_.setPeerVerifyMode(QSslSocket::VerifyPeer);
        ssl_config_.setPeerVerifyDepth(1); // 设置验证深度
        // // 确保SSL握手过程中请求客户端证书
        // ssl_config_.setSslOption(QSsl::SslOptionDisableEmptyFragments, true);
    }
    ssl_config_.setProtocol(QSsl::TlsV1_2OrLater);

    // 设置CA证书
    if (!ca_certificates_.isEmpty())
    {
        ssl_config_.setCaCertificates(ca_certificates_);
    }

    return true;
}

void TlsServer::handleLoginRequest(ClientSession *session, const QByteArray &data)
{
    if (!user_dao_)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::ServerInternal);
        return;
    }

    LoginRequestData loginData = MessageProtocol::deserializeLoginRequest(data);
    QString userName = QString::fromUtf8(loginData.userName);
    QString plainPassword = QString::fromUtf8(loginData.password);

    Logger::instance()->debug(
        QString("Processing login request for user: %1").arg(userName));

    // 获取用户信息（包含密码哈希和盐值）
    User user = user_dao_->getUserByUsername(userName);
    user_dao_->printUser(user); // 调试输出用户信息
    if (user.id == 0)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidUser);
        Logger::instance()->warning(
            QString("Login failed - User not found: %1").arg(userName));
        return;
    }

    // 检查用户状态
    if (user.status != UserStatus::Active)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::UserDisabled);
        Logger::instance()->warning(
            QString("Login failed - User disabled: %1").arg(userName));
        return;
    }

    // 使用PasswordUtils验证密码
    if (!PasswordUtils::verifyPassword(plainPassword, user.passwordHash, user.salt))
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidPassword);
        Logger::instance()->warning(
            QString("Login failed - Invalid password for user: %1").arg(userName));
        return;
    }

    // 检查用户权限（报告上传用户和管理员可以上传报告）
    if (user.role != UserRole::ReportUploader && user.role != UserRole::Admin)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::PermissionDenied);
        Logger::instance()->warning(
            QString("Login failed - Insufficient permissions: %1").arg(userName));
        return;
    }

    // 登录成功
    session->state = ClientState::Authenticated;
    session->userName = userName;
    session->isAuthenticated = true;
    updateClientActivity(session);

    // 登录成功后停止登录超时定时器
    if (session->loginTimer)
    {
        session->loginTimer->stop();
        session->loginTimer->deleteLater();
        session->loginTimer = nullptr;
    }

    // 发送成功响应 - 使用序列化处理字节序
    quint32 code = qToBigEndian(static_cast<quint32>(ErrorCode::Success));
    QByteArray response(reinterpret_cast<const char *>(&code), sizeof(code));
    sendResponse(session, MessageType::LOGIN_OK, response);

    Logger::instance()->info(
        QString("User logged in successfully: %1").arg(userName));
}
void TlsServer::handleReportRequest(ClientSession *session, const QByteArray &data)
{
    // 检查report_dao_是否有效
    if (!report_dao_)
    {
        Logger::instance()->error("ReportDAO is not set");
        sendResponse(session, MessageType::REPORT_FAIL, QByteArray());
        return;
    }

    try
    {
        ReportRequestData reportData = MessageProtocol::deserializeReportRequest(data);

        // 创建报告对象
        Report report;
        report.userName = session->userName;
        report.location = reportData.location;
        report.createdAt = QDateTime::currentDateTime();

        // 检查客户端会话中是否有服务器映射
        if (session->serverIpMap.isEmpty())
        {
            Logger::instance()->warning(
                QString("No server IP mapping found in session for user %1")
                    .arg(session->userName),
                "TlsServer");

            // 如果会话中没有映射，检查是否有服务器列表并创建映射
            if (!session->servers.isEmpty())
            {
                Logger::instance()->info("Creating server IP mapping from session server list", "TlsServer");
                for (const auto &server : session->servers)
                {
                    session->serverIpMap[server.serverId] = server.ipAddr;
                }
            }
            else
            {
                // 如果两者都没有，则从数据库获取（作为后备方案）
                Logger::instance()->warning("Fetching server list from database as fallback", "TlsServer");
                QList<ServerInfo> servers = getTestServerList();
                session->servers = servers;
                for (const auto &server : servers)
                {
                    session->serverIpMap[server.serverId] = server.ipAddr;
                }
            }
        }

        // 转换LatencyRecord为ReportRecord
        QList<ReportRecord> reportRecords;
        for (const LatencyRecord &record : reportData.records)
        {
            ReportRecord reportRecord;
            reportRecord.serverId = record.serverId;
            reportRecord.latency = record.latency;

            // 使用映射表查找IP地址
            quint32 serverIp = 0;
            if (session->serverIpMap.contains(record.serverId))
            {
                serverIp = session->serverIpMap.value(record.serverId);
            }
            else
            {
                // 如果找不到 IP，记录警告日志
                Logger::instance()->warning(QString("Failed to find IP for server ID: %1 when processing report from user: %2")
                                                .arg(record.serverId)
                                                .arg(session->userName));
            }

            reportRecord.serverIp = serverIp;
            reportRecords.append(reportRecord);
        }

        // 保存报告和记录（使用事务）
        ErrorCode result = report_dao_->createReport(report, reportRecords);

        if (result != ErrorCode::Success)
        {
            Logger::instance()->warning(QString("Failed to create report for user %1: %2")
                                            .arg(session->userName)
                                            .arg(static_cast<int>(result)),
                                        "TlsServer");
            sendErrorResponse(session, MessageType::REPORT_FAIL, result);
        }
        else
        {
            Logger::instance()->info(QString("Report created successfully for user %1")
                                         .arg(session->userName),
                                     "TlsServer");
            sendErrorResponse(session, MessageType::REPORT_OK, result);
        }
    }
    catch (const std::exception &e)
    {
        Logger::instance()->error(QString("Exception handling report request: %1")
                                      .arg(e.what()),
                                  "TlsServer");
        sendErrorResponse(session, MessageType::REPORT_FAIL, ErrorCode::ServerInternal);
    }
}

bool TlsServer::checkClientPermission(const QString &userName, int requiredLevel)
{
    if (!user_dao_)
        return false;

    User user = user_dao_->getUserByUsername(userName);
    return user.id != 0 && static_cast<int>(user.role) >= requiredLevel;
}

void TlsServer::sendResponse(ClientSession *session, MessageType type, const QByteArray &data)
{
    if (!session || !session->socket || !session->socket->isValid())
    {
        return;
    }

    MessageHeader header(type, data.size());
    QByteArray headerData = MessageProtocol::serializeHeader(header);

    session->socket->write(headerData);
    if (!data.isEmpty())
    {
        session->socket->write(data);
    }
}

// 添加缺失的方法实现
void TlsServer::sendErrorResponse(ClientSession *session, MessageType type, ErrorCode errorCode)
{
    if (!session || !session->socket || !session->socket->isValid())
    {
        return;
    }

    // 使用序列化方法处理字节序
    quint32 code = qToBigEndian(static_cast<quint32>(errorCode));
    QByteArray response(reinterpret_cast<const char *>(&code), sizeof(code));

    // 使用sendResponse发送
    sendResponse(session, type, response);

    Logger::instance()->debug(
        QString("Sent error response: type=%1, code=%2")
            .arg(static_cast<int>(type))
            .arg(static_cast<int>(errorCode)));
}

// 在文件中添加新函数实现
QString TlsServer::getSslProtocolName(QSslSocket *socket)
{
    QString protocol = "Unknown";
    QSsl::SslProtocol sslProtocol = socket->sessionProtocol();
    if (sslProtocol != QSsl::UnknownProtocol)
    {
        // 使用更安全的协议检测方式，避免直接使用已弃用的枚举值
        if (sslProtocol == QSsl::TlsV1_3)
        {
            protocol = "TLSv1.3";
        }
        else if (sslProtocol == QSsl::TlsV1_2)
        {
            protocol = "TLSv1.2";
        }
        else
        {
            // 对于所有其他协议（包括弃用的TLS 1.0和1.1），统一标记为旧版TLS
            protocol = "Legacy TLS";
        }
    }
    return protocol;
}

// 修改onSslReady方法以添加更多调试信息
void TlsServer::onSslReady()
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
    {
        return;
    }

    ClientSession *session = findSessionBySocket(socket);
    if (!session)
    {
        return;
    }

    // SSL握手完成信息记录
    QString clientIp = socket->peerAddress().toString();
    quint16 clientPort = socket->peerPort();
    QString protocol = getSslProtocolName(socket);
    QSslCipher cipher = socket->sessionCipher();

    // 验证客户端证书
    if (config_manager_ && config_manager_->getRequireClientCert())
    {
        if (!validateClientSubject(socket))
        {
            Logger::instance()->warning(QString("Client certificate validation failed: %1:%2").arg(clientIp).arg(clientPort));
            socket->disconnectFromHost();
            return;
        }
    }

    Logger::instance()->info(
        QString("Client connected - SSL handshake completed: %1:%2, Protocol: %3, Cipher: %4")
            .arg(clientIp)
            .arg(clientPort)
            .arg(protocol)
            .arg(cipher.name()));

    // 添加调试信息，确认socket处于可读状态
    if (socket->bytesAvailable() > 0)
    {
        Logger::instance()->debug(
            QString("Data already available after SSL handshake: %1 bytes")
                .arg(socket->bytesAvailable()));
    }
}

// 改进onDataReceived方法的错误处理和日志记录
void TlsServer::onDataReceived()
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
    {
        Logger::instance()->error("onDataReceived: Invalid socket");
        return;
    }

    ClientSession *session = findSessionBySocket(socket);
    if (!session)
    {
        Logger::instance()->error("onDataReceived: Session not found");
        socket->disconnectFromHost();
        return;
    }

    // 确保socket有效且可读
    if (!socket->isValid() || !socket->isReadable())
    {
        Logger::instance()->error("onDataReceived: Socket is not valid or readable");
        return;
    }

    QByteArray data = socket->readAll();
    QString clientIp = socket->peerAddress().toString();
    quint16 clientPort = socket->peerPort();

    Logger::instance()->info( // 提高日志级别以便更容易看到数据接收
        QString("Data received from %1:%2 (size: %3 bytes)")
            .arg(clientIp)
            .arg(clientPort)
            .arg(data.size()));

    // 添加原始数据的十六进制输出用于调试
    // Logger::instance()->debug(
    //     QString("Raw data (hex): %1").arg(QString(data.toHex(' '))));

    session->buffer.append(data);
    updateClientActivity(session); // 每次收到数据都更新活动时间

    // 处理消息
    while (true)
    {
        // 检查缓冲区是否至少有消息头大小
        if (session->buffer.size() < static_cast<qsizetype>(sizeof(MessageHeader)))
        {
            Logger::instance()->debug("Not enough data for message header");
            break;
        }

        // 尝试解析消息头
        try
        {
            MessageHeader header = MessageProtocol::deserializeHeader(session->buffer.left(sizeof(MessageHeader)));
            quint32 totalSize = sizeof(MessageHeader) + header.dataLength;

            Logger::instance()->debug(
                QString("Message header: type=%1, dataLength=%2, totalSize=%3")
                    .arg(header.msgType)
                    .arg(header.dataLength)
                    .arg(totalSize));

            // 检查缓冲区是否有完整消息
            if (session->buffer.size() < static_cast<qsizetype>(totalSize))
            {
                Logger::instance()->debug("Waiting for more data");
                break;
            }

            // 提取有效载荷并处理消息
            QByteArray payload = session->buffer.mid(sizeof(MessageHeader), header.dataLength);
            processMessage(session, header, payload);
            session->buffer.remove(0, totalSize);
        }
        catch (const std::exception &e)
        {
            Logger::instance()->error(
                QString("Error processing message: %1").arg(e.what()));
            // 出现严重错误，清空缓冲区并断开连接
            session->buffer.clear();
            socket->disconnectFromHost();
            break;
        }
        catch (...)
        {
            Logger::instance()->error("Unknown error processing message");
            session->buffer.clear();
            socket->disconnectFromHost();
            break;
        }
    }
}

// 增强updateClientActivity方法以记录活动
void TlsServer::updateClientActivity(ClientSession *session)
{
    if (!session)
        return;

    session->lastActiveTime = QDateTime::currentDateTime();

    // 添加socket非空检查，防止段错误
    if (session->socket && session->socket->isValid())
    {
        Logger::instance()->debug(
            QString("Client activity updated: %1:%2")
                .arg(session->socket->peerAddress().toString())
                .arg(session->socket->peerPort()));
    }
}
// 修改onSslErrors方法，智能区分不同类型的SSL错误
void TlsServer::onSslErrors(const QList<QSslError> &errors)
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
    {
        return;
    }

    // 记录所有SSL错误
    bool hasCriticalError = false;
    bool hasCertificateError = false;

    for (const QSslError &error : errors)
    {
        Logger::instance()->warning(
            QString("SSL Error: %1(%2)").arg(error.error()).arg(error.errorString()));

        // 检查是否是与证书相关的错误
        if (error.error() == QSslError::SelfSignedCertificate ||
            error.error() == QSslError::SelfSignedCertificateInChain ||
            error.error() == QSslError::InvalidCaCertificate ||
            error.error() == QSslError::CertificateUntrusted ||
            error.error() == QSslError::CertificateRevoked ||
            error.error() == QSslError::InvalidPurpose ||
            error.error() == QSslError::CertificateExpired ||
            error.error() == QSslError::CertificateNotYetValid ||
            error.error() == QSslError::HostNameMismatch ||
            error.error() == QSslError::NoPeerCertificate ||
            error.error() == QSslError::UnableToGetLocalIssuerCertificate ||
            error.error() == QSslError::UnableToVerifyFirstCertificate)
        {

            hasCertificateError = true;

            // 标记关键证书错误
            if (error.error() == QSslError::NoPeerCertificate ||
                error.error() == QSslError::CertificateExpired ||
                error.error() == QSslError::CertificateNotYetValid ||
                error.error() == QSslError::CertificateUntrusted ||
                error.error() == QSslError::CertificateRevoked)
            {
                hasCriticalError = true;
            }
        }
    }
    QString clientIp = socket->peerAddress().toString();
    quint16 clientPort = socket->peerPort();

    // 检查是否需要客户端证书
    if (config_manager_ && config_manager_->getRequireClientCert())
    {

        // 对于需要客户端证书的情况，只在关键证书验证错误时断开连接
        if (hasCriticalError || hasCertificateError)
        {
            Logger::instance()->warning(QString("Client certificate validation failed, closing connection:%1:%2").arg(clientIp).arg(clientPort));
            socket->disconnectFromHost();
            return;
        }
        if (!validateClientSubject(socket))
        {
            Logger::instance()->warning(QString("Client subject validation failed: %1:%2").arg(clientIp).arg(clientPort));
            socket->disconnectFromHost();
            return;
        }
    }
    socket->ignoreSslErrors(); // 忽略错误，继续握手
}

// 添加findSessionBySocket方法实现
ClientSession *TlsServer::findSessionBySocket(QSslSocket *socket)
{
    QMutexLocker locker(&clients_mutex_);
    return clients_.value(socket, nullptr);
}

void TlsServer::processMessage(ClientSession *session, const MessageHeader &header, const QByteArray &data)
{
    if (!session || !session->socket)
    {
        return;
    }

    MessageType msgType = static_cast<MessageType>(header.msgType);
    QString clientIp = session->socket->peerAddress().toString();
    quint16 clientPort = session->socket->peerPort();
    QString userName = session->userName.isEmpty() ? "(unauthenticated)" : session->userName;

    Logger::instance()->debug(
        QString("Processing message from %1:%2 (user: %3, type: %4, size: %5 bytes)")
            .arg(clientIp)
            .arg(clientPort)
            .arg(userName)
            .arg(static_cast<int>(msgType))
            .arg(header.dataLength));

    switch (msgType)
    {
    case MessageType::LOGIN_REQUEST:
        Logger::instance()->debug("Handling LOGIN_REQUEST message");
        handleLoginRequest(session, data);
        break;
    case MessageType::LIST_REQUEST:
        Logger::instance()->debug("Handling LIST_REQUEST message");
        handleListRequest(session);
        break;
    case MessageType::REPORT_REQUEST:
        Logger::instance()->debug("Handling REPORT_REQUEST message");
        handleReportRequest(session, data);
        break;
    default:
        Logger::instance()->warning(
            QString("Unknown message type: %1 from %2:%3")
                .arg(static_cast<int>(header.msgType))
                .arg(clientIp)
                .arg(clientPort));
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidParameter);
        break;
    }
}

void TlsServer::handleListRequest(ClientSession *session)
{
    if (!session->isAuthenticated)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::PermissionDenied);
        return;
    }

    // 获取服务器列表并创建ID到IP的映射
    QList<ServerInfo> servers = getTestServerList();

    // 保存服务器列表到客户端会话中
    session->servers = servers;

    // 创建并保存服务器ID到IP的映射
    QMap<quint32, quint32> ipMap;
    for (const auto &server : servers)
    {
        ipMap[server.serverId] = server.ipAddr;
    }
    session->serverIpMap = ipMap;

    // 记录日志
    Logger::instance()->info(
        QString("Stored %1 servers and their IP mapping in session for user %2")
            .arg(servers.size())
            .arg(session->userName),
        "TlsServer");

    // 发送服务器列表响应
    QByteArray response = MessageProtocol::serializeListResponse(servers);
    sendResponse(session, MessageType::LIST_RESPONSE, response);
}

// 修改getTestServerList方法使用数据库查询
QList<ServerInfo> TlsServer::getTestServerList()
{
    // 如果ServerDAO未设置，返回空列表并记录错误
    if (!server_dao_)
    {
        Logger::instance()->error("ServerDAO not set, cannot retrieve server list");
        return QList<ServerInfo>();
    }

    // 从数据库获取活跃的服务器列表
    QList<ServerInfo> servers = server_dao_->getActiveServers();

    Logger::instance()->info(
        QString("Retrieved %1 active servers from database")
            .arg(servers.size()));

    return servers;
}

void TlsServer::cleanupSession(ClientSession *session)
{
    if (!session)
        return;

    if (session->socket)
    {
        session->socket->disconnectFromHost();
        session->socket->deleteLater();
    }

    delete session;
}

bool TlsServer::loadCaCertificates()
{
    if (!config_manager_)
        return false;

    QString caCertPath = config_manager_->getCaCertificatePath();
    QFile caFile(caCertPath);
    if (!caFile.open(QIODevice::ReadOnly))
    {
        Logger::instance()->error(QString("Failed to open CA certificate file: %1").arg(caCertPath));
        return false;
    }

    QSslCertificate caCert(&caFile, QSsl::Pem);
    if (caCert.isNull())
    {
        Logger::instance()->error("Invalid CA certificate");
        return false;
    }

    ca_certificates_.clear();
    ca_certificates_.append(caCert);
    return true;
}

bool TlsServer::loadSubjectList(const QString &filePath, QSet<QString> &subjectSet)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::instance()->error(QString("Failed to open file: %1").arg(filePath));
        return false;
    }

    QTextStream in(&file);
    subjectSet.clear();
    int count = 0;

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith('#'))
        {
            subjectSet.insert(line);
            count++;
        }
    }

    Logger::instance()->info(QString("Loaded %1 subjects from %2").arg(count).arg(filePath));
    return true;
}

QString TlsServer::getCertificateSubject(const QSslCertificate &certificate)
{
    // 获取证书主题信息
    return certificate.subjectInfo(QSslCertificate::CommonName).join(", ");
}

bool TlsServer::validateClientSubject(QSslSocket *socket)
{
    if (!socket)
        return false;

    // 检查socket连接状态
    if (!socket->isOpen() || !socket->isValid())
    {
        Logger::instance()->warning("Client certificate validation failed: socket is not valid");
        return false;
    }

    // 获取客户端证书
    QSslCertificate clientCert = socket->peerCertificate();

    // 处理空证书情况
    if (clientCert.isNull())
    {
        Logger::instance()->warning("No client certificate provided");
        return false;
    }

    QString subject = getCertificateSubject(clientCert);

    // 检查证书是否已过期 - 使用expiryDate替代isExpired
    if (clientCert.isNull() || clientCert.expiryDate() < QDateTime::currentDateTime())
    {
        Logger::instance()->warning(QString("Invalid or expired client certificate: %1").arg(subject));
        return false;
    }

    // 检查主题是否在允许列表中
    return isSubjectAllowed(subject);
}

bool TlsServer::isSubjectAllowed(const QString &subject)
{
    if (use_whitelist_)
    {
        QMutexLocker locker(&whitelist_mutex_);
        bool allowed = whitelisted_subjects_.contains(subject);
        if (!allowed)
        {
            Logger::instance()->warning(QString("Subject not in whitelist: %1").arg(subject));
        }
        return allowed;
    }
    else if (use_blacklist_)
    {
        QMutexLocker locker(&blacklist_mutex_);
        bool blocked = blacklisted_subjects_.contains(subject);
        if (blocked)
        {
            Logger::instance()->warning(QString("Subject in blacklist: %1").arg(subject));
        }
        return !blocked;
    }

    // 如果没有启用白名单或黑名单，则允许所有主题
    return true;
}