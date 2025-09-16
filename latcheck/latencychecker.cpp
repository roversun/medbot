#include "latencychecker.h"
#include <QDebug>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

// LatencyWorker Implementation
LatencyWorker::LatencyWorker(const QStringList &ipList, QObject *parent)
    : QObject(parent)
    , m_ipList(ipList)
    , m_shouldStop(0)
{
}

void LatencyWorker::stop()
{
    m_shouldStop.storeRelaxed(1);
}

void LatencyWorker::startChecking()
{
    for (const QString &ip : m_ipList) {
        if (m_shouldStop.loadRelaxed()) {
            break;
        }
        
        int latency = pingHost(ip);
        emit resultReady(ip, latency);
    }
    
    emit finished();
}

int LatencyWorker::pingHost(const QString &host)
{
    if (m_shouldStop.loadRelaxed()) {
        return -1;
    }

#ifdef Q_OS_WIN
    HANDLE hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    QHostAddress addr(host);
    if (addr.isNull()) {
        IcmpCloseHandle(hIcmpFile);
        return -1;
    }

    char sendData[32] = "LatCheck Ping Data";
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData);
    LPVOID replyBuffer = malloc(replySize);

    if (!replyBuffer) {
        IcmpCloseHandle(hIcmpFile);
        return -1;
    }

    DWORD result = IcmpSendEcho(hIcmpFile,
                               addr.toIPv4Address(),
                               sendData,
                               sizeof(sendData),
                               NULL,
                               replyBuffer,
                               replySize,
                               5000); // 5 second timeout

    int latency = -1;
    if (result != 0) {
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
        if (pEchoReply->Status == IP_SUCCESS) {
            latency = pEchoReply->RoundTripTime;
        }
    }

    free(replyBuffer);
    IcmpCloseHandle(hIcmpFile);
    return latency;
#else
    // For non-Windows platforms, you would implement ping using raw sockets
    // or system ping command. For now, return a dummy value.
    QThread::msleep(100); // Simulate ping time
    return 50; // Dummy latency
#endif
}

// LatencyChecker Implementation
LatencyChecker::LatencyChecker(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_progress(0)
    , m_totalIps(0)
    , m_finishedWorkers(0)
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

void LatencyChecker::startChecking(const QVariantList &ipList, int threadCount)
{
    if (m_running) {
        return;
    }

    cleanup();
    
    QStringList ips;
    for (const QVariant &ip : ipList) {
        ips.append(ip.toString());
    }

    setTotalIps(ips.size());
    setProgress(0);
    setRunning(true);
    
    m_results.clear();
    m_finishedWorkers = 0;

    // Distribute IPs among threads
    int ipsPerThread = ips.size() / threadCount;
    int remainingIps = ips.size() % threadCount;

    int startIndex = 0;
    for (int i = 0; i < threadCount && startIndex < ips.size(); ++i) {
        int endIndex = startIndex + ipsPerThread;
        if (i < remainingIps) {
            endIndex++;
        }

        QStringList threadIps = ips.mid(startIndex, endIndex - startIndex);
        if (threadIps.isEmpty()) {
            break;
        }

        QThread *thread = new QThread(this);
        LatencyWorker *worker = new LatencyWorker(threadIps);
        
        worker->moveToThread(thread);
        
        connect(thread, &QThread::started, worker, &LatencyWorker::startChecking);
        connect(worker, &LatencyWorker::resultReady, this, &LatencyChecker::onWorkerResult);
        connect(worker, &LatencyWorker::finished, this, &LatencyChecker::onWorkerFinished);
        connect(worker, &LatencyWorker::finished, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        
        m_threads.append(thread);
        m_workers.append(worker);
        
        thread->start();
        startIndex = endIndex;
    }
}

void LatencyChecker::stopChecking()
{
    if (!m_running) {
        return;
    }

    // Signal all workers to stop
    for (LatencyWorker *worker : m_workers) {
        worker->stop();
    }

    // Wait for all threads to finish
    for (QThread *thread : m_threads) {
        if (thread->isRunning()) {
            thread->quit();
            thread->wait(3000); // Wait up to 3 seconds
        }
    }

    cleanup();
    setRunning(false);
}

void LatencyChecker::onWorkerResult(const QString &ip, int latency)
{
    QMutexLocker locker(&m_resultsMutex);
    
    QVariantMap result;
    result["ip"] = ip;
    result["latency"] = latency;
    m_results.append(result);
    
    setProgress(m_results.size());
    emit latencyResult(ip, latency);
}

void LatencyChecker::onWorkerFinished()
{
    m_finishedWorkers++;
    
    if (m_finishedWorkers >= m_workers.size()) {
        setRunning(false);
        emit checkingFinished(m_results);
    }
}

void LatencyChecker::setRunning(bool running)
{
    if (m_running != running) {
        m_running = running;
        emit runningChanged();
    }
}

void LatencyChecker::setProgress(int progress)
{
    if (m_progress != progress) {
        m_progress = progress;
        emit progressChanged();
    }
}

void LatencyChecker::setTotalIps(int total)
{
    if (m_totalIps != total) {
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