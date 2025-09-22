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
      auth_manager_(nullptr)
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
        Logger::instance()->error("TlsServer", "Failed to initialize SSL configuration");
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
            Logger::instance()->error("TlsServer",
                                      QString("Invalid host address: %1").arg(host));
            return false;
        }
    }

    if (!listen(address, port))
    {
        Logger::instance()->error("TlsServer",
                                  QString("Failed to start server on %1:%2 - %3")
                                      .arg(host)
                                      .arg(port)
                                      .arg(errorString()));
        return false;
    }

    cleanup_timer_->start();
    Logger::instance()->info("TlsServer",
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
        Logger::instance()->info("TlsServer", "TLS Server stopped");
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

void TlsServer::setConfigManager(ConfigManager *config)
{
    config_manager_ = config;
}

void TlsServer::setUserDAO(UserDAO *userDAO)
{
    user_dao_ = userDAO;
}

void TlsServer::setReportDAO(ReportDAO *reportDAO)
{
    report_dao_ = reportDAO;
}

// 添加缺失的 setServerDAO 方法实现
void TlsServer::setServerDAO(ServerDAO *serverDAO)
{
    server_dao_ = serverDAO;
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
        Logger::instance()->info("TlsServer",
                                 QString("Connection rejected - Max connections reached (%1:%2)")
                                     .arg(clientIp)
                                     .arg(clientPort));
        tempSocket.disconnectFromHost();
        return;
    }

    QSslSocket *sslSocket = new QSslSocket(this);
    if (!sslSocket->setSocketDescriptor(socketDescriptor))
    {
        Logger::instance()->error("TlsServer", "Failed to set socket descriptor");
        delete sslSocket;
        return;
    }

    // 获取并记录客户端连接信息
    QString clientIp = sslSocket->peerAddress().toString();
    quint16 clientPort = sslSocket->peerPort();
    Logger::instance()->info("TlsServer",
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
            Logger::instance()->warning("TlsServer", "Login timeout - disconnecting client");
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

    Logger::instance()->debug("TlsServer",
                              QString("SSL handshake initiated for client: %1:%2")
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

        Logger::instance()->info("TlsServer",
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

            Logger::instance()->warning("TlsServer",
                                        QString("Authentication timeout for client from %1")
                                            .arg(session->socket->peerAddress().toString()));

            session->socket->disconnectFromHost();
            delete session;
            it = clients_.erase(it);
            continue;
        }

        // 清理长时间不活跃的连接
        if (session->lastActiveTime.secsTo(now) > connection_timeout_)
        {
            Logger::instance()->info("TlsServer",
                                     QString("Connection timeout for user: %1").arg(session->userName));

            session->socket->disconnectFromHost();
            delete session;
            it = clients_.erase(it);
            continue;
        }

        ++it;
    }
}

bool TlsServer::initializeSsl()
{
    if (!config_manager_)
    {
        return false;
    }

    QString certPath = config_manager_->getCertificatePath();
    QString keyPath = config_manager_->getPrivateKeyPath();

    // 加载证书
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly))
    {
        Logger::instance()->error("TlsServer",
                                  QString("Failed to open certificate file: %1").arg(certPath));
        return false;
    }

    QSslCertificate cert(&certFile, QSsl::Pem);
    if (cert.isNull())
    {
        Logger::instance()->error("TlsServer", "Invalid certificate");
        return false;
    }

    // 加载私钥
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly))
    {
        Logger::instance()->error("TlsServer",
                                  QString("Failed to open private key file: %1").arg(keyPath));
        return false;
    }

    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (key.isNull())
    {
        Logger::instance()->error("TlsServer", "Invalid private key");
        return false;
    }

    // 配置SSL
    ssl_config_.setLocalCertificate(cert);
    ssl_config_.setPrivateKey(key);
    ssl_config_.setPeerVerifyMode(QSslSocket::VerifyNone);
    ssl_config_.setProtocol(QSsl::TlsV1_2OrLater);

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

    Logger::instance()->debug("TlsServer",
                              QString("Processing login request for user: %1").arg(userName));

    // 获取用户信息（包含密码哈希和盐值）
    User user = user_dao_->getUserByUsername(userName);
    user_dao_->printUser(user); // 调试输出用户信息
    if (user.id == 0)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidUser);
        Logger::instance()->warning("TlsServer",
                                    QString("Login failed - User not found: %1").arg(userName));
        return;
    }

    // 检查用户状态
    if (user.status != UserStatus::Active)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::UserDisabled);
        Logger::instance()->warning("TlsServer",
                                    QString("Login failed - User disabled: %1").arg(userName));
        return;
    }

    // 使用PasswordUtils验证密码
    if (!PasswordUtils::verifyPassword(plainPassword, user.passwordHash, user.salt))
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidPassword);
        Logger::instance()->warning("TlsServer",
                                    QString("Login failed - Invalid password for user: %1").arg(userName));
        return;
    }

    // 检查用户权限（报告上传用户和管理员可以上传报告）
    if (user.role != UserRole::ReportUploader && user.role != UserRole::Admin)
    {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::PermissionDenied);
        Logger::instance()->warning("TlsServer",
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
    QByteArray response(reinterpret_cast<const char*>(&code), sizeof(code));
    sendResponse(session, MessageType::LOGIN_OK, response);

    Logger::instance()->info("TlsServer",
                             QString("User logged in successfully: %1").arg(userName));
}
void TlsServer::setAuthManager(AuthManager* authManager) {
    auth_manager_ = authManager;
}
void TlsServer::handleReportRequest(ClientSession *session, const QByteArray &data)
{
    if (!report_dao_)
    {
        sendErrorResponse(session, MessageType::REPORT_FAIL, ErrorCode::ServerInternal);
        return;
    }

    ReportRequestData reportData = MessageProtocol::deserializeReportRequest(data);

    // 创建报告对象
    Report report;
    report.userName = session->userName;
    report.location = reportData.location;
    report.createdAt = QDateTime::currentDateTime();

    // 保存报告
    ErrorCode result = report_dao_->createReport(report);
    if (result != ErrorCode::Success)
    {
        sendErrorResponse(session, MessageType::REPORT_FAIL, result);
        return;
    }

    // 发送成功响应
    QJsonObject response;
    response["status"] = "success";
    response["message"] = "Report uploaded successfully";
    response["reportId"] = report.id;
    QJsonDocument responseDoc(response);
    sendResponse(session, MessageType::REPORT_OK, responseDoc.toJson());
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
    QByteArray response(reinterpret_cast<const char*>(&code), sizeof(code));
    
    // 使用sendResponse发送
    sendResponse(session, type, response);

    Logger::instance()->debug("TlsServer",
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
    if (!socket) {
        Logger::instance()->error("TlsServer", "onSslReady: Invalid socket");
        return;
    }

    ClientSession *session = findSessionBySocket(socket);
    if (!session) {
        Logger::instance()->error("TlsServer", "onSslReady: Session not found");
        return;
    }

    // SSL握手完成信息记录
    QString clientIp = socket->peerAddress().toString();
    quint16 clientPort = socket->peerPort();
    QString protocol = getSslProtocolName(socket);
    QSslCipher cipher = socket->sessionCipher();

    Logger::instance()->info("TlsServer",
                             QString("Client connected - SSL handshake completed: %1:%2, Protocol: %3, Cipher: %4")
                                 .arg(clientIp)
                                 .arg(clientPort)
                                 .arg(protocol)
                                 .arg(cipher.name()));
    
    // 添加调试信息，确认socket处于可读状态
    if (socket->bytesAvailable() > 0) {
        Logger::instance()->debug("TlsServer",
                                 QString("Data already available after SSL handshake: %1 bytes")
                                     .arg(socket->bytesAvailable()));
    }
}

// 改进onDataReceived方法的错误处理和日志记录
void TlsServer::onDataReceived()
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        Logger::instance()->error("TlsServer", "onDataReceived: Invalid socket");
        return;
    }

    ClientSession *session = findSessionBySocket(socket);
    if (!session) {
        Logger::instance()->error("TlsServer", "onDataReceived: Session not found");
        socket->disconnectFromHost();
        return;
    }

    // 确保socket有效且可读
    if (!socket->isValid() || !socket->isReadable()) {
        Logger::instance()->error("TlsServer", "onDataReceived: Socket is not valid or readable");
        return;
    }

    QByteArray data = socket->readAll();
    QString clientIp = socket->peerAddress().toString();
    quint16 clientPort = socket->peerPort();

    Logger::instance()->info("TlsServer", // 提高日志级别以便更容易看到数据接收
                           QString("Data received from %1:%2 (size: %3 bytes)")
                               .arg(clientIp)
                               .arg(clientPort)
                               .arg(data.size()));

    // 添加原始数据的十六进制输出用于调试
    Logger::instance()->debug("TlsServer",
                              QString("Raw data (hex): %1").arg(QString(data.toHex(' '))));

    session->buffer.append(data);
    updateClientActivity(session); // 每次收到数据都更新活动时间

    // 处理消息
    while (true) {
        // 检查缓冲区是否至少有消息头大小
        if (session->buffer.size() < static_cast<qsizetype>(sizeof(MessageHeader))) {
            Logger::instance()->debug("TlsServer", "Not enough data for message header");
            break;
        }

        // 尝试解析消息头
        try {
            MessageHeader header = MessageProtocol::deserializeHeader(session->buffer.left(sizeof(MessageHeader)));
            quint32 totalSize = sizeof(MessageHeader) + header.dataLength;

            Logger::instance()->debug("TlsServer",
                                      QString("Message header: type=%1, dataLength=%2, totalSize=%3")
                                          .arg(header.msgType)
                                          .arg(header.dataLength)
                                          .arg(totalSize));

            // 检查缓冲区是否有完整消息
            if (session->buffer.size() < static_cast<qsizetype>(totalSize)) {
                Logger::instance()->debug("TlsServer", "Waiting for more data");
                break;
            }

            // 提取有效载荷并处理消息
            QByteArray payload = session->buffer.mid(sizeof(MessageHeader), header.dataLength);
            processMessage(session, header, payload);
            session->buffer.remove(0, totalSize);
        } catch (const std::exception &e) {
            Logger::instance()->error("TlsServer",
                                     QString("Error processing message: %1").arg(e.what()));
            // 出现严重错误，清空缓冲区并断开连接
            session->buffer.clear();
            socket->disconnectFromHost();
            break;
        } catch (...) {
            Logger::instance()->error("TlsServer", "Unknown error processing message");
            session->buffer.clear();
            socket->disconnectFromHost();
            break;
        }
    }
}

