#include "auth/password_utils.h"
#include <QCryptographicHash>
#include <QUuid>
#include <QRegularExpression>

QString PasswordUtils::generatePasswordHash(const QString& password, const QString& salt)
{
    QByteArray data = (password + salt).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toHex();
}

QString PasswordUtils::generateSalt()
{
    return QUuid::createUuid().toString().remove('{').remove('}').remove('-');
}

bool PasswordUtils::verifyPassword(const QString& password, const QString& hash, const QString& salt)
{
    QString computedHash = generatePasswordHash(password, salt);
    return computedHash == hash;
}

bool PasswordUtils::validatePasswordStrength(const QString& password)
{
    if (password.length() < 6) {
        return false;
    }
    
    // 可以添加更多密码强度检查规则
    // 例如：包含大小写字母、数字、特殊字符等
    
    return true;
}