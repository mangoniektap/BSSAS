/**
 * @file DateSelection.qml
 * @brief 日期选择器组件，支持日历视图、年份列表和文本输入三种日期选择方式。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import BSSAS

Item {
    id: control

    property date selectedDate: new Date()
    property string title: "选择日期"
    property int minimumYear: 1900
    property int maximumYear: 2099
    readonly property real _overlayWidth: overlayLayer.width > 0
        ? overlayLayer.width
        : (overlayLayer.parent && overlayLayer.parent.width > 0 ? overlayLayer.parent.width : 1200)
    readonly property real _overlayHeight: overlayLayer.height > 0
        ? overlayLayer.height
        : (overlayLayer.parent && overlayLayer.parent.height > 0 ? overlayLayer.parent.height : 800)
    readonly property real _dialogMargin: 48
    readonly property real _dialogWidth: Math.round(Math.max(360, Math.min(_overlayWidth - _dialogMargin, _overlayWidth * 0.3)))
    readonly property real _dialogHeight: Math.round(Math.max(
        _inputMode === 2 ? 320 : 520,
        Math.min(_overlayHeight - _dialogMargin, _inputMode === 2 ? _dialogWidth : _overlayHeight * 0.7)))
    readonly property url _calendarIconSource: "qrc:/qt/qml/BSSASContent/images/calendar.png"
    readonly property url _correctIconSource: "qrc:/qt/qml/BSSASContent/images/correct.png"
    readonly property url _editIconSource: "qrc:/qt/qml/BSSASContent/images/pencil.png"
    readonly property url _errorIconSource: "qrc:/qt/qml/BSSASContent/images/error.png"
    readonly property url _leftIconSource: "qrc:/qt/qml/BSSASContent/images/left.png"
    readonly property url _rightIconSource: "qrc:/qt/qml/BSSASContent/images/right.png"

    property date _viewDate: new Date()
    /** 输入模式: 0=日历视图, 1=年份列表, 2=文本输入 */
    property int _inputMode: 0
    property bool _emitRejectedOnClose: false

    signal accepted(date date)
    signal rejected()
    signal closed()

    visible: false

    /**
     * @brief 为颜色附加透明度
     * @param sourceColor 源颜色值
     * @param alphaValue 透明度 (0~1)
     * @returns 附加透明度的新颜色
     */
    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    /**
     * @brief 规范化日期值，将年/月/日钳制到合法范围
     * @param value 日期对象或可转换值
     * @returns 规范化后的 Date 对象
     */
    function normalizeDate(value) {
        const dateValue = value instanceof Date ? value : new Date(value)

        if (isNaN(dateValue.getTime())) {
            return new Date()
        }

        const clampedYear = Math.max(minimumYear, Math.min(maximumYear, dateValue.getFullYear()))
        const clampedMonth = dateValue.getMonth()
        const clampedDay = Math.min(dateValue.getDate(), getDaysInMonth(clampedYear, clampedMonth))
        return new Date(clampedYear, clampedMonth, clampedDay)
    }

    /**
     * @brief 从 selectedDate 同步内部状态
     * @details 规范化选中日期，更新视图日期，重置输入模式为日历视图。
     */
    function syncFromSelection() {
        const normalizedDate = normalizeDate(selectedDate)
        if (!isSameDay(normalizedDate, selectedDate)) {
            selectedDate = normalizedDate
        }
        _viewDate = new Date(normalizedDate)
        _inputMode = 0

        if (dateInput) {
            dateInput.text = Qt.formatDate(normalizedDate, "MM/dd/yyyy")
            dateInput.errorText = ""
        }
    }

    /**
     * @brief 打开日期选择器弹窗
     * @details 将覆盖层挂载到顶层父组件，播放入场动画。
     */
    function open() {
        let root = control
        while (root.parent) {
            root = root.parent
        }

        syncFromSelection()

        overlayLayer.parent = root
        overlayLayer.z = 99999
        overlayLayer.anchors.fill = root

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
     * @brief 关闭日期选择器弹窗
     * @param shouldReject 为 true 时在关闭后触发 rejected 信号
     */
    function close(shouldReject) {
        _emitRejectedOnClose = shouldReject === true
        enterAnimation.stop()
        exitAnimation.stop()
        exitAnimation.start()
    }

    /**
     * @brief 获取指定月份的天数
     * @param year 年份
     * @param month 月份 (0-11)
     * @returns 该月的天数
     */
    function getDaysInMonth(year, month) {
        return new Date(year, month + 1, 0).getDate()
    }

    /**
     * @brief 获取指定月份第一天的星期索引
     * @param year 年份
     * @param month 月份 (0-11)
     * @returns 星期索引 (0=周日, 6=周六)
     */
    function getFirstDayOfMonth(year, month) {
        return new Date(year, month, 1).getDay()
    }

    /**
     * @brief 判断两个日期是否为同一天
     * @param firstDate 第一个日期
     * @param secondDate 第二个日期
     * @returns 同一天返回 true
     */
    function isSameDay(firstDate, secondDate) {
        return firstDate.getFullYear() === secondDate.getFullYear()
                && firstDate.getMonth() === secondDate.getMonth()
                && firstDate.getDate() === secondDate.getDate()
    }

    /**
     * @brief 在日历视图和文本输入模式之间切换
     */
    function toggleInputMode() {
        _inputMode = _inputMode === 2 ? 0 : 2
        if (_inputMode === 2 && dateInput) {
            dateInput.text = Qt.formatDate(selectedDate, "MM/dd/yyyy")
            dateInput.errorText = ""
        }
    }

    /**
     * @brief 解析并应用文本输入框中的日期
     * @details 支持 MM/DD/YYYY、MM-DD-YYYY、MM.DD.YYYY 格式。
     *          解析成功则更新选中日期并切回日历视图；失败则显示错误提示。
     * @returns 解析成功返回 true，否则返回 false
     */
    function applyInputDate() {
        const parts = dateInput.text.trim().split(/[\/.-]/)

        if (parts.length === 3) {
            const month = parseInt(parts[0], 10) - 1
            const day = parseInt(parts[1], 10)
            const year = parseInt(parts[2], 10)
            const parsedDate = new Date(year, month, day)

            if (!isNaN(parsedDate.getTime())
                    && parsedDate.getFullYear() === year
                    && parsedDate.getMonth() === month
                    && parsedDate.getDate() === day
                    && year >= minimumYear
                    && year <= maximumYear) {
                selectedDate = parsedDate
                _viewDate = new Date(parsedDate)
                _inputMode = 0
                dateInput.errorText = ""
                return true
            }
        }

        dateInput.errorText = "Invalid date"
        return false
    }

    onSelectedDateChanged: {
        if (!overlayLayer.visible) {
            syncFromSelection()
        }
    }

    component IconActionButton : Item {
        id: iconButton

        property url iconSource: ""
        property color iconColor: Theme.textMuted
        property real iconSize: 40
        property bool colorize: true
        property bool enabled: true
        readonly property bool _showHighlight: iconMouseArea.containsMouse || iconMouseArea.pressed
        readonly property real _buttonSize: Math.max(40, iconSize + 12)
        signal clicked()

        implicitWidth: _buttonSize
        implicitHeight: _buttonSize

        Rectangle {
            id: hoverBackground
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            radius: width / 2
            color: iconMouseArea.pressed
                ? control.colorWithAlpha(iconButton.iconColor, 0.14)
                : (iconMouseArea.containsMouse ? control.colorWithAlpha(iconButton.iconColor, 0.08) : "transparent")
            opacity: iconButton._showHighlight ? 1 : 0
            scale: iconButton._showHighlight ? 1 : 0.92

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
            anchors.fill: hoverBackground
            source: hoverBackground
            shadowEnabled: iconButton._showHighlight
            shadowColor: control.colorWithAlpha(Theme.shadowFloating, iconMouseArea.pressed ? 0.28 : 0.18)
            shadowBlur: 0.9
            shadowScale: 1.12
            opacity: hoverBackground.opacity
            z: -1
        }

        Image {
            id: iconImage
            anchors.centerIn: parent
            width: iconButton.iconSize
            height: iconButton.iconSize
            source: iconButton.iconSource
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
            visible: !iconButton.colorize
        }

        MultiEffect {
            anchors.fill: iconImage
            source: iconImage
            colorization: 1.0
            colorizationColor: iconButton.enabled
                ? iconButton.iconColor
                : control.colorWithAlpha(iconButton.iconColor, 0.38)
            visible: iconButton.colorize
        }

        MouseArea {
            id: iconMouseArea
            anchors.fill: parent
            enabled: iconButton.enabled
            hoverEnabled: enabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: iconButton.clicked()
        }
    }

    component TextActionButton : Item {
        id: textButton

        property string text: ""
        property string icon: ""
        property color contentColor: Theme.primary
        property bool enabled: true
        signal clicked()

        implicitWidth: rowLayout.implicitWidth + 24
        implicitHeight: 40

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: textMouseArea.pressed
                ? control.colorWithAlpha(textButton.contentColor, 0.12)
                : (textMouseArea.containsMouse ? control.colorWithAlpha(textButton.contentColor, 0.08) : "transparent")

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        RowLayout {
            id: rowLayout
            anchors.centerIn: parent
            spacing: 8

            Text {
                text: textButton.text
                color: textButton.enabled
                    ? textButton.contentColor
                    : control.colorWithAlpha(textButton.contentColor, 0.38)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
            }

            Text {
                visible: textButton.icon !== ""
                text: textButton.icon
                color: textButton.enabled
                    ? textButton.contentColor
                    : control.colorWithAlpha(textButton.contentColor, 0.38)
                font.family: Theme.iconFontFamily
                font.pixelSize: 18
                verticalAlignment: Text.AlignVCenter
            }
        }

        MouseArea {
            id: textMouseArea
            anchors.fill: parent
            enabled: textButton.enabled
            hoverEnabled: enabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: textButton.clicked()
        }
    }

    Item {
        id: overlayLayer
        visible: false
        focus: visible

        Keys.onEscapePressed: control.close(true)

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
            }
        }

        ParallelAnimation {
            id: exitAnimation

            onFinished: {
                overlayLayer.visible = false
                if (control._emitRejectedOnClose) {
                    control._emitRejectedOnClose = false
                    control.rejected()
                }
                control.closed()
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
            }
        }

        Rectangle {
            id: scrim
            anchors.fill: parent
            color: Theme.textTitle
            opacity: 0.0

            MouseArea {
                anchors.fill: parent
                onClicked: control.close(true)
                onWheel: function(wheel) { wheel.accepted = true }
            }
        }

        Item {
            id: animationWrapper
            anchors.centerIn: parent
            width: control._dialogWidth
            height: control._dialogHeight
            focus: true

            Behavior on height {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }

            Rectangle {
                id: pickerContainer
                anchors.fill: parent
                radius: 28
                color: Theme.primaryLighter
                clip: true

                MouseArea {
                    anchors.fill: parent
                    onWheel: function(wheel) { wheel.accepted = true }
                }

                Behavior on height {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutQuad
                    }
                }

                ColumnLayout {
                    id: mainColumn
                    anchors.fill: parent
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 24
                        spacing: 36

                        Text {
                            text: control.title
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: Theme.textMuted
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: Qt.formatDate(control.selectedDate, "ddd, MMM d")
                                font.family: Theme.fontFamily
                                font.pixelSize: 32
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                                Layout.fillWidth: true
                            }

                            IconActionButton {
                                iconSource: control._inputMode === 2 ? control._calendarIconSource : control._editIconSource
                                iconColor: Theme.textMuted
                                iconSize: 40
                                onClicked: control.toggleInputMode()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        Layout.preferredHeight: 1
                        color: Theme.border
                        visible: control._inputMode === 2
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: control._inputMode === 2 ? inputViewWrapper.implicitHeight : 340
                        currentIndex: control._inputMode

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 340
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12

                                TextActionButton {
                                    text: Qt.formatDate(control._viewDate, "MMMM yyyy")
                                    onClicked: control._inputMode = 1
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                IconActionButton {
                                    iconSource: control._leftIconSource
                                    iconColor: Theme.textMuted
                                    iconSize: 30
                                    enabled: control._viewDate.getFullYear() > control.minimumYear
                                        || control._viewDate.getMonth() > 0
                                    onClicked: control._viewDate = new Date(control._viewDate.getFullYear(), control._viewDate.getMonth() - 1, 1)
                                }

                                IconActionButton {
                                    iconSource: control._rightIconSource
                                    iconColor: Theme.textMuted
                                    iconSize: 30
                                    enabled: control._viewDate.getFullYear() < control.maximumYear
                                        || control._viewDate.getMonth() < 11
                                    onClicked: control._viewDate = new Date(control._viewDate.getFullYear(), control._viewDate.getMonth() + 1, 1)
                                }
                            }

                            Row {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                spacing: 0

                                Repeater {
                                    model: ["S", "M", "T", "W", "T", "F", "S"]

                                    Item {
                                        required property string modelData

                                        width: parent.width / 7
                                        height: 40

                                        Text {
                                            anchors.centerIn: parent
                                            text: parent.modelData
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            color: Theme.textPrimary
                                        }
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.bottomMargin: 12
                                columns: 7
                                rowSpacing: 0
                                columnSpacing: 0

                                Repeater {
                                    model: control.getFirstDayOfMonth(control._viewDate.getFullYear(), control._viewDate.getMonth())

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: width
                                    }
                                }

                                Repeater {
                                    model: control.getDaysInMonth(control._viewDate.getFullYear(), control._viewDate.getMonth())

                                    Item {
                                        id: dayDelegate
                                        required property int index

                                        property int dayNum: index + 1
                                        property date dateValue: new Date(control._viewDate.getFullYear(), control._viewDate.getMonth(), dayNum)
                                        property bool isSelected: control.isSameDay(dateValue, control.selectedDate)
                                        property bool isToday: control.isSameDay(dateValue, new Date())

                                        Layout.fillWidth: true
                                        Layout.preferredHeight: width

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: Math.min(40, Math.max(28, parent.width - 4))
                                            height: width
                                            radius: width / 2
                                            color: dayDelegate.isSelected ? Theme.primary : "transparent"
                                            border.width: !dayDelegate.isSelected && dayDelegate.isToday ? 1 : 0
                                            border.color: !dayDelegate.isSelected && dayDelegate.isToday
                                                ? Theme.primary
                                                : "transparent"

                                            Text {
                                                anchors.centerIn: parent
                                                text: dayDelegate.dayNum
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                color: dayDelegate.isSelected
                                                    ? Theme.textWhite
                                                    : Theme.textPrimary
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: control.selectedDate = dayDelegate.dateValue
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        ListView {
                            id: yearListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: Math.max(0, control.maximumYear - control.minimumYear + 1)

                            delegate: Item {
                                id: yearDelegate
                                required property int index

                                width: ListView.view.width
                                height: 56
                                property int year: control.minimumYear + index
                                property bool isSelected: year === control._viewDate.getFullYear()

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 100
                                    height: 40
                                    radius: 20
                                    color: yearDelegate.isSelected ? Theme.primary : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: yearDelegate.year
                                        font.family: Theme.fontFamily
                                        font.pixelSize: yearDelegate.isSelected ? 24 : 16
                                        font.weight: yearDelegate.isSelected ? Font.Bold : Font.Normal
                                        color: yearDelegate.isSelected
                                            ? Theme.textWhite
                                            : Theme.textPrimary
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const targetYear = yearDelegate.year
                                            const targetMonth = control._viewDate.getMonth()
                                            const targetDay = Math.min(control._viewDate.getDate(), control.getDaysInMonth(targetYear, targetMonth))
                                            control._viewDate = new Date(targetYear, targetMonth, targetDay)
                                            control._inputMode = 0
                                        }
                                    }
                                }
                            }

                            onVisibleChanged: {
                                if (visible && model > 0) {
                                    const targetIndex = Math.max(0, Math.min(model - 1, control._viewDate.getFullYear() - control.minimumYear))
                                    positionViewAtIndex(targetIndex, ListView.Center)
                                }
                            }
                        }

                        Item {
                            id: inputViewWrapper
                            Layout.fillWidth: true
                            implicitHeight: inputViewLayout.implicitHeight + 48

                            ColumnLayout {
                                id: inputViewLayout
                                anchors.fill: parent
                                anchors.margins: 24
                                spacing: 16

                                TextFields {
                                    id: dateInput
                                    Layout.fillWidth: true
                                    label: "Enter Date"
                                    placeholderText: "MM/DD/YYYY"
                                    text: Qt.formatDate(control.selectedDate, "MM/dd/yyyy")
                                    type: "outlined"
                                    labelBackgroundColor: Theme.primaryLighter
                                    onAccepted: control.applyInputDate()
                                }

                                Binding {
                                    target: dateInput
                                    property: "text"
                                    value: Qt.formatDate(control.selectedDate, "MM/dd/yyyy")
                                    when: control._inputMode === 2 && !dateInput.focused
                                }

                                Text {
                                    text: "Format: MM/DD/YYYY"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 24
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                        }

                        IconActionButton {
                            iconSource: control._errorIconSource
                            iconSize: 30
                            colorize: false
                            onClicked: control.close(true)
                        }

                        IconActionButton {
                            iconSource: control._correctIconSource
                            iconSize: 30
                            colorize: false
                            onClicked: {
                                if (control._inputMode === 2) {
                                    if (!control.applyInputDate()) {
                                        return
                                    }
                                }

                                control.accepted(control.selectedDate)
                                control.close()
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 12
                    }
                }
            }

            MultiEffect {
                source: pickerContainer
                anchors.fill: pickerContainer
                shadowEnabled: true
                shadowColor: Theme.shadowFloating
                shadowBlur: 12
                shadowVerticalOffset: 4
                shadowOpacity: 0.3
                z: -1
            }
        }
    }
}
