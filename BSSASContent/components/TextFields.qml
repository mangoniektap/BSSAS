import QtQuick
import QtQuick.Layouts
import BSSAS

Item {
    id: root

    property alias text: textInput.text
    property alias font: textInput.font
    property alias validator: textInput.validator
    property alias selectByMouse: textInput.selectByMouse
    property string placeholderText: ""
    property string label: ""
    property string leadingIcon: ""
    property string trailingIcon: ""
    property string supportingText: ""
    property string errorText: ""
    property bool error: errorText.length > 0
    property string type: "filled"
    property bool enabled: true
    property bool readOnly: false
    property color labelBackgroundColor: Theme.cardBg
    property bool forceFocused: false
    property bool isPassword: false
    property bool passwordVisible: false
    property bool floatPlaceholder: false
    property real fieldHeight: 56
    property real cornerRadius: Theme.radiusSmall
    property real contentPadding: 16
    property real iconPadding: 12
    property real iconSpacing: 16
    property color fieldBackgroundColor: type === "filled"
        ? (enabled ? Theme.pageBg : colorWithAlpha(Theme.textPrimary, 0.04))
        : "transparent"
    property color contentColor: enabled ? Theme.textPrimary : _disabledTextColor
    property color placeholderColor: colorWithAlpha(contentColor, 0.5)
    property color supportingColor: enabled ? Theme.textMuted : _disabledTextColor
    property color inactiveLabelColor: Theme.textMuted
    property color inactiveIndicatorColor: Theme.textMuted
    property color inactiveOutlineColor: Theme.primaryBorder
    property color activeAccentColor: Theme.primary
    property bool useInputFocusForOutline: true
    property real outlineStrokeWidth: 1
    property real focusOutlineStrokeWidth: 2
    readonly property bool focused: (useInputFocusForOutline && textInput.activeFocus) || forceFocused
    readonly property bool hasContent: text.length > 0
    readonly property bool isFloating: focused || hasContent
    readonly property bool _usesFloatingPlaceholder: floatPlaceholder && label === "" && placeholderText !== ""
    readonly property string _floatingText: label !== "" ? label : (_usesFloatingPlaceholder ? placeholderText : "")
    readonly property bool _showFloatingText: _floatingText !== "" && (label !== "" || _usesFloatingPlaceholder || isFloating)
    readonly property bool _reserveFloatingTextSpace: _showFloatingText && isFloating
    readonly property bool _floatingTextUsesPlaceholderStyle: label === "" && _usesFloatingPlaceholder
    readonly property int _labelAnimationDuration: 280
    readonly property real _labelBasePixelSize: textInput.font.pixelSize > 0
        ? textInput.font.pixelSize
        : Theme.fontBodyLarge
    readonly property real _labelFloatingPixelSize: _floatingTextUsesPlaceholderStyle
        ? Math.max(12, Math.round(_labelBasePixelSize * 0.72))
        : Math.max(12, Theme.fontBody)
    readonly property real _labelScale: isFloating
        ? Math.min(1.0, _labelFloatingPixelSize / Math.max(1, _labelBasePixelSize))
        : 1.0
    readonly property color _disabledTextColor: colorWithAlpha(Theme.textPrimary, 0.38)
    readonly property color _disabledOutlineColor: colorWithAlpha(Theme.textPrimary, 0.12)
    readonly property color _labelColor: {
        if (_floatingTextUsesPlaceholderStyle) {
            return placeholderColor
        }
        if (!enabled) {
            return _disabledTextColor
        }
        if (error) {
            return Theme.danger
        }
        if (focused) {
            return activeAccentColor
        }
        return inactiveLabelColor
    }
    readonly property color _indicatorColor: {
        if (!enabled) {
            return _disabledTextColor
        }
        if (error) {
            return Theme.danger
        }
        if (focused) {
            return activeAccentColor
        }
        return inactiveIndicatorColor
    }
    readonly property color _outlineColor: {
        if (!enabled) {
            return _disabledOutlineColor
        }
        if (error) {
            return Theme.danger
        }
        if (focused) {
            return activeAccentColor
        }
        return inactiveOutlineColor
    }
    readonly property real _notchPadding: 4
    readonly property bool _showOutlineNotch: type === "outlined" && _showFloatingText && isFloating
    readonly property real _notchMinX: container.radius + 8
    readonly property real _notchMaxX: container.width - container.radius - 8
    readonly property real _notchLeft: {
        if (!_showOutlineNotch) {
            return _notchMinX
        }

        const notchStart = fieldContent.x + labelContainer.x - _notchPadding
        return Math.max(_notchMinX, Math.min(_notchMaxX, notchStart))
    }
    readonly property real _notchRight: {
        if (!_showOutlineNotch) {
            return _notchMinX
        }

        const notchEnd = fieldContent.x + labelContainer.x + labelContainer.width + _notchPadding
        return Math.max(_notchLeft, Math.min(_notchMaxX, notchEnd))
    }

    signal accepted()
    signal editingFinished()
    signal trailingIconClicked()

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function drawOutlinedBorder(ctx, canvasWidth, canvasHeight, radius, strokeWidth, strokeColor, gapStart, gapEnd) {
        const halfStroke = strokeWidth / 2
        const left = halfStroke
        const top = halfStroke
        const right = canvasWidth - halfStroke
        const bottom = canvasHeight - halfStroke
        const safeRadius = Math.max(0, Math.min(radius, (canvasWidth - strokeWidth) / 2, (canvasHeight - strokeWidth) / 2))
        const hasGap = gapEnd > gapStart
        const notchStart = hasGap ? Math.max(left + safeRadius, gapStart) : left + safeRadius
        const notchEnd = hasGap ? Math.min(right - safeRadius, gapEnd) : left + safeRadius

        ctx.clearRect(0, 0, canvasWidth, canvasHeight)
        ctx.beginPath()
        ctx.lineWidth = strokeWidth
        ctx.strokeStyle = strokeColor
        ctx.lineJoin = "round"
        ctx.lineCap = "butt"

        ctx.moveTo(notchEnd, top)
        ctx.lineTo(right - safeRadius, top)

        if (safeRadius > 0) {
            ctx.arc(right - safeRadius, top + safeRadius, safeRadius, -Math.PI / 2, 0)
        }

        ctx.lineTo(right, bottom - safeRadius)

        if (safeRadius > 0) {
            ctx.arc(right - safeRadius, bottom - safeRadius, safeRadius, 0, Math.PI / 2)
        }

        ctx.lineTo(left + safeRadius, bottom)

        if (safeRadius > 0) {
            ctx.arc(left + safeRadius, bottom - safeRadius, safeRadius, Math.PI / 2, Math.PI)
        }

        ctx.lineTo(left, top + safeRadius)

        if (safeRadius > 0) {
            ctx.arc(left + safeRadius, top + safeRadius, safeRadius, Math.PI, Math.PI * 1.5)
        }

        ctx.lineTo(notchStart, top)
        ctx.stroke()
    }

    function requestOutlinePaint() {
        if (outlineCanvas) {
            outlineCanvas.requestPaint()
        }
        if (focusOutlineCanvas) {
            focusOutlineCanvas.requestPaint()
        }
    }

    implicitWidth: 280
    implicitHeight: fieldHeight + (supportingLabel.visible ? supportingLabel.implicitHeight + 8 : 0)

    onEnabledChanged: requestOutlinePaint()
    onErrorChanged: requestOutlinePaint()
    onFocusedChanged: requestOutlinePaint()
    onIsFloatingChanged: requestOutlinePaint()
    onLabelChanged: requestOutlinePaint()
    onPlaceholderTextChanged: requestOutlinePaint()
    onFloatPlaceholderChanged: requestOutlinePaint()
    onTypeChanged: requestOutlinePaint()
    onForceFocusedChanged: requestOutlinePaint()
    onUseInputFocusForOutlineChanged: requestOutlinePaint()
    onOutlineStrokeWidthChanged: requestOutlinePaint()
    onFocusOutlineStrokeWidthChanged: requestOutlinePaint()
    Component.onCompleted: requestOutlinePaint()

    Rectangle {
        id: container
        width: parent.width
        height: root.fieldHeight
        radius: root.cornerRadius
        color: root.fieldBackgroundColor
        border.width: 0
        border.color: "transparent"

        Canvas {
            id: outlineCanvas
            anchors.fill: parent
            visible: root.type === "outlined"
            antialiasing: true

            onPaint: {
                const ctx = getContext("2d")
                root.drawOutlinedBorder(
                    ctx,
                    width,
                    height,
                    container.radius,
                    root.outlineStrokeWidth,
                    root._outlineColor,
                    root._showOutlineNotch ? root._notchLeft : -1,
                    root._showOutlineNotch ? root._notchRight : -1
                )
            }
        }

        Canvas {
            id: focusOutlineCanvas
            anchors.fill: parent
            visible: root.type === "outlined"
            antialiasing: true
            opacity: root.focused ? 1 : 0

            onPaint: {
                const ctx = getContext("2d")
                root.drawOutlinedBorder(
                    ctx,
                    width,
                    height,
                    container.radius,
                    root.focusOutlineStrokeWidth,
                    root.error ? Theme.danger : root.activeAccentColor,
                    root._showOutlineNotch ? root._notchLeft : -1,
                    root._showOutlineNotch ? root._notchRight : -1
                )
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 150
                }
            }
        }

        Rectangle {
            visible: root.type === "filled"
            width: parent.width
            height: root.focused ? 2 : 1
            color: root._indicatorColor
            anchors.bottom: parent.bottom

            Behavior on height {
                NumberAnimation {
                    duration: 150
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on color {
                ColorAnimation {
                    duration: 150
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.leadingIcon ? root.iconPadding : root.contentPadding
            anchors.rightMargin: (root.trailingIcon || root.isPassword || root.error) ? root.iconPadding : root.contentPadding
            spacing: 0

            Text {
                visible: root.leadingIcon !== ""
                text: root.leadingIcon
                color: root.contentColor
                font.family: Theme.iconFontFamily
                font.pixelSize: 24
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.rightMargin: root.iconSpacing
            }

            Item {
                id: fieldContent
                Layout.fillWidth: true
                Layout.fillHeight: true

                Item {
                    id: labelContainer
                    visible: root._showFloatingText
                    width: Math.min(labelText.implicitWidth * labelText.scale, fieldContent.width)
                    height: labelText.implicitHeight * labelText.scale
                    y: root.type === "filled"
                        ? (root.isFloating ? 8 : 16)
                        : (root.isFloating ? -8 : 16)
                    z: 1

                    Behavior on y {
                        NumberAnimation {
                            duration: root._labelAnimationDuration
                            easing.type: Easing.InOutCubic
                        }
                    }

                    Rectangle {
                        visible: root.type === "outlined" && root.isFloating
                        anchors.fill: parent
                        anchors.leftMargin: -root._notchPadding
                        anchors.rightMargin: -root._notchPadding
                        anchors.topMargin: -1
                        anchors.bottomMargin: -1
                        radius: 2
                        color: root.labelBackgroundColor
                    }

                    Text {
                        id: labelText
                        x: 0
                        y: 0
                        width: Math.min(labelText.implicitWidth, fieldContent.width / Math.max(scale, 0.001))
                        height: implicitHeight
                        text: root._floatingText
                        color: root._labelColor
                        font.family: textInput.font.family
                        font.pixelSize: root._labelBasePixelSize
                        font.weight: textInput.font.weight
                        elide: Text.ElideRight
                        clip: true
                        scale: root._labelScale
                        transformOrigin: Item.TopLeft

                        Behavior on scale {
                            NumberAnimation {
                                duration: root._labelAnimationDuration
                                easing.type: Easing.InOutCubic
                            }
                        }
                    }

                    onWidthChanged: root.requestOutlinePaint()
                    onXChanged: root.requestOutlinePaint()
                }

                TextInput {
                    id: textInput
                    anchors.fill: parent
                    anchors.topMargin: root._reserveFloatingTextSpace
                        ? (root.type === "filled" ? 24 : 16)
                        : 0
                    anchors.bottomMargin: root._reserveFloatingTextSpace
                        ? (root.type === "filled" ? 8 : 16)
                        : 0
                    verticalAlignment: root._reserveFloatingTextSpace
                        ? TextInput.AlignBottom
                        : TextInput.AlignVCenter
                    color: root.contentColor
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: Font.Normal
                    selectionColor: Theme.primary
                    selectedTextColor: Theme.textWhite
                    enabled: root.enabled
                    readOnly: root.readOnly
                    selectByMouse: true
                    clip: true
                    echoMode: root.isPassword && !root.passwordVisible ? TextInput.Password : TextInput.Normal
                    passwordCharacter: "*"

                    onAccepted: {
                        root.accepted()
                        focus = false
                    }

                    onEditingFinished: root.editingFinished()
                }

                Text {
                    anchors.fill: textInput
                    visible: root.placeholderText !== ""
                        && !root.hasContent
                        && root.label === ""
                        && !root._usesFloatingPlaceholder
                    text: root.placeholderText
                    color: root.placeholderColor
                    font: textInput.font
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    clip: true
                }

                onXChanged: root.requestOutlinePaint()
                onWidthChanged: root.requestOutlinePaint()
            }

            Item {
                id: trailingItem
                visible: root.trailingIcon !== "" || root.isPassword || root.error
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.leftMargin: root.iconSpacing

                readonly property bool interactive: root.enabled && (root.isPassword || root.trailingIcon !== "")

                Rectangle {
                    anchors.centerIn: parent
                    width: 36
                    height: 36
                    radius: width / 2
                    visible: trailingMouseArea.containsMouse || trailingMouseArea.pressed
                    color: root.colorWithAlpha(Theme.textPrimary, trailingMouseArea.pressed ? 0.16 : 0.10)
                }

                Text {
                    anchors.centerIn: parent
                    text: root.isPassword
                        ? (root.passwordVisible ? "visibility_off" : "visibility")
                        : (root.trailingIcon !== "" ? root.trailingIcon : "error")
                    color: root.error && !root.isPassword && root.trailingIcon === ""
                        ? Theme.danger
                        : (root.error ? Theme.danger : root.contentColor)
                    font.family: Theme.iconFontFamily
                    font.pixelSize: 24
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }

                MouseArea {
                    id: trailingMouseArea
                    anchors.fill: parent
                    anchors.margins: -6
                    enabled: trailingItem.interactive
                    hoverEnabled: enabled
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (root.isPassword) {
                            root.passwordVisible = !root.passwordVisible
                            return
                        }

                        root.trailingIconClicked()
                    }
                }
            }
        }
    }

    Text {
        id: supportingLabel
        anchors.top: container.bottom
        anchors.topMargin: 4
        anchors.left: container.left
        anchors.leftMargin: 16
        anchors.right: container.right
        anchors.rightMargin: 16
        visible: text !== ""
        text: root.error ? root.errorText : root.supportingText
        color: root.error ? Theme.danger : root.supportingColor
        font.family: Theme.fontFamily
        font.pixelSize: 12
    }
}
