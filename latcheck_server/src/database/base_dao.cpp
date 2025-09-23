#include "database/base_dao.h"
#include "database/database_pool.h"
#include "logger/logger.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>

BaseDAO::BaseDAO(QObject *parent)
    : QObject(parent),
      in_transaction_(false),
      pool_(DatabasePool::instance()),
      transaction_connection_wrapper_(nullptr) // 初始化事务连接包装器
{
}

BaseDAO::~BaseDAO()
{
    // 确保事务连接被正确释放
    if (transaction_connection_wrapper_ != nullptr)
    {
        delete transaction_connection_wrapper_;
        transaction_connection_wrapper_ = nullptr;
    }
}

QSqlQuery BaseDAO::executeQuery(const QString &sql, const QVariantList &params)
{
    QSqlDatabase db;

    // 如果在事务中，使用事务连接
    if (in_transaction_ && transaction_connection_.isValid())
    {
        db = transaction_connection_;
    }
    else
    {
        db = pool_->getConnection();
    }

    QSqlQuery query(db);

    if (!db.isValid())
    {
        Logger::instance()->error("Database connection is not available");
        return query; // 返回无效查询对象
    }

    query.prepare(sql);

    for (const auto &param : params)
    {
        query.addBindValue(param);
    }

    bool success = query.exec();
    if (!success)
    {
        Logger::instance()->error(QString("Query failed: %1 - %2").arg(sql).arg(query.lastError().text()));
    }

    // 事务中的连接不在这里释放，而是在事务结束时统一释放
    if (!in_transaction_ || !transaction_connection_.isValid())
    {
        pool_->releaseConnection(db);
    }

    return query; // 返回查询对象
}

bool BaseDAO::executeUpdate(const QString &sql, const QVariantList &params)
{
    QSqlDatabase db;

    // 如果在事务中，使用事务连接
    if (in_transaction_ && transaction_connection_.isValid())
    {
        db = transaction_connection_;
    }
    else
    {
        db = pool_->getConnection();
    }

    if (!db.isValid())
    {
        Logger::instance()->error("Database connection is not available");
        return false;
    }

    QSqlQuery query(db);
    query.prepare(sql);

    for (const auto &param : params)
    {
        query.addBindValue(param);
    }

    bool success = query.exec();
    if (success)
    {
        Logger::instance()->debug(QString("Update executed successfully: %1, affected rows: %2")
                                      .arg(sql)
                                      .arg(query.numRowsAffected()),
                                  "DAO");
    }

    // 事务中的连接不在这里释放，而是在事务结束时统一释放
    if (!in_transaction_ || !transaction_connection_.isValid())
    {
        pool_->releaseConnection(db);
    }

    return success;
}

bool BaseDAO::beginTransaction()
{
    if (in_transaction_)
    {
        Logger::instance()->warning("Transaction already in progress");
        return false;
    }

    // 创建事务连接包装器
    transaction_connection_wrapper_ = new DatabaseConnection();
    transaction_connection_ = transaction_connection_wrapper_->database();

    if (!transaction_connection_.isValid() || !transaction_connection_.isOpen())
    {
        Logger::instance()->error("Database connection is not available for transaction");
        delete transaction_connection_wrapper_;
        transaction_connection_wrapper_ = nullptr;
        return false;
    }

    if (transaction_connection_.transaction())
    {
        in_transaction_ = true;
        Logger::instance()->debug("Transaction started");
        return true;
    }
    else
    {
        Logger::instance()->error(QString("Failed to start transaction: %1").arg(transaction_connection_.lastError().text()));
        delete transaction_connection_wrapper_;
        transaction_connection_wrapper_ = nullptr;
        return false;
    }
}

bool BaseDAO::commitTransaction()
{
    if (!in_transaction_)
    {
        Logger::instance()->warning("No transaction to commit");
        return false;
    }

    if (!transaction_connection_.isValid() || !transaction_connection_.isOpen())
    {
        Logger::instance()->error("Database connection is not available for commit");
        return false;
    }

    if (transaction_connection_.commit())
    {
        in_transaction_ = false;
        Logger::instance()->debug("Transaction committed");

        // 释放事务连接
        delete transaction_connection_wrapper_;
        transaction_connection_wrapper_ = nullptr;
        transaction_connection_ = QSqlDatabase();

        return true;
    }
    else
    {
        Logger::instance()->error(QString("Failed to commit transaction: %1").arg(transaction_connection_.lastError().text()));
        return false;
    }
}

bool BaseDAO::rollbackTransaction()
{
    if (!in_transaction_)
    {
        Logger::instance()->warning("No transaction to rollback");
        return false;
    }

    if (!transaction_connection_.isValid() || !transaction_connection_.isOpen())
    {
        Logger::instance()->error("Database connection is not available for rollback");
        return false;
    }

    if (transaction_connection_.rollback())
    {
        in_transaction_ = false;
        Logger::instance()->debug("Transaction rolled back");

        // 释放事务连接
        delete transaction_connection_wrapper_;
        transaction_connection_wrapper_ = nullptr;
        transaction_connection_ = QSqlDatabase();

        return true;
    }
    else
    {
        Logger::instance()->error(QString("Failed to rollback transaction: %1").arg(transaction_connection_.lastError().text()));
        return false;
    }
}

qint64 BaseDAO::getLastInsertId()
{
    QSqlDatabase db;

    // 如果在事务中，使用事务连接
    if (in_transaction_ && transaction_connection_.isValid())
    {
        db = transaction_connection_;
    }
    else
    {
        db = pool_->getConnection();
    }

    if (!db.isValid() || !db.isOpen())
    {
        Logger::instance()->error("Database connection is not available");
        if (!in_transaction_ || !transaction_connection_.isValid())
        {
            pool_->releaseConnection(db);
        }
        return -1;
    }

    QSqlQuery query("SELECT LAST_INSERT_ID()", db);
    qint64 result = -1;
    if (query.next())
    {
        result = query.value(0).toLongLong();
    }

    if (!in_transaction_ || !transaction_connection_.isValid())
    {
        pool_->releaseConnection(db);
    }

    return result;
}

void BaseDAO::bindParameters(QSqlQuery &query, const QVariantList &params)
{
    for (int i = 0; i < params.size(); ++i)
    {
        query.bindValue(i, params.at(i));
    }
}

void BaseDAO::logSqlError(const QSqlQuery &query, const QString &operation)
{
    QString errorMsg = QString("%1 failed: %2, SQL: %3")
                           .arg(operation)
                           .arg(query.lastError().text())
                           .arg(query.lastQuery());
    Logger::instance()->error(errorMsg);
}

QDateTime BaseDAO::fromDatabaseTimestamp(const QVariant &timestamp)
{
    if (timestamp.isNull())
    {
        return QDateTime();
    }
    return timestamp.toDateTime();
}

QVariant BaseDAO::toDatabaseTimestamp(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
    {
        return QVariant();
    }
    return dateTime;
}

bool BaseDAO::validateParameters(const QVariantList &params, int expectedCount)
{
    if (params.size() != expectedCount)
    {
        Logger::instance()->error(QString("Parameter count mismatch: expected %1, got %2")
                                      .arg(expectedCount)
                                      .arg(params.size()),
                                  "DAO");
        return false;
    }
    return true;
}