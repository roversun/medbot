#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QDir>
#include <QCoreApplication>

class Logger : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLogFile READ currentLogFile NOTIFY currentLogFileChanged)

public:
    explicit Logger(QObject *parent = nullptr);
    ~Logger();

    QString currentLogFile() const;

    Q_INVOKABLE void startNewSession(const QString &location);
    Q_INVOKABLE void endSession();
    Q_INVOKABLE void logMessage(const QString &message);
    Q_INVOKABLE void logLatencyResult(const QString &ip, int latency);

signals:
    void currentLogFileChanged();
    void logMessageAdded(const QString &message);

private:
    QFile *m_logFile;
    QTextStream *m_logStream;
    QString m_sessionLocation;
    QDateTime m_sessionStartTime;
    QDateTime m_currentHour; // 新增：当前日志文件对应的小时
    mutable QMutex m_logMutex;
    QString m_currentLogFile;

    QString getLogDirPath() const;
    bool ensureLogDirExists() const;
    void setCurrentLogFile(const QString &filename);
    QString generateLogFileName();
    QString generateHourlyLogFileName(const QDateTime &dateTime);   // 新增：生成按小时的日志文件名
    bool shouldCreateNewLogFile(const QDateTime &currentTime);      // 新增：判断是否需要创建新日志文件
    void createNewHourlyLogFile(const QDateTime &currentTime);      // 新增：创建新的小时日志文件
    QString formatTimestampedMessage(const QString &message) const; // 新增方法
};

#endif // LOGGER_H