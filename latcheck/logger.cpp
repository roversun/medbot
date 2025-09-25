#include "logger.h"
#include <QStandardPaths>
#include <QDir>
#include <QMutexLocker>
#include <QDebug> // 添加此行以支持qDebug输出

Logger::Logger(QObject *parent)
    : QObject(parent), m_logFile(nullptr), m_logStream(nullptr)
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

// Path management methods
QString Logger::getLogDirPath() const
{
    qDebug() << "Logger: Getting log directory path";
    QString appDir = QCoreApplication::applicationDirPath();
    return appDir + QDir::separator() + "logs";
}

bool Logger::ensureLogDirExists() const
{
    QString logDirPath = getLogDirPath();
    qDebug() << "Logger: Ensuring log dir exists:" << logDirPath; // 添加调试输出
    QDir logDir(logDirPath);
    if (!logDir.exists())
    {
        qDebug() << "Logger: Directory does not exist, attempting to create"; // 添加调试输出
        bool success = logDir.mkpath(".");
        if (!success)
        {
            qDebug() << "Logger: Failed to create directory:" << logDirPath; // 添加调试输出
            // 发送错误信息到日志显示区域
            const_cast<Logger *>(this)->logMessageAdded(
                QString("Failed to create directory: %1").arg(logDirPath));
        }
        return success;
    }
    qDebug() << "Logger: Directory already exists"; // 添加调试输出
    return true;
}

// 新增私有方法：统一格式化带时间戳的消息
QString Logger::formatTimestampedMessage(const QString &message) const
{
    return QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate).replace('T', ' '))
        .arg(message);
}

void Logger::startNewSession(const QString &location)
{
    qDebug() << "Logger: Entering startNewSession with location:" << location;

    endSession();

    QMutexLocker locker(&m_logMutex);

    m_sessionLocation = location;
    m_sessionStartTime = QDateTime::currentDateTime();

    // 使用exe目录下的logs目录
    QString logDirPath = getLogDirPath();

    if (!ensureLogDirExists())
    {
        QString errorMsg = formatTimestampedMessage(
            QString("Failed to create log directory: %1").arg(logDirPath));
        qDebug() << "Logger: Error creating directory:" << errorMsg;
        emit logMessageAdded(errorMsg);
        return;
    }

    QString filename = generateLogFileName();
    QString fullPath = QDir(getLogDirPath()).absoluteFilePath(filename);

    m_logFile = new QFile(fullPath, this);
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append))
    {
        qDebug() << "Logger: Log file opened successfully:" << fullPath;
        m_logStream = new QTextStream(m_logFile);
        setCurrentLogFile(fullPath);
    }
    else
    {
        QString errorMsg = formatTimestampedMessage(
            QString("Failed to create log file: %1 - Error: %2")
                .arg(fullPath)
                .arg(m_logFile->errorString()));
        qDebug() << "Logger: Error opening log file:" << errorMsg;
        emit logMessageAdded(errorMsg);
        delete m_logFile;
        m_logFile = nullptr;
    }
    qDebug() << "Logger: Exiting startNewSession";
}

QString Logger::generateHourlyLogFileName(const QDateTime &dateTime)
{
    return QString("latcheck_%1.log")
        .arg(dateTime.toString("yyyyMMdd_hh"));
}

bool Logger::shouldCreateNewLogFile(const QDateTime &currentTime)
{
    // 如果还没有当前小时记录，或者小时已经改变，则需要创建新文件
    return !m_currentHour.isValid() ||
           m_currentHour.toString("yyyyMMdd_hh") != currentTime.toString("yyyyMMdd_hh");
}

void Logger::createNewHourlyLogFile(const QDateTime &currentTime)
{
    // 关闭当前文件
    if (m_logStream)
    {
        delete m_logStream;
        m_logStream = nullptr;
    }
    if (m_logFile)
    {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }

    // 创建新的日志文件
    QString logDir = getLogDirPath();
    if (!ensureLogDirExists())
    {
        qDebug() << "Logger: Failed to create log directory";
        return;
    }

    QString fileName = generateHourlyLogFileName(currentTime);
    QString fullPath = QDir(logDir).filePath(fileName);

    // 修复：添加this作为父对象，确保资源正确管理
    m_logFile = new QFile(fullPath, this);
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append))
    {
        m_logStream = new QTextStream(m_logFile);
        m_logStream->setEncoding(QStringConverter::Utf8);

        // 如果是新文件，写入文件头
        if (m_logFile->size() == 0)
        {
            *m_logStream << "==========================================" << Qt::endl;
            *m_logStream << formatTimestampedMessage("New hourly log session started") << Qt::endl;
            *m_logStream << "==========================================" << Qt::endl;
        }

        m_currentHour = currentTime;
        setCurrentLogFile(fileName);
        qDebug() << "Logger: Created new hourly log file:" << fullPath;
    }
    else
    {
        qDebug() << "Logger: Failed to create hourly log file:" << fullPath;
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void Logger::logMessage(const QString &message)
{
    qDebug() << "Logger: Entering logMessage:" << message;
    QMutexLocker locker(&m_logMutex);

    QDateTime currentTime = QDateTime::currentDateTime();

    // 检查是否需要创建新的小时日志文件
    if (shouldCreateNewLogFile(currentTime))
    {
        createNewHourlyLogFile(currentTime);
    }

    if (m_logStream)
    {
        QString timestampedMessage = formatTimestampedMessage(message);

        qDebug() << "Logger: Writing message to stream";
        *m_logStream << timestampedMessage << Qt::endl;
        m_logStream->flush();

        qDebug() << "Logger: Emitting logMessageAdded";
        emit logMessageAdded(timestampedMessage);
    }
    qDebug() << "Logger: Exiting logMessage";
}

void Logger::logLatencyResult(const QString &ip, int latency)
{
    QMutexLocker locker(&m_logMutex);

    QDateTime currentTime = QDateTime::currentDateTime();

    // 检查是否需要创建新的小时日志文件
    if (shouldCreateNewLogFile(currentTime))
    {
        createNewHourlyLogFile(currentTime);
    }

    if (m_logStream)
    {
        QString message = QString("Latency to %1: %2ms").arg(ip).arg(latency);
        QString timestampedMessage = formatTimestampedMessage(message);

        *m_logStream << timestampedMessage << Qt::endl;
        m_logStream->flush();

        emit logMessageAdded(timestampedMessage);
    }
}

void Logger::setCurrentLogFile(const QString &filename)
{
    if (m_currentLogFile != filename)
    {
        m_currentLogFile = filename;
        emit currentLogFileChanged();
    }
}

QString Logger::generateLogFileName()
{
    return QString("latcheck_%1.log")
        .arg(m_sessionStartTime.toString("yyyyMMdd_hhmm"));
}

void Logger::endSession()
{
    qDebug() << "Logger: Entering endSession";
    QMutexLocker locker(&m_logMutex);

    if (m_logStream)
    {
        QString endMessage = formatTimestampedMessage(
            QString("Session ended at %1").arg(m_sessionLocation));
        *m_logStream << endMessage << Qt::endl;
        *m_logStream << "===========================================" << Qt::endl;
        m_logStream->flush();

        delete m_logStream;
        m_logStream = nullptr;
    }

    if (m_logFile)
    {
        qDebug() << "Logger: Closing log file";
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }
    qDebug() << "Logger: Exiting endSession";
}
