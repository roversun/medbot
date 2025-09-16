#include "logger.h"
#include <QStandardPaths>
#include <QDir>
#include <QMutexLocker>

Logger::Logger(QObject *parent)
    : QObject(parent)
    , m_logFile(nullptr)
    , m_logStream(nullptr)
{
}

Logger::~Logger()
{
    endSession();
}

QString Logger::currentLogFile() const
{
    return m_currentLogFile;
}

void Logger::startNewSession(const QString &location)
{
    QMutexLocker locker(&m_logMutex);
    
    endSession();
    
    m_sessionLocation = location;
    m_sessionStartTime = QDateTime::currentDateTime();
    
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(logDir);
    
    QString filename = generateLogFileName();
    QString fullPath = logDir + "/" + filename;
    
    m_logFile = new QFile(fullPath, this);
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_logStream = new QTextStream(m_logFile);
        setCurrentLogFile(fullPath);
        
        // Write session start marker
        QString startMessage = QString("=== LATCHECK SESSION START ===");
        *m_logStream << startMessage << Qt::endl;
        *m_logStream << QString("Start Time: %1").arg(m_sessionStartTime.toString(Qt::ISODate)) << Qt::endl;
        *m_logStream << QString("Location: %1").arg(location) << Qt::endl;
        *m_logStream << QString("==============================") << Qt::endl;
        m_logStream->flush();
        
        emit logMessageAdded(startMessage);
    }
}

void Logger::endSession()
{
    QMutexLocker locker(&m_logMutex);
    
    if (m_logStream) {
        QDateTime endTime = QDateTime::currentDateTime();
        QString endMessage = QString("=== LATCHECK SESSION END ===");
        *m_logStream << endMessage << Qt::endl;
        *m_logStream << QString("End Time: %1").arg(endTime.toString(Qt::ISODate)) << Qt::endl;
        *m_logStream << QString("Duration: %1 seconds").arg(m_sessionStartTime.secsTo(endTime)) << Qt::endl;
        *m_logStream << QString("=============================") << Qt::endl;
        m_logStream->flush();
        
        emit logMessageAdded(endMessage);
        
        delete m_logStream;
        m_logStream = nullptr;
    }
    
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void Logger::logMessage(const QString &message)
{
    QMutexLocker locker(&m_logMutex);
    
    if (m_logStream) {
        QString timestampedMessage = QString("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
            .arg(message);
        
        *m_logStream << timestampedMessage << Qt::endl;
        m_logStream->flush();
        
        emit logMessageAdded(timestampedMessage);
    }
}

void Logger::logLatencyResult(const QString &ip, int latency)
{
    QString message;
    if (latency >= 0) {
        message = QString("Ping %1: %2ms").arg(ip).arg(latency);
    } else {
        message = QString("Ping %1: TIMEOUT/ERROR").arg(ip);
    }
    
    logMessage(message);
}

void Logger::setCurrentLogFile(const QString &filename)
{
    if (m_currentLogFile != filename) {
        m_currentLogFile = filename;
        emit currentLogFileChanged();
    }
}

QString Logger::generateLogFileName()
{
    return QString("latcheck_%1.log")
        .arg(m_sessionStartTime.toString("yyyyMMdd_hhmm"));
}