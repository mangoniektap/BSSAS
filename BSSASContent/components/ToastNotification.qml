import QtQuick
import QtQuick.Effects
import BSSAS
import MangoComponent

Item {
    id: root
    readonly property Item _windowBackground: {
        const win = root.Window.window
        return win && win.mainWindowBackground ? win.mainWindowBackground : null
    }
    parent: root._windowBackground

    property Item anchorTarget: root._windowBackground
    property string text: ""
    property int duration: 3000
    property bool shadowEnabled: true
    property real radius: 20
    property int padding: 18
    property real minWidth: 320
    property real minHeight: 64
    property real maxWidth: 560
    property real topMargin: 20
    property real slideDistance: 18
    property font messageFont: Qt.font({
        family: Theme.fontFamily,
        pixelSize: 16,
        bold: true
    })
    property color accentColor: _defaultAccentColor
    property real yOffset: 0
    readonly property Item _defaultBlurSource: root._windowBackground
        ? root._windowBackground
        : root.anchorTarget
    property Item blurSource: root._defaultBlurSource
    property real blurAmount: 12
    readonly property color fgColor: root.accentColor
    readonly property color borderColor: colorWithAlpha(root.accentColor, 0.26)
    readonly property color _defaultAccentColor: Theme.primary
    readonly property color _successColor: Theme.primary
    readonly property color _errorColor: Theme.danger

    anchors.top: root.parent ? root.parent.top : undefined
    anchors.horizontalCenter: root.parent ? root.parent.horizontalCenter : undefined
    anchors.topMargin: root.topMargin + root.yOffset

    width: Math.max(minWidth, Math.min(contentItem.implicitWidth + padding * 2, maxWidth))
    height: Math.max(minHeight, contentItem.implicitHeight + padding * 2)
    visible: false
    opacity: 0
    z: 1500

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function show(msg, textColor) {
        if (!msg || msg.length === 0) {
            return
        }

        hideTimer.stop()
        enter.stop()
        out.stop()

        root.text = msg
        root.accentColor = textColor ? Qt.color(textColor) : root._defaultAccentColor
        root.visible = true
        root.opacity = 1
        root.yOffset = -root.slideDistance
        enter.restart()
        hideTimer.interval = root.duration
        hideTimer.start()
    }

    function showSuccess(msg) {
        show(msg, root._successColor)
    }

    function showError(msg) {
        show(msg, root._errorColor)
    }

    function hide() {
        hideTimer.stop()
        enter.stop()
        root.opacity = 0
        out.restart()
    }

    BlurGlass {
        id: background
        anchors.fill: parent
        blurSource: root.blurSource
        blurAmount: root.blurAmount
        overlayColor: Theme.primaryLighter
        overlayOpacity: 0.42
        borderRadius: root.radius
        borderWidth: 1
        borderColor: root.borderColor
        blurMax: 64
    }

    MultiEffect {
        source: background
        anchors.fill: background
        visible: root.shadowEnabled
        shadowEnabled: true
        shadowColor: Theme.shadowFloating
        shadowBlur: 1.0
        shadowVerticalOffset: 2
        shadowHorizontalOffset: 2
    }

    Text {
        id: contentItem
        anchors.centerIn: parent
        text: root.text
        color: root.fgColor
        wrapMode: Text.WrapAnywhere
        width: Math.min(root.maxWidth - root.padding * 2, implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        font: root.messageFont
    }

    Behavior on opacity {
        NumberAnimation {
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    PropertyAnimation {
        id: enter
        target: root
        property: "yOffset"
        from: -root.slideDistance
        to: 0
        duration: 220
        easing.type: Easing.OutCubic
    }

    PropertyAnimation {
        id: out
        target: root
        property: "yOffset"
        from: 0
        to: -root.slideDistance
        duration: 200
        easing.type: Easing.InCubic

        onFinished: {
            if (root.opacity === 0) {
                root.visible = false
            }
        }
    }

    Timer {
        id: hideTimer
        interval: root.duration
        running: false
        repeat: false
        onTriggered: root.hide()
    }
}
