// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

/** @file main.cpp
 *  @brief BSSAS 应用程序入口，负责初始化 QML 引擎、注册 C++ 后端对象到 QML 上下文并启动事件循环。
 */

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "environment.h"

#include "AdaptiveDownsampling.h"
#include "DebugTerminalManager.h"
#include "DaqDeviceManager.h"
#include "DataManager.h"
#include "DatabaseManager.h"
#include "GenerateManager.h"
#include "LocalizationIntestinalSound.h"
#include "Multi_featureJointDetection.h"
#include "RecognitionServiceManager.h"
#include "SignalDFTCalculation.h"
#include "SignalPreprocessing.h"
#include "SystemStatusMonitor.h"
#include "UpdateManager.h"
#include "WAVHandle.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("MANGONIEKTAP");
    QCoreApplication::setApplicationName("BSSASApp");
    QCoreApplication::setApplicationVersion(QStringLiteral(BSSAS_APP_VERSION_STR));

    set_qt_environment();
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/AppIcon.ico"));

    DatabaseManager* databaseManager = DatabaseManager::instance();
    databaseManager->initializeSearchDatabase();

    GenerateManager generateManager(&app);
    AdaptiveDownsampling adaptiveDownsampling;
    SignalDFTCalculation signalDFTCalculation;
    RecognitionServiceManager recognitionServiceManager(&app);
    UpdateManager updateManager(&app);
    DebugTerminalManager debugTerminalManager(&app);
    SystemStatusMonitor systemStatusMonitor(&app);

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("daqManager", DaqDeviceManager::instance());
    engine.rootContext()->setContextProperty("wavHandle", WAVHandle::instance());
    engine.rootContext()->setContextProperty(
        "signalPreprocessing",
        SignalPreprocessing::instance());
    engine.rootContext()->setContextProperty("dataManager", DataManager::instance());
    engine.rootContext()->setContextProperty("databaseManager", databaseManager);
    engine.rootContext()->setContextProperty(
        "multiFeatureJointDetection",
        Multi_featureJointDetection::instance());
    engine.rootContext()->setContextProperty(
        "localizationIntestinalSound",
        LocalizationIntestinalSound::instance());
    engine.rootContext()->setContextProperty("generateManager", &generateManager);
    engine.rootContext()->setContextProperty("adaptiveDownsampling", &adaptiveDownsampling);
    engine.rootContext()->setContextProperty("signalDFTCalculation", &signalDFTCalculation);
    engine.rootContext()->setContextProperty(
        "recognitionServiceManager",
        &recognitionServiceManager);
    engine.rootContext()->setContextProperty("updateManager", &updateManager);
    engine.rootContext()->setContextProperty("debugTerminalManager", &debugTerminalManager);
    engine.rootContext()->setContextProperty("systemStatusMonitor", &systemStatusMonitor);

    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &debugTerminalManager,
        &DebugTerminalManager::shutdown,
        Qt::DirectConnection);

    const QUrl url(mainQmlFile);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.addImportPath(":/");
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
