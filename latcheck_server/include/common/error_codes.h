#ifndef ERRORCODES_H
#define ERRORCODES_H

// 系统错误码
enum class ErrorCode : int
{
    Success = 0,

    // 用户相关错误 1000-1999
    InvalidUser = 1001,
    UserDisabled = 1002,
    PermissionDenied = 1003,
    InvalidPassword = 1004,
    UserExists = 1005,
    UserNotFound = 1006,
    PasswordTooShort = 1007,  // 密码太短
    PasswordTooSimple = 1008, // 密码太简单
    PasswordSameAsOld = 1009, // 新密码与旧密码相同

    // 数据库相关错误 2000-2999
    DatabaseError = 2001,
    ConnectionFailed = 2002,
    QueryFailed = 2003,
    TransactionFailed = 2004,

    // 数据格式错误 3000-3999
    InvalidData = 3001,
    InvalidJson = 3002,
    MissingParameter = 3003,
    InvalidParameter = 3004,

    // 网络相关错误 4000-4999
    NetworkError = 4001,
    ConnectionTimeout = 4002,
    TlsError = 4003,
    HttpError = 4004,

    // 服务器内部错误 5000-5999
    ServerInternal = 5001,
    ConfigError = 5002,
    LogError = 5003,
    SecurityError = 5004
};

// 错误码转字符串
const char *errorCodeToString(ErrorCode code);

#endif // ERRORCODES_H
