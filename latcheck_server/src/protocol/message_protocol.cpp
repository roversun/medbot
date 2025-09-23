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
QByteArray MessageProtocol::serializeHeader(const MessageHeader &header)
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
    if (data.size() < 8)
    {
        Logger::instance()->error("MessageProtocol", "Invalid header size");
        return header;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> header.msgType >> header.dataLength;

    return header;
}

LoginRequestData MessageProtocol::deserializeLoginRequest(const QByteArray &data)
{
    LoginRequestData loginData;

    if (data.size() != sizeof(LoginRequestData))
    {
        Logger::instance()->error("Invalid login request data size");
        return loginData;
    }

    memcpy(&loginData, data.constData(), sizeof(LoginRequestData));

    // 确保字符串以null结尾
    loginData.userName[sizeof(loginData.userName) - 1] = '\0';
    loginData.password[sizeof(loginData.password) - 1] = '\0';

    return loginData;
}

QByteArray MessageProtocol::serializeListResponse(const QList<ServerInfo> &servers)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 写入服务器数量
    stream << static_cast<quint32>(servers.size());

    // 写入每个服务器信息（只包含serverId和ipAddr）
    for (const ServerInfo &server : servers)
    {
        stream << server.serverId;
        stream << server.ipAddr;
    }

    return data;
}

ListResponseData MessageProtocol::deserializeListResponse(const QByteArray &data)
{
    ListResponseData response;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    // 读取服务器数量
    stream >> response.serverCount;

    // 读取每个服务器信息（只包含serverId和ipAddr）
    for (quint32 i = 0; i < response.serverCount; ++i)
    {
        ServerInfo server;
        stream >> server.serverId;
        stream >> server.ipAddr;

        response.servers.append(server);
    }

    return response;
}

QByteArray MessageProtocol::serializeReportRequest(const QString &location, const QList<LatencyRecord> &records)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // 写入固定128字节位置信息
    char locationBuffer[LOCATION_LEN] = {0}; // 初始化全部为0
    QByteArray locationBytes = location.toUtf8();
    // 复制位置信息，最多复制128字节
    qint64 copySize = qMin(static_cast<qint64>(locationBytes.size()), static_cast<qint64>(127)); // 留一个字节给结束符
    memcpy(locationBuffer, locationBytes.constData(), copySize);
    // 确保字符串以null结束
    locationBuffer[copySize] = '\0';

    // 写入128字节到流
    stream.writeRawData(locationBuffer, LOCATION_LEN);

    // 写入记录数量
    stream << static_cast<quint32>(records.size());

    // 写入每个延时记录
    for (const LatencyRecord &record : records)
    {
        stream << record.serverId << record.latency;
    }

    return data;
}
ReportRequestData MessageProtocol::deserializeReportRequest(const QByteArray &data)
{
    ReportRequestData reportData;
    // 检查数据长度是否足够（128字节位置信息 + 4字节记录数量 + 至少0条记录）
    if (data.size() < LOCATION_LEN + 4)
    {
        return reportData;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    // 读取128字节位置信息
    char locationBuffer[LOCATION_LEN] = {0};
    stream.readRawData(locationBuffer, sizeof(locationBuffer));
    // 复制到结构体的location数组中
    memcpy(reportData.location, locationBuffer, sizeof(locationBuffer));

    // 读取记录数量
    stream >> reportData.recordCount;

    // 安全检查：确保记录数量不会导致缓冲区溢出
    const quint32 maxAllowedRecords = 1000; // 根据实际需求调整此值
    if (reportData.recordCount > maxAllowedRecords)
    {
        reportData.recordCount = 0;
        return reportData;
    }

    // 检查剩余数据是否足够解析所有记录
    quint32 expectedRecordBytes = reportData.recordCount * 8; // 每条记录8字节（serverId和latency各4字节）
    if (stream.device()->bytesAvailable() < expectedRecordBytes)
    {
        reportData.recordCount = 0;
        return reportData;
    }

    // 读取延时记录
    for (quint32 i = 0; i < reportData.recordCount; ++i)
    {
        LatencyRecord record;
        stream >> record.serverId >> record.latency;
        reportData.records.append(record);
    }

    return reportData;
}

bool MessageProtocol::validateHeader(const MessageHeader &header)
{
    // 验证消息类型
    if (header.msgType < static_cast<quint32>(MessageType::LOGIN_REQUEST) ||
        header.msgType > static_cast<quint32>(MessageType::REPORT_FAIL))
    {
        return false;
    }

    // 验证数据长度（最大10MB）
    if (header.dataLength > 10485760)
    {
        return false;
    }

    return true;
}

QString MessageProtocol::getMessageTypeString(MessageType type)
{
    switch (type)
    {
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