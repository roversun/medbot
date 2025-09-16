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
    explicit LatencyWorker(const QStringList &ipList, QObject *parent = nullptr);
    void stop();

public slots:
    void startChecking();

signals:
    void resultReady(const QString &ip, int latency);
    void finished();

private:
    QStringList m_ipList;
    QAtomicInt m_shouldStop;
    
    int pingHost(const QString &host);
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

    Q_INVOKABLE void startChecking(const QVariantList &ipList, int threadCount);
    Q_INVOKABLE void stopChecking();

signals:
    void runningChanged();
    void progressChanged();
    void totalIpsChanged();
    void latencyResult(const QString &ip, int latency);
    void checkingFinished(const QVariantList &results);

private slots:
    void onWorkerResult(const QString &ip, int latency);
    void onWorkerFinished();

private:
    bool m_running;
    int m_progress;
    int m_totalIps;
    int m_finishedWorkers;
    
    QList<QThread*> m_threads;
    QList<LatencyWorker*> m_workers;
    QVariantList m_results;
    QMutex m_resultsMutex;
    
    void setRunning(bool running);
    void setProgress(int progress);
    void setTotalIps(int total);
    void cleanup();
};

#endif // LATENCYCHECKER_H