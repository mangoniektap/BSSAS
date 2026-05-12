/**
 * @file ToggleSwitch.qml
 * @brief 开关切换组件。支持自定义图标、悬停动效和禁用状态，点击轨道或滑块均可切换。
 */
import QtQuick
import QtQuick.Layouts
import BSSAS

Item {
    id: root

    property bool checked: false
    property bool enabled: true
    property bool interactive: true

    // 自定义图标支持（未指定时使用默认勾选/关闭图标）
    property bool showIcon: true
    property string icon: "✔"   // 选中时显示的图标

    signal clicked()

    implicitWidth: rowLayout.implicitWidth
    implicitHeight: Math.max(32, rowLayout.implicitHeight)

    // 主题颜色查找

    /**
     * @brief 生成带指定透明度的禁用状态颜色
     * @param alphaValue 透明度（0.0~1.0）
     * @returns 带透明度的RGBA颜色值
     */
    function disabledColor(alphaValue) {
        let color = Qt.color(Theme.textPrimary)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }
    
    RowLayout {
        id: rowLayout
        anchors.centerIn: parent
        spacing: 12
        
        // 开关容器（轨道+滑块）
        Item {
            implicitWidth: 52
            implicitHeight: 32
            
            // Track
            Rectangle {
                id: track
                anchors.fill: parent
                radius: 16
                
                color: {
                    if (!root.enabled) {
                        return root.checked ? root.disabledColor(0.12) : root.disabledColor(0.12)
                    }
                    return root.checked ? Theme.primary : Theme.pageBg
                }
                
                border.width: root.checked ? 0 : 2
                border.color: {
                    if (!root.enabled) {
                        return root.checked ? "transparent" : root.disabledColor(0.12)
                    }
                    return root.checked ? Theme.primary : Theme.primaryBorder
                }
                
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }
                
                // 轨道悬停效果
                MouseArea {
                    anchors.fill: parent
                    enabled: root.enabled && root.interactive
                    onClicked: {
                        root.checked = !root.checked
                        root.clicked()
                    }

                    // 轨道悬停动画
                    onHoveredChanged: {
                        if (hovered) {
                            track.scale = 1.05  // 轻微放大轨道
                            track.opacity = 1.1 // 轻微提亮轨道
                        } else {
                            track.scale = 1  // 重置缩放
                            track.opacity = 1  // 重置透明度
                        }
                    }
                }
            }
            
            // Thumb
            Rectangle {
                id: thumb
                width: root.checked ? 24 : 16
                height: width
                radius: width / 2
                
                anchors.verticalCenter: parent.verticalCenter
                x: root.checked ? (parent.width - width - 4) : 8
                
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                
                color: {
                    if (!root.enabled) {
                        return root.checked ? Theme.cardBg : root.disabledColor(0.38)
                    }
                    return root.checked ? Theme.textWhite : Theme.primaryBorder
                }
                
                // 图标（勾选标记）
                Text {
                    id: iconItem
                    anchors.centerIn: parent
                    text: root.icon
                    font.family: Theme.iconFontFamily
                    font.pixelSize: 16
                    visible: root.showIcon
                    
                    scale: root.checked ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
                    
                    color: {
                        if (!root.enabled) return root.disabledColor(0.38)
                        return Theme.primary
                    }
                }

                Behavior on color { ColorAnimation { duration: 150 } }
                
                // 滑块悬停效果
                MouseArea {
                    anchors.fill: parent
                    enabled: root.enabled && root.interactive
                    onClicked: {
                        root.checked = !root.checked
                        root.clicked()
                    }

                    // 滑块悬停动画
                    onHoveredChanged: {
                        if (hovered) {
                            thumb.scale = 1.05  // 轻微放大滑块
                            thumb.opacity = 1.1 // 轻微提亮滑块
                        } else {
                            thumb.scale = 1  // 重置缩放
                            thumb.opacity = 1  // 重置透明度
                        }
                    }
                }
            }
            
            // 涟漪效果（已注释）
           /*  Ripple {
                anchors.centerIn: thumb
                width: 40
                height: 40
                clipRadius: 20
                enabled: root.enabled
                rippleColor: root.checked ? Theme.primary : Theme.textPrimary
                
                onClicked: {
                    root.checked = !root.checked
                    root.clicked()
                }
            } */
        }
    }
}
