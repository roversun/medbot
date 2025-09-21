#include "logger/logger.h"
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <QMutexLocker>
#include <QDebug>
#include <iostream>

Logger* Logger::instance_ = nullptr;
QMutex Logger::mutex_;

Logger::Logger(QObject *parent)
    : QObject(parent)
    , log_level_(LogLevel::Info)
    , max_file_size_(10 * 1024 * 1024) // 10MB
    , max_files_(10)
    , current_file_size_(0)
{
    initializeLogDirectory();
    openLogFile();
}

Logger::~Logger()
{
    if (log_file_.isOpen()) {
        log_file_.close();
    }
    if (audit_file_.isOpen()) {
        audit_file_.close();
    }
}

Logger* Logger::instance()
{
    QMutexLocker locker(&mutex_);
    if (!instance_) {
        instance_ = new Logger();
    }
    return instance_;
}

void Logger::setLogLevel(LogLevel level)
{
    log_level_ = level;
}

void Logger::setLogDirectory(const QString& directory)
{
    log_directory_ = directory;
    initializeLogDirectory();
}

void Logger::setMaxFileSize(qint64 size)
{
    max_file_size_ = size;
}

void Logger::setMaxFiles(int count)
{
    max_files_ = count;
}

void Logger::debug(const QString& message, const QString& category)
{
    log(LogLevel::Debug, message, category);
}

void Logger::info(const QString& message, const QString& category)
{
    log(LogLevel::Info, message, category);
}

void Logger::warning(const QString& message, const QString& category)
{
    log(LogLevel::Warning, message, category);
}

void Logger::error(const QString& message, const QString& category)
{
    log(LogLevel::Error, message, category);
}

void Logger::critical(const QString& message, const QString& category)
{
    log(LogLevel::Critical, message, category);
}

void Logger::auditLog(const QString& userId, const QString& action, const QString& details, bool success)
{
    QMutexLocker locker(&audit_mutex_);
    
    if (!audit_file_.isOpen()) {
        openAuditFile();
    }
    
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString status = success ? "SUCCESS" : "FAILURE";
    QString logEntry = QString("%1|%2|%3|%4|%5\n")
                      .arg(timestamp)
                      .arg(userId)
                      .arg(action)
                      .arg(status)
                      .arg(details);
    
    QTextStream stream(&audit_file_);
    stream << logEntry;
    stream.flush();
    
    // 检查审计文件大小
    if (audit_file_.size() > max_file_size_) {
        rotateAuditFile();
    }
}

void Logger::log(LogLevel level, const QString& message, const QString& category)
{
    if (level < log_level_) {
        return;
    }
    
    QMutexLocker locker(&log_mutex_);
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString levelStr = logLevelToString(level);
    QString logEntry = QString("[%1] [%2] [%3] %4\n")
                      .arg(timestamp)
                      .arg(levelStr)
                      .arg(category.isEmpty() ? "GENERAL" : category)
                      .arg(message);
    
    // 写入文件
    if (log_file_.isOpen()) {
        QTextStream stream(&log_file_);
        stream << logEntry;
        stream.flush();
        
        current_file_size_ += logEntry.toUtf8().size();
        
        // 检查文件大小，需要轮转
        if (current_file_size_ > max_file_size_) {
            rotateLogFile();
        }
    }
    
    // 同时输出到控制台（Error和Critical级别）
    if (level >= LogLevel::Error) {
        std::cerr << logEntry.toStdString();
    } else if (level >= LogLevel::Warning) {
        std::cout << logEntry.toStdString();
    }
}

void Logger::initializeLogDirectory()
{
    if (log_directory_.isEmpty()) {
        log_directory_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    }
    
    QDir dir;
    if (!dir.exists(log_directory_)) {
        dir.mkpath(log_directory_);
    }
}

void Logger::openLogFile()
{
    if (log_file_.isOpen()) {
        log_file_.close();
    }
    
    QString fileName = log_directory_ + "/latcheck_server.log";
    log_file_.setFileName(fileName);
    
    if (log_file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
        current_file_size_ = log_file_.size();
    } else {
        qCritical() << "Failed to open log file:" << fileName;
    }
}

void Logger::openAuditFile()
{
    if (audit_file_.isOpen()) {
        audit_file_.close();
    }
    
    QString fileName = log_directory_ + "/audit.log";
    audit_file_.setFileName(fileName);
    
    if (!audit_file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qCritical() << "Failed to open audit file:" << fileName;
    }
}

void Logger::rotateLogFile()
{
    log_file_.close();
    
    // 轮转日志文件
    for (int i = max_files_ - 1; i > 0; --i) {
        QString oldFile = QString("%1/latcheck_server.log.%2").arg(log_directory_).arg(i);
        QString newFile = QString("%1/latcheck_server.log.%2").arg(log_directory_).arg(i + 1);
        
        if (QFile::exists(oldFile)) {
            QFile::remove(newFile);
            QFile::rename(oldFile, newFile);
        }
    }
    
    // 重命名当前日志文件
    QString currentFile = log_directory_ + "/latcheck_server.log";
    QString backupFile = log_directory_ + "/latcheck_server.log.1";
    QFile::rename(currentFile, backupFile);
    
    // 创建新的日志文件
    openLogFile();
}

void Logger::rotateAuditFile()
{
    audit_file_.close();
    
    // 轮转审计文件
    for (int i = max_files_ - 1; i > 0; --i) {
        QString oldFile = QString("%1/audit.log.%2").arg(log_directory_).arg(i);
        QString newFile = QString("%1/audit.log.%2").arg(log_directory_).arg(i + 1);
        
        if (QFile::exists(oldFile)) {
            QFile::remove(newFile);
            QFile::rename(oldFile, newFile);
        }
    }
    
    // 重命名当前审计文件
    QString currentFile = log_directory_ + "/audit.log";
    QString backupFile = log_directory_ + "/audit.log.1";
    QFile::rename(currentFile, backupFile);
    
    // 创建新的审计文件
    openAuditFile();
}

QString Logger::logLevelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

bool Logger::initialize(const LogConfig& config)
{
    config_ = config;
    
    // Set log level based on config
    if (config.level == "debug") {
        log_level_ = LogLevel::Debug;
    } else if (config.level == "info") {
        log_level_ = LogLevel::Info;
    } else if (config.level == "warning") {
        log_level_ = LogLevel::Warning;
    } else if (config.level == "error") {
        log_level_ = LogLevel::Error;
    } else if (config.level == "critical") {
        log_level_ = LogLevel::Critical;
    } else {
        log_level_ = LogLevel::Info; // default
    }
    
    // Set log directory from config
    if (!config.filePath.isEmpty()) {
        QFileInfo fileInfo(config.filePath);
        log_directory_ = fileInfo.absolutePath();
        initializeLogDirectory();
        openLogFile();
        openAuditFile();
    }
    
    return true;
}