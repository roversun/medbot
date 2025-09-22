#include "database/server_dao.h"
#include "logger/logger.h"
#include <QHostAddress>
#include <QSqlQuery>
#include <QVariant>

ServerDAO::ServerDAO(QObject *parent)
    : BaseDAO(parent)
{
}

QList<ServerInfo> ServerDAO::getActiveServers()
{
    QList<ServerInfo> servers;

    QString sql = "SELECT server_id, ip_addr FROM test_server WHERE active = ? ORDER BY server_id";
    QVariantList params;
    params << true;

    QSqlQuery query = executeQuery(sql, params);

    // 移除不正确的 isValid() 检查，query.next() 会自动处理空结果集的情况
    while (query.next())
    {
        servers.append(buildServerInfoFromQuery(query));
    }

    Logger::instance()->info("ServerDAO", QString("Retrieved %1 active servers from database").arg(servers.size()));
    return servers;
}

ServerInfo ServerDAO::getServerById(int serverId)
{
    ServerInfo server;

    if (serverId <= 0)
    {
        return server;
    }

    QString sql = "SELECT server_id, location, ip_addr FROM test_server WHERE server_id = ?";
    QVariantList params;
    params << serverId;

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
        server = buildServerInfoFromQuery(query);
    }

    return server;
}

ServerInfo ServerDAO::getServerByLocation(const QString &location)
{
    ServerInfo server;

    if (location.isEmpty())
    {
        return server;
    }

    QString sql = "SELECT server_id, location, ip_addr FROM test_server WHERE location = ?";
    QVariantList params;
    params << location;

    QSqlQuery query = executeQuery(sql, params);

    if (query.next())
    {
        server = buildServerInfoFromQuery(query);
    }

    return server;
}

QList<ServerInfo> ServerDAO::getAllServers()
{
    QList<ServerInfo> servers;

    QString sql = "SELECT server_id, location, ip_addr FROM test_server ORDER BY server_id";

    QSqlQuery query = executeQuery(sql, {});

    if (!query.isValid())
    {
        Logger::instance()->error("Failed to get all servers from database", "ServerDAO");
        return servers;
    }

    while (query.next())
    {
        servers.append(buildServerInfoFromQuery(query));
    }

    return servers;
}

bool ServerDAO::updateServerStatus(int serverId, bool active)
{
    if (serverId <= 0)
    {
        return false;
    }

    QString sql = "UPDATE test_server SET active = ? WHERE server_id = ?";
    QVariantList params;
    params << active << serverId;

    return executeUpdate(sql, params);
}

bool ServerDAO::addServer(const QString &location, quint32 ipAddr, bool active)
{
    if (location.isEmpty())
    {
        return false;
    }

    // 使用 INSERT ON DUPLICATE KEY UPDATE 语法来处理 location 唯一约束
    QString sql = "INSERT INTO test_server (location, ip_addr, active) VALUES (?, ?, ?) "
                  "ON DUPLICATE KEY UPDATE ip_addr = VALUES(ip_addr), active = VALUES(active)";
    QVariantList params;
    params << location << ipAddr << active;

    return executeUpdate(sql, params);
}

bool ServerDAO::deleteServer(int serverId)
{
    if (serverId <= 0)
    {
        return false;
    }

    QString sql = "DELETE FROM test_server WHERE server_id = ?";
    QVariantList params;
    params << serverId;

    return executeUpdate(sql, params);
}

ServerInfo ServerDAO::buildServerInfoFromQuery(const QSqlQuery &query)
{
    ServerInfo server;

    server.serverId = query.value("server_id").toUInt();
    server.ipAddr = query.value("ip_addr").toUInt();

    return server;
}