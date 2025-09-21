#include "database/database_pool.h"
#include "logger/logger.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QUuid>

DatabasePool* DatabasePool::instance_ = nullptr;
QMutex DatabasePool::mutex_;

DatabasePool::DatabasePool(QObject *parent)
    : QObject(parent)
    , active_connections_(0)
    , connection_counter_(0)
    , initialized_(false)
    , min_connections_(5)
    , max_connections_(20)
    , connection_timeout_(30000)
    , idle_timeout_(300)
{
    health_check_timer_ = new QTimer(this);
    connect(health_check_timer_, &QTimer::timeout, this, &DatabasePool::checkConnections);
}

DatabasePool::~DatabasePool()
{
    close();
}

DatabasePool* DatabasePool::instance()
{
    if (instance_ == nullptr) {
        QMutexLocker locker(&mutex_);
        if (instance_ == nullptr) {
            instance_ = new DatabasePool();
        }
    }
    return instance_;
}

bool DatabasePool::initialize(const DatabaseConfig& config)
{
    QMutexLocker locker(&connection_mutex_);
    
    if (initialized_) {
        Logger::instance()->warning("Database pool already initialized", "DATABASE");
        return true;
    }
    
    config_ = config;
    min_connections_ = config.minConnections;
    max_connections_ = config.maxConnections;
    connection_timeout_ = config.connectionTimeout;
    idle_timeout_ = config.idleTimeout;
    
    // 创建初始连接
    for (int i = 0; i < min_connections_; ++i) {
        QSqlDatabase db = createConnection();
        if (!db.isValid()) {
            Logger::instance()->error(QString("Failed to create initial connection %1").arg(i), "DATABASE");
            return false;
        }
        idle_connections_.enqueue(db);
    }
    
    initialized_ = true;
    Logger::instance()->info(QString("Database pool initialized with %1 connections").arg(idle_connections_.size()), "DATABASE");
    return true;
}

QSqlDatabase DatabasePool::getConnection()
{
    QMutexLocker locker(&connection_mutex_);
    
    if (!initialized_) {
        Logger::instance()->error("Database pool not initialized", "DATABASE");
        return QSqlDatabase();
    }
    
    // 等待可用连接，最多等待connectionTimeout毫秒
    if (idle_connections_.isEmpty() && active_connections_ >= max_connections_) {
        bool hasConnection = connection_condition_.wait(&connection_mutex_, connection_timeout_);
        if (!hasConnection) {
            Logger::instance()->error("Connection timeout: no available connections", "DATABASE");
            return QSqlDatabase();
        }
    }
    
    QSqlDatabase db;
    
    if (!idle_connections_.isEmpty()) {
        // 从空闲连接池获取连接
        db = idle_connections_.dequeue();
        
        // 验证连接是否有效
        if (!isConnectionValid(db)) {
            Logger::instance()->warning("Invalid connection found, creating new one", "DATABASE");
            db = createConnection();
        }
    } else if (active_connections_ < max_connections_) {
        // 创建新连接
        db = createConnection();
    }
    
    if (db.isValid() && db.isOpen()) {
        active_connections_++;
        Logger::instance()->debug(QString("Connection acquired, active: %1, idle: %2")
                       .arg(active_connections_).arg(idle_connections_.size()), "DATABASE");
    }
    
    return db;
}

void DatabasePool::releaseConnection(QSqlDatabase& db)
{
    if (!db.isValid()) {
        return;
    }
    
    QMutexLocker locker(&connection_mutex_);
    
    if (active_connections_ > 0) {
        active_connections_--;
    }
    
    // 验证连接是否仍然有效
    if (isConnectionValid(db)) {
        // 如果空闲连接数超过最小连接数，关闭多余连接
        if (idle_connections_.size() >= min_connections_) {
            db.close();
            Logger::instance()->debug("Connection closed (exceeds minimum pool size)", "DATABASE");
        } else {
            Logger::instance()->debug(QString("Connection released, active: %1, idle: %2")
                           .arg(active_connections_).arg(idle_connections_.size()), "DATABASE");
        }
    } else {
        Logger::instance()->warning("Invalid connection closed", "DATABASE");
    }
    
    // 通知等待连接的线程
    connection_condition_.wakeOne();
}

