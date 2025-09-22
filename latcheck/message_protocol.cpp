#include "message_protocol.h"
#include <QDataStream>
#include <QIODevice>
#include <QCryptographicHash>
#include <QtEndian>
#include <cstring>

MessageProtocol::MessageProtocol()
{
}

QByteArray MessageProtocol::serializeHeader(const MessageHeader& header)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    // 直接写入，让QDataStream处理字节序
    stream << header.msgType;
    stream << header.dataLength;
    
    return data;
}


MessageHeader MessageProtocol::deserializeHeader(const QByteArray &data)
{
    MessageHeader header;
    if (data.size() < 8) {
        return header; 
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> header.msgType >> header.dataLength;
    
    return header;
}

QByteArray MessageProtocol::serializeLoginRequest(const QString &userName, const QString &passwordHash)
{
    LoginRequestData loginData;
    
    // 复制用户名
    QByteArray userNameBytes = userName.toUtf8();
    memcpy(loginData.userName, userNameBytes.constData(), 
           qMin(userNameBytes.size(), static_cast<int>(sizeof(loginData.userName) - 1)));
    
    // 复制密码哈希
    QByteArray passwordBytes = passwordHash.toUtf8();
    memcpy(loginData.password, passwordBytes.constData(), 
           qMin(passwordBytes.size(), static_cast<int>(sizeof(loginData.password) - 1)));
    
    // 序列化为字节数组
    QByteArray data;
    data.append(reinterpret_cast<const char*>(&loginData), sizeof(LoginRequestData));
    return data;
}

LoginRequestData MessageProtocol::deserializeLoginRequest(const QByteArray &data)
{
    LoginRequestData loginData;
    if (data.size() >= sizeof(LoginRequestData)) {
        memcpy(&loginData, data.constData(), sizeof(LoginRequestData));
    }
    return loginData;
}

QByteArray MessageProtocol::serializeListResponse(const QList<ServerInfo> &servers)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    // 写入服务器数量
    stream << static_cast<quint32>(servers.size());
    
    // 写入每个服务器信息
    for (const ServerInfo &server : servers) {
        stream << server.serverId << server.ipAddr;
    }
    
    return data;
}

ListResponseData MessageProtocol::deserializeListResponse(const QByteArray &data)
{
    ListResponseData listData;
    if (data.size() < 4) {
        return listData;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> listData.serverCount;
    
    // 读取服务器信息
    for (quint32 i = 0; i < listData.serverCount && !stream.atEnd(); ++i) {
        ServerInfo server;
        stream >> server.serverId >> server.ipAddr;
        listData.servers.append(server);
    }
    
    return listData;
}

QByteArray MessageProtocol::serializeReportRequest(const QString &location, const QList<LatencyRecord> &records)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    // 写入位置信息长度和内容
    QByteArray locationBytes = location.toUtf8();
    stream << static_cast<quint32>(locationBytes.size());
    stream.writeRawData(locationBytes.constData(), locationBytes.size());
    
    // 写入记录数量
    stream << static_cast<quint32>(records.size());
    
    // 写入每个延时记录
    for (const LatencyRecord &record : records) {
        stream << record.serverId << record.latency;
    }
    
    return data;
}

ReportRequestData MessageProtocol::deserializeReportRequest(const QByteArray &data)
{
    ReportRequestData reportData;
    if (data.size() < 8) {
        return reportData;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    // 读取位置信息
    stream >> reportData.locationLength;
    if (reportData.locationLength > 0 && reportData.locationLength < 10000) { // 安全检查
        QByteArray locationBytes(reportData.locationLength, 0);
        stream.readRawData(locationBytes.data(), reportData.locationLength);
        reportData.location = QString::fromUtf8(locationBytes);
    }
    
    // 读取记录数量
    stream >> reportData.recordCount;
    
    // 读取延时记录
    for (quint32 i = 0; i < reportData.recordCount && !stream.atEnd(); ++i) {
        LatencyRecord record;
        stream >> record.serverId >> record.latency;
        reportData.records.append(record);
    }
    
    return reportData;
}

bool MessageProtocol::validateHeader(const MessageHeader &header)
{
    // 验证消息类型是否有效
    quint32 msgType = header.msgType;
    if (msgType < static_cast<quint32>(MessageType::LOGIN_REQUEST) || 
        msgType > static_cast<quint32>(MessageType::REPORT_FAIL)) {
        return false;
    }
    
    // 验证数据长度是否合理（不超过1MB）
    if (header.dataLength > 1024 * 1024) {
        return false;
    }
    
    return true;
}

QString MessageProtocol::getMessageTypeString(MessageType type)
{
    switch (type) {
    case MessageType::LOGIN_REQUEST:
        return "LOGIN_REQUEST";
    case MessageType::LOGIN_OK:
        return "LOGIN_OK";
    case MessageType::LOGIN_FAIL:
        return "LOGIN_FAIL";
    case MessageType::LIST_REQUEST:
        return "LIST_REQUEST";
    case MessageType::LIST_RESPONSE:
        return "LIST_RESPONSE";
    case MessageType::REPORT_REQUEST:
        return "REPORT_REQUEST";
    case MessageType::REPORT_OK:
        return "REPORT_OK";
    case MessageType::REPORT_FAIL:
        return "REPORT_FAIL";
    default:
        return "UNKNOWN";
    }
}
