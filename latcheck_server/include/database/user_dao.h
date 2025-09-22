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
    
    // 添加printUser方法声明
    void printUser(User& user);
    
    // 用户认证 - 需要传入已哈希的密码和盐值
    User authenticateUser(const QString& username, const QString& passwordHash, const QString& salt);
    
    // 创建用户 - 需要传入已哈希的密码和盐值
    ErrorCode createUser(const QString& username, const QString& passwordHash, const QString& salt, 
                        UserRole role, UserStatus status = UserStatus::Active);
    
    // 更新用户信息（不包括密码）
    ErrorCode updateUser(const User& user);
    
    // 删除用户
    ErrorCode deleteUser(qint64 userId);
    
    // 根据ID获取用户
    User getUserById(qint64 userId);
    
    // 根据用户名获取用户（包含密码哈希和盐值用于验证）
    User getUserByUsername(const QString& username);
    
    // 获取用户基本信息（不包含密码相关信息）
    User getUserBasicInfo(const QString& username);
    
    // 获取所有用户
    QList<User> getAllUsers();
    
    // 更新用户密码 - 需要传入新的哈希密码和盐值
    ErrorCode updateUserPassword(qint64 userId, const QString& newPasswordHash, const QString& newSalt);
    
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
    bool validateUserData(const QString& userName);
};

#endif // USERDAO_H