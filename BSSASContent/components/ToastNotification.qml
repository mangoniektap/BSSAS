/**
 * @file ToastNotification.qml
 * @brief 轻提示通知组件。从窗口顶部滑入显示成功或错误消息，支持自动消失、毛玻璃背景和自定义持续时间。
 */
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

    /**
     * @brief 将颜色值与指定透明度混合
     * @param sourceColor 源颜色值
     * @param alphaValue 透明度（0.0~1.0）
     * @returns 混合后的RGBA颜色值
     */
    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    /**
     * @brief 显示提示消息，从顶部滑入并自动消失
     * @param msg 消息文本，为空则忽略
     * @param textColor 消息文本颜色，未指定则使用默认强调色
     */
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

    /**
     * @brief 显示成功提示消息（绿色）
     * @param msg 消息文本
     */
    function showSuccess(msg) {
        show(msg, root._successColor)
    }

    /**
     * @brief 显示错误提示消息（红色）
     * @param msg 消息文本
     */
    function showError(msg) {
        show(msg, root._errorColor)
    }

    /**
     * @brief 隐藏提示消息，播放向上滑出动画
     */
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
