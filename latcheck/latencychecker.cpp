#include "latencychecker.h"
#include <QDebug>
#include <QMutexLocker>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

LatencyWorker::LatencyWorker(const QList<QPair<quint32, quint32>> &serverList, QObject *parent)
    : QObject(parent), m_serverList(serverList), m_shouldStop(0)
{
}

void LatencyWorker::stop()
{
    m_shouldStop.storeRelaxed(1);
}

void LatencyWorker::startChecking()
{
    // emit logMessage("Starting latency check for " + QString::number(m_serverList.size()) + " servers");

    for (int i = 0; i < m_serverList.size(); ++i)
    {
        if (m_shouldStop.loadRelaxed())
        {
            emit logMessage("Latency check stopped by request, processed " +
                            QString::number(i) + " of " + QString::number(m_serverList.size()) + " servers");
            break;
        }

        const auto &server = m_serverList[i];
        quint32 serverId = server.first;
        quint32 ipAddr = server.second;

        // 实现重试机制：最多尝试5次，如果有3次成功则提前中断，取最低延时值
        int bestLatency = MAX_LATENCY;
        int attempts = 0;
        int successCount = 0;
        const int maxAttempts = 5;
        const int targetSuccessCount = 3;
        QList<int> successfulLatencies;

        for (attempts = 0; attempts < maxAttempts; ++attempts)
        {
            if (m_shouldStop.loadRelaxed())
            {
                break;
            }

            int latency = pingHost(ipAddr);

            if (latency >= 0 && latency < MAX_LATENCY)
            {
                // 成功获取延时
                successCount++;
                successfulLatencies.append(latency);

                // 更新最佳结果
                if (latency < bestLatency)
                {
                    bestLatency = latency;
                }

                // 如果已经有3次成功，提前中断
                if (successCount >= targetSuccessCount)
                {
                    break;
                }
            }

            // 如果不是最后一次尝试，稍微等待后重试
            if (attempts < maxAttempts - 1)
            {
                QThread::msleep(500); // 等待500ms后重试
            }
        }

        emit resultReady(serverId, ipAddr, bestLatency);

        // 只输出失败的结果（所有重试都失败）
        if (bestLatency >= MAX_LATENCY)
        {
            emit logMessage("Failed to ping Server ID " + QString::number(serverId) + ": " + QHostAddress(ipAddr).toString() + " (tried " + QString::number(attempts + 1) + " times)");
        }
    }

    emit finished();
}

int LatencyWorker::pingHost(quint32 ipAddr)
{
    if (m_shouldStop.loadRelaxed())
    {
        return -1;
    }

#ifdef Q_OS_WIN
    // 移除详细的平台检测日志
    HANDLE hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE)
    {
        emit logMessage("Failed to create ICMP file handle for " + QHostAddress(ipAddr).toString());
        return -1;
    }

    char sendData[32] = "LatCheck Ping Data";
    char replyBuffer[sizeof(ICMP_ECHO_REPLY) + 32];

    // 移除发送请求的日志
    DWORD result = IcmpSendEcho(hIcmpFile,
                                qToBigEndian(ipAddr),
                                sendData,
                                sizeof(sendData),
                                NULL,
                                replyBuffer,
                                sizeof(replyBuffer),
                                5000);

    int latency = MAX_LATENCY;
    if (result > 0)
    {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
        if (pEchoReply->Status == IP_SUCCESS)
        {
            latency = pEchoReply->RoundTripTime;
            // 移除成功的详细日志
        }
        else
        {
            latency = MAX_LATENCY; // 明确设置为10000
            emit logMessage("Received ICMP echo reply with error status from " + QHostAddress(ipAddr).toString() + ", status: " + QString::number(pEchoReply->Status));
        }
    }
    else
    {
        latency = MAX_LATENCY; // 明确设置为10000
        DWORD error = GetLastError();
        // emit logMessage("No ICMP echo reply received from " + QHostAddress(ipAddr).toString() + ", error: " + QString::number(error));
    }

    IcmpCloseHandle(hIcmpFile);
    return latency;
