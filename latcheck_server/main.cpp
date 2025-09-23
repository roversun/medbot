#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <memory>

// 包含所有模块头文件
#include "config/config_manager.h"
#include "logger/logger.h"
#include "database/database_pool.h"
#include "database/user_dao.h"
#include "database/report_dao.h"
#include "database/server_dao.h" // 添加ServerDAO头文件
#include "auth/auth_manager.h"
#include "server/tls_server.h"
// 移除REST API头文件引用
// #include "api/rest_api_server.h"

// 在 LatCheckServer 类的 private 部分添加方法声明
class LatCheckServer : public QObject
{
    Q_OBJECT

public:
    explicit LatCheckServer(QObject *parent = nullptr)
        : QObject(parent), is_running_(false)
    {
        // 设置信号处理
        setupSignalHandlers();

        // 初始化清理定时器
        cleanup_timer_ = new QTimer(this);
        connect(cleanup_timer_, &QTimer::timeout, this, &LatCheckServer::performCleanup);
        cleanup_timer_->start(300000); // 每5分钟执行一次清理
    }

    ~LatCheckServer()
    {
        shutdown();
    }

    // 在initialize方法中，在ServerDAO初始化后调用新方法
    bool initialize()
    {
        try
        {
            // 1. 初始化配置管理器
            config_ = ConfigManager::instance();
            if (!config_->loadConfig("config/config.json"))
            {
                Logger::instance()->error("Failed to load configuration");
                return false;
            }

            // 2. 初始化日志系统
            LogConfig logConfig = config_->getLogConfig();
            if (!Logger::instance()->initialize(logConfig))
            {
                return false;
            }

            // 3. 初始化数据库连接池
            db_pool_ = DatabasePool::instance();
            DatabaseConfig dbConfig = config_->getDatabaseConfig();

            if (!db_pool_->initialize(dbConfig))
            {
                Logger::instance()->error("Failed to initialize database pool");
                return false;
            }

            Logger::instance()->info("Database pool initialized successfully");

            // 4. 初始化数据访问层
            user_dao_ = std::make_shared<UserDAO>();
            report_dao_ = std::make_shared<ReportDAO>();
            server_dao_ = std::make_shared<ServerDAO>(); // 添加ServerDAO初始化

            // 解析ip_result.txt文件并更新服务器信息
            parseIpResultFile();

            // 5. 初始化认证管理器
            logger_ = Logger::instance();
            auth_manager_ = std::make_shared<AuthManager>(user_dao_, std::shared_ptr<Logger>(logger_, [](Logger *) {}));

            // 移除API服务器初始化
            // api_server_ = std::make_shared<RestApiServer>(auth_manager_, user_dao_, report_dao_, config_);

            // 6. 初始化TLS服务器
            tls_server_ = std::make_shared<TlsServer>();
            // 修改第94行为：
            tls_server_->setConfigManager(QSharedPointer<ConfigManager>(config_, [](ConfigManager *) {}));

            tls_server_->setUserDAO(QSharedPointer<UserDAO>(user_dao_.get()));
            tls_server_->setReportDAO(QSharedPointer<ReportDAO>(report_dao_.get()));
            tls_server_->setServerDAO(QSharedPointer<ServerDAO>(server_dao_.get()));
            tls_server_->setAuthManager(QSharedPointer<AuthManager>(auth_manager_.get()));

            Logger::instance()->info("All components initialized successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            Logger::instance()->error(QString("Exception during initialization: %1").arg(e.what()));
            return false;
        }
        catch (...)
        {
            Logger::instance()->error("Unknown exception during initialization");
            return false;
        }
    }

    bool start()
    {
        if (is_running_)
        {
            Logger::instance()->warning("LatCheckServer is already running");
            return true;
        }

        try
        {
            // 移除API服务器启动代码
            // if (!api_server_->start()) {
            //     Logger::instance()->error("Failed to start REST API server");
            //     return false;
            // }

            // 启动TLS服务器
            ServerConfig serverConfig = config_->getServerConfig();
            if (!tls_server_->startServer(serverConfig.host, serverConfig.port))
            {
                Logger::instance()->error("Failed to start TLS server");
                return false;
            }

            is_running_ = true;
            Logger::instance()->info("LatCheckServer started successfully");

            // 输出服务器信息
            printServerInfo();

            return true;
        }
        catch (const std::exception &e)
        {
            Logger::instance()->error(QString("Exception during server startup: %1").arg(e.what()));
            return false;
        }
        catch (...)
        {
            Logger::instance()->error("Unknown exception during server startup");
            return false;
        }
    }

    void shutdown()
    {
        if (!is_running_)
        {
            return;
        }

        Logger::instance()->info("LatCheckServer shutting down...");

        // 停止清理定时器
        if (cleanup_timer_)
        {
            cleanup_timer_->stop();
        }

        // 移除API服务器停止代码
        // if (api_server_) {
        //     api_server_->stop();
        // }

        if (tls_server_)
        {
            tls_server_->stopServer();
        }

        // 清理资源
        if (auth_manager_)
        {
            auth_manager_->cleanupExpiredSessions();
        }

        if (db_pool_)
        {
            db_pool_->close();
        }

        is_running_ = false;
        Logger::instance()->info("LatCheckServer shutdown completed");
    }

    // 在LatCheckServer类的private部分添加新方法声明
private:
    ConfigManager *config_;
    Logger *logger_;
    DatabasePool *db_pool_;
    std::shared_ptr<UserDAO> user_dao_;
    std::shared_ptr<ReportDAO> report_dao_;
    std::shared_ptr<ServerDAO> server_dao_; // 添加ServerDAO成员变量
    std::shared_ptr<AuthManager> auth_manager_;
    std::shared_ptr<TlsServer> tls_server_;

    bool is_running_;
    QTimer *cleanup_timer_;

    // 添加信号处理相关成员
    static int sigintFd[2];
    static int sigtermFd[2];
    QSocketNotifier *snInt;
    QSocketNotifier *snTerm;

    void setupSignalHandlers()
    {
        // 创建socket pair用于信号处理
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigintFd))
            qFatal("Couldn't create SIGINT socketpair");
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigtermFd))
            qFatal("Couldn't create SIGTERM socketpair");

        snInt = new QSocketNotifier(sigintFd[1], QSocketNotifier::Read, this);
        connect(snInt, &QSocketNotifier::activated, this, &LatCheckServer::handleSigInt);
        snTerm = new QSocketNotifier(sigtermFd[1], QSocketNotifier::Read, this);
        connect(snTerm, &QSocketNotifier::activated, this, &LatCheckServer::handleSigTerm);

        // 设置信号处理器
        struct sigaction sigint, sigterm;

        sigint.sa_handler = LatCheckServer::intSignalHandler;
        sigemptyset(&sigint.sa_mask);
        sigint.sa_flags = SA_RESTART;

        sigterm.sa_handler = LatCheckServer::termSignalHandler;
        sigemptyset(&sigterm.sa_mask);
        sigterm.sa_flags = SA_RESTART;

        if (sigaction(SIGINT, &sigint, 0))
            qFatal("Couldn't install SIGINT handler");
        if (sigaction(SIGTERM, &sigterm, 0))
            qFatal("Couldn't install SIGTERM handler");
    }

    // 静态信号处理函数
    static void intSignalHandler(int)
    {
        char a = 1;
        // 使用变量接收返回值并标记为unused
        [[maybe_unused]] ssize_t result = ::write(sigintFd[0], &a, sizeof(a));
    }

    static void termSignalHandler(int)
    {
        char a = 1;
        // 使用变量接收返回值并标记为unused
        [[maybe_unused]] ssize_t result = ::write(sigtermFd[0], &a, sizeof(a));
    }

    // 在LatCheckServer类的private方法部分添加新方法的实现
    // 修改 parseIpResultFile() 方法
    void parseIpResultFile()
    {
        QString filePath = QString("config/ip_result.txt");
        QFile file(filePath);

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            Logger::instance()->error(QString("Failed to open IP result file: %1").arg(file.fileName()));
            return;
        }

        // 手动获取一个数据库连接
        QSqlDatabase db = DatabasePool::instance()->getConnection();
        if (!db.isValid() || !db.isOpen())
        {
            Logger::instance()->error("Failed to get database connection for server insertion");
            file.close();
            return;
        }

        // 在当前连接上开始事务
        if (!db.transaction())
        {
            Logger::instance()->error("Failed to begin transaction for server insertion");
            DatabasePool::instance()->releaseConnection(db);
            file.close();
            return;
        }

        QTextStream in(&file);
        QString header = in.readLine(); // 跳过表头

        int successCount = 0;
        int failCount = 0;
        bool transactionSuccess = true;

        while (!in.atEnd() && transactionSuccess)
        {
            QString line = in.readLine();
            QStringList fields = line.split(',');

            // 387, success, 185.247.184.62, 542ms, 意大利 米兰 SU区

            if (fields.size() >= 4 && fields[1].trimmed() == "success")
            {
                QString ip = fields[2].trimmed();
                QString description = fields[4].trimmed();

                // 将 IP 地址转换为整数
                quint32 ipInt = ipToInt(ip);

                // 直接在当前连接上执行SQL，不通过server_dao_的方法
                QSqlQuery query(db);
                query.prepare("INSERT INTO test_server (location, ip_addr, active) VALUES (?, ?, ?) "
                              "ON DUPLICATE KEY UPDATE ip_addr = VALUES(ip_addr), active = VALUES(active)");
                query.addBindValue(description);
                query.addBindValue(ipInt);
                query.addBindValue(true);

                if (!query.exec())
                {
                    Logger::instance()->error(QString("Failed to add server: %1, IP: %2 - %3")
                                                  .arg(description)
                                                  .arg(ip)
                                                  .arg(query.lastError().text()));
                    failCount++;
                }
                else
                {
                    successCount++;
                }
            }
        }

        // 提交或回滚事务
        if (transactionSuccess && db.commit())
        {
            Logger::instance()->info(QString("Server insertion completed: %1 succeeded, %2 failed")
                                         .arg(successCount)
                                         .arg(failCount));
        }
        else
        {
            db.rollback();
            Logger::instance()->error("Transaction for server insertion rolled back due to errors");
        }

        // 释放数据库连接
        DatabasePool::instance()->releaseConnection(db);
        file.close();
    }
    // 实现 IP 地址转换方法
    quint32 ipToInt(const QString &ip)
    {
        QHostAddress address(ip);
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
        {
            throw std::invalid_argument(QString("Invalid IPv4 address: %1").arg(ip).toStdString());
        }

        return address.toIPv4Address();
    }

