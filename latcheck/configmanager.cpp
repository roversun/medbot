#include "configmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#include <QSysInfo>
#include <QRandomGenerator>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#include <QNetworkInterface>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#undef interface  // Undefine the Windows macro to avoid conflict
#endif

// RSA私钥 - 用于解密
static const char* PRIVATE_KEY_PEM = 
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDWJtt/aWA4gNh8\n"
    "hykeiKBPzs47jgl64eA2YmXpYy5qqhpcPUox/1yZ19sptSknCmcMyyhFSDehQ6yh\n"
    "z3yx1A9oy2FV5Pwci4Fx2ITk4+mhAi63uNuqb8LXHJ/pmIRF3+qU4e0f/udmQ8kB\n"
    "L46NpS+Bx6pEUOtY1QQnuOLUXE0NWhfwQbJ6ltaZDsvj7Uy1fWSGngeSSeXEPpxV\n"
    "o0kQaWczegVJSshBrEVPjpt2/ggrJZZlhxpGS94I02Py/wsHDdH0/YKW3R/H7VUE\n"
    "slSMLUJYJ5+E4YVcLWQ92er3HNUS9OmFWBfgcegU09mTPv9ok2EV1x3G9PNzHTd2\n"
    "JMt8/3wbAgMBAAECggEAMzrse5h72kiIgZHb9l/86dqxX3HlNq5ecuQrOR/u9Llo\n"
    "wPQwkbFjN7J5zYp3I83LRTY8lgZmuH92BueaDviKDynISUIURcfh2ZhQgmk7dLcO\n"
    "8giZFDbUR52ja8B+tDCS77b0gRj7xm0k4tG7qor/yG45f0pUYtXivpXuPZsvG6gn\n"
    "1CJ17pBNzEHu/6Oh1cvwahmrhiYdrclD7UKlEeFImwrK23CGwUgpwnUYpB0f0Zr/\n"
    "NeM3P9oSK+mgu1nYq5+nmMvbVLl3hN57cf9UVGltWCz/5LhfbyvG6oKK96yc0lds\n"
    "oWyK4hNNWnA5dHOynijYXbQkuKnmteSVDvRTOFtHcQKBgQDtggMy9IRVrm0F/lSU\n"
    "CDZJW8cmy2gGPU+9+dOraUNSqlsRaDWWX9KQzm5rGzO0U5VLm+IG3zDUEHyel8R/\n"
    "vhIUcAnZfHm6oJF7V4y79OV6BvkbgVBzcS7nt0SnomQCTaxZQo6nd+Q5v1asq4WO\n"
    "D1Rv4654lbVxgru7nlIsQGLsDQKBgQDm01BDOMTOXsxODUPJ3SgtIM/tnTey+/YO\n"
    "0DmEkEhtfRSJYVZ5NXAbdmZzBlzk7RqlwZqsFyv815ZHWUrfUh9AXpamlDgnKjMv\n"
    "2deKOdofnco5ZZERRbIyniND4fBMwzB6sXnIeZCYyM77xxS8ItCmb0CLHmUKe3Ji\n"
    "edhmRXx2xwKBgQDeq8zaAfrr5ZtbSiX7n3Nd9YjSK1O8vdC8OLQleYvfvc2hlUTg\n"
    "VbgxgQhurFMeVlqbR2tyq9+4gjiD66ATb5y9wojJeXUa7p1WuS89rI1UiaMVEXje\n"
    "speaMO1SCOKgcjgKe2cJRVMYfPYC7tBI7sBoIlsZvKoe4MWnUfiSek8EJQKBgGMh\n"
    "ycy06sD+sZur0rs1QfXvq50O8kbqMwi1oJ4wIQr0twcxSKQRsS36MZGg3/BpbBJY\n"
    "DYdvBDM4m4/U53T1nfpVJToxWdRoKUeZtRnmMql1aM4xspXKaveWNusGc99jzMRE\n"
    "yFqy6sjTJSG4pE0QXq+8A+o7iYVw8vUcAzYZDZUtAoGAb0N4n6OUNrNCt+PDDHnc\n"
    "0ExhaVNlArtnUT3yyZli1EuQvuBgF3yml3bHeMJlDxAKb29ncsxR8t272MvsTWVn\n"
    "/SnY04gJyUy9al3ADu4dRSS+vMicNybVBo7ociVb7td6BZRYKuq2BIJhcdj9xhFj\n"
    "PxxkIuWesbxAnGZ1T2sbwcs=\n"
    "-----END PRIVATE KEY-----";

