/**
 * @file BlurGlass.qml
 * @brief 模糊玻璃效果容器组件，提供背景模糊、拖拽和内容承载功能。
 */
import QtQuick
import QtQuick.Effects
import BSSAS

Item {
    id: root

    /** 公共属性 */
    property Item blurSource
    property real blurAmount: 1
    property bool dragable: false

    property real blurMax: 64
    property real borderRadius: 25
    property color borderColor: "transparent"
    property real borderWidth: 0
    property color overlayColor: Theme.cardBg
    property real overlayOpacity: 0.2

    default property alias content: contentItem.data
    readonly property point _sourceTopLeft: root.blurSource
        ? root.mapToItem(root.blurSource, 0, 0)
        : Qt.point(0, 0)

    implicitWidth: 300
    implicitHeight: 200

    /** 拖拽功能 */
    MouseArea {
        anchors.fill: parent
        drag.target: root
        drag.axis: Drag.XAndYAxis
        enabled: root.dragable
    }

    /** 捕获背景内容 */
    ShaderEffectSource {
        id: effectSource
        anchors.fill: parent
        sourceItem: root.blurSource
        sourceRect: root.blurSource
            ? Qt.rect(root._sourceTopLeft.x, root._sourceTopLeft.y, root.width, root.height)
            : Qt.rect(0, 0, 0, 0)
        textureSize: Qt.size(Math.max(1, root.width), Math.max(1, root.height))
        live: true
        recursive: false
        visible: true
        opacity: 0
    }

    /** 创建遮罩 */
    Item {
        id: maskItem
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        visible: true
        opacity: 0
        Rectangle {
            anchors.fill: parent
            radius: root.borderRadius
            color: Theme.textWhite  // Must be opaque, otherwise mask won't work
        }
    }
    /** 启用遮罩效果 */
    MultiEffect {
        anchors.fill: effectSource
        source: effectSource
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: root.blurMax
        blur: root.blurMax > 0 ? Math.max(0, Math.min(1, root.blurAmount / root.blurMax)) : 0
        maskEnabled: true
        maskSource: maskItem
    }

    /** 叠加主题色，避免过亮或过透明 */
    Rectangle {
        anchors.fill: parent
        radius: root.borderRadius
        color: root.overlayColor
        z: 1
        opacity: root.overlayOpacity
        border.color: root.borderColor
        border.width: root.borderWidth
    }

    /** 内容容器 */
    Item {
        id: contentItem
        anchors.fill: parent
        clip: true
        z: 2
    }
}
