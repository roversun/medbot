#include "server/tls_server.h"
#include "config/config_manager.h"
#include "logger/logger.h"
#include "auth/auth_manager.h"
#include "database/user_dao.h"
#include "database/report_dao.h"
#include "protocol/message_protocol.h"
#include "common/error_codes.h"

#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QHostAddress>



TlsServer::TlsServer(QObject *parent)
    : QTcpServer(parent)
    , config_manager_(nullptr)
    , user_dao_(nullptr)
    , report_dao_(nullptr)
    , cleanup_timer_(new QTimer(this))
    , max_connections_(100)
    , connection_timeout_(300)  // 5分钟
    , auth_timeout_(30)         // 30秒
{
    // 设置清理定时器
    cleanup_timer_->setInterval(60000); // 每分钟清理一次
    connect(cleanup_timer_, &QTimer::timeout, this, &TlsServer::onCleanupTimer);
}

TlsServer::~TlsServer()
{
    stopServer();
}

bool TlsServer::startServer(const QString& host, quint16 port)
{
    if (!initializeSsl()) {
        Logger::instance()->error("TlsServer", "Failed to initialize SSL configuration");
        return false;
    }
    
    QHostAddress address;
    if (host == "0.0.0.0" || host.isEmpty()) {
        // 明确监听IPv4的所有地址
        address = QHostAddress::AnyIPv4;
    } else if (host == "::" || host == "[::]" || host.toLower() == "any") {
        // 监听IPv6的所有地址
        address = QHostAddress::AnyIPv6;
    } else if (host.toLower() == "dual" || host.toLower() == "all") {
        // 尝试同时监听IPv4和IPv6
        address = QHostAddress::Any;
    } else {
        // 使用指定的IP地址
        address.setAddress(host);
        if (address.isNull()) {
            Logger::instance()->error("TlsServer", 
                QString("Invalid host address: %1").arg(host));
            return false;
        }
    }
    
    if (!listen(address, port)) {
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
    if (isListening()) {
        close();
        
        // 断开所有客户端连接
        QMutexLocker locker(&clients_mutex_);
        for (auto it = clients_.begin(); it != clients_.end(); ++it) {
            if (it.value()->socket) {
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
    for (const ClientSession* session : clients_) {
        if (session && session->state == ClientState::Authenticated) {
            count++;
        }
    }
    return count;
}

void TlsServer::setConfigManager(ConfigManager* config)
{
    config_manager_ = config;
}

void TlsServer::setUserDAO(UserDAO* userDAO)
{
    user_dao_ = userDAO;
}

void TlsServer::setReportDAO(ReportDAO* reportDAO)
{
    report_dao_ = reportDAO;
}

void TlsServer::incomingConnection(qintptr socketDescriptor)
{
    // 检查连接数限制
    if (getConnectionCount() >= max_connections_) {
        QTcpSocket tempSocket;
        tempSocket.setSocketDescriptor(socketDescriptor);
        tempSocket.disconnectFromHost();
        return;
    }
    
    QSslSocket* sslSocket = new QSslSocket(this);
    if (!sslSocket->setSocketDescriptor(socketDescriptor)) {
        delete sslSocket;
        return;
    }
    
    // 配置SSL
    sslSocket->setSslConfiguration(ssl_config_);
    
    // 创建客户端会话
    ClientSession* session = new ClientSession();
    session->socket = sslSocket;
    session->connectTime = QDateTime::currentDateTime();
    session->lastActiveTime = session->connectTime;
    
    {
        QMutexLocker locker(&clients_mutex_);
        clients_.insert(sslSocket, session);
    }
    
    // 连接信号
    connect(sslSocket, &QSslSocket::encrypted, this, &TlsServer::onSslReady);
    connect(sslSocket, &QSslSocket::readyRead, this, &TlsServer::onDataReceived);
    connect(sslSocket, &QSslSocket::disconnected, this, &TlsServer::onClientDisconnected);
    
    // 启动SSL握手
    sslSocket->startServerEncryption();
}

void TlsServer::onClientDisconnected()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) return;
    
    QMutexLocker locker(&clients_mutex_);
    ClientSession* session = clients_.value(socket);
    if (session) {
        Logger::instance()->info("TlsServer", 
            QString("Client disconnected: %1").arg(session->userName));
        delete session;
    }
    clients_.remove(socket);
    socket->deleteLater();
}

void TlsServer::onCleanupTimer()
{
    QDateTime now = QDateTime::currentDateTime();
    QMutexLocker locker(&clients_mutex_);
    
    for (auto it = clients_.begin(); it != clients_.end();) {
        ClientSession* session = it.value();
        
        // 清理未认证超时的连接
        if (session->state == ClientState::Connected && 
            session->connectTime.secsTo(now) > auth_timeout_) {
            
            Logger::instance()->warning("TlsServer", 
                QString("Authentication timeout for client from %1")
                    .arg(session->socket->peerAddress().toString()));
            
            session->socket->disconnectFromHost();
            delete session;
            it = clients_.erase(it);
            continue;
        }
        
        // 清理长时间不活跃的连接
        if (session->lastActiveTime.secsTo(now) > connection_timeout_) {
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
    if (!config_manager_) {
        return false;
    }
    
    QString certPath = config_manager_->getCertificatePath();
    QString keyPath = config_manager_->getPrivateKeyPath();
    
    // 加载证书
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        Logger::instance()->error("TlsServer", 
            QString("Failed to open certificate file: %1").arg(certPath));
        return false;
    }
    
    QSslCertificate cert(&certFile, QSsl::Pem);
    if (cert.isNull()) {
        Logger::instance()->error("TlsServer", "Invalid certificate");
        return false;
    }
    
    // 加载私钥
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        Logger::instance()->error("TlsServer", 
            QString("Failed to open private key file: %1").arg(keyPath));
        return false;
    }
    
    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (key.isNull()) {
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

void TlsServer::handleLoginRequest(ClientSession* session, const QByteArray& data)
{
    if (!user_dao_) {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::ServerInternal);
        return;
    }
    
    LoginRequestData loginData = MessageProtocol::deserializeLoginRequest(data);
    QString userName = QString::fromUtf8(loginData.userName);
    QString passwordHash = QString::fromUtf8(loginData.passwordHash);
    
    // 验证用户凭据
    User user = user_dao_->getUserByUsername(userName);
    if (user.id == 0) {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidPassword);
        return;
    }
    
    // 验证密码
    if (user.password != passwordHash) {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidPassword);
        return;
    }
    
    // 登录成功
    session->state = ClientState::Authenticated;
    session->userName = userName;
    updateClientActivity(session);
    
    // 发送成功响应
    QJsonObject response;
    response["status"] = "success";
    response["message"] = "Login successful";
    QJsonDocument responseDoc(response);
    sendResponse(session, MessageType::LOGIN_OK, responseDoc.toJson());
}

void TlsServer::handleReportRequest(ClientSession* session, const QByteArray& data)
{
    if (!report_dao_) {
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
    if (result != ErrorCode::Success) {
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

bool TlsServer::checkClientPermission(const QString& userName, int requiredLevel)
{
    if (!user_dao_) return false;
    
    User user = user_dao_->getUserByUsername(userName);
    return user.id != 0 && static_cast<int>(user.role) >= requiredLevel;
}

void TlsServer::sendResponse(ClientSession* session, MessageType type, const QByteArray& data)
{
    if (!session || !session->socket || !session->socket->isValid()) {
        return;
    }
    
    MessageHeader header(type, data.size());
    QByteArray headerData = MessageProtocol::serializeHeader(header);
    
    session->socket->write(headerData);
    if (!data.isEmpty()) {
        session->socket->write(data);
    }
}

// 添加缺失的方法实现
void TlsServer::sendErrorResponse(ClientSession* session, MessageType type, ErrorCode errorCode) {
    if (!session || !session->socket || !session->socket->isValid()) {
        return;
    }
    
    QByteArray response = MessageProtocol::createSimpleResponse(type, errorCode);
    session->socket->write(response);
}

void TlsServer::onSslReady() {
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    if (socket) {
        qDebug() << "SSL handshake completed for client:" << socket->peerAddress().toString();
        
        ClientSession* session = findSessionBySocket(socket);
        if (session) {
            updateClientActivity(session);
        }
    }
}

void TlsServer::onSslErrors(const QList<QSslError>& errors) {
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) {
        return;
    }
    
    // 记录SSL错误
    for (const QSslError& error : errors) {
        Logger::instance()->warning("TlsServer", 
            QString("SSL Error: %1").arg(error.errorString()));
    }
    
    // 对于自签名证书等可接受的错误，可以选择忽略
    // 这里为了简化，忽略所有SSL错误（生产环境中需要更严格的处理）
    socket->ignoreSslErrors();
}

// 添加findSessionBySocket方法实现
ClientSession* TlsServer::findSessionBySocket(QSslSocket* socket) {
    QMutexLocker locker(&clients_mutex_);
    return clients_.value(socket, nullptr);
}

void TlsServer::onDataReceived()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) {
        return;
    }
    
    ClientSession* session = findSessionBySocket(socket);
    if (!session) {
        return;
    }
    
    QByteArray data = socket->readAll();
    session->buffer.append(data);
    
    while (session->buffer.size() >= sizeof(MessageHeader)) {
        MessageHeader header = MessageProtocol::deserializeHeader(session->buffer.left(sizeof(MessageHeader)));
        int totalSize = sizeof(MessageHeader) + header.dataLength;
        
        if (session->buffer.size() < totalSize) {
            break; // 等待更多数据
        }
        
        QByteArray payload = session->buffer.mid(
            sizeof(MessageHeader), header.dataLength);
        
        processMessage(session, header, payload);
        session->buffer.remove(0, totalSize);
    }
}

void TlsServer::processMessage(ClientSession* session, const MessageHeader& header, const QByteArray& data)
{
    MessageType msgType = static_cast<MessageType>(header.msgType);
    
    switch (msgType) {
        case MessageType::LOGIN_REQUEST:
            handleLoginRequest(session, data);
            break;
        case MessageType::LIST_REQUEST:
            handleListRequest(session);
            break;
        case MessageType::REPORT_REQUEST:
            handleReportRequest(session, data);
            break;
        default:
            qWarning() << 
                QString("Unknown message type: %1").arg(static_cast<int>(header.msgType));
            sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::InvalidParameter);
            break;
    }
}

