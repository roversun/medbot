#include <QGuiApplication>
// 首先包含Windows特定头文件
#ifdef Q_OS_WIN
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

// 然后再包含其他Qt和自定义头文件
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "networkmanager.h"
#include "configmanager.h"
#include "latencychecker.h"
#include "logger.h"
#include "locationservice.h"
#include <QDir>
#include <QLoggingCategory>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 初始化Winsock（应用程序启动时一次性初始化）
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0)
    {
        qDebug() << "WSAStartup failed with error:" << wsaResult;
        return -1;
    }
#endif

    // 设置虚拟键盘环境变量
    qputenv("QT_IM_MODULE", "qtvirtualkeyboard");
    // 首先设置默认语言
    qputenv("QT_VIRTUALKEYBOARD_DEFAULT_LOCALE", "en_US");
    qputenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE", "1");
    qputenv("QT_VIRTUALKEYBOARD_AVAILABLE_LOCALES", "en_US zh_CN");
    qputenv("QT_VIRTUALKEYBOARD_ACTIVE_LOCALES", "en_US zh_CN");
    qputenv("QT_VIRTUALKEYBOARD_STYLE", "default");
    qputenv("QT_VIRTUALKEYBOARD_LAYOUT_PATH", ":/qt-project.org/imports/QtQuick/VirtualKeyboard/Layouts");
    qputenv("QT_VIRTUALKEYBOARD_HEIGHT_RATIO", "0.3");

    QGuiApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("LatCheck");
    app.setApplicationVersion("2.0");
    app.setOrganizationName("LatCheck");

    // Register QML types
    qmlRegisterType<NetworkManager>("LatCheck", 2, 0, "NetworkManager");
    qmlRegisterType<ConfigManager>("LatCheck", 2, 0, "ConfigManager");
    qmlRegisterType<LatencyChecker>("LatCheck", 2, 0, "LatencyChecker");
    qmlRegisterType<Logger>("LatCheck", 2, 0, "Logger");
    qmlRegisterType<LocationService>("LatCheck", 2, 0, "LocationService");

    // Create and expose global instances BEFORE creating engine
    ConfigManager configManager;
    NetworkManager networkManager(nullptr, &configManager); // 传递configManager参数
    LatencyChecker latencyChecker;
    LocationService locationService;
    Logger logger;

    // Set up connections between objects
    locationService.setLogger(&logger);

    QQmlApplicationEngine engine;

    // Register singletons
    qmlRegisterSingletonInstance("LatCheck", 2, 0, "LocationService", &locationService);
    qmlRegisterSingletonInstance("LatCheck", 2, 0, "Logger", &logger);

    // Set context properties BEFORE loading QML
    engine.rootContext()->setContextProperty("configManager", &configManager);
    engine.rootContext()->setContextProperty("networkManager", &networkManager);
    engine.rootContext()->setContextProperty("latencyChecker", &latencyChecker);
    engine.rootContext()->setContextProperty("logger", &logger);
    engine.rootContext()->setContextProperty("locationService", &locationService);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []()
        { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // Load QML AFTER setting context properties
    engine.loadFromModule("latcheck", "Main");

    int result = app.exec();

#ifdef Q_OS_WIN
    // 应用程序退出时清理
    WSACleanup();
#endif

    return result;
}
