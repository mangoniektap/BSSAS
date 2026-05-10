#ifndef APP_ENVIRONMENT_H
#define APP_ENVIRONMENT_H

#include <QByteArray>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(BSSASPlugin)
Q_IMPORT_QML_PLUGIN(BSSASContentPlugin)
Q_IMPORT_QML_PLUGIN(MangoComponentPlugin)
Q_IMPORT_QML_PLUGIN(BSSASSettingsStoragePlugin)

inline constexpr char mainQmlFile[] = "qrc:/qt/qml/BSSASContent/App.qml";

inline void set_qt_environment()
{
    qputenv("QML_COMPAT_RESOLVE_URLS_ON_ASSIGNMENT", QByteArrayLiteral("1"));
    qputenv("QT_ENABLE_HIGHDPI_SCALING", QByteArrayLiteral("0"));
    qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.qml.connections=false"));
    qputenv("QT_QUICK_CONTROLS_CONF", QByteArrayLiteral(":/qtquickcontrols2.conf"));
}

#endif // APP_ENVIRONMENT_H
