#ifndef REPORTDAO_H
#define REPORTDAO_H

#include "common/error_codes.h"
#include <QList>
#include "database/base_dao.h"
#include "common/types.h"

class ReportDAO : public BaseDAO
{
    Q_OBJECT

public:
    explicit ReportDAO(QObject *parent = nullptr);

    // 创建报告
    ErrorCode createReport(const Report &report);

    // 创建报告（带记录列表）
    ErrorCode createReport(const Report &report, const QList<ReportRecord> &records = QList<ReportRecord>());

    // 根据ID获取报告
    Report getReportById(qint64 reportId);

    // 根据用户名获取报告列表
    QList<Report> getReportsByUserName(const QString &userName, int limit = 100, int offset = 0);

    // 根据检测位置获取报告列表
    QList<Report> getReportsByLocation(const QString &location, int limit = 100, int offset = 0);

    // 根据时间范围获取报告列表
    QList<Report> getReportsByTimeRange(const QDateTime &startTime, const QDateTime &endTime, int limit = 100, int offset = 0);

    // 获取所有报告列表
    QList<Report> getAllReports(int limit = 100, int offset = 0);

    // 获取报告数量
    int getReportCount();

    // 根据用户名获取报告数量
    int getReportCountByUserName(const QString &userName);

    // 根据检测位置获取报告数量
    int getReportCountByLocation(const QString &location);

    // 获取最新报告
    Report getLatestReportByUserName(const QString &userName);

    // 获取报告详细记录
    QList<ReportRecord> getReportRecords(qint64 reportId);

private:
    // 从查询结果构建报告对象
    Report buildReportFromQuery(const QSqlQuery &query);

    // 从查询结果构建报告详细对象
    ReportRecord buildReportRecordFromQuery(const QSqlQuery &query);

    // 验证报告数据
    bool validateReportData(const Report &report);
};
#endif // REPORTDAO_H