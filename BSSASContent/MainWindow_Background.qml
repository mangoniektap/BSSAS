import QtQuick
import QtQuick.Controls
import BSSAS

Item {
    anchors.fill: parent
    //暴露精确到具体参数
    property alias display_area: right_bottom_rectangle
    
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
            topMargin: parent.height * 0.1
            bottom: parent.bottom
            bottomMargin: 40
            right: parent.right
            rightMargin: 40
        }
        color: Theme.contentBg
        radius:50
        border.width: 1
        border.color: Theme.border
    }
}
