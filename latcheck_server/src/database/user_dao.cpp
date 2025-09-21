#include "database/user_dao.h"
#include "database/base_dao.h"
#include "logger/logger.h"
#include "common/error_codes.h"

#include <QCryptographicHash>
#include <QUuid>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>

UserDAO::UserDAO(QObject *parent)
    : BaseDAO(parent)
{
}

User UserDAO::authenticateUser(const QString& username, const QString& password)
{
    User user;
    
    if (username.isEmpty() || password.isEmpty()) {
        Logger::instance()->warning("Empty username or password for authentication", "USER_DAO");
        return user;
    }
    
    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE username = ? AND status != ?"; 
    
    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.next()) {
        QString storedHash = query.value("password_hash").toString();
        QString salt = query.value("salt").toString();
        
        if (verifyPassword(password, storedHash, salt)) {
            user = buildUserFromQuery(query);
            
            // 更新最后登录时间
            updateLastLoginTime(user.id);
            
            Logger::instance()->auditLog(QString::number(user.id), "LOGIN", 
                                        QString("User %1 logged in successfully").arg(username), true);
        } else {
            Logger::instance()->auditLog("0", "LOGIN", 
                                        QString("Failed login attempt for user %1").arg(username), false);
        }
    } else {
        Logger::instance()->auditLog("0", "LOGIN", 
                                    QString("Login attempt for non-existent user %1").arg(username), false);
    }
    
    return user;
}

