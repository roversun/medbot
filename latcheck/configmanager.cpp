#include "configmanager.h"
#include <QStandardPaths>
#include <QDir>

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_serverIp("127.0.0.1")
    , m_serverPort(8080)
    , m_threadCount(50)
    , m_autoLocation(false)
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configPath);
    m_settings = new QSettings(configPath + "/latcheck.ini", QSettings::IniFormat, this);
    loadConfig();
}

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

bool ConfigManager::setPassword(const QString &password)
{
    if (password.isEmpty()) {
        return false;
    }
    
    m_salt = generateSalt();
    m_passwordHash = hashPassword(password, m_salt);
    return true;
}

bool ConfigManager::verifyPassword(const QString &password)
{
    if (m_salt.isEmpty() || m_passwordHash.isEmpty()) {
        return false;
    }
    
    QString hash = hashPassword(password, m_salt);
    return hash == m_passwordHash;
}

void ConfigManager::saveConfig()
{
    m_settings->setValue("server/ip", m_serverIp);
    m_settings->setValue("server/port", m_serverPort);
    m_settings->setValue("threading/count", m_threadCount);
    m_settings->setValue("auth/username", m_username);
    m_settings->setValue("auth/password_hash", m_passwordHash);
    m_settings->setValue("auth/salt", m_salt);
    m_settings->setValue("location/text", m_location);
    m_settings->setValue("location/auto", m_autoLocation);
    m_settings->setValue("certificates/client_cert", m_clientCertPath);
    m_settings->setValue("certificates/client_key", m_clientKeyPath);
    m_settings->sync();
}

void ConfigManager::loadConfig()
{
    m_serverIp = m_settings->value("server/ip", "127.0.0.1").toString();
    m_serverPort = m_settings->value("server/port", 8080).toInt();
    m_threadCount = m_settings->value("threading/count", 50).toInt();
    m_username = m_settings->value("auth/username", "").toString();
    m_passwordHash = m_settings->value("auth/password_hash", "").toString();
    m_salt = m_settings->value("auth/salt", "").toString();
    m_location = m_settings->value("location/text", "").toString();
    m_autoLocation = m_settings->value("location/auto", false).toBool();
    m_clientCertPath = m_settings->value("certificates/client_cert", "").toString();
    m_clientKeyPath = m_settings->value("certificates/client_key", "").toString();
}

QString ConfigManager::generateSalt()
{
    QByteArray salt;
    for (int i = 0; i < 16; ++i) {
        salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    return salt.toHex();
}

QString ConfigManager::hashPassword(const QString &password, const QString &salt)
{
    QByteArray data = (password + salt).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toHex();
}