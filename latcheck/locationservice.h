#ifndef LOCATIONSERVICE_H
#define LOCATIONSERVICE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class Logger;

class LocationService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool locationEnabled READ locationEnabled NOTIFY locationEnabledChanged)
    Q_PROPERTY(QString currentLocation READ currentLocation NOTIFY currentLocationChanged)
    Q_PROPERTY(bool isUpdating READ isUpdating NOTIFY isUpdatingChanged)

public:
    explicit LocationService(QObject *parent = nullptr);
    ~LocationService();

    bool locationEnabled() const;
    QString currentLocation() const;
    bool isUpdating() const;

    Q_INVOKABLE void startLocationUpdate();
    Q_INVOKABLE void stopLocationUpdate();
    
    void setLogger(Logger *logger);

signals:
    void locationEnabledChanged();
    void currentLocationChanged(const QString &location);  // 添加参数
    void isUpdatingChanged();
    void locationUpdateFailed(const QString &error);

private slots:
    void onIpLocationFinished();

private:
    QNetworkAccessManager *m_networkManager;
    bool m_locationEnabled;
    QString m_currentLocation;
    bool m_isUpdating;
    QTimer *m_timeoutTimer;
    Logger *m_logger;

    void setLocationEnabled(bool enabled);
    void setCurrentLocation(const QString &location);
    void setIsUpdating(bool updating);
};

#endif // LOCATIONSERVICE_H
