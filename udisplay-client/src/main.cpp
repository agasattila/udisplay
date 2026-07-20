// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "DeviceController.h"
#include "DiscoveryModel.h"
#include "DeviceInfo.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#ifdef HAVE_BLE
#include <QBluetoothDeviceInfo>
#endif

#ifdef Q_OS_ANDROID
#include <QBluetoothPermission>
#include <QLocationPermission>
#endif

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
#ifdef HAVE_BLE
    qRegisterMetaType<QBluetoothDeviceInfo>("QBluetoothDeviceInfo");
#endif
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    app.setOrganizationName(QStringLiteral("uDisplay"));
    app.setApplicationName(QStringLiteral("uDisplay Client"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

#ifdef Q_OS_ANDROID
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Precise);
    qApp->requestPermission(locationPermission, [](const QPermission &permission) {
        if (permission.status() == Qt::PermissionStatus::Granted) {
                //ok
            }
        });
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("uDisplay Client"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption designOpt(
        QStringLiteral("design"),
        QStringLiteral("Design mode: watch YAML file and live-preview the UI."),
        QStringLiteral("file"));
    parser.addOption(designOpt);
    QCommandLineOption debugOpt(
        QStringLiteral("debug"),
        QStringLiteral("Print the fully-resolved widget model after every parse "
                        "(works in both -design and real device mode)."));
    parser.addOption(debugOpt);
    parser.process(app);

    DeviceController controller;
    DiscoveryModel   discoveryModel;

    if (parser.isSet(debugOpt))
        controller.setDebugMode(true);

    if (parser.isSet(designOpt))
        controller.startDesignMode(parser.value(designOpt));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(
        QStringLiteral("discoveryModel"), &discoveryModel);

    const QUrl url(QStringLiteral("qrc:/UDisplay/qml/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);
    return app.exec();
}