#else
    // 移除非Windows平台的详细日志
    QThread::msleep(100);
    return 50;
#endif
}

// LatencyChecker Implementation
// LatencyChecker构造函数修改
LatencyChecker::LatencyChecker(QObject *parent)
    : QObject(parent), m_running(false), m_progress(0), m_totalIps(0), m_finishedWorkers(0)
{
}

// 修复所有缺少的getter方法
bool LatencyChecker::running() const
{
    return m_running;
}

int LatencyChecker::progress() const
{
    return m_progress;
}

int LatencyChecker::totalIps() const
{
    return m_totalIps;
}

// 添加析构函数实现
LatencyChecker::~LatencyChecker()
{
    stopChecking();
}

// 添加完整的startChecking方法实现
void LatencyChecker::startChecking(const QVariantList &serverList, int threadCount)
{
    if (m_running)
    {
        emit logMessage("Latency check already running, ignoring start request");
        return;
    }

    qDebug() << "Start latency check with " << threadCount << " threads";
    emit logMessage("Initializing latency check with " + QString::number(threadCount) + " threads");
    cleanup();

    // 直接提取服务器ID和IP地址的整数值
    QList<QPair<quint32, quint32>> servers;
    for (const QVariant &item : serverList)
    {
        if (item.canConvert<QVariantMap>())
        {
            QVariantMap server = item.toMap();
            quint32 serverId = server["server_id"].toUInt();
            quint32 ipAddr = server["ip_address"].toUInt();

            QHostAddress addr(ipAddr);
            if (!addr.isNull() && addr.protocol() == QAbstractSocket::IPv4Protocol)
            {
                servers.append(qMakePair(serverId, ipAddr));
            }
            else
            {
                emit logMessage(QString("Warning: Invalid IP address value %1").arg(ipAddr));
            }
        }
    }

    if (servers.isEmpty())
    {
        emit logMessage("Error: No valid servers found in server list");
        return;
    }

    setTotalIps(servers.size());
    setProgress(0);
    setRunning(true);

    m_results.clear();
    m_successResults.clear(); // 清空成功结果记录
    m_failedResults.clear();  // 清空失败结果记录
    m_finishedWorkers = 0;

    // 分配服务器到各个线程
    int serversPerThread = servers.size() / threadCount;
    int remainingServers = servers.size() % threadCount;

    emit logMessage("Distributing " + QString::number(servers.size()) + " servers among " + QString::number(threadCount) + " threads");

    int startIndex = 0;
    for (int i = 0; i < threadCount && startIndex < servers.size(); ++i)
    {
        int endIndex = startIndex + serversPerThread;
        if (i < remainingServers)
        {
            endIndex++;
        }

        QList<QPair<quint32, quint32>> threadServers = servers.mid(startIndex, endIndex - startIndex);
        if (threadServers.isEmpty())
        {
            break;
        }

        QThread *thread = new QThread(this);
        LatencyWorker *worker = new LatencyWorker(threadServers);

        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &LatencyWorker::startChecking);
        connect(worker, &LatencyWorker::resultReady, this, &LatencyChecker::onWorkerResult);
        connect(worker, &LatencyWorker::finished, this, &LatencyChecker::onWorkerFinished);
        connect(worker, &LatencyWorker::finished, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(worker, &LatencyWorker::logMessage, this, &LatencyChecker::onWorkerLogMessage);

        m_threads.append(thread);
        m_workers.append(worker);

        thread->start();
        startIndex = endIndex;
    }
}