ErrorCode UserDAO::createUser(const User& user)
{
    if (!validateUserData(user)) {
        return ErrorCode::InvalidParameter;
    }
    
    // 检查用户名是否已存在
    if (isUsernameExists(user.userName)) {
        return ErrorCode::UserExists;
    }
    
    QString salt = generateSalt();
    QString passwordHash = generatePasswordHash(user.password, salt);
    
    QString sql = "INSERT INTO users (username, password_hash, salt, role, status, created_at, updated_at) "
                  "VALUES (?, ?, ?, ?, ?, NOW(), NOW())";
    
    QVariantList params;
    params << user.userName << passwordHash << salt 
           << static_cast<int>(user.role) << static_cast<int>(user.status);
    
    if (executeUpdate(sql, params)) {
        Logger::instance()->auditLog("SYSTEM", "CREATE_USER", 
                                    QString("User %1 created successfully").arg(user.userName), true);
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::updateUser(const User& user)
{
    if (user.id <= 0) {
        return ErrorCode::InvalidParameter;
    }
    
    if (!validateUserData(user)) {
        return ErrorCode::InvalidParameter;
    }
    
    // 检查用户名是否被其他用户使用
    User existingUser = getUserByUsername(user.userName);
    if (existingUser.id > 0 && existingUser.id != user.id) {
        return ErrorCode::UserExists;
    }
    
    QString sql = "UPDATE users SET username = ?, role = ?, status = ?, updated_at = NOW() "
                  "WHERE user_id = ?";
    
    QVariantList params;
    params << user.userName << static_cast<int>(user.role) 
           << static_cast<int>(user.status) << user.id;
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.isValid()) {
        Logger::instance()->auditLog(QString::number(user.id), "UPDATE_USER", 
                                    QString("User %1 updated successfully").arg(user.userName), true);
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::deleteUser(qint64 userId)
{
    if (userId <= 0) {
        return ErrorCode::InvalidParameter;
    }
    
    // 软删除：更新状态为已删除
    QString sql = "UPDATE users SET status = ?, updated_at = NOW() WHERE user_id = ?";
    
    QVariantList params;
    params << static_cast<int>(UserStatus::Deleted) << userId;
    
    if (executeUpdate(sql, params)) {
        Logger::instance()->auditLog(QString::number(userId), "DELETE_USER", 
                                    QString("User %1 deleted successfully").arg(userId), true);
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

User UserDAO::getUserById(qint64 userId)
{
    User user;
    
    if (userId <= 0) {
        return user;
    }
    
    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE user_id = ? AND status != ?";
    
    QVariantList params;
    params << userId << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.next()) {
        user = buildUserFromQuery(query);
    }
    
    return user;
}

User UserDAO::getUserByUsername(const QString& username)
{
    User user;
    
    if (username.isEmpty()) {
        return user;
    }
    
    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE username = ? AND status != ?";
    
    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.next()) {
        user = buildUserFromQuery(query);
    }
    
    return user;
}

QList<User> UserDAO::getAllUsers()
{
    QList<User> users;
    
    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE status != ? ORDER BY created_at DESC";
    
    QVariantList params;
    params << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    while (query.next()) {
        users.append(buildUserFromQuery(query));
    }
    
    return users;
}

ErrorCode UserDAO::updateUserPassword(qint64 userId, const QString& newPassword)
{
    if (userId <= 0 || newPassword.isEmpty()) {
        return ErrorCode::InvalidParameter;
    }
    
    if (newPassword.length() < 6) {
        Logger::instance()->warning("Password too short", "USER_DAO");
        return ErrorCode::InvalidParameter;
    }
    
    QString salt = generateSalt();
    QString passwordHash = generatePasswordHash(newPassword, salt);
    
    QString sql = "UPDATE users SET password_hash = ?, salt = ?, updated_at = NOW() WHERE user_id = ?";
    
    QVariantList params;
    params << passwordHash << salt << userId;
    
    if (executeUpdate(sql, params)) {
        Logger::instance()->auditLog(QString::number(userId), "UPDATE_PASSWORD", 
                                    "Password updated successfully", true);
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::updateUserStatus(qint64 userId, UserStatus status)
{
    if (userId <= 0) {
        return ErrorCode::InvalidParameter;
    }
    
    QString sql = "UPDATE users SET status = ?, updated_at = NOW() WHERE user_id = ?";
    
    QVariantList params;
    params << static_cast<int>(status) << userId;
    
    if (executeUpdate(sql, params)) {
        Logger::instance()->auditLog(QString::number(userId), "UPDATE_STATUS", 
                                    QString("Status updated to %1").arg(static_cast<int>(status)), true);
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::updateLastLoginTime(qint64 userId)
{
    if (userId <= 0) {
        return ErrorCode::InvalidParameter;
    }
    
    QString sql = "UPDATE users SET last_login_at = NOW() WHERE user_id = ?";
    
    QVariantList params;
    params << userId;
    
    if (executeUpdate(sql, params)) {
        return ErrorCode::Success;
    }
    
    return ErrorCode::DatabaseError;
}

bool UserDAO::isUsernameExists(const QString& username)
{
    if (username.isEmpty()) {
        return false;
    }
    
    QString sql = "SELECT COUNT(*) FROM users WHERE username = ? AND status != ?";
    
    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

int UserDAO::getUserCount()
{
    QString sql = "SELECT COUNT(*) FROM users WHERE status != ?";
    
    QVariantList params;
    params << static_cast<int>(UserStatus::Deleted);
    
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

User UserDAO::buildUserFromQuery(const QSqlQuery& query)
{
    User user;
    user.id = query.value("user_id").toLongLong();
    user.userName = query.value("username").toString();
    user.password = ""; // 不返回密码
    user.role = static_cast<UserRole>(query.value("role").toInt());
    user.status = static_cast<UserStatus>(query.value("status").toInt());
    user.createdAt = fromDatabaseTimestamp(query.value("created_at"));
    user.updatedAt = fromDatabaseTimestamp(query.value("updated_at"));
    user.lastLoginAt = fromDatabaseTimestamp(query.value("last_login_at"));
    
    return user;
}

bool UserDAO::validateUserData(const User& user)
{
    // 验证用户名
    if (user.userName.isEmpty() || user.userName.length() > 50) {
        Logger::instance()->warning("Invalid username length", "USER_DAO");
        return false;
    }
    
    // 用户名只能包含字母、数字、下划线
    QRegularExpression usernameRegex("^[a-zA-Z0-9_]+$");
    if (!usernameRegex.match(user.userName).hasMatch()) {
        Logger::instance()->warning("Invalid username format", "USER_DAO");
        return false;
    }
    
    // 验证密码（仅在创建用户时）
    if (!user.password.isEmpty() && user.password.length() < 6) {
        Logger::instance()->warning("Password too short", "USER_DAO");
        return false;
    }
    
    return true;
}

QString UserDAO::generatePasswordHash(const QString& password, const QString& salt)
{
    QByteArray data = (password + salt).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toHex();
}

QString UserDAO::generateSalt()
{
    return QUuid::createUuid().toString().remove('{').remove('}').remove('-');
}

bool UserDAO::verifyPassword(const QString& password, const QString& hash, const QString& salt)
{
    QString computedHash = generatePasswordHash(password, salt);
    return computedHash == hash;
}