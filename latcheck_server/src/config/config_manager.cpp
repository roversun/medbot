#include "config/config_manager.h"
#include "logger/logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QDebug>

ConfigManager *ConfigManager::instance_ = nullptr;
QMutex ConfigManager::mutex_;

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
}

ConfigManager::~ConfigManager()
{
}

ConfigManager *ConfigManager::instance()
{
    QMutexLocker locker(&mutex_);
    if (!instance_)
    {
        instance_ = new ConfigManager();
    }
    return instance_;
}

bool ConfigManager::loadConfig(const QString &configPath)
{
    QMutexLocker locker(&config_mutex_);

    config_path_ = configPath;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "Failed to open config file:" << configPath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError)
    {
        qCritical() << "Failed to parse config JSON:" << error.errorString();
        return false;
    }

    config_ = doc.object();
    qInfo() << "Configuration loaded successfully from:" << configPath;

    return true;
}

bool ConfigManager::reloadConfig()
{
    return loadConfig(config_path_);
}

QJsonObject ConfigManager::getConfigSection(const QString &section) const
{
    QMutexLocker locker(&config_mutex_);
    return config_.value(section).toObject();
}

QString ConfigManager::getDatabaseHost() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("host").toString("localhost");
}

int ConfigManager::getDatabasePort() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("port").toInt(3306);
}

QString ConfigManager::getDatabaseName() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("database").toString("latcheck");
}

QString ConfigManager::getDatabaseUser() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("username").toString("root");
}

QString ConfigManager::getDatabasePassword() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("password").toString("");
}

int ConfigManager::getDatabaseMaxConnections() const
{
    QJsonObject dbConfig = getConfigSection("database");
    return dbConfig.value("max_connections").toInt(10);
}

QString ConfigManager::getServerHost() const
{
    QJsonObject serverConfig = getConfigSection("server");
    return serverConfig.value("host").toString("0.0.0.0");
}

int ConfigManager::getServerPort() const
{
    QJsonObject serverConfig = getConfigSection("server");
    return serverConfig.value("port").toInt(8443);
}

QString ConfigManager::getCertificatePath() const
{
    QJsonObject serverConfig = getConfigSection("server");
    return serverConfig.value("certificate").toString("config/certs/server.crt");
}

QString ConfigManager::getPrivateKeyPath() const
{
    QJsonObject serverConfig = getConfigSection("server");
    return serverConfig.value("private_key").toString("config/certs/server.key");
}

QString ConfigManager::getApiHost() const
{
    QJsonObject apiConfig = getConfigSection("api");
    return apiConfig.value("host").toString("0.0.0.0");
}

int ConfigManager::getApiPort() const
{
    QJsonObject apiConfig = getConfigSection("api");
    return apiConfig.value("port").toInt(8080);
}

QString ConfigManager::getLogLevel() const
{
    QJsonObject logConfig = getConfigSection("logging");
    return logConfig.value("level").toString("INFO");
}

QString ConfigManager::getLogDirectory() const
{
    QJsonObject logConfig = getConfigSection("logging");
    return logConfig.value("directory").toString("logs");
}

int ConfigManager::getLogMaxFileSize() const
{
    QJsonObject logConfig = getConfigSection("logging");
    return logConfig.value("max_file_size").toInt(10485760); // 10MB
}

int ConfigManager::getLogMaxFiles() const
{
    QJsonObject logConfig = getConfigSection("logging");
    return logConfig.value("max_files").toInt(10);
}

// 服务器配置
int ConfigManager::getTlsPort() const
{
    return getConfigSection("server").value("tls_port").toInt(8443);
}

int ConfigManager::getHttpsPort() const
{
    return getConfigSection("server").value("https_port").toInt(8080);
}

// Fix the LogConfig method to match the actual struct definition
LogConfig ConfigManager::getLogConfig() const
{
    LogConfig config;
    QJsonObject logConfig = getConfigSection("logging");

    config.level = logConfig.value("level").toString("INFO");
    config.filePath = logConfig.value("file_path").toString("logs/server.log"); // Use filePath instead of directory
    config.maxFileSize = logConfig.value("max_file_size").toInt(10485760);      // 10MB
    config.maxFiles = logConfig.value("max_files").toInt(5);
    config.enableConsole = logConfig.value("enable_console").toBool(true);
    config.enableFile = logConfig.value("enable_file").toBool(true);
    config.format = logConfig.value("format").toString("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] %{message}");

    return config;
}

