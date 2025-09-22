#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include "common/types.h"

class Logger : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Critical = 4
    };
    Q_ENUM(LogLevel)
    
    // 单例模式
    static Logger* instance();
    
    // 初始化日志系统
    bool initialize(const LogConfig& config);
    
    // 设置日志级别和目录
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;
    void setLogDirectory(const QString& directory);
    void setMaxFileSize(qint64 size);
    void setMaxFiles(int count);
    
    // 日志记录方法
    void debug(const QString& message, const QString& category = QString());
    void info(const QString& message, const QString& category = QString());
    void warning(const QString& message, const QString& category = QString());
    void error(const QString& message, const QString& category = QString());
    void critical(const QString& message, const QString& category = QString());
    
    // 审计日志方法
    void auditLog(const QString& userId, const QString& action, const QString& details, bool success = true);
    
    // 通用日志记录方法
    void log(LogLevel level, const QString& message, const QString& category = QString());
    
    // 刷新日志缓冲区
    void flush();
    
    // 关闭日志系统
    void close();

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger();
    
    // 禁用拷贝构造和赋值操作
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    static Logger* instance_;
    static QMutex mutex_;

private slots:
    void rotateLogFile();
    
private:
    // 成员变量
    LogConfig config_;
    LogLevel log_level_;
    QString log_directory_;
    qint64 max_file_size_;
    int max_files_;
    qint64 current_file_size_;
    
    QFile log_file_;
    QFile audit_file_;
    QTextStream* log_stream_;
    QMutex log_mutex_;
    QMutex audit_mutex_;
    
    // 私有方法
    void initializeLogDirectory();
    void openLogFile();
    void openAuditFile();
    void writeToFile(const QString& formattedMessage);
    void writeToConsole(const QString& formattedMessage, LogLevel level);
    QString formatMessage(LogLevel level, const QString& message, const QString& category);
    QString logLevelToString(LogLevel level);
    bool shouldLog(LogLevel level) const;
    void checkAndRotateFile();
    void rotateAuditFile();
};

#endif // LOGGER_H

