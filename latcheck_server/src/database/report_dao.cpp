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

ErrorCode ReportDAO::createReport(const Report& report)
{
    if (!validateReportData(report)) {
        Logger::instance()->error("Invalid report data", "ReportDAO");
        return ErrorCode::InvalidData;
    }

    QSqlQuery query = executeQuery(
        "INSERT INTO latcheck_report (check_location, user_name, created_time) VALUES (?, ?, ?)",
        {report.location, report.userName, report.createdAt.isValid() ? report.createdAt : QDateTime::currentDateTime()}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to create report", "ReportDAO");
        return ErrorCode::DatabaseError;
    }

    // 记录审计日志
    Logger::instance()->auditLog(report.userName, "CREATE_REPORT", 
        QString("Report created - Location: %1, User: %2")
        .arg(report.location, report.userName));

    return ErrorCode::Success;
}

Report ReportDAO::getReportById(qint64 reportId)
{
    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE report_id = ?",
        {reportId}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to get database connection", "ReportDAO");
        return Report();
    }

    if (query.next()) {
        return buildReportFromQuery(query);
    }
    
    QString error = QString("Failed to get report by ID: %1").arg(reportId);
    Logger::instance()->error(error, "ReportDAO");
    return Report();
}

QList<Report> ReportDAO::getReportsByUserName(const QString& userName, int limit, int offset)
{
    QList<Report> reports;
    
    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE user_name = ? ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {userName, limit, offset}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to get database connection", "ReportDAO");
        return reports;
    }

    while (query.next()) {
        reports.append(buildReportFromQuery(query));
    }
    
    return reports;
}

QList<Report> ReportDAO::getReportsByLocation(const QString& location, int limit, int offset)
{
    QList<Report> reports;
    
    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report WHERE check_location = ? ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {location, limit, offset}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to get database connection", "ReportDAO");
        return reports;
    }

    while (query.next()) {
        reports.append(buildReportFromQuery(query));
    }
    
    return reports;
}

QList<Report> ReportDAO::getAllReports(int limit, int offset)
{
    QList<Report> reports;
    
    QSqlQuery query = executeQuery(
        "SELECT report_id, check_location, user_name, created_time FROM latcheck_report ORDER BY created_time DESC LIMIT ? OFFSET ?",
        {limit, offset}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to get database connection", "ReportDAO");
        return reports;
    }

    while (query.next()) {
        reports.append(buildReportFromQuery(query));
    }
    
    return reports;
}

ErrorCode ReportDAO::createReportDetail(qint64 reportId, const QList<ReportDetail>& details)
{
    if (details.isEmpty()) {
        return ErrorCode::Success;
    }

    if (!beginTransaction()) {
        Logger::instance()->error("Failed to start transaction", "ReportDAO");
        return ErrorCode::TransactionFailed;
    }

    for (const auto& detail : details) {
        QVariantList params;
        params << reportId << detail.serverName << detail.serverIp 
               << detail.latency << detail.status << detail.testTime << detail.additionalInfo;
        
        QSqlQuery query = executeQuery(
            "INSERT INTO latcheck_report_detail (report_id, server_name, server_ip, latency, status, test_time, additional_info) VALUES (?, ?, ?, ?, ?, ?, ?)",
            params
        );
        
        if (!query.isValid()) {
            rollbackTransaction();
            Logger::instance()->error("Failed to insert report detail", "ReportDAO");
            return ErrorCode::DatabaseError;
        }
    }

    if (!commitTransaction()) {
        Logger::instance()->error("Failed to commit transaction", "ReportDAO");
        return ErrorCode::TransactionFailed;
    }

    return ErrorCode::Success;
}

QList<ReportDetail> ReportDAO::getReportDetails(qint64 reportId)
{
    QList<ReportDetail> details;
    
    QSqlQuery query = executeQuery(
        "SELECT record_id, report_id, item_name, item_value, description, created_time FROM latcheck_report_detail WHERE report_id = ?",
        {reportId}
    );
    
    if (!query.isValid()) {
        Logger::instance()->error("Failed to get database connection", "ReportDAO");
        return details;
    }

    while (query.next()) {
        details.append(buildReportDetailFromQuery(query));
    }
    
    return details;
}

int ReportDAO::getReportCount()
{
    QSqlQuery query = executeQuery("SELECT COUNT(*) FROM latcheck_report");
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

Report ReportDAO::buildReportFromQuery(const QSqlQuery& query)
{
    Report report;
    report.id = query.value("report_id").toLongLong();
    report.location = query.value("check_location").toString();
    report.userName = query.value("user_name").toString();
    report.createdAt = query.value("created_time").toDateTime();
    return report;
}

ReportDetail ReportDAO::buildReportDetailFromQuery(const QSqlQuery& query)
{
    ReportDetail detail;
    detail.id = query.value("record_id").toLongLong();
    detail.reportId = query.value("report_id").toLongLong();
    detail.serverName = query.value("server_name").toString();
    detail.serverIp = query.value("server_ip").toString();
    detail.latency = query.value("latency").toDouble();
    detail.status = query.value("status").toString();
    detail.testTime = query.value("test_time").toDateTime();
    detail.additionalInfo = query.value("additional_info").toString();
    return detail;
}

bool ReportDAO::validateReportData(const Report& report)
{
    if (report.location.isEmpty() || report.location.length() > 64) {
        return false;
    }
    
    if (report.userName.isEmpty() || report.userName.length() > 32) {
        return false;
    }
    
    return true;
}