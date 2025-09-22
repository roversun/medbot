#ifndef SERVERDAO_H
#define SERVERDAO_H

#include "base_dao.h"
#include "protocol/message_protocol.h"
#include <QList>
#include <QString>

class ServerDAO : public BaseDAO
{
Q_OBJECT

public:
    explicit ServerDAO(QObject *parent = nullptr);
    
    // 获取活跃的服务器列表
    QList<ServerInfo> getActiveServers();
    
    // 根据ID获取服务器信息
    ServerInfo getServerById(int serverId);
    
    // 根据位置获取服务器信息
    ServerInfo getServerByLocation(const QString& location);
    
    // 获取所有服务器（包括非活跃的）
    QList<ServerInfo> getAllServers();
    
    // 更新服务器状态
    bool updateServerStatus(int serverId, bool active);
    
    // 添加新服务器
    bool addServer(const QString& location, quint32 ipAddr, bool active = true);
    
    // 删除服务器
    bool deleteServer(int serverId);
    
private:
    // 从查询结果构建ServerInfo对象
    ServerInfo buildServerInfoFromQuery(const QSqlQuery& query);
};

#endif // SERVERDAO_H