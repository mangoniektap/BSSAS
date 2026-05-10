pragma ComponentBehavior: Bound

import QtQuick
import BSSAS

Item {
    id: root

    property color blockColor: Theme.textMuted
    property int blockCount: 6
    property int columns: 3
    property real gap: 5
    property int animationDuration: 600
    property bool running: true
    property real inactiveOpacity: 0.3
    property real inactiveScale: 0.5
    property real inactiveRotation: 5
    property var animationDelays: [0, 200, 300, 400, 500, 600]

    readonly property int _safeColumns: Math.max(1, root.columns)
    readonly property int _safeRows: Math.max(1, Math.ceil(root.blockCount / root._safeColumns))
    readonly property real _cellWidth: Math.max(0, (width - gap * (root._safeColumns - 1)) / root._safeColumns)
    readonly property real _cellHeight: Math.max(0, (height - gap * (root._safeRows - 1)) / root._safeRows)

    implicitWidth: 70
    implicitHeight: 70

    Grid {
        anchors.fill: parent
        columns: root._safeColumns
        rowSpacing: root.gap
        columnSpacing: root.gap

        Repeater {
            model: root.blockCount

            delegate: Rectangle {
                id: block

                required property int index

                readonly property int delayMs: index < root.animationDelays.length
                    ? root.animationDelays[index]
                    : index * 100

                width: root._cellWidth
                height: root._cellHeight
                radius: Math.min(width, height) * 0.18
                color: root.blockColor
                opacity: root.inactiveOpacity
                scale: root.inactiveScale
                rotation: root.inactiveRotation
                transformOrigin: Item.Center

                function resetState() {
                    opacity = root.inactiveOpacity
                    scale = root.inactiveScale
                    rotation = root.inactiveRotation
                }

                function restartAnimation() {
                    startDelay.stop()
                    blink.stop()
                    resetState()

                    if (!root.running || !root.visible) {
                        return
                    }

                    if (delayMs > 0) {
                        startDelay.start()
                    } else {
                        blink.start()
                    }
                }

                Timer {
                    id: startDelay
                    interval: block.delayMs
                    repeat: false
                    onTriggered: blink.start()
                }

                SequentialAnimation {
                    id: blink
                    loops: Animation.Infinite

                    ParallelAnimation {
                        NumberAnimation {
                            target: block
                            property: "opacity"
                            from: root.inactiveOpacity
                            to: 1.0
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                        NumberAnimation {
                            target: block
                            property: "scale"
                            from: root.inactiveScale
                            to: 1.0
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                        NumberAnimation {
                            target: block
                            property: "rotation"
                            from: root.inactiveRotation
                            to: 0
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                    }

                    ParallelAnimation {
                        NumberAnimation {
                            target: block
                            property: "opacity"
                            from: 1.0
                            to: root.inactiveOpacity
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                        NumberAnimation {
                            target: block
                            property: "scale"
                            from: 1.0
                            to: root.inactiveScale
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                        NumberAnimation {
                            target: block
                            property: "rotation"
                            from: 0
                            to: root.inactiveRotation
                            duration: root.animationDuration
                            easing.type: Easing.Linear
                        }
                    }
                }

                onDelayMsChanged: restartAnimation()
                Component.onCompleted: restartAnimation()

                Connections {
                    target: root

                    function onRunningChanged() {
                        block.restartAnimation()
                    }

                    function onVisibleChanged() {
                        block.restartAnimation()
                    }
                }
            }
        }
    }
}
