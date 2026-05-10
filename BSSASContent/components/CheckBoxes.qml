import QtQuick
import QtQuick.Layouts
import BSSAS

Item {
    id: root

    property bool checked: false
    property bool indeterminate: false
    property string text: ""
    property bool enabled: true
    property bool interactive: true

    readonly property bool _visualChecked: checked || indeterminate
    readonly property color _disabledColor: colorWithAlpha(Theme.textPrimary, 0.38)
    readonly property color _stateLayerColor: colorWithAlpha(_visualChecked ? Theme.primary : Theme.textPrimary,
                                                              stateMouseArea.pressed ? 0.16 : 0.10)

    signal clicked()

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function toggle() {
        if (!enabled || !interactive) {
            return
        }

        checked = !checked
        indeterminate = false
        clicked()
    }

    implicitWidth: root.text.length > 0 ? rowLayout.implicitWidth : 40
    implicitHeight: 40

    RowLayout {
        id: rowLayout
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Item {
            implicitWidth: 40
            implicitHeight: 40

            Rectangle {
                anchors.centerIn: parent
                width: 36
                height: 36
                radius: width / 2
                color: root.enabled && (stateMouseArea.containsMouse || stateMouseArea.pressed)
                    ? root._stateLayerColor
                    : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 24
                height: 24
                radius: 8
                color: root._visualChecked
                    ? (root.enabled ? Theme.primary : root._disabledColor)
                    : "transparent"
                border.width: root._visualChecked ? 0 : 2
                border.color: root.enabled ? Theme.textMuted : root._disabledColor

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }

                Behavior on border.color {
                    ColorAnimation {
                        duration: 120
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    radius: width / 2
                    color: root.enabled ? Theme.textWhite : Theme.cardBg
                    opacity: root._visualChecked ? 1 : 0
                    scale: root._visualChecked ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }

                    Behavior on scale {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutBack
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.indeterminate ? "remove" : "✔"
                        font.family: root.indeterminate ? Theme.iconFontFamily : Theme.fontFamily
                        font.pixelSize: root.indeterminate ? 14 : 12
                        font.weight: root.indeterminate ? Font.Normal : Font.Bold
                        color: root.enabled ? Theme.primary : root._disabledColor
                    }
                }
            }

            MouseArea {
                id: stateMouseArea
                anchors.fill: parent
                enabled: root.enabled && root.interactive
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.toggle()
            }
        }

        Text {
            visible: root.text.length > 0
            text: root.text
            color: root.enabled ? Theme.textPrimary : root._disabledColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
            verticalAlignment: Text.AlignVCenter
            Layout.leftMargin: 4
            Layout.rightMargin: 16

            MouseArea {
                anchors.fill: parent
                enabled: root.enabled && root.interactive
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.toggle()
            }
        }
    }
}
