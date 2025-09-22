#ifndef LATENCYCHECKER_H
#define LATENCYCHECKER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QHostAddress>
#include <QAtomicInt>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class LatencyWorker : public QObject
{
    Q_OBJECT

public:
    explicit LatencyWorker(const QList<QPair<quint32, quint32>> &serverList, QObject *parent = nullptr);
    void stop();

public slots:
    void startChecking();

signals:
    void resultReady(quint32 serverId, quint32 ipAddr, int latency);
    void finished();
    void logMessage(const QString &message);

private:
    QList<QPair<quint32, quint32>> m_serverList;
    QAtomicInt m_shouldStop;

    int pingHost(quint32 ipAddr);
};

class LatencyChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int totalIps READ totalIps NOTIFY totalIpsChanged)

public:
    explicit LatencyChecker(QObject *parent = nullptr);
    ~LatencyChecker();

    bool running() const;
    int progress() const;
    int totalIps() const;

    Q_INVOKABLE void startChecking(const QVariantList &serverList, int threadCount = 4);
    Q_INVOKABLE void stopChecking();

private slots:
    void onWorkerResult(quint32 serverId, quint32 ipAddr, int latency);
    void onWorkerFinished();
    void onWorkerLogMessage(const QString &message);

private:
    void setRunning(bool running);
    void setProgress(int progress);
    void setTotalIps(int total);
    void cleanup();

    bool m_running;
    int m_progress;
    int m_totalIps;
    int m_finishedWorkers;
    QList<QThread*> m_threads;
    QList<LatencyWorker*> m_workers;
    QVariantList m_results;
    QMutex m_resultsMutex;
    
    // 添加成功和失败结果的详细记录
    QList<QPair<quint32, int>> m_successResults;  // serverId, latency
    QList<quint32> m_failedResults;  // serverId

signals:
    void runningChanged();
    void progressChanged();
    void totalIpsChanged();
    void latencyResult(quint32 serverId, quint32 ipAddr, int latency);
    void checkingFinished(const QVariantList &results);
    void logMessage(const QString &message);
};

#endif // LATENCYCHECKER_H