// RSA公钥证书 - 用于加密
static const char* CERTIFICATE_PEM = 
    "-----BEGIN CERTIFICATE-----\n"
    "MIID1DCCArygAwIBAgIUOMj611Cghs9/QobdbhoJPczvBz4wDQYJKoZIhvcNAQEL\n"
    "BQAwWDELMAkGA1UEBhMCQ04xCzAJBgNVBAgMAlNIMQswCQYDVQQHDAJTSDEPMA0G\n"
    "A1UECgwGTWVkYm90MQ0wCwYDVQQLDARITEhUMQ8wDQYDVQQDDAZWUE5fQ0EwHhcN\n"
    "MjUwODA0MDc1NTU4WhcNMjYwODA0MDc1NTU4WjBjMQswCQYDVQQGEwJDTjELMAkG\n"
    "A1UECAwCU0gxCzAJBgNVBAcMAlNIMQ8wDQYDVQQKDAZNZWRib3QxDTALBgNVBAsM\n"
    "BEhMSFQxGjAYBgNVBAMMEWNsaWVudC5tZWRib3QuY29tMIIBIjANBgkqhkiG9w0B\n"
    "AQEFAAOCAQ8AMIIBCgKCAQEA1ibbf2lgOIDYfIcpHoigT87OO44JeuHgNmJl6WMu\n"
    "aqoaXD1KMf9cmdfbKbUpJwpnDMsoRUg3oUOsoc98sdQPaMthVeT8HIuBcdiE5OPp\n"
    "oQIut7jbqm/C1xyf6ZiERd/qlOHtH/7nZkPJAS+OjaUvgceqRFDrWNUEJ7ji1FxN\n"
    "DVoX8EGyepbWmQ7L4+1MtX1khp4HkknlxD6cVaNJEGlnM3oFSUrIQaxFT46bdv4I\n"
    "KyWWZYcaRkveCNNj8v8LBw3R9P2Clt0fx+1VBLJUjC1CWCefhOGFXC1kPdnq9xzV\n"
    "EvTphVgX4HHoFNPZkz7/aJNhFdcdxvTzcx03diTLfP98GwIDAQABo4GKMIGHMB8G\n"
    "A1UdIwQYMBaAFFdxBWrd4J46DJsZrGpyTHcIi1XLMAkGA1UdEwQCMAAwCwYDVR0P\n"
    "BAQDAgTwMC0GA1UdEQQmMCSCEWNsaWVudC5tZWRib3QuY29tgglsb2NhbGhvc3SH\n"
    "BH8AAAEwHQYDVR0OBBYEFOz8cV7PWFKPTh+dzyFFlnE7p+35MA0GCSqGSIb3DQEB\n"
    "CwUAA4IBAQA80Cds6k7vLoSD1clX0s6TheHSklt4TAKcRDZerHKL1J2U7F9R3NFO\n"
    "ONw2IQMsqjEGpAOvnbhl5uldzZQffFvO6xP9B2mXhN8CMytEDRXHh7GUL4xzLioh\n"
    "NtGEh3S33PgUndkCZFJaj1v/neqtxiUyKWpI1nIRscYW+R7fZznmQ+36ZBYv322s\n"
    "cWt8V2abu2a0eLwZz8EuoQ+VGnmiQDYUy3b6f+Y9pIkg9Tl2RDu0u5qlDO+aw64O\n"
    "0cIoJqTMnTF0XZXBQAz5mVK7wA2zgowNDLbZ37c2aV3uDxEXyqUKLI041MpougLE\n"
    "sajV3iEzLoib8m2MYKh+SgOtexL6fweQ\n"
    "-----END CERTIFICATE-----";

// 移除 PRIVATE_KEY_PEM 和 CERTIFICATE_PEM 常量定义

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_serverIp("127.0.0.1")
    , m_serverPort(8080)
    , m_threadCount(50)
    , m_autoLocation(false)
    , m_ignoreSslErrors(true)
{
    m_configFilePath = getConfigFilePath();
    ensureConfigDirExists();
    loadConfig();
}

