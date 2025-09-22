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
    virtual ~BaseDAO() = default;

    // 开始事务
    bool beginTransaction();

    // 提交事务
    bool commitTransaction();

    // 回滚事务
    bool rollbackTransaction();

protected:
    // 执行查询
    QSqlQuery executeQuery(const QString &sql, const QVariantList &params = QVariantList());

    // 执行更新操作（INSERT, UPDATE, DELETE）
    bool executeUpdate(const QString &sql, const QVariantList &params = QVariantList());

    // 获取最后插入的ID
    qint64 getLastInsertId();

    // 绑定参数
    void bindParameters(QSqlQuery &query, const QVariantList &params);

    // 记录SQL错误
    void logSqlError(const QSqlQuery &query, const QString &operation);

    // 时间戳转换
    QDateTime fromDatabaseTimestamp(const QVariant &timestamp);
    QVariant toDatabaseTimestamp(const QDateTime &dateTime);

    // 验证参数
    bool validateParameters(const QVariantList &params, int expectedCount);

private:
    QSqlDatabase database_;
    bool in_transaction_;
    DatabasePool *pool_; // 添加数据库连接池指针
};

#endif // BASEDAO_H
