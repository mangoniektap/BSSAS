/**
 * @file MainWindow_Background.qml
 * @brief 主窗口背景层。提供应用底色和内容展示区域。
 */
import QtQuick
import QtQuick.Controls
import BSSAS

Item {
    anchors.fill: parent
    // 暴露精确到具体参数
    property alias display_area: right_bottom_rectangle
    readonly property bool compactWindow: Constants.isCompactContent(width, height)
    readonly property int contentRightMargin: compactWindow ? 24 : 40
    readonly property int contentBottomMargin: compactWindow ? 24 : 40
    readonly property real contentTopRatio: compactWindow ? 0.075 : 0.1
    readonly property int contentRadius: compactWindow ? 32 : 50
    
    Rectangle {
        id: mainwindow_background
        anchors.fill: parent
        color: Theme.appBg
        radius:25
    }
    Rectangle {
        id: right_bottom_rectangle
        width: Math.floor(parent.width * 0.775)
        anchors{
            top: parent.top
            topMargin: parent.height * contentTopRatio
            bottom: parent.bottom
            bottomMargin: contentBottomMargin
            right: parent.right
            rightMargin: contentRightMargin
        }
        color: Theme.contentBg
        radius: contentRadius
        border.width: 1
        border.color: Theme.border
    }
}
