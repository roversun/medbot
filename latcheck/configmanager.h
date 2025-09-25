#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QSslKey>
#include <QSslCertificate>
#include <QNetworkInterface>
#include <QSettings>
#include <QCryptographicHash>
#include <QBuffer>
#include <windows.h>
#include <wincrypt.h>
#include <QByteArray>
#include <QString>

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
    Q_PROPERTY(bool ignoreSslErrors READ ignoreSslErrors WRITE setIgnoreSslErrors NOTIFY ignoreSslErrorsChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    // Getters
    QString serverIp() const;
    int serverPort() const;
    int threadCount() const;
    QString username() const;
    QString location() const;
    bool autoLocation() const;
    QString clientCertPath() const;
    QString clientKeyPath() const;
    bool ignoreSslErrors() const;

    // 添加获取证书subject名称的方法
    Q_INVOKABLE QString getCertificateSubjectName() const;

    // 添加证书相关方法到公有部分
    QSslKey getPrivateKey() const;
    QSslCertificate getPublicCert() const;
    QSslCertificate getCACertificate() const;

    // Setters
    void setServerIp(const QString &ip);
    void setServerPort(int port);
    void setThreadCount(int count);
    void setUsername(const QString &username);
    void setLocation(const QString &location);
    void setAutoLocation(bool enabled);
    void setClientCertPath(const QString &path);
    void setClientKeyPath(const QString &path);
    void setIgnoreSslErrors(bool ignore);

    Q_INVOKABLE bool setPassword(const QString &password);
    Q_INVOKABLE bool verifyPassword(const QString &password);

    Q_INVOKABLE bool saveConfig();
    void loadConfig();

signals:
    void serverIpChanged();
    void serverPortChanged();
    void threadCountChanged();
    void usernameChanged();
    void locationChanged();
    void autoLocationChanged();
    void clientCertPathChanged();
    void clientKeyPathChanged();
    void ignoreSslErrorsChanged();

private:
    QString m_configFilePath;
    QString getConfigFilePath() const;
    QString getConfigDirPath() const;
    bool ensureConfigDirExists() const;

    QJsonObject toJsonObject() const;
    void fromJsonObject(const QJsonObject &json);

    QString generateSalt();
    QString hashPassword(const QString &password, const QString &salt);

    // RSA加密相关方法
    QByteArray generateIVFromMachineID();
    QByteArray encryptWithCryptoAPI(const QByteArray &data, const QByteArray &key);
    QByteArray decryptWithCryptoAPI(const QByteArray &encryptedData, const QByteArray &key);

    QString encryptPassword(const QString &password);
    QString decryptPassword(const QString &encryptedPassword);
    QString getMachineFingerprint(); // 机器指纹方法

    // 为每个证书添加独立的懒加载成员变量
    mutable QSslKey m_privateKey;
    mutable QSslCertificate m_publicCert;
    mutable QSslCertificate m_caCertificate;
    mutable bool m_privateKeyLoaded = false;
    mutable bool m_publicCertLoaded = false;
    mutable bool m_caCertificateLoaded = false;

    QString m_serverIp;
    int m_serverPort;
    int m_threadCount;
    QString m_username;
    QString m_passwordHash;
    QString m_salt;
    QString m_location;
    bool m_autoLocation;
    QString m_clientCertPath;
    QString m_clientKeyPath;
    bool m_ignoreSslErrors;
};

#endif // CONFIGMANAGER_H