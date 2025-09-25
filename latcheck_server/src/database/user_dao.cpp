#include "database/user_dao.h"
#include "database/base_dao.h"
#include "logger/logger.h"
#include "common/error_codes.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QDebug>

UserDAO::UserDAO(QObject *parent)
    : BaseDAO(parent)
{
}

User UserDAO::authenticateUser(const QString &username, const QString &passwordHash, const QString &salt)
{
    User user;

    if (username.isEmpty() || passwordHash.isEmpty() || salt.isEmpty())
    {
        Logger::instance()->warning("Empty parameters for authentication", "USER_DAO");
        return user;
    }

    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE username = ? AND status != ?";

    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
        QString storedHash = query.value("password_hash").toString();
        QString storedSalt = query.value("salt").toString();

        // 验证密码哈希和盐值
        if (passwordHash == storedHash && salt == storedSalt)
        {
            user = buildUserFromQuery(query);

            // 更新最后登录时间
            updateLastLoginTime(user.id);

            Logger::instance()->auditLog(QString::number(user.id), "LOGIN",
                                         QString("User %1 logged in successfully").arg(username), true);
        }
        else
        {
            Logger::instance()->auditLog("0", "LOGIN",
                                         QString("Failed login attempt for user %1").arg(username), false);
        }
    }
    else
    {
        Logger::instance()->auditLog("0", "LOGIN",
                                     QString("Login attempt for non-existent user %1").arg(username), false);
    }

    return user;
}

