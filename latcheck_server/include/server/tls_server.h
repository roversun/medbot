#ifndef TLSSERVER_H
#define TLSSERVER_H

#include <QTcpServer>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>
#include <QHash>
#include <QMutex>

// 前向声明
class ConfigManager;
class Logger;
class UserDAO;
class ReportDAO;
class ServerDAO; // 添加ServerDAO前向声明

#include "config/config_manager.h"
#include "logger/logger.h"
#include "auth/auth_manager.h"
#include "database/user_dao.h"
#include "database/report_dao.h"
#include "database/server_dao.h" // 添加ServerDAO头文件
#include "protocol/message_protocol.h"
#include "common/error_codes.h"
#include "common/types.h"

// 客户端连接状态
enum class ClientState
{
    Connected,     // 已连接
    Authenticated, // 已认证
    Disconnected   // 已断开
};

// 客户端会话信息
struct ClientSession
{
    QSslSocket *socket;
    ClientState state;
    QString userName;
    QDateTime connectTime;
    QDateTime lastActiveTime;
    QByteArray pendingData;
    QByteArray buffer;
    bool isAuthenticated;
    QTimer *loginTimer;
    QList<ServerInfo> servers;          // 存储客户端的服务器列表
    QMap<quint32, quint32> serverIpMap; // 存储服务器ID到IP地址的映射

    ClientSession() : socket(nullptr),
                      state(ClientState::Connected),
                      isAuthenticated(false),
                      loginTimer(nullptr)
    {
        connectTime = QDateTime::currentDateTime();
        lastActiveTime = connectTime;
    }
};

class TlsServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit TlsServer(QObject *parent = nullptr);
    ~TlsServer();

    bool startServer(const QString &host, quint16 port);
    bool startServer(quint16 port); // 向后兼容
    void stopServer();

    // 获取连接数
    int getConnectionCount() const;

    // 获取认证用户数
    int getAuthenticatedUserCount() const;

    // 设置依赖组件
    void setConfigManager(QSharedPointer<ConfigManager> config);
    void setUserDAO(QSharedPointer<UserDAO> userDAO);
    void setReportDAO(QSharedPointer<ReportDAO> reportDAO);
    void setServerDAO(QSharedPointer<ServerDAO> serverDAO);
    void setAuthManager(QSharedPointer<AuthManager> authManager);

protected:
    // 重写新连接处理
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    // SSL就绪处理
    void onSslReady();

    // SSL错误处理
    void onSslErrors(const QList<QSslError> &errors);

    // 数据接收处理
    void onDataReceived();

    // 客户端断开处理
    void onClientDisconnected();

    // 清理定时器
    void onCleanupTimer();

private:
    // SSL初始化
    bool initializeSsl();

    // 添加缺失的方法声明
    ClientSession *findSessionBySocket(QSslSocket *socket);

    // 消息处理
    void processMessage(ClientSession *session, const MessageHeader &header, const QByteArray &data);

    // 登录请求处理
    void handleLoginRequest(ClientSession *session, const QByteArray &data);

    // 列表请求处理
    void handleListRequest(ClientSession *session);

    // 报告请求处理
    void handleReportRequest(ClientSession *session, const QByteArray &data);

    // 发送响应
    void sendResponse(ClientSession *session, MessageType type, const QByteArray &data = QByteArray());

    // 发送错误响应
    void sendErrorResponse(ClientSession *session, MessageType type, ErrorCode errorCode);

    // 获取测试服务器列表
    QList<ServerInfo> getTestServerList();

    // 清理会话
    void cleanupSession(ClientSession *session);

    // 检查客户端权限
    bool checkClientPermission(const QString &userName, int requiredLevel);

    // 更新客户端活动时间
    void updateClientActivity(ClientSession *session);

    // 获取SSL协议可读名称
    QString getSslProtocolName(QSslSocket *socket);

    bool loadCaCertificates();
    bool loadSubjectList(const QString &filePath, QSet<QString> &subjectSet);
    bool isSubjectAllowed(const QString &subject);
    bool validateClientSubject(QSslSocket *socket);
    QString getCertificateSubject(const QSslCertificate &certificate);

private:
    QSharedPointer<ConfigManager> config_manager_;
    QSharedPointer<UserDAO> user_dao_;
    QSharedPointer<ReportDAO> report_dao_;
    QSharedPointer<ServerDAO> server_dao_;

    QSslConfiguration ssl_config_;
    QHash<QSslSocket *, ClientSession *> clients_;
    QTimer *cleanup_timer_;

    mutable QMutex clients_mutex_;

    // 配置参数
    int max_connections_;
    int connection_timeout_; // 秒
    int auth_timeout_;       // 秒

    QSharedPointer<AuthManager> auth_manager_;

    QList<QSslCertificate> ca_certificates_;
    bool use_whitelist_;
    bool use_blacklist_;
    QString whitelist_path_;
    QString blacklist_path_;
    QSet<QString> whitelisted_subjects_;
    QSet<QString> blacklisted_subjects_;
    QMutex whitelist_mutex_;
    QMutex blacklist_mutex_;
};

#endif // TLSSERVER_H