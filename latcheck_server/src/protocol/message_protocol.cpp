#include "protocol/message_protocol.h"
#include "logger/logger.h"
#include "common/types.h"

#include <QDataStream>
#include <QBuffer>
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
    
    stream << hostToNetwork(header.msgType);
    stream << hostToNetwork(header.dataLength);
    
    return data;
}

MessageHeader MessageProtocol::deserializeHeader(const QByteArray& data)
{
    MessageHeader header;
    
    if (data.size() < 8) {
        Logger::instance()->error("Invalid header size", "MessageProtocol");
        return header;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    quint32 msgType, dataLength;
    stream >> msgType >> dataLength;
    
    header.msgType = networkToHost(msgType);
    header.dataLength = networkToHost(dataLength);
    
    return header;
}

QByteArray MessageProtocol::serializeLoginRequest(const QString& userName, const QString& passwordHash)
{
    LoginRequestData data;
    
    // 复制用户名和密码哈希到固定长度字段
    strncpy(data.userName, userName.toUtf8().constData(), sizeof(data.userName) - 1);
    strncpy(data.passwordHash, passwordHash.toUtf8().constData(), sizeof(data.passwordHash) - 1);
    
    QByteArray result;
    result.append(reinterpret_cast<const char*>(&data), sizeof(data));
    
    return result;
}

LoginRequestData MessageProtocol::deserializeLoginRequest(const QByteArray& data)
{
    LoginRequestData loginData;
    
    if (data.size() != sizeof(LoginRequestData)) {
        Logger::instance()->error("Invalid login request data size", "MessageProtocol");
        return loginData;
    }
    
    memcpy(&loginData, data.constData(), sizeof(LoginRequestData));
    
    // 确保字符串以null结尾
    loginData.userName[sizeof(loginData.userName) - 1] = '\0';
    loginData.passwordHash[sizeof(loginData.passwordHash) - 1] = '\0';
    
    return loginData;
}

QByteArray MessageProtocol::serializeListResponse(const QList<ServerInfo>& servers)
{
    ListResponseData data;
    data.serverCount = static_cast<quint32>(servers.size());
    
    QByteArray result;
    result.append(reinterpret_cast<const char*>(&data.serverCount), sizeof(data.serverCount));
    
    for (const auto& server : servers) {
        result.append(reinterpret_cast<const char*>(&server), sizeof(ServerInfo));
    }
    
    return result;
}

ListResponseData MessageProtocol::deserializeListResponse(const QByteArray& data)
{
    ListResponseData listData;
    
    if (data.size() < sizeof(quint32)) {
        Logger::instance()->error("Invalid list response data size", "MessageProtocol");
        return listData;
    }
    
    // 读取服务器数量
    memcpy(&listData.serverCount, data.constData(), sizeof(quint32));
    
    // 验证数据大小
    quint32 expectedSize = sizeof(quint32) + listData.serverCount * sizeof(ServerInfo);
    if (static_cast<quint32>(data.size()) != expectedSize) {
        Logger::instance()->error("Invalid server list data size", "MessageProtocol");
        listData.serverCount = 0;
        return listData;
    }
    
    // 读取服务器信息
    const char* ptr = data.constData() + sizeof(quint32);
    for (quint32 i = 0; i < listData.serverCount && i < 100; ++i) {
        ServerInfo server;
        memcpy(&server, ptr, sizeof(ServerInfo));
        listData.servers.append(server);
        ptr += sizeof(ServerInfo);
    }
    
    return listData;
}

QByteArray MessageProtocol::serializeReportRequest(const QString& location, const QList<LatencyRecord>& records)
{
    ReportRequestData data;
    
    // 设置位置信息
    data.location = location;
    data.locationLength = static_cast<quint32>(location.toUtf8().size());
    data.recordCount = static_cast<quint32>(records.size());
    data.records = records;
    
    QByteArray result;
    
    // 序列化位置长度
    result.append(reinterpret_cast<const char*>(&data.locationLength), sizeof(data.locationLength));
    
    // 序列化位置信息
    QByteArray locationBytes = location.toUtf8();
    result.append(locationBytes);
    
    // 序列化记录数量
    result.append(reinterpret_cast<const char*>(&data.recordCount), sizeof(data.recordCount));
    
    // 序列化延时记录
    for (const auto& record : records) {
        result.append(reinterpret_cast<const char*>(&record), sizeof(LatencyRecord));
    }
    
    return result;
}

ReportRequestData MessageProtocol::deserializeReportRequest(const QByteArray& data)
{
    ReportRequestData reportData;
    
    if (data.size() < sizeof(quint32) + sizeof(reportData.recordCount)) {
        Logger::instance()->error("Invalid report request data size", "MessageProtocol");
        return reportData;
    }
    
    const char* ptr = data.constData();
    
    // 读取位置信息长度
    memcpy(&reportData.locationLength, ptr, sizeof(reportData.locationLength));
    ptr += sizeof(reportData.locationLength);
    
    // 验证数据大小
    if (data.size() < sizeof(quint32) + reportData.locationLength + sizeof(reportData.recordCount)) {
        Logger::instance()->error("Invalid report request data size for location", "MessageProtocol");
        return reportData;
    }
    
    // 读取位置信息
    reportData.location = QString::fromUtf8(ptr, reportData.locationLength);
    ptr += reportData.locationLength;
    
    // 验证位置长度
    if (reportData.location.isEmpty() || reportData.location.length() > 64) {
        Logger::instance()->error("Invalid location length", "MessageProtocol");
        return reportData;
    }
    
    // 读取记录数量
    memcpy(&reportData.recordCount, ptr, sizeof(reportData.recordCount));
    ptr += sizeof(reportData.recordCount);
    
    // 验证数据大小
    quint32 expectedSize = sizeof(quint32) + reportData.locationLength + sizeof(reportData.recordCount) + 
                          reportData.recordCount * sizeof(LatencyRecord);
    if (static_cast<quint32>(data.size()) != expectedSize) {
        Logger::instance()->error("Invalid report request data size for records", "MessageProtocol");
        return reportData;
    }
    
    // 读取延时记录
    for (quint32 i = 0; i < reportData.recordCount; ++i) {
        LatencyRecord record;
        memcpy(&record, ptr, sizeof(LatencyRecord));
        reportData.records.append(record);
        ptr += sizeof(LatencyRecord);
    }
    
    return reportData;
}

QByteArray MessageProtocol::createSimpleResponse(MessageType type, ErrorCode errorCode)
{
    MessageHeader header(type, sizeof(quint32));
    
    QByteArray result;
    result.append(serializeHeader(header));
    
    quint32 code = static_cast<quint32>(errorCode);
    result.append(reinterpret_cast<const char*>(&code), sizeof(code));
    
    return result;
}

bool MessageProtocol::validateHeader(const MessageHeader& header)
{
    // 验证消息类型
    if (header.msgType < static_cast<quint32>(MessageType::LOGIN_REQUEST) ||
        header.msgType > static_cast<quint32>(MessageType::REPORT_FAIL)) {
        return false;
    }
    
    // 验证数据长度（最大10MB）
    if (header.dataLength > 10485760) {
        return false;
    }
    
    return true;
}

QString MessageProtocol::getMessageTypeString(MessageType type)
{
    switch (type) {
        case MessageType::LOGIN_REQUEST: return "LOGIN_REQUEST";
        case MessageType::LOGIN_OK: return "LOGIN_OK";
        case MessageType::LOGIN_FAIL: return "LOGIN_FAIL";
        case MessageType::LIST_REQUEST: return "LIST_REQUEST";
        case MessageType::LIST_RESPONSE: return "LIST_RESPONSE";
        case MessageType::REPORT_REQUEST: return "REPORT_REQUEST";
        case MessageType::REPORT_OK: return "REPORT_OK";
        case MessageType::REPORT_FAIL: return "REPORT_FAIL";
        default: return "UNKNOWN";
    }
}

quint32 MessageProtocol::hostToNetwork(quint32 value)
{
    return qToBigEndian(value);
}

quint32 MessageProtocol::networkToHost(quint32 value)
{
    return qFromBigEndian(value);
}