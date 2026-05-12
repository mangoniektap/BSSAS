/**
 * @file DirectionAwareHoverCard.qml
 * @brief 方向感知悬停卡片组件，根据鼠标位置产生 3D 旋转倾斜效果。
 */
import QtQuick
import BSSAS

Item {
    id: root
    
    // === 外部接口 ===
    property real maxRotationAngle: 20      // 最大旋转角度
    property real rotationX: 0              // 当前 X 旋转角度
    property real rotationY: 0              // 当前 Y 旋转角度
    property bool isHovered: mouseArea.containsMouse
    property color containerColor: Theme.pageBg
    default property alias content: contentItem.data  // 插槽，允许外部添加内容

    // === 缩放动画 ===
    scale: mouseArea.pressed ? 0.95 : 1.0
    Behavior on scale { SpringAnimation { spring: 2; damping: 0.2 } }

    // === 卡片背景与旋转效果 ===
    Rectangle {
        id: card
        anchors.fill: parent
        radius: 20
        color: root.containerColor

        transform: [
            // Y 轴旋转
            Rotation {
                id: yRotation
                origin.x: card.width / 2
                origin.y: card.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: root.rotationY
                Behavior on angle {
                    enabled: !root.isHovered
                    PropertyAnimation { duration: 300; easing.type: Easing.OutCubic }
                }
            },
            // X 轴旋转
            Rotation {
                id: xRotation
                origin.x: card.width / 2
                origin.y: card.height / 2
                axis { x: 1; y: 0; z: 0 }
                angle: root.rotationX
                Behavior on angle {
                    enabled: !root.isHovered
                    PropertyAnimation { duration: 300; easing.type: Easing.OutCubic }
                }
            }
        ]

        // === 内容容器 ===
        Item {
            id: contentItem
            anchors.fill: parent
            anchors.margins: 15
        }
    }

    // === 鼠标交互 ===
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        onPositionChanged: function(mouse) { updateRotation(mouse.x, mouse.y) }
        onExited: resetRotation()

        /**
         * @brief 根据鼠标位置更新卡片旋转角度
         * @param x 鼠标相对于卡片的 X 坐标
         * @param y 鼠标相对于卡片的 Y 坐标
         */
        function updateRotation(x, y) {
            const centerX = root.width / 2
            const centerY = root.height / 2

            const dx = x - centerX
            const dy = y - centerY

            root.rotationY = root.maxRotationAngle * dx / centerX
            root.rotationX = -root.maxRotationAngle * dy / centerY
        }

        /**
         * @brief 重置卡片旋转角度为 0
         */
        function resetRotation() {
            root.rotationX = 0
            root.rotationY = 0
        }
    }
}
