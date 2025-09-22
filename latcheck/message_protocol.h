#ifndef MESSAGEPROTOCOL_H
#define MESSAGEPROTOCOL_H

#include <QtGlobal>
#include <QByteArray>
#include <QString>
#include <QList>
#include "error_codes.h"

// 消息类型定义
enum class MessageType : quint32
{
    LOGIN_REQUEST = 0x0001,  // 登录请求
    LOGIN_OK = 0x0002,       // 登录成功
    LOGIN_FAIL = 0x0003,     // 登录失败
    LIST_REQUEST = 0x0004,   // 服务器列表请求
    LIST_RESPONSE = 0x0005,  // 服务器列表响应
    REPORT_REQUEST = 0x0006, // 报告上传请求
    REPORT_OK = 0x0007,      // 报告上传成功
    REPORT_FAIL = 0x0008     // 报告上传失败
};

// 消息头结构（8字节）
struct MessageHeader
{
    quint32 msgType;    // 消息类型
    quint32 dataLength; // 数据长度

    MessageHeader() : msgType(0), dataLength(0) {}
    MessageHeader(MessageType type, quint32 length)
        : msgType(static_cast<quint32>(type)), dataLength(length) {}
};

// 登录请求数据
struct LoginRequestData
{
    char userName[32];     // 用户名（32字节）
    char password[32]; // 密码（32字节）

    LoginRequestData()
    {
        memset(userName, 0, sizeof(userName));
        memset(password, 0, sizeof(password));
    }
};

// 服务器信息
struct ServerInfo
{
    quint32 serverId; // 服务器ID（4字节）
    quint32 ipAddr;   // IP地址（4字节）

    ServerInfo() : serverId(0), ipAddr(0) {}
    ServerInfo(quint32 id, quint32 ip) : serverId(id), ipAddr(ip) {}
};

// 服务器列表响应数据
struct ListResponseData
{
    quint32 serverCount;       // 服务器数量（4字节）
    QList<ServerInfo> servers; // 服务器列表

    ListResponseData() : serverCount(0) {}
};

// 延时记录
struct LatencyRecord
{
    quint32 serverId; // 服务器ID（4字节）
    quint32 latency;  // 延时值（4字节，毫秒）

    LatencyRecord() : serverId(0), latency(0) {}
    LatencyRecord(quint32 id, quint32 lat) : serverId(id), latency(lat) {}
};

// 报告上传请求数据
struct ReportRequestData
{
    quint32 locationLength;       // 位置信息长度（4字节）
    QString location;             // 位置信息（变长）
    quint32 recordCount;          // 记录数量（4字节）
    QList<LatencyRecord> records; // 延时记录列表

    ReportRequestData() : locationLength(0), recordCount(0) {}
};

// 消息协议处理类
class MessageProtocol
{
public:
    MessageProtocol();

    // 序列化消息头
    static QByteArray serializeHeader(const MessageHeader &header);

    // 反序列化消息头
    static MessageHeader deserializeHeader(const QByteArray &data);

    // 序列化登录请求
    static QByteArray serializeLoginRequest(const QString &userName, const QString &passwordHash);

    // 反序列化登录请求
    static LoginRequestData deserializeLoginRequest(const QByteArray &data);

    // 序列化服务器列表响应
    static QByteArray serializeListResponse(const QList<ServerInfo> &servers);

    // 反序列化服务器列表响应
    static ListResponseData deserializeListResponse(const QByteArray &data);

    // 序列化报告上传请求
    static QByteArray serializeReportRequest(const QString &location, const QList<LatencyRecord> &records);

    // 反序列化报告上传请求
    static ReportRequestData deserializeReportRequest(const QByteArray &data);

    // 验证消息头
    static bool validateHeader(const MessageHeader &header);

    // 获取消息类型字符串
    static QString getMessageTypeString(MessageType type);

};

#endif // MESSAGEPROTOCOL_H