// Fix the TlsConfig method to match the actual struct definition
TlsConfig ConfigManager::getTlsConfig() const
{
    TlsConfig config;
    QJsonObject tlsConfig = getConfigSection("server");

    config.certificatePath = tlsConfig.value("certificate").toString("config/certs/server.crt");
    config.privateKeyPath = tlsConfig.value("private_key").toString("config/certs/server.key");
    config.protocol = tlsConfig.value("protocol").toString("TLSv1.2");
    config.requireClientCert = tlsConfig.value("require_client_cert").toBool(false);
    config.clientCertPath = tlsConfig.value("client_cert_path").toString("");

    // Handle cipher suites array
    QJsonArray cipherArray = tlsConfig.value("cipher_suites").toArray();
    for (const QJsonValue &cipher : cipherArray)
    {
        config.cipherSuites.append(cipher.toString());
    }

    return config;
}

// Add the other missing methods
DatabaseConfig ConfigManager::getDatabaseConfig() const
{
    DatabaseConfig config;
    QJsonObject dbConfig = getConfigSection("database");

    config.host = dbConfig.value("host").toString("localhost");
    config.port = dbConfig.value("port").toInt(3306);
    config.database = dbConfig.value("database").toString("latcheck");
    config.username = dbConfig.value("username").toString("root");
    config.password = dbConfig.value("password").toString("");
    config.minConnections = dbConfig.value("min_connections").toInt(5);
    config.maxConnections = dbConfig.value("max_connections").toInt(10);
    config.connectionTimeout = dbConfig.value("connection_timeout").toInt(30);
    config.idleTimeout = dbConfig.value("idle_timeout").toInt(300);
    config.charset = dbConfig.value("charset").toString("utf8mb4");
    config.enableSSL = dbConfig.value("enable_ssl").toBool(false);
    config.sslCert = dbConfig.value("ssl_cert").toString("");
    config.sslKey = dbConfig.value("ssl_key").toString("");
    config.sslCA = dbConfig.value("ssl_ca").toString("");

    return config;
}

ServerConfig ConfigManager::getServerConfig() const
{
    ServerConfig config;
    QJsonObject serverConfig = getConfigSection("server");

    config.host = serverConfig.value("host").toString("0.0.0.0");
    config.port = serverConfig.value("port").toInt(8443);
    config.maxConnections = serverConfig.value("max_connections").toInt(1000);
    config.connectionTimeout = serverConfig.value("connection_timeout").toInt(300);
    config.enableSSL = serverConfig.value("enable_ssl").toBool(true);
    config.certificatePath = serverConfig.value("certificate").toString("config/certs/server.crt");
    config.privateKeyPath = serverConfig.value("private_key").toString("config/certs/server.key");
    config.logLevel = serverConfig.value("log_level").toString("INFO");

    return config;
}

ApiConfig ConfigManager::getApiConfig() const
{
    ApiConfig config;
    QJsonObject apiConfig = getConfigSection("api");

    config.host = apiConfig.value("host").toString("0.0.0.0");
    config.port = apiConfig.value("port").toInt(8080);
    config.enableSSL = apiConfig.value("enable_ssl").toBool(false);
    config.certificatePath = apiConfig.value("certificate").toString("");
    config.privateKeyPath = apiConfig.value("private_key").toString("");
    config.enableCORS = apiConfig.value("enable_cors").toBool(true);
    config.logLevel = apiConfig.value("log_level").toString("INFO");

    // Handle allowed origins array
    QJsonArray originsArray = apiConfig.value("allowed_origins").toArray();
    for (const QJsonValue &origin : originsArray)
    {
        config.allowedOrigins.append(origin.toString());
    }

    return config;
}

void ConfigManager::destroyInstance()
{
    QMutexLocker locker(&mutex_);
    if (instance_)
    {
        delete instance_;
        instance_ = nullptr;
    }
}

QString ConfigManager::getCaCertificatePath() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("ca_certificate").toString();
}

bool ConfigManager::getRequireClientCert() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("require_client_cert").toBool();
}

bool ConfigManager::getUseWhitelist() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("use_whitelist").toBool();
}

bool ConfigManager::getUseBlacklist() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("use_blacklist").toBool();
}

QString ConfigManager::getWhitelistPath() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("whitelist_path").toString();
}

QString ConfigManager::getBlacklistPath() const
{
    QMutexLocker locker(&mutex_);
    return config_.value("server").toObject().value("blacklist_path").toString();
}