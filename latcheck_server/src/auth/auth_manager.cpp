#include "auth/auth_manager.h"
#include "database/user_dao.h"
#include "logger/logger.h"
#include "common/error_codes.h"

#include <QUuid>
#include <QCryptographicHash>
#include <QMutexLocker>
#include "auth/password_utils.h"



AuthManager::AuthManager(std::shared_ptr<UserDAO> userDAO, std::shared_ptr<Logger> logger)
    : user_dao_(userDAO)
    , logger_(logger)
    , session_timeout_minutes_(30)
    , max_login_attempts_(5)
    , lockout_duration_minutes_(15)
    , rate_limit_window_seconds_(60)
    , max_requests_per_window_(10)
{
}

AuthManager::~AuthManager() {
}

QString AuthManager::authenticateUser(const QString& username, const QString& password, const QString& clientIP) {
    // 检查速率限制
    if (!checkRateLimit(clientIP)) {
        logger_->warning(QString("Rate limit exceeded for IP: %1").arg(clientIP));
        return QString();
    }
    
    // 检查账户是否被锁定
    if (isAccountLocked(username)) {
        logger_->warning(QString("Login attempt for locked account: %1").arg(username));
        recordLoginAttempt(username, clientIP, false);
        return QString();
    }
    
    // 获取用户信息
    User user = user_dao_->getUserByUsername(username);
    if (user.id == 0) {
        logger_->warning(QString("Failed login attempt for user: %1 from IP: %2").arg(username, clientIP));
        recordLoginAttempt(username, clientIP, false);
        return QString();
    }
    
    // 使用PasswordUtils验证密码
    if (!PasswordUtils::verifyPassword(password, user.passwordHash, user.salt)) {
        logger_->warning(QString("Failed login attempt for user: %1 from IP: %2").arg(username, clientIP));
        recordLoginAttempt(username, clientIP, false);
        return QString();
    }
    
    // 验证成功，创建会话
    QString sessionToken = generateSessionToken();
    UserSession session;
    session.username = username;
    session.sessionToken = sessionToken;
    session.loginTime = QDateTime::currentDateTime();
    session.lastActivity = session.loginTime;
    session.clientIP = clientIP;
    session.isActive = true;
    
    {
        QMutexLocker sessionLocker(&session_mutex_);
        active_sessions_[sessionToken] = session;
    }
    
    // 更新用户最后登录时间
    user_dao_->updateLastLoginTime(user.id);
    
    // 记录成功登录
    recordLoginAttempt(username, clientIP, true);
    logger_->info(QString("User %1 logged in successfully from IP: %2").arg(username, clientIP));
    
    return sessionToken;
}

bool AuthManager::validateSession(const QString& sessionToken) {
    QMutexLocker locker(&session_mutex_);
    
    auto it = active_sessions_.find(sessionToken);
    if (it == active_sessions_.end()) {
        return false;
    }
    
    UserSession& session = it.value();
    if (!session.isActive) {
        return false;
    }
    
    // 检查会话是否过期
    QDateTime now = QDateTime::currentDateTime();
    if (session.lastActivity.addSecs(session_timeout_minutes_ * 60) < now) {
        session.isActive = false;
        logger_->info(QString("Session expired for user: %1").arg(session.username));
        return false;
    }
    
    return true;
}

bool AuthManager::logoutUser(const QString& sessionToken) {
    QMutexLocker locker(&session_mutex_);
    
    auto it = active_sessions_.find(sessionToken);
    if (it == active_sessions_.end()) {
        return false;
    }
    
    UserSession& session = it.value();
    session.isActive = false;
    
    logger_->info(QString("User %1 logged out").arg(session.username));
    active_sessions_.erase(it);
    
    return true;
}

UserSession* AuthManager::getSession(const QString& sessionToken) {
    QMutexLocker locker(&session_mutex_);
    
    auto it = active_sessions_.find(sessionToken);
    if (it == active_sessions_.end()) {
        return nullptr;
    }
    
    return &it.value();
}

void AuthManager::updateSessionActivity(const QString& sessionToken) {
    QMutexLocker locker(&session_mutex_);
    
    auto it = active_sessions_.find(sessionToken);
    if (it != active_sessions_.end() && it.value().isActive) {
        it.value().lastActivity = QDateTime::currentDateTime();
    }
}

