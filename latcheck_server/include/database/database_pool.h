#ifndef DATABASEPOOL_H
#define DATABASEPOOL_H

#include <QObject>
#include <QSqlDatabase>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QThread>
#include "common/types.h"
#include "logger/logger.h"



class DatabasePool : public QObject
{
    Q_OBJECT

public:
    static DatabasePool* instance();
    static void destroyInstance();
    
    // 初始化连接池
    bool initialize(const DatabaseConfig& config);
    
    // 获取数据库连接
    QSqlDatabase getConnection();
    
    // 释放数据库连接
    void releaseConnection(QSqlDatabase& db);
    
    // 关闭连接池
    void close();
    
    // 获取连接池状态
    int getActiveConnections() const { return active_connections_; }
    int getIdleConnections() const { return idle_connections_.size(); }
    int getTotalConnections() const { return active_connections_ + idle_connections_.size(); }
    
    // 测试数据库连接
    bool testConnection();
    
private slots:
    void checkConnections();
    
private:
    explicit DatabasePool(QObject *parent = nullptr);
    ~DatabasePool();
    
    friend class std::default_delete<DatabasePool>;
    // 创建新连接
    QSqlDatabase createConnection();
    
    // 验证连接是否有效
    bool isConnectionValid(const QSqlDatabase& db);
    
    // 清理无效连接
    void cleanupInvalidConnections();
    
    static DatabasePool* instance_;
    static QMutex mutex_;
    
    DatabaseConfig config_;
    QQueue<QSqlDatabase> idle_connections_;
    QMutex connection_mutex_;
    QWaitCondition connection_condition_;
    
    int active_connections_;
    int connection_counter_;
    bool initialized_;
    
    QTimer* health_check_timer_;
    
    // 连接池配置
    int min_connections_;
    int max_connections_;
    int connection_timeout_; // 获取连接超时时间（毫秒）
    int idle_timeout_;       // 空闲连接超时时间（秒）
};

// RAII连接管理器
class DatabaseConnection
{
public:
    DatabaseConnection();
    ~DatabaseConnection();
    
    QSqlDatabase& database() { return db_; }
    bool isValid() const { return db_.isValid() && db_.isOpen(); }
    
private:
    QSqlDatabase db_;
};

#endif // DATABASEPOOL_H