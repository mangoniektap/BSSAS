/**
 * @file TimeSelection.qml
 * @brief 时间选择器组件。支持表盘拨动和文本输入两种模式，含24小时/12小时切换及AM/PM选择。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BSSAS

Item {
    id: root

    property int hour: new Date().getHours()
    property int minute: new Date().getMinutes()
    property bool is24Hour: true
    property string title: "选择时间"
    readonly property real _overlayWidth: overlayLayer.width > 0
        ? overlayLayer.width
        : (overlayLayer.parent && overlayLayer.parent.width > 0 ? overlayLayer.parent.width : 1280)
    readonly property real _overlayHeight: overlayLayer.height > 0
        ? overlayLayer.height
        : (overlayLayer.parent && overlayLayer.parent.height > 0 ? overlayLayer.parent.height : 800)
    readonly property real _dialogWidth: Math.round(_overlayWidth * 0.25)
    readonly property real _dialogHeight: Math.round(_overlayHeight * (root._inputMode === 1 ? 0.3 : 0.6))
    readonly property color _panelColor: Theme.primaryLighter
    readonly property color _surfaceAccentColor: Theme.pageBg
    readonly property url _correctIconSource: "qrc:/qt/qml/BSSASContent/images/correct.png"
    readonly property url _errorIconSource: "qrc:/qt/qml/BSSASContent/images/error.png"
    readonly property color _selectedContainerColor: colorWithAlpha(Theme.primary, 0.18)
    readonly property color _selectedSegmentColor: colorWithAlpha(Theme.primary, 0.14)
    readonly property color _pressedStateColor: colorWithAlpha(Theme.textPrimary, 0.10)
    readonly property url _keyboardIconSource: "qrc:/qt/qml/BSSASContent/images/keyboard.png"
    readonly property url _scheduleIconSource: "qrc:/qt/qml/BSSASContent/images/time.png"

    property int _mode: 0 // 0: hour, 1: minute
    property int _inputMode: 0 // 0: dial, 1: text input
    property int _tempHour: hour
    property int _tempMinute: minute
    property bool _emitRejectedOnClose: false

    signal accepted(int hour, int minute)
    signal rejected()
    signal closed()

    visible: false

    onHourChanged: {
        if (!overlayLayer.visible) {
            syncFromSelection()
        }
    }
    onMinuteChanged: {
        if (!overlayLayer.visible) {
            syncFromSelection()
        }
    }

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
     * @brief 将数值补零至两位字符串
     * @param value 待格式化的数值
     * @returns 两位补零字符串
     */
    function padNumber(value) {
        return normalizeMinuteValue(value).toString().padStart(2, "0")
    }

    /**
     * @brief 将小时值归一化到0~23范围内
     * @param value 输入的小时值
     * @returns 归一化后的小时值（0~23）
     */
    function normalizeHourValue(value) {
        const parsedValue = parseInt(value, 10)
        if (isNaN(parsedValue)) {
            return 0
        }

        return Math.max(0, Math.min(23, parsedValue))
    }

    /**
     * @brief 将分钟值归一化到0~59范围内
     * @param value 输入的分钟值
     * @returns 归一化后的分钟值（0~59）
     */
    function normalizeMinuteValue(value) {
        const parsedValue = parseInt(value, 10)
        if (isNaN(parsedValue)) {
            return 0
        }

        return Math.max(0, Math.min(59, parsedValue))
    }

    /**
     * @brief 根据当前制式显示小时数值，12小时制下0点显示为12
     * @param value 小时值
     * @returns 显示用的小时数值
     */
    function displayHourNumber(value) {
        const normalizedHour = normalizeHourValue(value)
        if (is24Hour) {
            return normalizedHour
        }

        const hourIn12 = normalizedHour % 12
        return hourIn12 === 0 ? 12 : hourIn12
    }

    /**
     * @brief 将小时值格式化为两位显示文本
     * @param value 小时值
     * @returns 两位补零的小时文本
     */
    function displayHourText(value) {
        return displayHourNumber(value).toString().padStart(2, "0")
    }

    /**
     * @brief 刷新文本输入框中显示的小时和分钟值
     */
    function refreshInputs() {
        if (hourInput) {
            hourInput.text = displayHourText(_tempHour)
            hourInput.errorText = ""
        }

        if (minuteInput) {
            minuteInput.text = padNumber(_tempMinute)
            minuteInput.errorText = ""
        }
    }

    /**
     * @brief 将外部hour/minute属性同步到内部临时变量，并重置模式和输入状态
     */
    function syncFromSelection() {
        _tempHour = normalizeHourValue(hour)
        _tempMinute = normalizeMinuteValue(minute)
        _mode = 0
        _inputMode = 0
        refreshInputs()
    }

    /**
     * @brief 打开时间选择器弹窗，将覆盖层挂载到顶层并播放入场动画
     */
    function open() {
        let topRoot = root
        while (topRoot.parent) {
            topRoot = topRoot.parent
        }

        syncFromSelection()

        overlayLayer.parent = topRoot
        overlayLayer.z = 99999
        overlayLayer.anchors.fill = topRoot

        exitAnimation.stop()
        enterAnimation.stop()

        animationWrapper.scale = 0.9
        animationWrapper.opacity = 0.0
        scrim.opacity = 0.0
        _emitRejectedOnClose = false

        overlayLayer.visible = true
        animationWrapper.forceActiveFocus()
        enterAnimation.start()
    }

    /**
     * @brief 关闭时间选择器，可选是否触发rejected信号
     * @param shouldReject 是否在关闭后发送rejected信号
     */
    function close(shouldReject) {
        _emitRejectedOnClose = shouldReject === true
        enterAnimation.stop()
        exitAnimation.stop()
        exitAnimation.start()
    }

    /**
     * @brief 切换上午/下午（12小时制使用）
     * @param isPm true表示下午，false表示上午
     */
    function setMeridiem(isPm) {
        const baseHour = normalizeHourValue(_tempHour) % 12
        _tempHour = baseHour + (isPm ? 12 : 0)
        if (hourInput && !hourInput.focused) {
            hourInput.text = displayHourText(_tempHour)
        }
    }

    /**
     * @brief 应用文本输入的时间值，校验并更新临时变量
     * @returns 校验通过返回true，否则返回false
     */
    function applyInputTime() {
        const parsedHour = parseInt(hourInput.text.trim(), 10)
        const parsedMinute = parseInt(minuteInput.text.trim(), 10)

        hourInput.errorText = ""
        minuteInput.errorText = ""

        if (isNaN(parsedMinute) || parsedMinute < 0 || parsedMinute > 59) {
            minuteInput.errorText = "Use 00-59"
            return false
        }

        let resolvedHour = 0
        if (is24Hour) {
            if (isNaN(parsedHour) || parsedHour < 0 || parsedHour > 23) {
                hourInput.errorText = "Use 00-23"
                return false
            }

            resolvedHour = parsedHour
        } else {
            if (isNaN(parsedHour) || parsedHour < 1 || parsedHour > 12) {
                hourInput.errorText = "Use 01-12"
                return false
            }

            resolvedHour = parsedHour % 12
            if (_tempHour >= 12) {
                resolvedHour += 12
            }
        }

        _tempHour = normalizeHourValue(resolvedHour)
        _tempMinute = normalizeMinuteValue(parsedMinute)
        refreshInputs()
        return true
    }

    /**
     * @brief 计算小时标签在表盘上的半径，24小时制下内圈/外圈区分
     * @param value 小时值
     * @returns 表盘半径像素值
     */
    function dialRadiusForHour(value) {
        if (_mode === 1 || !is24Hour) {
            return 108
        }

        return value === 0 || value > 12 ? 108 : 72
    }

    /**
     * @brief 计算两个分钟值在表盘上的环形最短距离
     * @param firstMinute 第一个分钟值
     * @param secondMinute 第二个分钟值
     * @returns 环形最短距离（0~30）
     */
    function circularMinuteDistance(firstMinute, secondMinute) {
        const delta = Math.abs(normalizeMinuteValue(firstMinute) - normalizeMinuteValue(secondMinute))
        return Math.min(delta, 60 - delta)
    }

    /**
     * @brief 判断表盘上的分钟标签是否被选中
     * @param labelMinute 标签代表的分钟值
     * @returns 是否被当前选中分钟覆盖
     */
    function isMinuteLabelSelected(labelMinute) {
        return circularMinuteDistance(_tempMinute, labelMinute) <= 2
    }

    /**
     * @brief 根据鼠标位置更新小时或分钟值，将鼠标坐标转为表盘角度后映射到时间
     * @param mouseX 鼠标X坐标（相对于控件）
     * @param mouseY 鼠标Y坐标（相对于控件）
     * @param controlWidth 控件宽度
     * @param controlHeight 控件高度
     */
    function updateTimeFromMouse(mouseX, mouseY, controlWidth, controlHeight) {
        const dx = mouseX - controlWidth / 2
        const dy = mouseY - controlHeight / 2
        const distance = Math.sqrt(dx * dx + dy * dy)

        let angle = Math.atan2(dy, dx) * 180 / Math.PI + 90
        if (angle < 0) {
            angle += 360
        }

        if (_mode === 0) {
            let nextHour = Math.round(angle / 30) % 12
            if (nextHour === 0) {
                nextHour = 12
            }

            if (is24Hour) {
                const useInnerRing = distance < 90
                if (useInnerRing) {
                    _tempHour = nextHour === 12 ? 12 : nextHour
                } else {
                    _tempHour = nextHour === 12 ? 0 : nextHour + 12
                }
            } else {
                const keepPm = _tempHour >= 12
                _tempHour = nextHour % 12
                if (keepPm) {
                    _tempHour += 12
                }
            }

            if (hourInput && !hourInput.focused) {
                hourInput.text = displayHourText(_tempHour)
            }
        } else {
            _tempMinute = Math.round(angle / 6) % 60
            if (minuteInput && !minuteInput.focused) {
                minuteInput.text = padNumber(_tempMinute)
            }
        }
    }

    Item {
        id: overlayLayer
        visible: false
        focus: visible

        Keys.onEscapePressed: root.close(true)

        ParallelAnimation {
            id: enterAnimation

            NumberAnimation {
                target: scrim
                property: "opacity"
                from: 0.0
                to: 0.32
                duration: 150
                easing.type: Easing.OutQuad
            }

            NumberAnimation {
                target: animationWrapper
                property: "scale"
                from: 0.9
                to: 1.0
                duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 1.0
            }

            NumberAnimation {
                target: animationWrapper
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: 150
                easing.type: Easing.OutQuad
            }
        }

        ParallelAnimation {
            id: exitAnimation

            onFinished: {
                overlayLayer.visible = false
                if (root._emitRejectedOnClose) {
                    root._emitRejectedOnClose = false
                    root.rejected()
                }
                root.closed()
            }

            NumberAnimation {
                target: scrim
                property: "opacity"
                from: 0.32
                to: 0.0
                duration: 150
            }

            NumberAnimation {
                target: animationWrapper
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: 100
                easing.type: Easing.InQuad
            }

            NumberAnimation {
                target: animationWrapper
                property: "scale"
                from: 1.0
                to: 0.95
                duration: 100
                easing.type: Easing.InQuad
            }
        }

        Rectangle {
            id: scrim
            anchors.fill: parent
            color: Theme.textTitle
            opacity: 0.0

            MouseArea {
                anchors.fill: parent
                onClicked: root.close(true)
                onWheel: function(wheel) { wheel.accepted = true }
            }
        }

        Item {
            id: animationWrapper
            anchors.centerIn: parent
            width: root._dialogWidth
            height: root._dialogHeight
            focus: true
            scale: 0.9
            opacity: 0.0

            Behavior on height {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }

            Rectangle {
                id: panel
                anchors.fill: parent
                radius: 28
                color: root._panelColor
                clip: true

                ColumnLayout {
                    id: mainColumn
                    anchors.fill: parent
                    spacing: 0

        Text {
            text: root._inputMode === 1 ? "Enter time" : root.title
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
            Layout.topMargin: 24
            Layout.leftMargin: 24
            Layout.bottomMargin: 20
        }

        StackLayout {
            currentIndex: root._inputMode
            Layout.fillWidth: true
            Layout.preferredHeight: root._inputMode === 1
                ? inputViewWrapper.implicitHeight
                : dialViewWrapper.implicitHeight

            ColumnLayout {
                id: dialViewWrapper
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 24
                    spacing: 0

                    Item {
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 80
                        radius: 8
                        color: root._mode === 0 ? root._selectedContainerColor : root._surfaceAccentColor

                        Text {
                            anchors.centerIn: parent
                            text: root.displayHourText(root._tempHour)
                            color: root._mode === 0 ? Theme.primary : Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: 57
                            font.weight: Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._mode = 0
                        }
                    }

                    Text {
                        text: ":"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: 57
                        font.weight: Font.Normal
                        Layout.margins: 6
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Rectangle {
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 80
                        radius: 8
                        color: root._mode === 1 ? root._selectedContainerColor : root._surfaceAccentColor

                        Text {
                            anchors.centerIn: parent
                            text: root.padNumber(root._tempMinute)
                            color: root._mode === 1 ? Theme.primary : Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: 57
                            font.weight: Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._mode = 1
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    ColumnLayout {
                        visible: !root.is24Hour
                        spacing: 0
                        Layout.leftMargin: 12
                        Layout.rightMargin: 24

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: 8
                            color: root._tempHour < 12 ? root._selectedSegmentColor : root._surfaceAccentColor
                            border.width: 1
                            border.color: Theme.primaryBorder

                            Text {
                                anchors.centerIn: parent
                                text: "AM"
                                color: root._tempHour < 12 ? Theme.primary : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setMeridiem(false)
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: 8
                            color: root._tempHour >= 12 ? root._selectedSegmentColor : root._surfaceAccentColor
                            border.width: 1
                            border.color: Theme.primaryBorder

                            Text {
                                anchors.centerIn: parent
                                text: "PM"
                                color: root._tempHour >= 12 ? Theme.primary : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setMeridiem(true)
                            }
                        }
                    }
                }

                Item {
                    id: dial
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 256
                    Layout.preferredHeight: 256

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: root._surfaceAccentColor
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.primary
                    }

                    Item {
                        id: hand
                        anchors.centerIn: parent
                        width: dial.width
                        height: dial.height

                        property real radius: root._mode === 1 ? 108 : root.dialRadiusForHour(root._tempHour)

                        rotation: root._mode === 0
                            ? (root._tempHour % 12) * 30
                            : root._tempMinute * 6

                        Behavior on rotation {
                            RotationAnimation {
                                duration: 150
                                direction: RotationAnimation.Shortest
                            }
                        }

                        Behavior on radius {
                            NumberAnimation {
                                duration: 150
                                easing.type: Easing.OutQuad
                            }
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.verticalCenter
                            width: 2
                            height: hand.radius - 24
                            color: Theme.primary
                            antialiasing: true
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.verticalCenter
                            anchors.bottomMargin: hand.radius - 24
                            width: 48
                            height: 48
                            radius: 24
                            color: Theme.primary
                        }
                    }

                    Repeater {
                        model: root._mode === 0 && root.is24Hour ? 24 : 12

                        Item {
                            required property int index

                            readonly property int value: {
                                if (root._mode === 1) {
                                    return index * 5
                                }
                                if (!root.is24Hour) {
                                    return index === 0 ? 12 : index
                                }
                                if (index === 0) {
                                    return 12
                                }
                                if (index < 12) {
                                    return index
                                }
                                if (index === 12) {
                                    return 0
                                }
                                return index
                            }
                            readonly property bool isInner: root._mode === 0 && root.is24Hour && index < 12
                            readonly property real angleRad: (index * 30 - 90) * Math.PI / 180
                            readonly property real labelRadius: root._mode === 1 ? 108 : (isInner ? 72 : 108)
                            readonly property bool isSelected: root._mode === 0
                                ? value === root._tempHour
                                : root.isMinuteLabelSelected(value)

                            anchors.fill: parent

                            Text {
                                text: root.padNumber(parent.value)
                                color: parent.isSelected ? Theme.textWhite : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: parent.isInner ? 12 : Theme.fontBodyLarge
                                font.weight: Font.Medium
                                x: dial.width / 2 + parent.labelRadius * Math.cos(parent.angleRad) - width / 2
                                y: dial.height / 2 + parent.labelRadius * Math.sin(parent.angleRad) - height / 2
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            root.updateTimeFromMouse(mouse.x, mouse.y, width, height)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                root.updateTimeFromMouse(mouse.x, mouse.y, width, height)
                            }
                        }
                        onReleased: {
                            if (root._mode === 0) {
                                root._mode = 1
                            }
                        }
                    }
                }
            }

            Item {
                id: inputViewWrapper
                Layout.fillWidth: true
                implicitHeight: inputViewLayout.implicitHeight + 48

                ColumnLayout {
                    id: inputViewLayout
                    anchors.centerIn: parent
                    spacing: 24

                    RowLayout {
                        spacing: 12

                        TextFields {
                            id: hourInput
                            Layout.preferredWidth: 96
                            Layout.fillWidth: false
                            label: "Hour"
                            type: "outlined"
                            labelBackgroundColor: root._panelColor
                            text: root.displayHourText(root._tempHour)

                            onEditingFinished: root.applyInputTime()
                        }

                        Binding {
                            target: hourInput
                            property: "text"
                            value: root.displayHourText(root._tempHour)
                            when: !hourInput.focused
                        }

                        Text {
                            text: ":"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: 32
                            font.weight: Font.Normal
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextFields {
                            id: minuteInput
                            Layout.preferredWidth: 96
                            Layout.fillWidth: false
                            label: "Minute"
                            type: "outlined"
                            labelBackgroundColor: root._panelColor
                            text: root.padNumber(root._tempMinute)

                            onEditingFinished: root.applyInputTime()
                        }

                        Binding {
                            target: minuteInput
                            property: "text"
                            value: root.padNumber(root._tempMinute)
                            when: !minuteInput.focused
                        }
                    }

                    RowLayout {
                        visible: !root.is24Hour
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 0

                        Rectangle {
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 40
                            radius: 8
                            color: root._tempHour < 12 ? root._selectedSegmentColor : "transparent"
                            border.width: 1
                            border.color: Theme.primaryBorder

                            Text {
                                anchors.centerIn: parent
                                text: "AM"
                                color: root._tempHour < 12 ? Theme.primary : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setMeridiem(false)
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 40
                            radius: 8
                            color: root._tempHour >= 12 ? root._selectedSegmentColor : "transparent"
                            border.width: 1
                            border.color: Theme.primaryBorder

                            Text {
                                anchors.centerIn: parent
                                text: "PM"
                                color: root._tempHour >= 12 ? Theme.primary : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setMeridiem(true)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.topMargin: 24
            spacing: 0

            Item {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52

                Rectangle {
                    id: modeToggleBackground
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: width / 2
                    color: modeToggleArea.pressed
                        ? root.colorWithAlpha(Theme.textPrimary, 0.14)
                        : (modeToggleArea.containsMouse
                            ? root.colorWithAlpha(Theme.textPrimary, 0.08)
                            : "transparent")
                    opacity: modeToggleArea.containsMouse || modeToggleArea.pressed ? 1 : 0
                    scale: modeToggleArea.containsMouse || modeToggleArea.pressed ? 1 : 0.92

                    Behavior on color {
                        ColorAnimation {
                            duration: 120
                        }
                    }
 
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }

                    Behavior on scale {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutQuad
                        }
                    }
                }

                MultiEffect {
                    anchors.fill: modeToggleBackground
                    source: modeToggleBackground
                    shadowEnabled: modeToggleArea.containsMouse || modeToggleArea.pressed
                    shadowColor: root.colorWithAlpha(Theme.shadowFloating, modeToggleArea.pressed ? 0.28 : 0.18)
                    shadowBlur: 0.9
                    shadowScale: 1.12
                    opacity: modeToggleBackground.opacity
                    z: -1
                }

                Image {
                    id: modeToggleIconImage
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    source: root._inputMode === 0
                        ? root._keyboardIconSource
                        : root._scheduleIconSource
                    sourceSize.width: width
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    visible: false
                }

                MultiEffect {
                    anchors.fill: modeToggleIconImage
                    source: modeToggleIconImage
                    colorization: 1.0
                    colorizationColor: Theme.textMuted
                }

                MouseArea {
                    id: modeToggleArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root._inputMode === 0) {
                            root.refreshInputs()
                        }
                        root._inputMode = root._inputMode === 0 ? 1 : 0
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: cancelButton
                text: "Cancel"
                focusPolicy: Qt.NoFocus
                padding: 9

                contentItem: Item {
                    implicitWidth: 30
                    implicitHeight: 30

                    Image {
                        anchors.centerIn: parent
                        width: 30
                        height: 30
                        source: root._errorIconSource
                        sourceSize.width: width
                        sourceSize.height: height
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }

                background: Rectangle {
                    radius: height / 2
                    color: cancelButton.pressed
                        ? root._pressedStateColor
                        : (cancelButton.hovered
                            ? root.colorWithAlpha(Theme.textPrimary, 0.06)
                            : "transparent")
                }

                onClicked: {
                    root.close(true)
                }
            }

            Button {
                id: confirmButton
                text: "OK"
                focusPolicy: Qt.NoFocus
                padding: 9

                contentItem: Item {
                    implicitWidth: 30
                    implicitHeight: 30

                    Image {
                        anchors.centerIn: parent
                        width: 30
                        height: 30
                        source: root._correctIconSource
                        sourceSize.width: width
                        sourceSize.height: height
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }

                background: Rectangle {
                    radius: height / 2
                    color: confirmButton.pressed
                        ? root._pressedStateColor
                        : (confirmButton.hovered
                            ? root.colorWithAlpha(Theme.textPrimary, 0.06)
                            : "transparent")
                }

                onClicked: {
                    if (root._inputMode === 1 && !root.applyInputTime()) {
                        return
                    }

                    root.hour = root.normalizeHourValue(root._tempHour)
                    root.minute = root.normalizeMinuteValue(root._tempMinute)
                    root.accepted(root.hour, root.minute)
                    root.close()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
        }
                }
            }
        }
    }
}