void AuthManager::cleanupExpiredSessions() {
    QMutexLocker locker(&session_mutex_);
    
    QDateTime now = QDateTime::currentDateTime();
    auto it = active_sessions_.begin();
    
    while (it != active_sessions_.end()) {
        if (!it.value().isActive || 
            it.value().lastActivity.addSecs(session_timeout_minutes_ * 60) < now) {
            logger_->debug(QString("Cleaning up expired session for user: %1").arg(it.value().username));
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool AuthManager::isAccountLocked(const QString& username) {
    unlockExpiredAccounts();
    
    auto it = locked_accounts_.find(username);
    return it != locked_accounts_.end();
}

void AuthManager::recordLoginAttempt(const QString& username, const QString& clientIP, bool successful) {
    LoginAttempt attempt;
    attempt.username = username;
    attempt.clientIP = clientIP;
    attempt.attemptTime = QDateTime::currentDateTime();
    attempt.successful = successful;
    
    login_attempts_[username].append(attempt);
    
    if (!successful) {
        // 检查失败次数
        int failedAttempts = 0;
        QDateTime cutoff = QDateTime::currentDateTime().addSecs(-lockout_duration_minutes_ * 60);
        
        for (const auto& loginAttempt : login_attempts_[username]) {
            if (loginAttempt.attemptTime > cutoff && !loginAttempt.successful) {
                failedAttempts++;
            }
        }
        
        if (failedAttempts >= max_login_attempts_) {
            lockAccount(username);
        }
    }
    
    cleanupLoginAttempts();
}

bool AuthManager::checkRateLimit(const QString& clientIP) {
    QDateTime now = QDateTime::currentDateTime();
    QDateTime windowStart = now.addSecs(-rate_limit_window_seconds_);
    
    // 清理过期的请求记录
    auto& requests = rate_limit_tracker_[clientIP];
    requests.erase(std::remove_if(requests.begin(), requests.end(),
                                  [windowStart](const QDateTime& time) {
                                      return time < windowStart;
                                  }), requests.end());
    
    // 检查当前窗口内的请求数量
    if (requests.size() >= max_requests_per_window_) {
        return false;
    }
    
    // 记录当前请求
    requests.append(now);
    return true;
}

QString AuthManager::generateSessionToken() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void AuthManager::lockAccount(const QString& username) {
    locked_accounts_[username] = QDateTime::currentDateTime().addSecs(lockout_duration_minutes_ * 60);
    logger_->warning(QString("Account locked: %1").arg(username));
}

void AuthManager::unlockExpiredAccounts() {
    QDateTime now = QDateTime::currentDateTime();
    auto it = locked_accounts_.begin();
    
    while (it != locked_accounts_.end()) {
        if (it.value() <= now) {
            logger_->info(QString("Account unlocked: %1").arg(it.key()));
            it = locked_accounts_.erase(it);
        } else {
            ++it;
        }
    }
}

void AuthManager::cleanupLoginAttempts() {
    QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    
    for (auto& attempts : login_attempts_) {
        attempts.erase(std::remove_if(attempts.begin(), attempts.end(),
                                      [cutoff](const LoginAttempt& attempt) {
                                          return attempt.attemptTime < cutoff;
                                      }), attempts.end());
    }
}

void AuthManager::cleanupRateLimitData() {
    QDateTime cutoff = QDateTime::currentDateTime().addSecs(-rate_limit_window_seconds_ * 2);
    
    for (auto& requests : rate_limit_tracker_) {
        requests.erase(std::remove_if(requests.begin(), requests.end(),
                                      [cutoff](const QDateTime& time) {
                                          return time < cutoff;
                                      }), requests.end());
    }
}

// 配置方法实现
void AuthManager::setSessionTimeout(int minutes) {
    session_timeout_minutes_ = minutes;
}

void AuthManager::setMaxLoginAttempts(int attempts) {
    max_login_attempts_ = attempts;
}

void AuthManager::setLockoutDuration(int minutes) {
    lockout_duration_minutes_ = minutes;
}

void AuthManager::setRateLimitWindow(int seconds) {
    rate_limit_window_seconds_ = seconds;
}

void AuthManager::setMaxRequestsPerWindow(int requests) {
    max_requests_per_window_ = requests;
}