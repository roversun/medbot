#ifndef TYPES_H
#define TYPES_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QSqlDatabase>
#include "error_codes.h"

// 用户状态枚举
enum class UserStatus {
    Active,
    Inactive,
    Suspended,
    Deleted
};

// 用户角色枚举 - 移到这里，在User结构体之前
enum class UserRole {
    Admin,           // 管理员
    ReportUploader,  // 报告上传者
    ReportViewer     // 报告查询者
};

// 用户结构
struct User {
    qint64 id = 0;
    QString userName;
    QString passwordHash;
    QString salt; 
    QString email;
    UserRole role = UserRole::ReportUploader;  // 默认为报告上传者
    UserStatus status = UserStatus::Inactive;  // 改回 UserStatus 类型
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime lastLoginAt;
    int loginAttempts = 0;
    QDateTime lockedUntil;
};

// 报告状态枚举 - 原UserRole的位置
enum class ReportStatus {
    Pending,
    Processing,
    Completed,
    Failed
};

// 报告详细信息结构
struct ReportDetail {
    qint64 id = 0;
    qint64 reportId = 0;
    QString serverName;
    QString serverIp;
    double latency = 0.0;
    QString status;
    QDateTime testTime;
    QString additionalInfo;
};

// 报告结构
struct Report {
    qint64 id = 0;
    QString userName;
    QString location;
    ReportStatus status = ReportStatus::Pending;
    QDateTime createdAt;
    QDateTime updatedAt;
    QList<ReportDetail> details;
};

// 数据库配置结构
struct DatabaseConfig {
    QString host = "localhost";
    int port = 3306;
    QString database = "latcheck";
    QString username = "root";
    QString password;
    int minConnections = 5;      // 添加最小连接数
    int maxConnections = 10;
    int connectionTimeout = 30;
    int idleTimeout = 300;       // 添加空闲超时时间（秒）
    QString charset = "utf8mb4";
    bool enableSSL = false;
    QString sslCert;
    QString sslKey;
    QString sslCA;
    
    // 修复初始化顺序，按声明顺序初始化
    DatabaseConfig() : port(3306), minConnections(5), maxConnections(10), connectionTimeout(30), idleTimeout(300), charset("utf8mb4"), enableSSL(false) {}
};

// 服务器配置结构
struct ServerConfig {
    QString host = "0.0.0.0";
    int port = 8443;
    int maxConnections = 1000;
    int connectionTimeout = 300;
    bool enableSSL = true;
    QString certificatePath;
    QString privateKeyPath;
    QString logLevel = "INFO";
};

// API配置结构
struct ApiConfig {
    QString host = "0.0.0.0";
    int port = 8080;
    bool enableSSL = false;
    QString certificatePath;
    QString privateKeyPath;
    bool enableCORS = true;
    QStringList allowedOrigins;
    QString logLevel = "INFO";
};

// TLS配置结构
struct TlsConfig {
    QString certificatePath;
    QString privateKeyPath;
    QString protocol = "TLSv1.2";
    QStringList cipherSuites;
    bool requireClientCert = false;
    QString clientCertPath;
};

// 日志配置结构
struct LogConfig {
    QString level = "INFO";
    QString filePath;
    int maxFileSize = 10485760; // 10MB
    int maxFiles = 5;
    bool enableConsole = true;
    bool enableFile = true;
    QString format = "[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] %{message}";
};

// API响应结构
struct ApiResponse {
    int statusCode = 200;
    QString message;
    QJsonObject data;
    
    ApiResponse() = default;
    ApiResponse(int code, const QString& msg, const QJsonObject& responseData = QJsonObject())
        : statusCode(code), message(msg), data(responseData) {}
};

#endif // TYPES_H