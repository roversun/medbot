#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QCryptographicHash>
#include <QRandomGenerator>

class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverIp READ serverIp WRITE setServerIp NOTIFY serverIpChanged)
    Q_PROPERTY(int serverPort READ serverPort WRITE setServerPort NOTIFY serverPortChanged)
    Q_PROPERTY(int threadCount READ threadCount WRITE setThreadCount NOTIFY threadCountChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY locationChanged)
    Q_PROPERTY(bool autoLocation READ autoLocation WRITE setAutoLocation NOTIFY autoLocationChanged)
    Q_PROPERTY(QString clientCertPath READ clientCertPath WRITE setClientCertPath NOTIFY clientCertPathChanged)
    Q_PROPERTY(QString clientKeyPath READ clientKeyPath WRITE setClientKeyPath NOTIFY clientKeyPathChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    QString serverIp() const;
    void setServerIp(const QString &ip);

    int serverPort() const;
    void setServerPort(int port);

    int threadCount() const;
    void setThreadCount(int count);

    QString username() const;
    void setUsername(const QString &username);

    QString location() const;
    void setLocation(const QString &location);

    bool autoLocation() const;
    void setAutoLocation(bool enabled);

    QString clientCertPath() const;
    void setClientCertPath(const QString &path);

    QString clientKeyPath() const;
    void setClientKeyPath(const QString &path);

    Q_INVOKABLE bool setPassword(const QString &password);
    Q_INVOKABLE bool verifyPassword(const QString &password);
    Q_INVOKABLE void saveConfig();
    Q_INVOKABLE void loadConfig();

signals:
    void serverIpChanged();
    void serverPortChanged();
    void threadCountChanged();
    void usernameChanged();
    void locationChanged();
    void autoLocationChanged();
    void clientCertPathChanged();
    void clientKeyPathChanged();

private:
    QSettings *m_settings;
    QString m_serverIp;
    int m_serverPort;
    int m_threadCount;
    QString m_username;
    QString m_location;
    bool m_autoLocation;
    QString m_clientCertPath;
    QString m_clientKeyPath;
    QString m_passwordHash;
    QString m_salt;

    QString generateSalt();
    QString hashPassword(const QString &password, const QString &salt);
};

#endif // CONFIGMANAGER_H