// 修复字符串拼接语法错误
void LatencyChecker::onWorkerFinished()
{
    // 将静态互斥锁改为局部变量
    QMutexLocker locker(&m_resultsMutex); // 使用现有的结果互斥锁保护所有共享数据

    m_finishedWorkers++;
    qDebug() << "Worker finished, total:" << m_finishedWorkers << "/" << m_workers.size();

    // 只有当所有worker都完成时，才进行后续处理
    if (m_finishedWorkers >= m_workers.size())
    {
        // 立即设置running为false，确保可以重新启动扫描
        setRunning(false);

        // 在释放互斥锁后执行lambda函数
        // 创建临时变量保存结果状态，避免在lambda中访问共享数据
        const int resultsCount = m_results.size();
        const int successCount = m_successResults.size();
        const int failedCount = m_failedResults.size();
        const auto finalResults = m_results;

        // 释放锁
        locker.unlock();

        // 使用QTimer::singleShot在主事件循环中发射信号，避免死锁
        QTimer::singleShot(0, this, [this, resultsCount, successCount, failedCount, finalResults]()
                           {
            QString logMsg = QString("Latency check complete: %1 servers processed, %2 successful, %3 failed")
                               .arg(resultsCount).arg(successCount).arg(failedCount);
            qDebug() << logMsg;
            emit logMessage(logMsg);
            emit checkingFinished(finalResults); });
    }
}

void LatencyChecker::onWorkerResult(quint32 serverId, quint32 ipAddr, int latency)
{
    QMutexLocker locker(&m_resultsMutex);

    QVariantMap result;
    result["server_id"] = serverId;
    result["ip_address"] = ipAddr;
    result["latency"] = latency;
    m_results.append(result);

    // 记录成功和失败的详细结果
    if (latency >= 0 && latency < MAX_LATENCY)
    {
        m_successResults.append(qMakePair(serverId, latency));
    }
    else
    {
        m_failedResults.append(serverId);
    }

    emit latencyResult(serverId, ipAddr, latency);

    setProgress(m_results.size());

    // 每处理100个结果输出一次进度信息
    if (m_results.size() % 100 == 0 || m_results.size() == m_totalIps)
    {
        emit logMessage("Progress: " + QString::number(m_results.size()) + "/" + QString::number(m_totalIps) +
                        " processed, " + QString::number(m_successResults.size()) + " successful");
    }
}

void LatencyChecker::setRunning(bool running)
{
    if (m_running != running)
    {
        m_running = running;
        emit runningChanged();
    }
}

void LatencyChecker::setProgress(int progress)
{
    if (m_progress != progress)
    {
        m_progress = progress;
        emit progressChanged();
    }
}

void LatencyChecker::setTotalIps(int total)
{
    if (m_totalIps != total)
    {
        m_totalIps = total;
        emit totalIpsChanged();
    }
}

void LatencyChecker::cleanup()
{
    // 停止所有工作线程
    stopChecking();

    // 清空所有结果
    m_results.clear();
    m_successResults.clear();
    m_failedResults.clear();

    // 重置计数器
    m_finishedWorkers = 0;
    setProgress(0);
    // setTotalIps(0);

    // 确保设置为非运行状态
    setRunning(false);

    m_threads.clear();
    m_workers.clear();
    m_finishedWorkers = 0;
}

void LatencyChecker::onWorkerLogMessage(const QString &message)
{
    // 只转发失败相关的日志消息
    if (message.contains("Failed") ||
        message.contains("Error") ||
        message.contains("Warning") ||
        message.contains("error status") ||
        message.contains("No ICMP echo reply") ||
        message.contains("Initializing") ||
        message.contains("Distributing") ||
        message.contains("Creating worker thread") ||
        message.contains("Starting latency check for"))
    {
        emit logMessage(message);
    }
}

void LatencyChecker::stopChecking()
{
    if (!m_running)
    {
        return;
    }

    // Signal all workers to stop
    for (LatencyWorker *worker : m_workers)
    {
        worker->stop();
    }

    // Wait for all threads to finish
    for (QThread *thread : m_threads)
    {
        if (thread->isRunning())
        {
            thread->quit();
            thread->wait(3000); // Wait up to 3 seconds
        }
    }

    cleanup();
    setRunning(false);
}
