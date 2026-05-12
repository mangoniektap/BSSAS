/** @file environment.h
 *  @brief 应用程序环境初始化 —— 导入 QML 插件并设置 Qt 运行时环境变量
 *
 *  本模块在 main() 早期被调用，负责声明 QML 插件的导入关系并为 Qt
 *  运行时配置必要的环境变量（高 DPI 缩放、日志规则、控件样式等）。
 */

#ifndef APP_ENVIRONMENT_H
#define APP_ENVIRONMENT_H

#include <QByteArray>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(BSSASPlugin)
Q_IMPORT_QML_PLUGIN(BSSASContentPlugin)
Q_IMPORT_QML_PLUGIN(MangoComponentPlugin)
Q_IMPORT_QML_PLUGIN(BSSASSettingsStoragePlugin)

/** @brief 应用程序主 QML 文件路径 */
inline constexpr char mainQmlFile[] = "qrc:/qt/qml/BSSASContent/App.qml";

/** @brief 设置 Qt 运行时环境变量
 *
 *  配置内容包括：
 *  - QML URL 解析策略
 *  - 高 DPI 缩放行为
 *  - 日志过滤规则
 *  - Quick Controls 2 配置文件路径
 */
inline void set_qt_environment()
{
    qputenv("QML_COMPAT_RESOLVE_URLS_ON_ASSIGNMENT", QByteArrayLiteral("1"));
    qputenv("QT_ENABLE_HIGHDPI_SCALING", QByteArrayLiteral("0"));
    qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.qml.connections=false"));
    qputenv("QT_QUICK_CONTROLS_CONF", QByteArrayLiteral(":/qtquickcontrols2.conf"));
}

#endif // APP_ENVIRONMENT_H
