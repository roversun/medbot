#ifndef BASEDAO_H
#define BASEDAO_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include "common/types.h"
#include "common/error_codes.h"
#include "database/database_pool.h"

class BaseDAO : public QObject
{
    Q_OBJECT

public:
    explicit BaseDAO(QObject *parent = nullptr);
    virtual ~BaseDAO() override;

    // 执行SQL查询
    QSqlQuery executeQuery(const QString &sql, const QVariantList &params = {});
    // 执行SQL更新
    bool executeUpdate(const QString &sql, const QVariantList &params = {});

    // 事务管理
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // 获取最后插入的ID
    qint64 getLastInsertId();

protected:
    // 参数绑定
    void bindParameters(QSqlQuery &query, const QVariantList &params);
    // SQL错误日志
    void logSqlError(const QSqlQuery &query, const QString &operation);

    // 时间戳转换
    QDateTime fromDatabaseTimestamp(const QVariant &timestamp);
    QVariant toDatabaseTimestamp(const QDateTime &dateTime);

    // 参数验证
    bool validateParameters(const QVariantList &params, int expectedCount);

private:
    // 调整顺序以匹配构造函数中的初始化顺序
    bool in_transaction_;
    DatabasePool *pool_;
    // 事务连接管理
    QSqlDatabase transaction_connection_;
    DatabaseConnection *transaction_connection_wrapper_;
};

#endif // BASEDAO_H