// Path management methods
QString ConfigManager::getConfigDirPath() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).absoluteFilePath("config");
}

QString ConfigManager::getConfigFilePath() const
{
    return QDir(getConfigDirPath()).absoluteFilePath("config.json");
}

bool ConfigManager::ensureConfigDirExists() const
{
    QDir configDir(getConfigDirPath());
    if (!configDir.exists()) {
        return configDir.mkpath(".");
    }
    return true;
}

// JSON conversion methods
QJsonObject ConfigManager::toJsonObject() const
{
    QJsonObject json;
    
    QJsonObject server;
    server["ip"] = m_serverIp;
    server["port"] = m_serverPort;
    json["server"] = server;
    
    QJsonObject threading;
    threading["count"] = m_threadCount;
    json["threading"] = threading;
    
    QJsonObject auth;
    auth["username"] = m_username;
    auth["password_hash"] = m_passwordHash;
    auth["salt"] = m_salt;
    json["auth"] = auth;
    
    QJsonObject location;
    location["text"] = m_location;
    location["auto"] = m_autoLocation;
    json["location"] = location;
    
    QJsonObject certificates;
    certificates["client_cert"] = m_clientCertPath;
    certificates["client_key"] = m_clientKeyPath;
    json["certificates"] = certificates;
    
    QJsonObject ssl;
    ssl["ignore_errors"] = m_ignoreSslErrors;
    json["ssl"] = ssl;
    
    return json;
}

void ConfigManager::fromJsonObject(const QJsonObject &json)
{
    // Server settings
    QJsonObject server = json["server"].toObject();
    m_serverIp = server["ip"].toString("127.0.0.1");
    m_serverPort = server["port"].toInt(8080);
    
    // Threading settings
    QJsonObject threading = json["threading"].toObject();
    m_threadCount = threading["count"].toInt(50);
    
    // Auth settings
    QJsonObject auth = json["auth"].toObject();
    m_username = auth["username"].toString("");
    m_passwordHash = auth["password_hash"].toString("");
    m_salt = auth["salt"].toString("");
    
    // Location settings
    QJsonObject location = json["location"].toObject();
    m_location = location["text"].toString("");
    m_autoLocation = location["auto"].toBool(false);
    
    // Certificate settings
    QJsonObject certificates = json["certificates"].toObject();
    m_clientCertPath = certificates["client_cert"].toString("");
    m_clientKeyPath = certificates["client_key"].toString("");
    
    // SSL settings
    QJsonObject ssl = json["ssl"].toObject();
    m_ignoreSslErrors = ssl["ignore_errors"].toBool(true);
}

