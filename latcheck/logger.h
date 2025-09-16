#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

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
    QString m_currentLogFile;
    QMutex m_logMutex;
    QString m_sessionLocation;
    QDateTime m_sessionStartTime;
    
    void setCurrentLogFile(const QString &filename);
    QString generateLogFileName();
};

#endif // LOGGER_H