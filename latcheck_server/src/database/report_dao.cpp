#include "database/report_dao.h"
#include "database/base_dao.h"
#include "logger/logger.h"
#include "common/error_codes.h"
#include "common/types.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>

ReportDAO::ReportDAO(QObject *parent)
    : BaseDAO(parent)
{
}

ErrorCode ReportDAO::createReport(const Report &report, const QList<ReportRecord> &records)
{
    if (!validateReportData(report))
    {
        Logger::instance()->error("Invalid report data", "ReportDAO");
        return ErrorCode::InvalidData;
    }

    // 开始事务
    if (!beginTransaction())
    {
        Logger::instance()->error("Failed to start transaction", "ReportDAO");
        return ErrorCode::TransactionFailed;
    }

    // 保存报告
    QSqlQuery query = executeQuery(
        "INSERT INTO latcheck_report (check_location, user_name, created_time) VALUES (?, ?, ?)",
        {report.location, report.userName, report.createdAt.isValid() ? report.createdAt : QDateTime::currentDateTime()});

    if (query.lastError().type() != QSqlError::NoError)
    {
        rollbackTransaction();
        Logger::instance()->error(QString("Failed to create report: %1").arg(query.lastError().text()), "ReportDAO");
        return ErrorCode::DatabaseError;
    }

    // 获取刚创建的报告ID
    qint64 reportId = getLastInsertId();
    if (reportId <= 0)
    {
        rollbackTransaction();
        Logger::instance()->error("Failed to get last inserted report ID", "ReportDAO");
        return ErrorCode::DatabaseError;
    }

    // 保存报告记录
    if (!records.isEmpty())
    {
        for (const auto &record : records)
        {
            QVariantList params;
            params << reportId << record.serverIp << record.serverId << record.latency;

            QSqlQuery recordQuery = executeQuery(
                "INSERT INTO report_record (report_id, server_ip, server_id, latency) VALUES (?, ?, ?, ?)",
                params);

            if (recordQuery.lastError().type() != QSqlError::NoError)
            {
                rollbackTransaction();
                Logger::instance()->error(QString("Failed to insert report record: %1").arg(recordQuery.lastError().text()), "ReportDAO");
                return ErrorCode::DatabaseError;
            }
        }
    }

    // 提交事务
    if (!commitTransaction())
    {
        rollbackTransaction();
        Logger::instance()->error("Failed to commit transaction", "ReportDAO");
        return ErrorCode::TransactionFailed;
    }

    // 记录审计日志
    Logger::instance()->auditLog(report.userName, "CREATE_REPORT",
                                 QString("Report created - Location: %1, User: %2, Records: %3")
                                     .arg(report.location)
                                     .arg(report.userName)
                                     .arg(records.size()));

    // 添加成功日志
    Logger::instance()->info(QString("Successfully created report ID %1 with %2 records for user %3")
                                 .arg(reportId)
                                 .arg(records.size())
                                 .arg(report.userName),
                             "ReportDAO");

    return ErrorCode::Success;
}

Report ReportDAO::getReportById(qint64 reportId)
{
    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE report_id = ?",
        {reportId});

    if (query.lastError().type() != QSqlError::NoError)
    {
        // 修复：使用链式arg()调用来替换多个占位符
        Logger::instance()->error(QString("Failed to get report by ID %1: %2").arg(reportId).arg(query.lastError().text()), "ReportDAO");
        return Report();
    }

    if (query.next())
    {
        return buildReportFromQuery(query);
    }

    QString error = QString("Failed to get report by ID: %1").arg(reportId);
    Logger::instance()->error(error, "ReportDAO");
    return Report();
}

// 修复1：getReportsByUserName函数中的错误
QList<Report> ReportDAO::getReportsByUserName(const QString &userName, int limit, int offset)
{
    QList<Report> reports;

    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE user_name = ? ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {userName, limit, offset});

    if (query.lastError().type() != QSqlError::NoError)
    {
        // 修复：使用链式arg()调用替换同时传递两个参数
        Logger::instance()->error(QString("Failed to get reports by user name %1: %2").arg(userName).arg(query.lastError().text()), "ReportDAO");
        return reports;
    }

    while (query.next())
    {
        reports.append(buildReportFromQuery(query));
    }

    return reports;
}

// 修复2：getReportsByLocation函数中的错误
QList<Report> ReportDAO::getReportsByLocation(const QString &location, int limit, int offset)
{
    QList<Report> reports;

    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE check_location = ? ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {location, limit, offset});

    if (query.lastError().type() != QSqlError::NoError)
    {
        // 修复：使用链式arg()调用替换同时传递两个参数
        Logger::instance()->error(QString("Failed to get reports by location %1: %2").arg(location).arg(query.lastError().text()), "ReportDAO");
        return reports;
    }

    while (query.next())
    {
        reports.append(buildReportFromQuery(query));
    }

    return reports;
}

QList<Report> ReportDAO::getAllReports(int limit, int offset)
{
    QList<Report> reports;

    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {limit, offset});

    if (query.lastError().type() != QSqlError::NoError)
    {
        Logger::instance()->error(QString("Failed to get all reports: %1").arg(query.lastError().text()), "ReportDAO");
        return reports;
    }

    while (query.next())
    {
        reports.append(buildReportFromQuery(query));
    }

    return reports;
}

// 修改 getReportRecords 方法
QList<ReportRecord> ReportDAO::getReportRecords(qint64 reportId)
{
    QList<ReportRecord> records;

    QSqlQuery query = executeQuery(
        "SELECT record_id, report_id, server_ip, server_id, latency FROM report_record WHERE report_id = ?",
        {reportId});

    if (query.lastError().type() != QSqlError::NoError)
    {
        Logger::instance()->error(QString("Failed to get report records by report ID %1: %2").arg(reportId).arg(query.lastError().text()), "ReportDAO");
        return records;
    }

    while (query.next())
    {
        records.append(buildReportRecordFromQuery(query));
    }

    return records;
}

// 修改 buildReportRecordFromQuery 方法
ReportRecord ReportDAO::buildReportRecordFromQuery(const QSqlQuery &query)
{
    ReportRecord record;
    record.id = query.value("record_id").toLongLong();
    record.reportId = query.value("report_id").toLongLong();
    record.serverIp = static_cast<quint32>(query.value("server_ip").toUInt());
    record.serverId = query.value("server_id").toInt();
    record.latency = query.value("latency").toInt();
    return record;
}

int ReportDAO::getReportCount()
{
    QSqlQuery query = executeQuery("SELECT COUNT(*) FROM latcheck_report");

    if (query.next())
    {
        return query.value(0).toInt();
    }

    return 0;
}

Report ReportDAO::buildReportFromQuery(const QSqlQuery &query)
{
    Report report;
    report.id = query.value("report_id").toLongLong();
    report.location = query.value("check_location").toString();
    report.userName = query.value("user_name").toString();
    report.createdAt = query.value("created_time").toDateTime();
    return report;
}

bool ReportDAO::validateReportData(const Report &report)
{
    if (report.location.isEmpty() || report.location.length() > 64)
    {
        return false;
    }

    if (report.userName.isEmpty() || report.userName.length() > 32)
    {
        return false;
    }

    return true;
}