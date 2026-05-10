import QtQuick
import QtQuick.Controls
import BSSAS
import MangoComponent

Item {
    id: root
    anchors.fill: parent
    visible: root.opened || root.closing
    enabled: visible
    focus: visible

    property bool opened: false
    property bool closing: false
    property bool startupSettled: false
    property real edgeMargin: 18
    property real panelRadius: 50
    property int panelAnimationDuration: 280
    property int backdropAnimationDuration: 220
    readonly property int closeVisibilityDelay: Math.max(panelAnimationDuration, backdropAnimationDuration) + 40

    readonly property real panelWidth: Math.max(0, Math.min(root.width - root.edgeMargin, Math.min(420, Math.max(320, root.width * 0.32))))
    readonly property bool darkMode: {
        const color = Qt.color(systemPalette.window)
        return (0.299 * color.r + 0.587 * color.g + 0.114 * color.b) < 0.5
    }
    readonly property color backdropColor: withOpacity(systemPalette.shadow, darkMode ? 0.44 : 0.22)
    readonly property color panelTopColor: withOpacity(systemPalette.window, darkMode ? 0.985 : 0.998)
    readonly property color panelBottomColor: withOpacity(systemPalette.base, darkMode ? 0.94 : 0.97)
    readonly property color cardColor: withOpacity(systemPalette.base, darkMode ? 0.84 : 0.90)
    readonly property color badgeColor: withOpacity(systemPalette.highlight, darkMode ? 0.20 : 0.14)
    readonly property color primaryTextColor: systemPalette.windowText
    readonly property color secondaryTextColor: withOpacity(systemPalette.windowText, darkMode ? 0.72 : 0.60)
    readonly property color dividerColor: withOpacity(systemPalette.mid, darkMode ? 0.42 : 0.20)
    readonly property color accentColor: systemPalette.highlight
    readonly property string currentThemeText: darkMode ? "深色模式" : "浅色模式"

    signal closeRequested()

    function withOpacity(colorValue, alpha) {
        const color = Qt.color(colorValue)
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    Component.onCompleted: Qt.callLater(function() {
        root.startupSettled = true
    })

    onOpenedChanged: {
        closeVisibilityTimer.stop()

        if (opened) {
            closing = false
            return
        }

        if (startupSettled && visible) {
            closing = true
            closeVisibilityTimer.start()
        } else {
            closing = false
        }
    }

    SystemPalette {
        id: systemPalette
        colorGroup: SystemPalette.Active
    }

    ToastNotification {
        id: updateToast
        duration: 2400
        topMargin: 26
    }

    Timer {
        id: closeVisibilityTimer
        interval: root.closeVisibilityDelay
        repeat: false

        onTriggered: {
            if (!root.opened) {
                root.closing = false
            }
        }
    }

    Keys.onEscapePressed: function(event) {
        event.accepted = true
        root.closeRequested()
    }

    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: root.backdropColor
        opacity: root.opened ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: root.backdropAnimationDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.visible

        onClicked: function(mouse) {
            if (mouse.x < panelWrap.x) {
                root.closeRequested()
            }
        }
    }

    Item {
        id: panelWrap
        x: root.opened ? root.width - width - root.edgeMargin : root.width
        y: root.edgeMargin
        width: root.panelWidth
        height: root.height - root.edgeMargin * 2

        Behavior on x {
            enabled: root.startupSettled && (root.opened || root.closing)
            NumberAnimation {
                duration: root.panelAnimationDuration
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            id: panel
            anchors.fill: parent
            radius: root.panelRadius
            border.color: root.dividerColor
            border.width: 1
            clip: true

            gradient: Gradient {
                GradientStop { position: 0.0; color: root.panelTopColor }
                GradientStop { position: 1.0; color: root.panelBottomColor }
            }
        }

        Rectangle {
            width: 88
            height: 5
            radius: height / 2
            anchors.top: parent.top
            anchors.topMargin: 18
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.withOpacity(root.primaryTextColor, root.darkMode ? 0.16 : 0.10)
        }

        ScrollView {
            id: settingsScrollView
            anchors {
                top: parent.top
                topMargin: 48
                left: parent.left
                right: parent.right
                bottom: footerDivider.top
                leftMargin: 28
                rightMargin: 28
                bottomMargin: 18
            }
            clip: true
            contentWidth: availableWidth

            ScrollBar.horizontal: ScrollBar {
                policy: ScrollBar.AlwaysOff
            }

            Column {
                id: contentColumn
                width: settingsScrollView.availableWidth
                spacing: 18

                Text {
                    text: "SYSTEM SETTINGS"
                    color: root.secondaryTextColor
                    font.pixelSize: 14
                    font.letterSpacing: 1.6
                }

                Text {
                    width: parent.width
                    text: "界面偏好"
                    color: root.primaryTextColor
                    font.pixelSize: 28
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "这里会跟随系统主题，为界面提供更舒适的视觉层次与统一配色。"
                    color: root.secondaryTextColor
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 22
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: themeCard.implicitHeight + 28
                    radius: 26
                    color: root.cardColor
                    border.color: root.dividerColor
                    border.width: 1

                    Column {
                        id: themeCard
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        Rectangle {
                            width: 84
                            height: 30
                            radius: 15
                            color: root.badgeColor

                            Text {
                                anchors.centerIn: parent
                                text: "主题"
                                color: root.accentColor
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }

                        Text {
                            text: root.currentThemeText
                            color: root.primaryTextColor
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Text {
                            width: parent.width
                            text: "支持深色与浅色模式，并与系统外观保持一致，减少长时间使用时的视觉负担。"
                            color: root.secondaryTextColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 20
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: terminalCard.implicitHeight + 28
                    radius: 26
                    color: root.cardColor
                    border.color: root.dividerColor
                    border.width: 1

                    Column {
                        id: terminalCard
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        Rectangle {
                            width: 84
                            height: 30
                            radius: 15
                            color: root.badgeColor

                            Text {
                                anchors.centerIn: parent
                                text: "调试"
                                color: root.accentColor
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 12

                            Column {
                                width: parent.width - terminalManageButton.width - parent.spacing
                                spacing: 6

                                Text {
                                    width: parent.width
                                    text: "终端管理"
                                    color: root.primaryTextColor
                                    font.pixelSize: 20
                                    font.bold: true
                                }

                                Text {
                                    width: parent.width
                                    text: debugTerminalManager.nativeConsoleVisible ? "Windows 原生控制台已显示" : "Windows 原生控制台已关闭"
                                    color: root.secondaryTextColor
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Button {
                                id: terminalManageButton
                                width: 96
                                height: 40
                                anchors.verticalCenter: parent.verticalCenter
                                hoverEnabled: true
                                text: debugTerminalManager.nativeConsoleVisible ? "隐藏终端" : "打开终端"

                                contentItem: Text {
                                    text: terminalManageButton.text
                                    color: root.darkMode ? root.primaryTextColor : Theme.textWhite
                                    font.pixelSize: 13
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: height / 2
                                    border.width: 1
                                    border.color: root.withOpacity(root.accentColor, root.darkMode ? 0.32 : 0.18)
                                    color: terminalManageButton.down
                                        ? (root.darkMode ? Qt.darker(root.badgeColor, 1.15) : Qt.darker(root.accentColor, 1.10))
                                        : (terminalManageButton.hovered
                                            ? (root.darkMode ? root.badgeColor : root.accentColor)
                                            : (root.darkMode ? root.withOpacity(root.accentColor, 0.14) : root.withOpacity(root.accentColor, 0.92)))
                                }

                                onClicked: debugTerminalManager.nativeConsoleVisible = !debugTerminalManager.nativeConsoleVisible
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: outlineCard.implicitHeight + 28
                    radius: 26
                    color: root.cardColor
                    border.color: root.dividerColor
                    border.width: 1

                    Column {
                        id: outlineCard
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        Text {
                            text: "说明"
                            color: root.primaryTextColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            width: parent.width
                            text: "当前版本会优先使用系统调色板、圆角卡片和分层背景。"
                            color: root.secondaryTextColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 20
                        }

                        Text {
                            width: parent.width
                            text: "如果系统主题发生变化，界面会自动同步刷新。"
                            color: root.secondaryTextColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 20
                        }
                    }
                }
            }
        }

        Rectangle {
            id: footerDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footerColumn.top
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            anchors.bottomMargin: 18
            height: 1
            color: root.dividerColor
        }

        Column {
            id: footerColumn
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 28
                rightMargin: 28
                bottomMargin: 28
            }
            spacing: 14

            Text {
                width: parent.width
                text: "版本: " + Constants.version
                color: root.primaryTextColor
                font.pixelSize: 16
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "Copyright  2026 BSSAS Team"
                color: root.secondaryTextColor
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                id: checkUpdateButton
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, 190)
                height: 48
                hoverEnabled: true
                enabled: !updateManager.busy
                scale: down ? 0.98 : (hovered ? 1.01 : 1.0)

                onClicked: updateManager.checkForUpdates(true)

                contentItem: Text {
                    text: updateManager.checking
                        ? "检查中..."
                        : (updateManager.downloading
                            ? "下载中..."
                            : "检查更新")
                    color: root.darkMode ? root.primaryTextColor : Theme.textWhite
                    font.pixelSize: 15
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: height / 2
                    border.width: 1
                    border.color: root.withOpacity(root.accentColor, root.darkMode ? 0.32 : 0.18)
                    color: checkUpdateButton.down
                        ? (root.darkMode ? Qt.darker(root.badgeColor, 1.15) : Qt.darker(root.accentColor, 1.10))
                        : (checkUpdateButton.hovered
                            ? (root.darkMode ? root.badgeColor : root.accentColor)
                            : (root.darkMode ? root.withOpacity(root.accentColor, 0.14) : root.withOpacity(root.accentColor, 0.92)))

                    Behavior on color {
                        ColorAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Behavior on scale {
                    NumberAnimation {
                        duration: 160
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Text {
                width: parent.width
                visible: updateManager.statusMessage.length > 0
                text: updateManager.statusMessage
                color: root.secondaryTextColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeightMode: Text.FixedHeight
                lineHeight: 18
            }
        }
    }
}