// Config methods
bool ConfigManager::saveConfig()
{
    try {
        if (!ensureConfigDirExists()) {
            return false;
        }
        
        QJsonObject json = toJsonObject();
        QJsonDocument doc(json);
        
        QFile file(m_configFilePath);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        
        file.write(doc.toJson());
        file.close();
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigManager::loadConfig()
{
    QFile file(m_configFilePath);
    if (!file.exists()) {
        // 使用默认值，不需要做任何事情
        return;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        return;
    }
    
    fromJsonObject(doc.object());
}

// 添加getter和setter方法
bool ConfigManager::ignoreSslErrors() const
{
    return m_ignoreSslErrors;
}

void ConfigManager::setIgnoreSslErrors(bool ignore)
{
    if (m_ignoreSslErrors != ignore) {
        m_ignoreSslErrors = ignore;
        emit ignoreSslErrorsChanged();
    }
}

// Add all missing getter methods
QString ConfigManager::serverIp() const
{
    return m_serverIp;
}

void ConfigManager::setServerIp(const QString &ip)
{
    if (m_serverIp != ip) {
        m_serverIp = ip;
        emit serverIpChanged();
    }
}

int ConfigManager::serverPort() const
{
    return m_serverPort;
}

void ConfigManager::setServerPort(int port)
{
    if (m_serverPort != port) {
        m_serverPort = port;
        emit serverPortChanged();
    }
}

int ConfigManager::threadCount() const
{
    return m_threadCount;
}

void ConfigManager::setThreadCount(int count)
{
    if (m_threadCount != count) {
        m_threadCount = count;
        emit threadCountChanged();
    }
}

QString ConfigManager::username() const
{
    return m_username;
}

void ConfigManager::setUsername(const QString &username)
{
    if (m_username != username) {
        m_username = username;
        emit usernameChanged();
    }
}

QString ConfigManager::location() const
{
    return m_location;
}

void ConfigManager::setLocation(const QString &location)
{
    if (m_location != location) {
        m_location = location;
        emit locationChanged();
    }
}

bool ConfigManager::autoLocation() const
{
    return m_autoLocation;
}

void ConfigManager::setAutoLocation(bool enabled)
{
    if (m_autoLocation != enabled) {
        m_autoLocation = enabled;
        emit autoLocationChanged();
    }
}

QString ConfigManager::clientCertPath() const
{
    return m_clientCertPath;
}

void ConfigManager::setClientCertPath(const QString &path)
{
    if (m_clientCertPath != path) {
        m_clientCertPath = path;
        emit clientCertPathChanged();
    }
}

QString ConfigManager::clientKeyPath() const
{
    return m_clientKeyPath;
}

void ConfigManager::setClientKeyPath(const QString &path)
{
    if (m_clientKeyPath != path) {
        m_clientKeyPath = path;
        emit clientKeyPathChanged();
    }
}

// RSA加密解密方法
QSslKey ConfigManager::getPrivateKey() const
{
    return QSslKey(QByteArray(PRIVATE_KEY_PEM), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
}

QSslCertificate ConfigManager::getCertificate() const
{
    return QSslCertificate(QByteArray(CERTIFICATE_PEM), QSsl::Pem);
}

// 获取公钥（从证书中提取）
QSslKey ConfigManager::getPublicKey() const
{
    QSslCertificate cert = getCertificate();
    return cert.publicKey();
}

// 简化机器指纹获取方法，移除网卡MAC地址获取
QString ConfigManager::getMachineFingerprint()
{
    QStringList identifiers;
    
    // 仅获取系统UUID（Windows）
#ifdef Q_OS_WIN
    QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography", QSettings::NativeFormat);
    QString machineGuid = registry.value("MachineGuid").toString();
    if (!machineGuid.isEmpty()) {
        identifiers << machineGuid;
    }
#endif
    
    // 如果没有获取到系统UUID，使用系统信息作为备选
    if (identifiers.isEmpty()) {
        identifiers << QSysInfo::machineHostName();
        identifiers << QSysInfo::productType();
    }
    
    // 组合并哈希
    QString combined = identifiers.join("-");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(combined.toUtf8());
    return hash.result().toHex();
}

// 简化密码加密方法，使用简单的对称加密
QString ConfigManager::encryptPassword(const QString &password)
{
    if (password.isEmpty()) {
        return QString();
    }
    
    // 使用机器指纹生成密钥
    QString machineId = getMachineFingerprint();
    QByteArray key = QCryptographicHash::hash(machineId.toUtf8(), QCryptographicHash::Sha256);
    
    // 使用Windows CryptoAPI加密
    QByteArray encryptedData = encryptWithCryptoAPI(password.toUtf8(), key);
    
    if (encryptedData.isEmpty()) {
        qWarning() << "Failed to encrypt password";
        return QString();
    }
    
    return encryptedData.toBase64();
}

QString ConfigManager::decryptPassword(const QString &encryptedPassword)
{
    if (encryptedPassword.isEmpty()) {
        return QString();
    }
    
    QByteArray encryptedData = QByteArray::fromBase64(encryptedPassword.toUtf8());
    
    // 使用机器指纹生成密钥
    QString machineId = getMachineFingerprint();
    QByteArray key = QCryptographicHash::hash(machineId.toUtf8(), QCryptographicHash::Sha256);
    
    // 使用Windows CryptoAPI解密
    QByteArray decryptedData = decryptWithCryptoAPI(encryptedData, key);
    
    if (decryptedData.isEmpty()) {
        qWarning() << "Failed to decrypt password";
        return QString();
    }
    
    return QString::fromUtf8(decryptedData);
}

QString ConfigManager::generateSalt()
{
    // 生成16字节的随机盐
    QByteArray salt;
    for (int i = 0; i < 16; ++i) {
        salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    return salt.toHex();
}

bool ConfigManager::verifyPassword(const QString &password)
{
    if (m_passwordHash.isEmpty()) {
        return password.isEmpty();
    }
    
    // 解密存储的密码并比较
    QString decryptedPassword = decryptPassword(m_passwordHash);
    return decryptedPassword == password;
}


// 添加一个生成基于机器ID的IV的辅助方法
QByteArray ConfigManager::generateIVFromMachineID()
{
    QString machineFingerprint = getMachineFingerprint();
    
    // 使用机器指纹生成固定的16字节IV
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(machineFingerprint.toUtf8());
    hash.addData("IV_SALT"); // 添加盐值以区分密钥和IV的用途
    
    QByteArray hashResult = hash.result();
    // 取前16字节作为AES的IV
    return hashResult.left(16);
}

QByteArray ConfigManager::encryptWithCryptoAPI(const QByteArray &data, const QByteArray &key)
{
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    QByteArray result;
    
    do {
        // 获取加密服务提供者
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            break;
        }
        
        // 创建哈希对象
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            break;
        }
        
        // 添加密钥数据到哈希
        if (!CryptHashData(hHash, (BYTE*)key.constData(), key.size(), 0)) {
            break;
        }
        
        // 从哈希派生密钥
        if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
            break;
        }
        
        // 使用基于机器ID的IV
        QByteArray iv = generateIVFromMachineID();
        
        // 设置加密模式为CBC并设置IV
        DWORD mode = CRYPT_MODE_CBC;
        if (!CryptSetKeyParam(hKey, KP_MODE, (BYTE*)&mode, 0)) {
            break;
        }
        
        if (!CryptSetKeyParam(hKey, KP_IV, (BYTE*)iv.data(), 0)) {
            break;
        }
        
        // 准备加密数据
        QByteArray encryptData = data;
        DWORD dataLen = encryptData.size();
        DWORD bufferLen = dataLen + 16; // AES块大小填充
        encryptData.resize(bufferLen);
        
        // 执行加密
        if (!CryptEncrypt(hKey, 0, TRUE, 0, (BYTE*)encryptData.data(), &dataLen, bufferLen)) {
            break;
        }
        
        encryptData.resize(dataLen);
        
        // 由于IV是基于机器ID生成的，不需要存储IV，直接返回加密数据
        result = encryptData;
        
    } while (false);
    
    // 清理资源
    if (hKey) CryptDestroyKey(hKey);
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    
    return result;
}

QByteArray ConfigManager::decryptWithCryptoAPI(const QByteArray &encryptedData, const QByteArray &key)
{
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    QByteArray result;
    
    do {
        // 获取加密服务提供者
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            break;
        }
        
        // 创建哈希对象
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            break;
        }
        
        // 添加密钥数据到哈希
        if (!CryptHashData(hHash, (BYTE*)key.constData(), key.size(), 0)) {
            break;
        }
        
        // 从哈希派生密钥
        if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
            break;
        }
        
        // 使用相同的基于机器ID的IV
        QByteArray iv = generateIVFromMachineID();
        
        // 设置解密模式为CBC并设置IV
        DWORD mode = CRYPT_MODE_CBC;
        if (!CryptSetKeyParam(hKey, KP_MODE, (BYTE*)&mode, 0)) {
            break;
        }
        
        if (!CryptSetKeyParam(hKey, KP_IV, (BYTE*)iv.data(), 0)) {
            break;
        }
        
        // 准备解密数据
        QByteArray decryptData = encryptedData;
        DWORD dataLen = decryptData.size();
        
        // 执行解密
        if (!CryptDecrypt(hKey, 0, TRUE, 0, (BYTE*)decryptData.data(), &dataLen)) {
            break;
        }
        
        decryptData.resize(dataLen);
        result = decryptData;
        
    } while (false);
    
    // 清理资源
    if (hKey) CryptDestroyKey(hKey);
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    
    return result;
}


QString ConfigManager::hashPassword(const QString &password, const QString &salt)
{
    QByteArray combined = (password + salt).toUtf8();
    return QCryptographicHash::hash(combined, QCryptographicHash::Sha256).toHex();
}

bool ConfigManager::setPassword(const QString &password)
{
    if (password.isEmpty()) {
        m_passwordHash = "";
        m_salt = "";
        return true;
    }
    
    // 使用RSA加密存储
    m_passwordHash = encryptPassword(password);
    m_salt = generateSalt();
    return true;
}
