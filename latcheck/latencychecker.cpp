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
    emit logMessage("Starting latency check for " + QString::number(m_serverList.size()) + " servers");

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
        
        // 实现重试机制：最多尝试3次，取最低延时值
        int bestLatency = -1;
        int attempts = 0;
        const int maxAttempts = 3;
        
        for (attempts = 0; attempts < maxAttempts; ++attempts)
        {
            if (m_shouldStop.loadRelaxed())
            {
                break;
            }
            
            int latency = pingHost(ipAddr);
            
            if (latency >= 0)
            {
                // 成功获取延时，更新最佳结果
                if (bestLatency < 0 || latency < bestLatency)
                {
                    bestLatency = latency;
                }
                // 如果第一次就成功，不需要继续重试
                break;
            }
            
            // 如果不是最后一次尝试，稍微等待后重试
            if (attempts < maxAttempts - 1)
            {
                QThread::msleep(500); // 等待500ms后重试
            }
        }
        
        emit resultReady(serverId, ipAddr, bestLatency);
        
        // 只输出失败的结果（所有重试都失败）
        if (bestLatency < 0) {
            emit logMessage("Failed to ping Server ID " + QString::number(serverId) + ": " + QHostAddress(ipAddr).toString() + " (tried " + QString::number(attempts) + " times)");
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

    int latency = -1;
    if (result != 0)
    {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
        if (pEchoReply->Status == IP_SUCCESS)
        {
            latency = pEchoReply->RoundTripTime;
            // 移除成功的详细日志
        }
        else
        {
            latency = -1;  // 明确设置为-1
            emit logMessage("Received ICMP echo reply with error status from " + QHostAddress(ipAddr).toString() + ", status: " + QString::number(pEchoReply->Status));
        }
    }
    else
    {
        latency = -1;  // 明确设置为-1
        DWORD error = GetLastError();
        emit logMessage("No ICMP echo reply received from " + QHostAddress(ipAddr).toString() + ", error: " + QString::number(error));
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
LatencyChecker::LatencyChecker(QObject *parent)
    : QObject(parent), m_running(false), m_progress(0), m_totalIps(0), m_finishedWorkers(0)
{
}

LatencyChecker::~LatencyChecker()
{
    stopChecking();
}

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

void LatencyChecker::startChecking(const QVariantList &serverList, int threadCount)
{
    if (m_running)
    {
        emit logMessage("Latency check already running, ignoring start request");
        return;
    }

    emit logMessage("Initializing latency check with " + QString::number(threadCount) + " threads");
    cleanup();

    // 直接提取服务器ID和IP地址的整数值
    QList<QPair<quint32, quint32>> servers;
    for (const QVariant &item : serverList)
    {
        if (item.canConvert<QVariantMap>()) {
            QVariantMap server = item.toMap();
            quint32 serverId = server["server_id"].toUInt();
            QString ipString = server["ip_address"].toString();
            
            QHostAddress addr(ipString);
            if (!addr.isNull() && addr.protocol() == QAbstractSocket::IPv4Protocol) {
                quint32 ipAddr = addr.toIPv4Address();
                servers.append(qMakePair(serverId, ipAddr));
            } else {
                emit logMessage("Warning: Invalid IP address: " + ipString);
            }
        }
    }

    if (servers.isEmpty()) {
        emit logMessage("Error: No valid servers found in server list");
        return;
    }

    setTotalIps(servers.size());
    setProgress(0);
    setRunning(true);

    m_results.clear();
    m_successResults.clear();  // 清空成功结果记录
    m_failedResults.clear();   // 清空失败结果记录
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

        emit logMessage("Creating worker thread " + QString::number(i + 1) + " with " + QString::number(threadServers.size()) + " servers");

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

// 新添加的槽函数，用于处理Worker的日志消息
void LatencyChecker::onWorkerLogMessage(const QString &message)
{
    // 只转发失败相关的日志消息，过滤掉成功的详细日志，并去掉[LatencyChecker]前缀
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

void LatencyChecker::onWorkerResult(quint32 serverId, quint32 ipAddr, int latency)
{
    QMutexLocker locker(&m_resultsMutex);
    
    QVariantMap result;
    result["server_id"] = serverId;
    result["ip_address"] = QHostAddress(ipAddr).toString();
    result["latency"] = latency;
    m_results.append(result);
    
    // 记录成功和失败的详细结果
    if (latency >= 0) {
        m_successResults.append(qMakePair(serverId, latency));
    } else {
        m_failedResults.append(serverId);
    }
    
    emit latencyResult(serverId, ipAddr, latency);
    
    setProgress(m_results.size());
    
    // 每处理100个结果输出一次进度信息
    if (m_results.size() % 100 == 0) {
        emit logMessage("Progress: " + QString::number(m_results.size()) + "/" + QString::number(m_totalIps) + 
                       " processed, " + QString::number(m_successResults.size()) + " successful");
    }
}

void LatencyChecker::onWorkerFinished()
{
    m_finishedWorkers++;

    // 添加额外的检查，确保所有ping操作真正完成
    qDebug() << "Worker finished, total:" << m_finishedWorkers << "/" << m_workers.size();

    // 只有当所有worker都完成且没有活跃的ping操作时，才将running设置为false
    if (m_finishedWorkers >= m_workers.size())
    {
        // 确保结果计数与总IP数匹配
        if (m_results.size() >= m_totalIps)
        {
            qDebug() << "All ping operations completed, setting running to false";
            
            // 输出详细的成功和失败统计
            QString successList;
            for (const auto &success : m_successResults) {
                if (!successList.isEmpty()) successList += ", ";
                successList += "id" + QString::number(success.first) + "-" + QString::number(success.second) + "ms";
            }
            
            QString failedList;
            for (quint32 failedId : m_failedResults) {
                if (!failedList.isEmpty()) failedList += ", ";
                failedList += "id" + QString::number(failedId);
            }
            
            emit logMessage("Latency check completed: " + QString::number(m_results.size()) + " total");
            if (!successList.isEmpty()) {
                emit logMessage("成功检测：" + successList);
            }
            if (!failedList.isEmpty()) {
                emit logMessage("检测失败：" + failedList);
            }
            
            setRunning(false);
            emit checkingFinished(m_results);
        }
        else
        {
            qDebug() << "Warning: Not all ping results received yet. Results:" << m_results.size() << "/" << m_totalIps;
            // 添加一个小延迟，给剩余结果时间到达
            QTimer::singleShot(500, this, [this]()
                               {
                // 输出详细的成功和失败统计
                QString successList;
                for (const auto &success : m_successResults) {
                    if (!successList.isEmpty()) successList += ", ";
                    successList += "id" + QString::number(success.first) + "-" + QString::number(success.second) + "ms";
                }
                
                QString failedList;
                for (quint32 failedId : m_failedResults) {
                    if (!failedList.isEmpty()) failedList += ", ";
                    failedList += "id" + QString::number(failedId);
                }
                
                emit logMessage("Latency check completed: " + QString::number(m_results.size()) + " total");
                if (!successList.isEmpty()) {
                    emit logMessage("成功检测：" + successList);
                }
                if (!failedList.isEmpty()) {
                    emit logMessage("检测失败：" + failedList);
                }
                
                setRunning(false);
                emit checkingFinished(m_results); });
        }
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
    m_threads.clear();
    m_workers.clear();
    m_finishedWorkers = 0;
}



