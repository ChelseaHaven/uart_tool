#include "application/applicationservices.h"
#include "presentation/logdocumentsettings.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QDebug>

int main(int argc, char *argv[]) {
#ifdef Q_OS_LINUX
    // Prefer XWayland when it is available, avoiding Wayland text-input focus
    // warnings on this desktop. An explicitly selected platform takes priority.
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("DISPLAY")
        && (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") || qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland"))
        qputenv("QT_QPA_PLATFORM","xcb;wayland");
#endif
    QGuiApplication app(argc,argv);
    app.setApplicationName("UART Tool"); app.setOrganizationName("UartTool");
    QQuickStyle::setStyle("Basic");
    registerUartUiTypes();
    ApplicationServices services;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("numericStream", &services.numericStream());
    engine.rootContext()->setContextProperty("appSettings", &services.settings());
    engine.rootContext()->setContextProperty("serialConnection", &services.serialConnection());
    engine.rootContext()->setContextProperty("sendSession", &services.sendSession());
    engine.rootContext()->setContextProperty("receiveSession", &services.receiveSession());
    engine.rootContext()->setContextProperty("protocolSession", &services.protocolSession());
    engine.rootContext()->setContextProperty("csvRecorder", &services.recorder());
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{ QCoreApplication::exit(1); },Qt::QueuedConnection);
    engine.loadFromModule("uart_tool","Main");
    if(engine.rootObjects().isEmpty()) return 1;
    if(app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(1500,&app,[&]{
            auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            const int i=app.arguments().indexOf("--snapshot");
            if(i>=0 && i+1<app.arguments().size()) {
                if(!window || !window->grabWindow().save(app.arguments().at(i+1))) { app.exit(2); return; }
            }
            qInfo()<<"QML startup passed; platform:"<<QGuiApplication::platformName()<<"available ports:"<<services.serialConnection().ports().size(); app.exit(0);
        });
    }
    return app.exec();
}
