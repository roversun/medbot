#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QString>
#include <QDateTime>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QMutex>
#include <QHash>
#include <memory>

#include "database/user_dao.h"
#include "logger/logger.h"
#include "common/types.h"



struct UserSession {
    QString username;
    QString sessionToken;
    QDateTime loginTime;
    QDateTime lastActivity;
    QString clientIP;
    bool isActive;
};

struct LoginAttempt {
    QString clientIP;
    QDateTime attemptTime;
    bool successful;
    QString username;
};

class AuthManager {
public:
    explicit AuthManager(std::shared_ptr<UserDAO> userDAO, std::shared_ptr<Logger> logger);
    ~AuthManager();
    
    // 用户认证
    QString authenticateUser(const QString& username, const QString& password, const QString& clientIP);
    bool validateSession(const QString& sessionToken);
    bool logoutUser(const QString& sessionToken);
    
    UserSession* getSession(const QString& sessionToken);
    void updateSessionActivity(const QString& sessionToken);
    void cleanupExpiredSessions();
    
    bool isAccountLocked(const QString& username);
    void recordLoginAttempt(const QString& username, const QString& clientIP, bool successful);
    bool checkRateLimit(const QString& clientIP);
    
    void setSessionTimeout(int minutes);
    void setMaxLoginAttempts(int attempts);
    void setLockoutDuration(int minutes);
    void setRateLimitWindow(int seconds);
    void setMaxRequestsPerWindow(int requests);
    
private:
    std::shared_ptr<UserDAO> user_dao_;
    std::shared_ptr<Logger> logger_;
    
    // 会话管理
    QHash<QString, UserSession> active_sessions_;
    QMutex session_mutex_;
    
    // 安全相关
    QHash<QString, QList<LoginAttempt>> login_attempts_;
    QHash<QString, QDateTime> locked_accounts_;
    QHash<QString, QList<QDateTime>> rate_limit_tracker_;
    QMutex security_mutex_;
    
    // 配置参数
    int session_timeout_minutes_;
    int max_login_attempts_;
    int lockout_duration_minutes_;
    int rate_limit_window_seconds_;
    int max_requests_per_window_;
    
    // 私有方法
    QString generateSessionToken();
    void lockAccount(const QString& username);
    void unlockExpiredAccounts();
    void cleanupLoginAttempts();
    void cleanupRateLimitData();
};

#endif // AUTHMANAGER_H