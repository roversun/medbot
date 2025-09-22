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
        Logger::instance()->error("Invalid login request data size", "MessageProtocol");
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
    ReportRequestData data;

    // 设置位置信息
    data.location = location;
    data.locationLength = static_cast<quint32>(location.toUtf8().size());
    data.recordCount = static_cast<quint32>(records.size());
    data.records = records;

    QByteArray result;

    // 序列化位置长度
    result.append(reinterpret_cast<const char *>(&data.locationLength), sizeof(data.locationLength));

    // 序列化位置信息
    QByteArray locationBytes = location.toUtf8();
    result.append(locationBytes);

    // 序列化记录数量
    result.append(reinterpret_cast<const char *>(&data.recordCount), sizeof(data.recordCount));

    // 序列化延时记录
    for (const auto &record : records)
    {
        result.append(reinterpret_cast<const char *>(&record), sizeof(LatencyRecord));
    }

    return result;
}

ReportRequestData MessageProtocol::deserializeReportRequest(const QByteArray &data)
{
    ReportRequestData reportData;

    // 修复第160行的警告
    if (static_cast<size_t>(data.size()) < sizeof(quint32) + sizeof(reportData.recordCount))
    {
        Logger::instance()->error("Invalid report request data size", "MessageProtocol");
        return reportData;
    }

    const char *ptr = data.constData();

    // 读取位置信息长度
    memcpy(&reportData.locationLength, ptr, sizeof(reportData.locationLength));
    ptr += sizeof(reportData.locationLength);

    // 验证数据大小
    if (static_cast<size_t>(data.size()) < sizeof(quint32) + reportData.locationLength + sizeof(reportData.recordCount))
    {
        // 添加详细日志信息以帮助调试
        quint32 expectedSize = sizeof(quint32) + reportData.locationLength + sizeof(reportData.recordCount);
        Logger::instance()->error(QString("Invalid report request data size: received %1 bytes, expected at least %2 bytes (locationLength=%3, recordCount field size=%4)").arg(data.size()).arg(expectedSize).arg(reportData.locationLength).arg(sizeof(reportData.recordCount)), "MessageProtocol");
        return reportData;
    }

    // 读取位置信息
    reportData.location = QString::fromUtf8(ptr, reportData.locationLength);
    ptr += reportData.locationLength;

    // 验证位置长度
    if (reportData.location.isEmpty() || reportData.location.length() > 64)
    {
        Logger::instance()->error("Invalid location length", "MessageProtocol");
        return reportData;
    }

    // 读取记录数量
    memcpy(&reportData.recordCount, ptr, sizeof(reportData.recordCount));
    ptr += sizeof(reportData.recordCount);

    // 验证数据大小
    quint32 expectedSize = sizeof(quint32) + reportData.locationLength + sizeof(reportData.recordCount) +
                           reportData.recordCount * sizeof(LatencyRecord);
    if (static_cast<quint32>(data.size()) != expectedSize)
    {
        Logger::instance()->error("Invalid report request data size for records", "MessageProtocol");
        return reportData;
    }

    // 读取延时记录
    for (quint32 i = 0; i < reportData.recordCount; ++i)
    {
        LatencyRecord record;
        memcpy(&record, ptr, sizeof(LatencyRecord));
        reportData.records.append(record);
        ptr += sizeof(LatencyRecord);
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