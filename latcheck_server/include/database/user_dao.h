#ifndef USERDAO_H
#define USERDAO_H

#include "database/base_dao.h"
#include "common/types.h"
#include "common/error_codes.h"
#include <QList>

class UserDAO : public BaseDAO
{
    Q_OBJECT

public:
    explicit UserDAO(QObject *parent = nullptr);
    
    // 用户认证
    User authenticateUser(const QString& username, const QString& password);
    
    // 创建用户
    ErrorCode createUser(const User& user);
    
    // 更新用户信息
    ErrorCode updateUser(const User& user);
    
    // 删除用户
    ErrorCode deleteUser(qint64 userId);
    
    // 根据ID获取用户
    User getUserById(qint64 userId);
    
    // 根据用户名获取用户
    User getUserByUsername(const QString& username);
    
    // 获取所有用户
    QList<User> getAllUsers();
    
    // 更新用户密码
    ErrorCode updateUserPassword(qint64 userId, const QString& newPassword);
    
    // 更新用户状态
    ErrorCode updateUserStatus(qint64 userId, UserStatus status);
    
    // 更新最后登录时间
    ErrorCode updateLastLoginTime(qint64 userId);
    
    // 检查用户名是否存在
    bool isUsernameExists(const QString& username);
    
    // 获取用户总数
    int getUserCount();
    
private:
    // 从查询结果构建用户对象
    User buildUserFromQuery(const QSqlQuery& query);
    
    // 验证用户数据
    bool validateUserData(const User& user);
    
    // 生成密码哈希
    QString generatePasswordHash(const QString& password, const QString& salt);
    
    // 生成盐值
    QString generateSalt();
    
    // 验证密码
    bool verifyPassword(const QString& password, const QString& hash, const QString& salt);
};
#endif // USERDAO_H