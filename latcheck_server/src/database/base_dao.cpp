#include "database/base_dao.h"
#include "database/database_pool.h"
#include "logger/logger.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>



BaseDAO::BaseDAO(QObject *parent)
    : QObject(parent)
    , in_transaction_(false)
    , pool_(DatabasePool::instance())  // 初始化连接池指针
{
}

QSqlQuery BaseDAO::executeQuery(const QString& sql, const QVariantList& params)
{
    QSqlDatabase db = pool_->getConnection();
    QSqlQuery query(db);
    
    if (!db.isValid()) {
        Logger::instance()->error("Database connection is not available", "DAO");
        return query;  // 返回无效查询对象
    }

    query.prepare(sql);
    
    for (const auto& param : params) {
        query.addBindValue(param);
    }
    
    bool success = query.exec();
    if (success) {
        Logger::instance()->debug(QString("Query executed successfully: %1").arg(sql), "DAO");
    } else {
        Logger::instance()->error(QString("Query failed: %1 - %2").arg(sql).arg(query.lastError().text()), "DAO");
    }
    
    pool_->releaseConnection(db);
    return query;  // 返回查询对象而不是bool
}

bool BaseDAO::executeUpdate(const QString& sql, const QVariantList& params)
{
    QSqlDatabase db = pool_->getConnection();
    if (!db.isValid()) {
        Logger::instance()->error("Database connection is not available", "DAO");
        return false;
    }

    QSqlQuery query(db);
    query.prepare(sql);
    
    for (const auto& param : params) {
        query.addBindValue(param);
    }
    
    bool success = query.exec();
    if (success) {
        Logger::instance()->debug(QString("Update executed successfully: %1, affected rows: %2")
                                .arg(sql).arg(query.numRowsAffected()), "DAO");
    }
    
    pool_->releaseConnection(db);
    return success;
}

bool BaseDAO::beginTransaction()
{
    DatabaseConnection dbConn;
    QSqlDatabase db = dbConn.database();
    
    if (!db.isValid() || !db.isOpen()) {
        Logger::instance()->error("Database connection is not available for transaction", "DAO");
        return false;
    }
    
    if (in_transaction_) {
        Logger::instance()->warning("Transaction already in progress", "DAO");
        return false;
    }
    
    if (db.transaction()) {
        in_transaction_ = true;
        Logger::instance()->debug("Transaction started", "DAO");
        return true;
    } else {
        Logger::instance()->error(QString("Failed to start transaction: %1").arg(db.lastError().text()), "DAO");
        return false;
    }
}

bool BaseDAO::commitTransaction()
{
    DatabaseConnection dbConn;
    QSqlDatabase db = dbConn.database();
    
    if (!in_transaction_) {
        Logger::instance()->warning("No transaction to commit", "DAO");
        return false;
    }
    
    if (db.commit()) {
        in_transaction_ = false;
        Logger::instance()->debug("Transaction committed", "DAO");
        return true;
    } else {
        Logger::instance()->error(QString("Failed to commit transaction: %1").arg(db.lastError().text()), "DAO");
        return false;
    }
}

bool BaseDAO::rollbackTransaction()
{
    DatabaseConnection dbConn;
    QSqlDatabase db = dbConn.database();
    
    if (!in_transaction_) {
        Logger::instance()->warning("No transaction to rollback", "DAO");
        return false;
    }
    
    if (db.rollback()) {
        in_transaction_ = false;
        Logger::instance()->debug("Transaction rolled back", "DAO");
        return true;
    } else {
        Logger::instance()->error(QString("Failed to rollback transaction: %1").arg(db.lastError().text()), "DAO");
        return false;
    }
}

qint64 BaseDAO::getLastInsertId()
{
    DatabaseConnection dbConn;
    QSqlDatabase db = dbConn.database();
    
    QSqlQuery query("SELECT LAST_INSERT_ID()", db);
    if (query.next()) {
        return query.value(0).toLongLong();
    }
    return -1;
}

void BaseDAO::bindParameters(QSqlQuery& query, const QVariantList& params)
{
    for (int i = 0; i < params.size(); ++i) {
        query.bindValue(i, params.at(i));
    }
}

void BaseDAO::logSqlError(const QSqlQuery& query, const QString& operation)
{
    QString errorMsg = QString("%1 failed: %2, SQL: %3")
                      .arg(operation)
                      .arg(query.lastError().text())
                      .arg(query.lastQuery());
    Logger::instance()->error(errorMsg, "DAO");
}

QDateTime BaseDAO::fromDatabaseTimestamp(const QVariant& timestamp)
{
    if (timestamp.isNull()) {
        return QDateTime();
    }
    return timestamp.toDateTime();
}

QVariant BaseDAO::toDatabaseTimestamp(const QDateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return QVariant();
    }
    return dateTime;
}

bool BaseDAO::validateParameters(const QVariantList& params, int expectedCount)
{
    if (params.size() != expectedCount) {
        Logger::instance()->error(QString("Parameter count mismatch: expected %1, got %2")
                                 .arg(expectedCount).arg(params.size()), "DAO");
        return false;
    }
    return true;
}