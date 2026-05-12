/**
 * @file Select.qml
 * @brief 下拉选择框组件。基于 ComboBox 封装，支持自定义样式、瀑布流弹出动画和滚动指示器。
 */
import QtQuick
import QtQuick.Controls
import BSSAS
ComboBox {
    id: root

    property int delegateHeight: 30
    property int visibleCount: 3
    property int popupPadding: 5
    property int popupSpacing: 6
    property real popupOffset: 5
    property bool showScrollIndicator: true

    property color textColor: "black"
    property color outlineColor: Theme.textPrimary
    property color activeOutlineColor: Theme.primary
    property color indicatorColor: Theme.primaryBorder
    property color optionHighlightColor: Theme.primaryLight
    property color popupColor: Theme.pageBg
    property color popupBorderColor: Theme.primaryBorder

    readonly property int _effectiveVisibleCount: Math.max(1, Math.min(visibleCount, count > 0 ? count : 1))
    readonly property int _popupHeight: _effectiveVisibleCount * delegateHeight
        + Math.max(0, _effectiveVisibleCount - 1) * popupSpacing
        + popupPadding * 2

    implicitWidth: 150
    implicitHeight: 50

    contentItem: Text {
        text: root.displayText
        color: root.textColor
        font: root.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: 15
        rightPadding: 28
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: height / 2
        border.width: 1
        border.color: root.pressed || root.visualFocus || root.popup.visible
            ? root.activeOutlineColor
            : root.outlineColor
        color: "transparent"

        Behavior on border.color {
            ColorAnimation {
                duration: 150
            }
        }
    }

    indicator: Text {
        text: ">"
        font.pixelSize: 12
        font.bold: true
        color: root.indicatorColor
        anchors {
            right: parent.right
            rightMargin: 15
            verticalCenter: parent.verticalCenter
        }
        rotation: root.popup.visible ? 90 : 0
        transformOrigin: Item.Center

        Behavior on rotation {
            NumberAnimation {
                duration: 200
            }
        }
    }

    popup: Popup {
        id: popupControl
        y: root.height + root.popupOffset
        width: root.width
        padding: root.popupPadding
        clip: true

        property int openSerial: 0

        height: visible ? root._popupHeight : 0
        opacity: visible ? 1 : 0

        onVisibleChanged: {
            if (visible) {
                openSerial++
            }
        }

        background: Rectangle {
            radius: 12
            color: root.popupColor
            border.width: 1
            border.color: root.popupBorderColor
        }

        Behavior on height {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }

        contentItem: ListView {
            clip: true
            height: Math.max(0, popupControl.height - popupControl.padding * 2)
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            spacing: root.popupSpacing
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            interactive: contentHeight > height

            ScrollIndicator.vertical: ScrollIndicator {
                visible: root.showScrollIndicator
            }
        }
    }

    delegate: ItemDelegate {
        width: root.width - root.popupPadding * 2
        height: root.delegateHeight
        padding: 0
        clip: true
        hoverEnabled: true

        property int openSerialMirror: popupControl.openSerial

        contentItem: Rectangle {
            id: delegateBg
            anchors.fill: parent
            anchors.margins: 3
            radius: 8
            color: hovered || root.currentIndex === index ? root.optionHighlightColor : "transparent"

            Text {
                anchors.centerIn: parent
                text: root.textAt(index)
                color: root.textColor
                font: root.font
            }

            SequentialAnimation {
                id: waterfallAnimation
                running: false

                PauseAnimation {
                    duration: index * 60
                }

                ParallelAnimation {
                    NumberAnimation {
                        target: delegateBg
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 160
                        easing.type: Easing.OutCubic
                    }

                    NumberAnimation {
                        target: delegateBg
                        property: "y"
                        from: -8
                        to: 0
                        duration: 220
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        background: null

        onOpenSerialMirrorChanged: {
            if (popupControl.visible) {
                delegateBg.opacity = 0
                delegateBg.y = -8
                waterfallAnimation.restart()
            }
        }

        onClicked: {
            root.currentIndex = index
            root.popup.close()
        }
    }
}
