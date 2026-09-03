#include "app/ApplicationController.h"
#include "app/SingleInstanceCoordinator.h"
#include "BuildConfig.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QWindow>
#include <QtCore/QDebug>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtCore/QUrl>

#include <utility>

namespace {

bool traceStartupEnabled()
{
    return qEnvironmentVariableIntValue("CATCLICKER_TRACE_STARTUP") == 1;
}

template<typename... Args>
void traceStartup(Args &&...args)
{
    if (traceStartupEnabled()) {
        QDebug output = qInfo();
        output << "[startup]";
        (output << ... << std::forward<Args>(args));
    }
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    traceStartup("QApplication created");
    app.setOrganizationName(QStringLiteral("CatClicker"));
    app.setApplicationName(QStringLiteral("CatClicker"));
    app.setApplicationVersion(QStringLiteral(CATCLICKER_VERSION));
    app.setDesktopFileName(QStringLiteral("catclicker"));
    app.setWindowIcon(QIcon(QStringLiteral(":/CatClicker/branding/catclicker.png")));

    CatClicker::SingleInstanceCoordinator instanceCoordinator;
    const auto instanceResult = instanceCoordinator.start();
    if (instanceResult == CatClicker::SingleInstanceCoordinator::StartResult::Secondary) {
        return 0;
    }
    if (instanceResult == CatClicker::SingleInstanceCoordinator::StartResult::Error) {
        qCritical() << "Could not establish the CatClicker single-instance endpoint:"
                    << instanceCoordinator.errorString();
        return 1;
    }

    qmlRegisterType<CatClicker::ApplicationController>("CatClicker", 1, 0, "ApplicationController");

    traceStartup("constructing ApplicationController");
    CatClicker::ApplicationController controller;
    traceStartup("ApplicationController constructed");

    traceStartup("constructing QQmlApplicationEngine");
    QQmlApplicationEngine engine;
    traceStartup("QQmlApplicationEngine constructed");
    traceStartup("adding qrc import path");
    engine.addImportPath(QStringLiteral("qrc:/"));
    traceStartup("qrc import path added");
    traceStartup("setting appController context property");
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    traceStartup("context property set");
    const QUrl mainQmlUrl(QStringLiteral("qrc:/CatClicker/qml/Main.qml"));
    traceStartup("BEFORE engine.load", mainQmlUrl);
    engine.load(mainQmlUrl);
    traceStartup("AFTER engine.load");
    traceStartup("root object count:", engine.rootObjects().size());

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "[startup] no QML root object";
        return 1;
    }

    if (QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(&instanceCoordinator, &CatClicker::SingleInstanceCoordinator::activationRequested,
                         window, [window]() {
                             if (!window->isVisible()) window->show();
                             window->requestActivate();
                         });
    }

    traceStartup("entering app.exec");
    const int result = app.exec();
    traceStartup("app.exec returned", result);
    return result;
}
