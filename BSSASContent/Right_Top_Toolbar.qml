/**
 * @file Right_Top_Toolbar.qml
 * @brief 右上角工具栏。提供设置、最小化、全屏和关闭按钮，支持悬浮动画效果。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import BSSAS
Item{
    id: root
    anchors.fill: parent
    signal minimizeClicked()
    signal fullscreenClicked()
    signal closeClicked()
    signal settingClicked()

    Item{
        id: close
        anchors{
            top: parent.top
            topMargin: 10
            right: parent.right
            rightMargin: 17
        }
        width: 40
        height: 40
        HoverHandler {
            id: close_hover
            target: close
        }
        Image {
            id: closeIMAGE
            anchors.centerIn: parent
            fillMode: Image.PreserveAspectFit
            source: "images/close.png"
            visible: false
        }

        MultiEffect {
            id: close_effect
            anchors.fill: parent
            source: closeIMAGE
            transformOrigin: Item.Center

            colorization: close_hover.hovered ? 0.45 : 0.0
            colorizationColor: Theme.textWhite

            brightness: close_hover.hovered ? 0.1 : 0.0
            contrast: close_hover.hovered ? 1.1 : 1.0

            Behavior on colorization { NumberAnimation { duration: 150 } }
            Behavior on brightness   { NumberAnimation { duration: 150 } }
            Behavior on contrast     { NumberAnimation { duration: 150 } }

            scale: (close_hover.hovered ? 1.2 : 1.0)
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        TapHandler {
        onTapped: root.closeClicked()
        }
    }

    Item{
        id: full_screen
        anchors{
                top: parent.top
                topMargin: 10
                right: close.left
                rightMargin: 12
            }
        width: 35
        height: 35
        HoverHandler {
            id: full_screen_hover
            target: full_screen
        }
        Image {
            id: full_screenIMAGE
            anchors.centerIn: parent

            fillMode: Image.PreserveAspectFit
            source: "images/full_screen.png"
            visible: false
        }

        MultiEffect {
            id: full_screen_effect
            anchors.fill: parent
            source: full_screenIMAGE
            transformOrigin: Item.Center

            colorization: full_screen_hover.hovered ? 0.45 : 0.0
            colorizationColor: Theme.textWhite

            brightness: full_screen_hover.hovered ? 0.1 : 0.0
            contrast: full_screen_hover.hovered ? 1.1 : 1.0

            Behavior on colorization { NumberAnimation { duration: 150 } }
            Behavior on brightness   { NumberAnimation { duration: 150 } }
            Behavior on contrast     { NumberAnimation { duration: 150 } }

            scale: (full_screen_hover.hovered ? 1.2 : 1.0)
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        TapHandler {
        onTapped: root.fullscreenClicked()
        }
    }

    Item{
        id: minimize
         anchors{
                top: parent.top
                topMargin: 10
                right: full_screen.left
                rightMargin: 15
            }
        width: 35
        height: 35
        HoverHandler {
            id: minimize_hover
            target: minimize
        }
        Image {
            id: minimizeIMAGE
            anchors.centerIn: parent
            fillMode: Image.PreserveAspectFit
            source: "images/minus.png"
            visible: false
        }

        MultiEffect {
            id: minimize_effect
            anchors.fill: parent
            source: minimizeIMAGE
            transformOrigin: Item.Center

            colorization: minimize_hover.hovered ? 0.45 : 0.0
            colorizationColor: Theme.textWhite

            brightness: minimize_hover.hovered ? 0.1 : 0.0
            contrast: minimize_hover.hovered ? 1.1 : 1.0

            Behavior on colorization { NumberAnimation { duration: 150 } }
            Behavior on brightness   { NumberAnimation { duration: 150 } }
            Behavior on contrast     { NumberAnimation { duration: 150 } }

            scale: (minimize_hover.hovered ? 1.2 : 1.0)
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        TapHandler {
        onTapped: root.minimizeClicked()
        }
    }   

    Rectangle {
        id: seperator01
        anchors{
            top: parent.top
            topMargin: 12.5
            right: minimize.left
            rightMargin: 20
        }
        width: 2
        height: 35
        color: Theme.textDisabled
        opacity: 0.3
    }

    Item{
        id: setting
        anchors{
            top: parent.top
            topMargin: 10
            right: seperator01.left
            rightMargin: 15
        }
        width: 35
        height: 35
        HoverHandler {
            id: setting_hover
            target: setting
        }
        Image {
            id: settingIMAGE
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: parent.horizontalCenter
            fillMode: Image.PreserveAspectFit
            source: "images/setting.png"
            visible: false

            smooth:true
            mipmap:true
        }

        MultiEffect {
            id: setting_effect
            anchors.fill: parent
            source: settingIMAGE
            transformOrigin: Item.Center


            colorization: setting_hover.hovered ? 0.45 : 0.0
            colorizationColor: Theme.textWhite

            brightness: setting_hover.hovered ? 0.1 : 0.0
            contrast: setting_hover.hovered ? 1.1 : 1.0

            Behavior on colorization { NumberAnimation { duration: 150 } }
            Behavior on brightness   { NumberAnimation { duration: 150 } }
            Behavior on contrast     { NumberAnimation { duration: 150 } }

            scale: (setting_hover.hovered ? 1.2 : 1.0)
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        TapHandler {
            onTapped: root.settingClicked()
        }
    }
}
