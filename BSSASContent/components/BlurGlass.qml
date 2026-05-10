import QtQuick
import QtQuick.Effects
import BSSAS

Item {
    id: root

    // Public Properties
    property Item blurSource
    property real blurAmount: 1
    property bool dragable: false

    property real blurMax: 64
    property real borderRadius: 24
    property color borderColor: "transparent"
    property real borderWidth: 0
    property color overlayColor: Theme.cardBg
    property real overlayOpacity: 0.2

    default property alias content: contentItem.data
    readonly property point _sourceTopLeft: root.blurSource
        ? root.mapToItem(root.blurSource, 0, 0)
        : Qt.point(0, 0)

    width: 300
    height: 200

    // Drag Functionality
    MouseArea {
        anchors.fill: parent
        drag.target: root
        drag.axis: Drag.XAndYAxis
        enabled: root.dragable
    }

    // Capture Background Content
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

    // Create Mask
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
    // Enable Mask
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

    // Overlay Theme Color, avoid being too bright/transparent
    Rectangle {
        anchors.fill: parent
        radius: root.borderRadius
        color: root.overlayColor
        z: 1
        opacity: root.overlayOpacity
        border.color: root.borderColor
        border.width: root.borderWidth
    }

    // Content Container
    Item {
        id: contentItem
        anchors.fill: parent
        clip: true
        z: 2
    }
}