void TlsServer::handleListRequest(ClientSession* session)
{
    if (!session->isAuthenticated) {
        sendErrorResponse(session, MessageType::LOGIN_FAIL, ErrorCode::PermissionDenied);
        return;
    }
    
    QList<ServerInfo> servers = getTestServerList();
    QByteArray response = MessageProtocol::serializeListResponse(servers);
    sendResponse(session, MessageType::LIST_RESPONSE, response);
}

QList<ServerInfo> TlsServer::getTestServerList()
{
    QList<ServerInfo> servers;
    
    ServerInfo server1;
    server1.serverId = 1;
    server1.ipAddr = QHostAddress("192.168.1.100").toIPv4Address();
    servers.append(server1);
    
    ServerInfo server2;
    server2.serverId = 2;
    server2.ipAddr = QHostAddress("192.168.1.101").toIPv4Address();
    servers.append(server2);
    
    return servers;
}

void TlsServer::cleanupSession(ClientSession* session)
{
    if (!session) return;
    
    if (session->socket) {
        session->socket->disconnectFromHost();
        session->socket->deleteLater();
    }
    
    delete session;
}

void TlsServer::updateClientActivity(ClientSession* session)
{
    if (session) {
        session->lastActiveTime = QDateTime::currentDateTime();
    }
}