public slots:
    void handleSigInt()
    {
        snInt->setEnabled(false);
        char tmp;
        // 使用变量接收返回值并标记为unused
        [[maybe_unused]] ssize_t result = ::read(sigintFd[1], &tmp, sizeof(tmp));

        Logger::instance()->info("Received SIGINT, shutting down gracefully...");
        shutdown();
        QCoreApplication::quit();

        snInt->setEnabled(true);
    }

    void handleSigTerm()
    {
        snTerm->setEnabled(false);
        char tmp;
        // 使用变量接收返回值并标记为unused
        [[maybe_unused]] ssize_t result = ::read(sigtermFd[1], &tmp, sizeof(tmp));

        Logger::instance()->info("Received SIGTERM, shutting down gracefully...");
        shutdown();
        QCoreApplication::quit();
    }

    void performCleanup()
    {
        try
        {
            // 清理过期会话
            if (auth_manager_)
            {
                auth_manager_->cleanupExpiredSessions();
            }

            // 数据库连接池健康检查
            if (db_pool_)
            {
                // DatabasePool内部会自动进行健康检查，不需要手动调用cleanup
            }

            Logger::instance()->debug("Periodic cleanup completed");
        }
        catch (const std::exception &e)
        {
            Logger::instance()->error(QString("Exception during cleanup: %1").arg(e.what()));
        }
        catch (...)
        {
            Logger::instance()->error("Unknown exception during cleanup");
        }
    }

    void printServerInfo()
    {
        Logger::instance()->info("=== LatCheck Server Information ===");

        // 服务器基本信息
        ServerConfig serverConfig = config_->getServerConfig();
        Logger::instance()->info(QString("Server Version: 1.0"));
        Logger::instance()->info(QString("Listening Address: %1:%2").arg(serverConfig.host).arg(serverConfig.port));
        Logger::instance()->info(QString("Max Connections: %1").arg(serverConfig.maxConnections));
        Logger::instance()->info(QString("Connection Timeout: %1 seconds").arg(serverConfig.connectionTimeout));

        // TLS信息
        TlsConfig tlsConfig = config_->getTlsConfig();
        Logger::instance()->info(QString("TLS Protocol: %1").arg(tlsConfig.protocol));
        Logger::instance()->info(QString("Certificate Path: %1").arg(config_->getCertificatePath()));
        Logger::instance()->info(QString("Private Key Path: %1").arg(config_->getPrivateKeyPath()));
        Logger::instance()->info(QString("Client Cert Required: %1").arg(tlsConfig.requireClientCert ? "Yes" : "No"));

        // 数据库信息
        DatabaseConfig dbConfig = config_->getDatabaseConfig();
        Logger::instance()->info(QString("Database: %1@%2:%3").arg(dbConfig.database).arg(dbConfig.host).arg(dbConfig.port));
        Logger::instance()->info(QString("DB Connection Pool: %1-%2 connections").arg(dbConfig.minConnections).arg(dbConfig.maxConnections));
        Logger::instance()->info(QString("DB Connection Timeout: %1 seconds").arg(dbConfig.connectionTimeout));

        // 日志信息
        LogConfig logConfig = config_->getLogConfig();
        Logger::instance()->info(QString("Log Level: %1").arg(logConfig.level));
        if (!logConfig.filePath.isEmpty())
        {
            Logger::instance()->info(QString("Log File: %1").arg(logConfig.filePath));
        }

        Logger::instance()->info("Server is ready to accept connections");
        Logger::instance()->info("===================================");
    }
};

// 定义静态成员
int LatCheckServer::sigintFd[2];
int LatCheckServer::sigtermFd[2];

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 创建服务器实例
    LatCheckServer server;

    // 初始化服务器
    if (!server.initialize())
    {
        Logger::instance()->error("Failed to initialize server");
        return -1;
    }

    // 启动服务器
    if (!server.start())
    {
        Logger::instance()->error("Failed to start server");
        return -1;
    }

    // 运行应用程序
    int result = app.exec();

    // 服务器会在析构函数中自动关闭
    return result;
}

#include "main.moc"