void DatabasePool::close()
{
    QMutexLocker locker(&connection_mutex_);
    
    if (health_check_timer_) {
        health_check_timer_->stop();
    }
    
    // 关闭所有空闲连接
    while (!idle_connections_.isEmpty()) {
        QSqlDatabase db = idle_connections_.dequeue();
        if (db.isOpen()) {
            db.close();
        }
    }
    
    active_connections_ = 0;
    initialized_ = false;
    
    Logger::instance()->info("Database pool closed", "DATABASE");
}

bool DatabasePool::testConnection()
{
    QSqlDatabase db = getConnection();
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }
    
    QSqlQuery query(db);
    bool result = query.exec("SELECT 1");
    
    releaseConnection(db);
    return result;
}

void DatabasePool::checkConnections()
{
    QMutexLocker locker(&connection_mutex_);
    
    Logger::instance()->debug(QString("Health check: active=%1, idle=%2")
                   .arg(active_connections_).arg(idle_connections_.size()), "DATABASE");
    
    cleanupInvalidConnections();
    
    // 确保最小连接数
    while (idle_connections_.size() < min_connections_) {
        QSqlDatabase db = createConnection();
        if (db.isValid() && db.isOpen()) {
            idle_connections_.enqueue(db);
        } else {
            Logger::instance()->error("Failed to create connection during health check", "DATABASE");
            break;
        }
    }
}

QSqlDatabase DatabasePool::createConnection()
{
    QString connectionName = QString("connection_%1_%2")
                            .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())))
                            .arg(++connection_counter_);
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connectionName);
    db.setHostName(config_.host);
    db.setPort(config_.port);
    db.setDatabaseName(config_.database);
    db.setUserName(config_.username);
    db.setPassword(config_.password);
    
    // 设置连接选项
    QString options = QString("MYSQL_OPT_CHARSET=%1;MYSQL_OPT_RECONNECT=1")
                     .arg(config_.charset);
    
    if (config_.enableSSL) {
        options += ";MYSQL_OPT_SSL_MODE=SSL_MODE_REQUIRED";
        if (!config_.sslCert.isEmpty()) {
            options += QString(";MYSQL_OPT_SSL_CERT=%1").arg(config_.sslCert);
        }
        if (!config_.sslKey.isEmpty()) {
            options += QString(";MYSQL_OPT_SSL_KEY=%1").arg(config_.sslKey);
        }
        if (!config_.sslCA.isEmpty()) {
            options += QString(";MYSQL_OPT_SSL_CA=%1").arg(config_.sslCA);
        }
    }
    options="";
    db.setConnectOptions(options);
    
    if (!db.open()) {
        Logger::instance()->error(QString("Failed to open database connection: %1")
                       .arg(db.lastError().text()), "DATABASE");
        return QSqlDatabase();
    }
    
    Logger::instance()->debug(QString("Created new database connection: %1").arg(connectionName), "DATABASE");
    return db;
}

bool DatabasePool::isConnectionValid(const QSqlDatabase& db)
{
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }
    
    QSqlQuery query(db);
    return query.exec("SELECT 1");
}

void DatabasePool::cleanupInvalidConnections()
{
    QQueue<QSqlDatabase> validConnections;
    
    while (!idle_connections_.isEmpty()) {
        QSqlDatabase db = idle_connections_.dequeue();
        if (isConnectionValid(db)) {
            validConnections.enqueue(db);
        } else {
            db.close();
            Logger::instance()->warning("Removed invalid connection from pool", "DATABASE");
        }
    }
    
    idle_connections_ = validConnections;
}

// RAII连接管理器实现
DatabaseConnection::DatabaseConnection()
{
    db_ = DatabasePool::instance()->getConnection();
}

DatabaseConnection::~DatabaseConnection()
{
    if (db_.isValid()) {
        DatabasePool::instance()->releaseConnection(db_);
    }
}