/**
 * @file Constants.qml
 * @brief 全局常量定义单例。管理窗口尺寸、屏幕适配比例及版本号等运行时常量。
 */
pragma Singleton
import QtQuick
import QtQuick.Window

QtObject {
    readonly property real screenwidthratio: 0.765
    readonly property real screenaspectratio: 16 / 10
    readonly property int screenmargin: 20
    readonly property int width: Math.round(Math.min(Screen.width * screenwidthratio, Screen.width - screenmargin * 2, (Screen.height - screenmargin * 2) * screenaspectratio))
    readonly property int height: Math.round(width / screenaspectratio)
    readonly property real screenheightratio: Screen.height > 0 ? height / Screen.height : 0
    readonly property int x: Math.floor(Screen.width - width) / 2
    readonly property int y: Math.floor(Screen.height - height) / 2

    readonly property int referenceWindowWidth: 1928
    readonly property int referenceWindowHeight: 1205
    readonly property int minimumTargetWindowWidth: 1469
    readonly property int minimumTargetWindowHeight: 918
    readonly property int compactContentWidth: 1280
    readonly property int compactContentHeight: 860
    readonly property int narrowContentWidth: 1008

    function isCompactContent(contentWidth, contentHeight) {
        return contentWidth < compactContentWidth || contentHeight < compactContentHeight
    }

    function isNarrowContent(contentWidth) {
        return contentWidth < narrowContentWidth
    }

    readonly property int pageCacheRetentionMs: 10 * 60 * 1000
    readonly property string version: "26.05.20"
}
