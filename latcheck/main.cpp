#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "networkmanager.h"
#include "configmanager.h"
#include "latencychecker.h"
#include "logger.h"
#include "locationservice.h"

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));

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
    NetworkManager networkManager;
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
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
        
    // Load QML AFTER setting context properties
    engine.loadFromModule("latcheck", "Main");

    return app.exec();
}