// 修改 createUser 方法中的 validateUserData 调用
ErrorCode UserDAO::createUser(const QString &username, const QString &passwordHash, const QString &salt,
                              UserRole role, UserStatus status)
{
    // 调用已声明的方法版本，不再传递 role 参数
    if (!validateUserData(username) || passwordHash.isEmpty() || salt.isEmpty())
    {
        return ErrorCode::InvalidParameter;
    }

    // 检查用户名是否已存在
    if (isUsernameExists(username))
    {
        return ErrorCode::UserExists;
    }

    QString sql = "INSERT INTO users (username, password_hash, salt, role, status, created_at, updated_at) "
                  "VALUES (?, ?, ?, ?, ?, NOW(), NOW())";

    QVariantList params;
    params << username << passwordHash << salt
           << static_cast<int>(role) << static_cast<int>(status);

    if (executeUpdate(sql, params))
    {
        Logger::instance()->auditLog("SYSTEM", "CREATE_USER",
                                     QString("User %1 created successfully").arg(username), true);
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

User UserDAO::getUserBasicInfo(const QString &username)
{
    User user;

    if (username.isEmpty())
    {
        return user;
    }

    QString sql = "SELECT user_id, username, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE username = ? AND status != ?";

    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
        user.id = query.value("user_id").toLongLong();
        user.userName = query.value("username").toString();
        user.role = static_cast<UserRole>(query.value("role").toInt());
        user.status = static_cast<UserStatus>(query.value("status").toInt());
        user.createdAt = fromDatabaseTimestamp(query.value("created_at"));
        user.updatedAt = fromDatabaseTimestamp(query.value("updated_at"));
        user.lastLoginAt = fromDatabaseTimestamp(query.value("last_login_at"));
        // 不返回密码相关信息
    }

    return user;
}

ErrorCode UserDAO::updateUserPassword(qint64 userId, const QString &newPasswordHash, const QString &newSalt)
{
    if (userId <= 0 || newPasswordHash.isEmpty() || newSalt.isEmpty())
    {
        return ErrorCode::InvalidParameter;
    }

    QString sql = "UPDATE users SET password_hash = ?, salt = ?, updated_at = NOW() WHERE user_id = ?";

    QVariantList params;
    params << newPasswordHash << newSalt << userId;

    if (executeUpdate(sql, params))
    {
        Logger::instance()->auditLog(QString::number(userId), "UPDATE_PASSWORD",
                                     "Password updated successfully", true);
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

// 修改 updateUser 方法中的 validateUserData 调用
ErrorCode UserDAO::updateUser(const User &user)
{
    if (user.id <= 0)
    {
        return ErrorCode::InvalidParameter;
    }

    // 直接调用已声明的方法版本，不需要额外参数
    if (!validateUserData(user.userName))
    {
        return ErrorCode::InvalidParameter;
    }

    // 检查用户名是否被其他用户使用
    User existingUser = getUserByUsername(user.userName);
    if (existingUser.id > 0 && existingUser.id != user.id)
    {
        return ErrorCode::UserExists;
    }

    QString sql = "UPDATE users SET username = ?, role = ?, status = ?, updated_at = NOW() "
                  "WHERE user_id = ?";

    QVariantList params;
    params << user.userName << static_cast<int>(user.role)
           << static_cast<int>(user.status) << user.id;

    QSqlQuery query = executeQuery(sql, params);

    if (query.isValid())
    {
        Logger::instance()->auditLog(QString::number(user.id), "UPDATE_USER",
                                     QString("User %1 updated successfully").arg(user.userName), true);
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::deleteUser(qint64 userId)
{
    if (userId <= 0)
    {
        return ErrorCode::InvalidParameter;
    }

    // 软删除：更新状态为已删除
    QString sql = "UPDATE users SET status = ?, updated_at = NOW() WHERE user_id = ?";

    QVariantList params;
    params << static_cast<int>(UserStatus::Deleted) << userId;

    if (executeUpdate(sql, params))
    {
        Logger::instance()->auditLog(QString::number(userId), "DELETE_USER",
                                     QString("User %1 deleted successfully").arg(userId), true);
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

User UserDAO::getUserById(qint64 userId)
{
    User user;

    if (userId <= 0)
    {
        return user;
    }

    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE user_id = ? AND status != ?";

    QVariantList params;
    params << userId << static_cast<int>(UserStatus::Deleted);

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
        user = buildUserFromQuery(query);
    }

    return user;
}

User UserDAO::getUserByUsername(const QString &username)
{
    User user;

    if (username.isEmpty())
    {
        return user;
    }

    QString sql = "SELECT user_id, username, password_hash, salt, role, status, "
                  "created_at, updated_at, last_login_at FROM users WHERE username = ? AND status = ?";

    QVariantList params;
    params << username << static_cast<int>(UserStatus::Active);

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
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

    while (query.next())
    {
        users.append(buildUserFromQuery(query));
    }

    return users;
}

ErrorCode UserDAO::updateUserStatus(qint64 userId, UserStatus status)
{
    if (userId <= 0)
    {
        return ErrorCode::InvalidParameter;
    }

    QString sql = "UPDATE users SET status = ?, updated_at = NOW() WHERE user_id = ?";

    QVariantList params;
    params << static_cast<int>(status) << userId;

    if (executeUpdate(sql, params))
    {
        // 添加更详细的状态变更日志
        QString statusStr;
        switch (status)
        {
        case UserStatus::Active:
            statusStr = "Active";
            break;
        case UserStatus::Inactive:
            statusStr = "Inactive";
            break;
        case UserStatus::Suspended:
            statusStr = "Suspended";
            break;
        case UserStatus::Deleted:
            statusStr = "Deleted";
            break;
        default:
            statusStr = "Unknown";
            break;
        }

        Logger::instance()->auditLog(QString::number(userId), "UPDATE_STATUS",
                                     QString("User %1 status updated to %2").arg(userId).arg(statusStr), true);
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

ErrorCode UserDAO::updateLastLoginTime(qint64 userId)
{
    if (userId <= 0)
    {
        return ErrorCode::InvalidParameter;
    }

    QString sql = "UPDATE users SET last_login_at = NOW() WHERE user_id = ?";

    QVariantList params;
    params << userId;

    if (executeUpdate(sql, params))
    {
        return ErrorCode::Success;
    }

    return ErrorCode::DatabaseError;
}

bool UserDAO::isUsernameExists(const QString &username)
{
    if (username.isEmpty())
    {
        return false;
    }

    QString sql = "SELECT COUNT(*) FROM users WHERE username = ? AND status != ?";

    QVariantList params;
    params << username << static_cast<int>(UserStatus::Deleted);

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
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

    if (query.next())
    {
        return query.value(0).toInt();
    }

    return 0;
}

User UserDAO::buildUserFromQuery(const QSqlQuery &query)
{
    User user;
    user.id = query.value("user_id").toLongLong();
    user.userName = query.value("username").toString();
    user.passwordHash = query.value("password_hash").toString();
    user.salt = query.value("salt").toString();
    user.role = static_cast<UserRole>(query.value("role").toInt());
    user.status = static_cast<UserStatus>(query.value("status").toInt());
    user.createdAt = fromDatabaseTimestamp(query.value("created_at"));
    user.updatedAt = fromDatabaseTimestamp(query.value("updated_at"));
    user.lastLoginAt = fromDatabaseTimestamp(query.value("last_login_at"));

    return user;
}

bool UserDAO::validateUserData(const QString &userName)
{
    // 验证用户名
    if (userName.isEmpty() || userName.length() > 32)
    {
        Logger::instance()->warning("Invalid username length", "USER_DAO");
        return false;
    }

    // 用户名只能包含字母、数字、下划线
    QRegularExpression usernameRegex("^[a-zA-Z0-9_]+$");
    if (!usernameRegex.match(userName).hasMatch())
    {
        Logger::instance()->warning("Invalid username format", "USER_DAO");
        return false;
    }

    return true;
}

void UserDAO::printUser(User &user)
{
    // 将用户角色转换为字符串
    QString roleStr;
    switch (user.role)
    {
    case UserRole::Admin:
        roleStr = "Admin";
        break;
    case UserRole::ReportUploader:
        roleStr = "ReportUploader";
        break;
    case UserRole::ReportViewer:
        roleStr = "ReportViewer";
        break;
    default:
        roleStr = "Unknown";
        break;
    }

    // 将用户状态转换为字符串
    QString statusStr;
    switch (user.status)
    {
    case UserStatus::Active:
        statusStr = "Active";
        break;
    case UserStatus::Inactive:
        statusStr = "Inactive";
        break;
    case UserStatus::Suspended:
        statusStr = "Suspended";
        break;
    case UserStatus::Deleted:
        statusStr = "Deleted";
        break;
    default:
        statusStr = "Unknown";
        break;
    }

    // 构建用户信息字符串 - 使用QStringList代替多个+操作符
    QStringList userInfoParts;
    userInfoParts << "User Information:";
    userInfoParts << QString("  ID: %1").arg(user.id);
    userInfoParts << QString("  Username: %1").arg(user.userName);
    userInfoParts << QString("  Email: %1").arg(user.email);
    userInfoParts << QString("  Role: %1").arg(roleStr);
    userInfoParts << QString("  Status: %1").arg(statusStr);
    userInfoParts << QString("  Created At: %1").arg(user.createdAt.toString(Qt::ISODate));
    userInfoParts << QString("  Updated At: %1").arg(user.updatedAt.toString(Qt::ISODate));
    userInfoParts << QString("  Last Login At: %1").arg(user.lastLoginAt.toString(Qt::ISODate));
    userInfoParts << QString("  Login Attempts: %1").arg(user.loginAttempts);
    userInfoParts << QString("  Locked Until: %1").arg(user.lockedUntil.toString(Qt::ISODate));

    // 使用平台特定的换行符连接字符串
    QString userInfo = userInfoParts.join(QChar('\n'));

    // 输出到控制台 - 使用QTextStream确保正确处理换行符
    QTextStream(stdout) << userInfo << Qt::endl;

    // 记录到日志（保持简洁的单行日志）
    Logger::instance()->info(QString("User Info - ID: %1, Username: %2, Role: %3, Status: %4")
                                 .arg(user.id)
                                 .arg(user.userName)
                                 .arg(roleStr)
                                 .arg(statusStr),
                             "USER_DAO");
}