// 增强updateClientActivity方法以记录活动
void TlsServer::updateClientActivity(ClientSession *session)
{
    if (!session) return;
    
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
void TlsServer::onSslErrors(const QList<QSslError> &errors)
{
    QSslSocket *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket)
    {
        return;
    }

    // 记录SSL错误
    for (const QSslError &error : errors)
    {
        Logger::instance()->warning("TlsServer",
                                    QString("SSL Error: %1").arg(error.errorString()));
    }

    // 对于自签名证书等可接受的错误，可以选择忽略
    // 这里为了简化，忽略所有SSL错误（生产环境中需要更严格的处理）
    socket->ignoreSslErrors();
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

    Logger::instance()->debug("TlsServer",
                              QString("Processing message from %1:%2 (user: %3, type: %4, size: %5 bytes)")
                                  .arg(clientIp)
                                  .arg(clientPort)
                                  .arg(userName)
                                  .arg(static_cast<int>(msgType))
                                  .arg(header.dataLength));

    switch (msgType)
    {
    case MessageType::LOGIN_REQUEST:
        Logger::instance()->debug("TlsServer", "Handling LOGIN_REQUEST message");
        handleLoginRequest(session, data);
        break;
    case MessageType::LIST_REQUEST:
        Logger::instance()->debug("TlsServer", "Handling LIST_REQUEST message");
        handleListRequest(session);
        break;
    case MessageType::REPORT_REQUEST:
        Logger::instance()->debug("TlsServer", "Handling REPORT_REQUEST message");
        handleReportRequest(session, data);
        break;
    default:
        Logger::instance()->warning("TlsServer",
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

    QList<ServerInfo> servers = getTestServerList();
    QByteArray response = MessageProtocol::serializeListResponse(servers);
    sendResponse(session, MessageType::LIST_RESPONSE, response);
}

// 修改getTestServerList方法使用数据库查询
QList<ServerInfo> TlsServer::getTestServerList()
{
    // 如果ServerDAO未设置，返回空列表并记录错误
    if (!server_dao_) {
        Logger::instance()->error("TlsServer", "ServerDAO not set, cannot retrieve server list");
        return QList<ServerInfo>();
    }
    
    // 从数据库获取活跃的服务器列表
    QList<ServerInfo> servers = server_dao_->getActiveServers();
    
    Logger::instance()->info("TlsServer", 
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