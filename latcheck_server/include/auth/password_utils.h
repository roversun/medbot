#ifndef PASSWORDUTILS_H
#define PASSWORDUTILS_H

#include <QString>
#include <QByteArray>

class PasswordUtils
{
public:
    // 生成密码哈希
    static QString generatePasswordHash(const QString& password, const QString& salt);
    
    // 生成盐值
    static QString generateSalt();
    
    // 验证密码
    static bool verifyPassword(const QString& password, const QString& hash, const QString& salt);
    
    // 密码强度验证
    static bool validatePasswordStrength(const QString& password);
    
private:
    PasswordUtils() = delete; // 工具类，禁止实例化
};

#endif // PASSWORDUTILS_H