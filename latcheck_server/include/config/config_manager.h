#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMutex>
#include "common/types.h"
#include "logger/logger.h"

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    // 单例模式
    static ConfigManager* instance();
    static void destroyInstance();
    
    // 配置加载和获取方法
    bool loadConfig(const QString& configPath);
    bool reloadConfig();
    QJsonObject getConfigSection(const QString& section) const;
    
    // 配置结构体获取方法 - 添加这些缺失的方法
    DatabaseConfig getDatabaseConfig() const;
    ServerConfig getServerConfig() const;
    ApiConfig getApiConfig() const;
    TlsConfig getTlsConfig() const;
    LogConfig getLogConfig() const;
    
    // 数据库配置获取方法
    QString getDatabaseHost() const;
    int getDatabasePort() const;
    QString getDatabaseName() const;
    QString getDatabaseUser() const;
    QString getDatabasePassword() const;
    int getDatabaseMaxConnections() const;
    
    // 服务器配置获取方法
    QString getServerHost() const;
    int getServerPort() const;
    QString getCertificatePath() const;
    QString getPrivateKeyPath() const;
    
    // API配置获取方法
    QString getApiHost() const;
    int getApiPort() const;
    
    // 日志配置获取方法
    QString getLogLevel() const;
    QString getLogDirectory() const;
    int getLogMaxFileSize() const;
    int getLogMaxFiles() const;
    
    // TLS配置获取方法
    int getTlsPort() const;
    int getHttpsPort() const;
    
private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager(); // 保持私有，但添加友元或删除器
    
    // 添加友元删除器
    friend class std::default_delete<ConfigManager>;
    
    // 单例模式相关
    static ConfigManager* instance_;
    static QMutex mutex_;
    
    // 配置相关成员变量
    QString config_file_path_;
    QString config_path_;  // 添加缺失的成员变量
    QJsonObject config_json_;
    QJsonObject config_;   // 添加缺失的成员变量
    mutable QMutex config_mutex_;  // 添加缺失的互斥锁
    
    // 配置结构体
    DatabaseConfig database_config_;
    ServerConfig server_config_;
    ApiConfig api_config_;
    TlsConfig tls_config_;
    LogConfig log_config_;
    
    // 私有方法
    void loadDefaultConfig();
    bool parseJsonConfig(const QJsonObject& json);
    QJsonObject configToJson() const;
};

#endif // CONFIGMANAGER_H