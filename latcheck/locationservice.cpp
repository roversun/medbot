#include "locationservice.h"
#include "logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

LocationService::LocationService(QObject *parent)
    : QObject(parent)
    , m_positionSource(nullptr)
    , m_networkManager(nullptr)
    , m_locationEnabled(true)
    , m_currentLocation("Unknown")
    , m_isUpdating(false)
    , m_timeoutTimer(nullptr)
    , m_logger(nullptr)
{
    // 不再使用GPS定位，改用网络定位
    // m_positionSource = QGeoPositionInfoSource::createDefaultSource(this);
    
    // 初始化网络管理器用于IP地理位置查询
    m_networkManager = new QNetworkAccessManager(this);
    
    // 超时定时器
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(10000); // 10秒超时
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        setIsUpdating(false);
        if (m_logger) {
            m_logger->logMessage("Location detection timeout, please check network connection");
        }
        emit locationUpdateFailed("Location update timeout");
    });
}

LocationService::~LocationService()
{
    stopLocationUpdate();
}

bool LocationService::locationEnabled() const
{
    return m_locationEnabled;
}

QString LocationService::currentLocation() const
{
    return m_currentLocation;
}

bool LocationService::isUpdating() const
{
    return m_isUpdating;
}

void LocationService::startLocationUpdate()
{
    if (m_logger) {
        m_logger->logMessage("Getting location information via IP address...");
    }
    setIsUpdating(true);
    
    // 使用免费的IP地理位置API
    QUrl url("http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon,query");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LatCheck/1.0");
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &LocationService::onIpLocationFinished);
    
    m_timeoutTimer->start();
}

void LocationService::stopLocationUpdate()
{
    if (m_timeoutTimer->isActive()) {
        m_timeoutTimer->stop();
    }
    
    setIsUpdating(false);
}

void LocationService::onIpLocationFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    
    reply->deleteLater();
    m_timeoutTimer->stop();
    setIsUpdating(false);
    
    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = "Network error: " + reply->errorString();
        if (m_logger) {
            m_logger->logMessage(errorMsg);
        }
        emit locationUpdateFailed(errorMsg);
        return;
    }
    
    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        QString errorMsg = "JSON parse error: " + parseError.errorString();
        if (m_logger) {
            m_logger->logMessage(errorMsg);
        }
        emit locationUpdateFailed(errorMsg);
        return;
    }
    
    QJsonObject obj = doc.object();
    QString status = obj["status"].toString();
    
    if (status != "success") {
        QString errorMsg = "Location API error: " + obj["message"].toString();
        if (m_logger) {
            m_logger->logMessage(errorMsg);
        }
        emit locationUpdateFailed(errorMsg);
        return;
    }
    
    // 解析位置信息
    QString country = obj["country"].toString();
    QString region = obj["regionName"].toString();
    QString city = obj["city"].toString();
    double lat = obj["lat"].toDouble();
    double lon = obj["lon"].toDouble();
    QString ip = obj["query"].toString();
    
    QString location = QString("%1, %2, %3").arg(city, region, country);
    setCurrentLocation(location);
    
    if (m_logger) {
        m_logger->logMessage(QString("IP-based location successful: %1").arg(location));
        m_logger->logMessage(QString("Coordinates: %1, %2 (IP: %3)")
                            .arg(lat, 0, 'f', 6)
                            .arg(lon, 0, 'f', 6)
                            .arg(ip));
    }
}

void LocationService::setLogger(Logger *logger)
{
    m_logger = logger;
}

void LocationService::onReverseGeocodeFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    
    reply->deleteLater();
    setIsUpdating(false);
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        
        // 解析OpenStreetMap Nominatim API响应
        if (obj.contains("display_name")) {
            QString location = obj["display_name"].toString();
            // 简化地址显示
            QStringList parts = location.split(", ");
            if (parts.size() >= 3) {
                location = parts.mid(0, 3).join(", ");
            }
            setCurrentLocation(location);
        } else {
            setCurrentLocation("Location detected but address unavailable");
        }
    } else {
        emit locationUpdateFailed("Failed to get address: " + reply->errorString());
    }
}

void LocationService::setLocationEnabled(bool enabled)
{
    if (m_locationEnabled != enabled) {
        m_locationEnabled = enabled;
        emit locationEnabledChanged();
    }
}

void LocationService::setCurrentLocation(const QString &location)
{
    if (m_currentLocation != location) {
        m_currentLocation = location;
        emit currentLocationChanged(location);  // 传递location参数
    }
}

void LocationService::setIsUpdating(bool updating)
{
    if (m_isUpdating != updating) {
        m_isUpdating = updating;
        emit isUpdatingChanged();
    }
}

void LocationService::reverseGeocode(const QGeoCoordinate &coordinate)
{
    // 使用OpenStreetMap Nominatim API进行反向地理编码
    QString url = QString("https://nominatim.openstreetmap.org/reverse?format=json&lat=%1&lon=%2&zoom=10&addressdetails=1")
                     .arg(coordinate.latitude())
                     .arg(coordinate.longitude());
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LatCheck/1.0");
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &LocationService::onReverseGeocodeFinished);
}
