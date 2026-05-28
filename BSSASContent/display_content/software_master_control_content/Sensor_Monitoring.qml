/**
 * @file Sensor_Monitoring.qml
 * @brief 传感器监测页面。在归一化腹部坐标系中配置传感器解剖位置，支持坐标微调与批量重置。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS
import MangoComponent

Item {
    id: root

    property var localizationPoints: []
    property var workingPoints: []
    property var applyLocalizationAndBack: null
    property int selectedPointIndex: -1
    property bool selectedAdjustX: true
    readonly property color accentColor: Theme.primary
    readonly property color borderColor: colorWithAlpha(accentColor, 0.34)
    readonly property color panelColor: colorWithAlpha(Theme.primaryLight, 0.24)
    readonly property color chipColor: Theme.disabledBg
    readonly property real wheelStep: 0.01
    readonly property int pointRowSpacing: 8
    readonly property bool compactContent: Constants.isCompactContent(width, height)
    readonly property bool stackedContent: compactContent || Constants.isNarrowContent(width)
    readonly property int pageMargin: compactContent ? 20 : 30
    readonly property int contentPanelHeight: stackedContent
        ? Math.max(700, Math.min(840, height - pageMargin * 2))
        : Math.max(520, height - pageMargin * 2 - 118)

    /**
     * @brief 为源颜色叠加 alpha 通道值，返回新的 rgba 颜色。
     * @param sourceColor 源颜色
     * @param alphaValue alpha 值 [0,1]
     * @returns 带 alpha 的 Qt.rgba 颜色值
     */
    function colorWithAlpha(sourceColor, alphaValue) {
        const c = Qt.color(sourceColor)
        return Qt.rgba(c.r, c.g, c.b, alphaValue)
    }

    /**
     * @brief 将值限制在最小与最大值之间。
     * @param value 输入值
     * @param minimumValue 最小值
     * @param maximumValue 最大值
     * @returns 夹紧后的值
     */
    function clamp(value, minimumValue, maximumValue) {
        return Math.max(minimumValue, Math.min(maximumValue, value))
    }

    /**
     * @brief 将数值转换为保留两位小数的字符串。
     * @param value 数值
     * @returns 两位小数字符串
     */
    function toFixedString(value) {
        return Number(value).toFixed(2)
    }

    /**
     * @brief 将数字 1-10 转换为中文小写数字（一二三...），超出范围返回原字符串。
     * @param value 输入数字
     * @returns 中文数字字符串
     */
    function toChineseNumber(value) {
        const numbers = ["一", "二", "三", "四", "五", "六", "七", "八", "九", "十"]
        const n = Number(value)
        if (!isNaN(n) && n >= 1 && n <= numbers.length) {
            return numbers[n - 1]
        }
        return String(value)
    }

    /**
     * @brief 将通道标识符（如 CH1）转换为中文显示名（如 通道一）。
     * @param channelText 通道标识文本
     * @param pointIndex 定位点索引（回退使用）
     * @returns 中文通道名称
     */
    function displayChannel(channelText, pointIndex) {
        const source = channelText ? String(channelText) : ("CH" + (pointIndex + 1))
        const matched = /^CH(\d+)$/i.exec(source)
        if (matched && matched.length > 1) {
            return "通道" + root.toChineseNumber(Number(matched[1]))
        }
        return source
    }

    /**
     * @brief 从解剖标签中提取括号内的简称，如 "回盲部-最核心区" 保持不变；若无括号则返回原值。
     * @param labelText 原始解剖标签
     * @returns 提取后的解剖简称
     */
    function anatomicalLabel(labelText) {
        const source = labelText ? String(labelText) : ""
        const matched = /^[^（）()]+[（(]([^（）()]+)[）)]$/.exec(source)
        return matched && matched.length > 1 ? matched[1] : source
    }

    /**
     * @brief 深拷贝定位点数组，缺失或无效字段用默认值填充并夹紧坐标范围。
     * @param sourcePoints 源定位点数组
     * @returns 合法化的定位点数组
     */
    function copyPoints(sourcePoints) {
        const defaults = root.defaultPoints()
        const target = []
        if (!sourcePoints) {
            return defaults
        }

        const sourceCount = Math.min(sourcePoints.length, defaults.length)
        for (let i = 0; i < sourceCount; ++i) {
            const point = sourcePoints[i]
            const defaultPoint = i < defaults.length ? defaults[i] : defaults[defaults.length - 1]
            const rawX = Number(point && point.x !== undefined ? point.x : defaultPoint.x)
            const rawY = Number(point && point.y !== undefined ? point.y : defaultPoint.y)
            target.push({
                channel: point && point.channel ? point.channel : defaultPoint.channel,
                label: point && point.label ? root.anatomicalLabel(point.label) : defaultPoint.label,
                x: isNaN(rawX) ? defaultPoint.x : root.clamp(rawX, -1.0, 1.0),
                y: isNaN(rawY) ? defaultPoint.y : root.clamp(rawY, 0.0, 1.0)
            })
        }

        for (let j = target.length; j < defaults.length; ++j) {
            target.push(defaults[j])
        }

        return target
    }

    /**
     * @brief 返回七个传感器的默认解剖定位点（回盲部、膀胱上方/小肠下段、乙状结肠等）。
     * @returns 默认定位点数组
     */
    function defaultPoints() {
        return [
            ({ channel: "CH1", label: "回盲部-最核心区", x: 0.52, y: 0.28 }),
            ({ channel: "CH2", label: "膀胱上方/小肠下段", x: 0.00, y: 0.22 }),
            ({ channel: "CH3", label: "乙状结肠", x: -0.50, y: 0.28 }),
            ({ channel: "CH4", label: "升结肠/小肠右", x: 0.58, y: 0.56 }),
            ({ channel: "CH5", label: "脐部", x: 0.00, y: 0.50 }),
            ({ channel: "CH6", label: "降结肠/小肠左", x: -0.58, y: 0.56 }),
            ({ channel: "CH7", label: "肝曲/升结肠上段", x: 0.62, y: 0.74 })
        ]
    }

    /**
     * @brief 更新指定索引定位点的 (x, y) 坐标并夹紧至合法范围。
     * @param index 定位点索引
     * @param xValue 新的 x 坐标 [-1,1]
     * @param yValue 新的 y 坐标 [0,1]
     */
    function updatePoint(index, xValue, yValue) {
        const nextPoints = root.copyPoints(root.workingPoints)
        if (index < 0 || index >= nextPoints.length) {
            return
        }

        nextPoints[index].x = root.clamp(Number(xValue), -1.0, 1.0)
        nextPoints[index].y = root.clamp(Number(yValue), 0.0, 1.0)
        root.workingPoints = nextPoints
    }

    /**
     * @brief 微调指定定位点的 x 或 y 坐标，方向正值增大、负值减小，步长为 wheelStep。
     * @param index 定位点索引
     * @param adjustX 是否调整 x 轴（否则调整 y 轴）
     * @param direction 调整方向：1 增大，-1 减小，0 不调整
     */
    function adjustPointAxis(index, adjustX, direction) {
        const point = root.workingPoints && index >= 0 && index < root.workingPoints.length
            ? root.workingPoints[index]
            : null
        if (!point || direction === 0) {
            return
        }

        const currentX = Number(point.x)
        const currentY = Number(point.y)
        if (adjustX) {
            const nextX = root.clamp(currentX + direction * root.wheelStep, -1.0, 1.0)
            root.updatePoint(index, Math.round(nextX * 100) / 100, currentY)
        } else {
            const nextY = root.clamp(currentY + direction * root.wheelStep, 0.0, 1.0)
            root.updatePoint(index, currentX, Math.round(nextY * 100) / 100)
        }
    }

    /**
     * @brief 选中指定定位点的坐标字段（x 或 y）用于滚轮微调。
     * @param index 定位点索引
     * @param adjustX 是否选中 x 轴字段
     */
    function selectField(index, adjustX) {
        root.selectedPointIndex = index
        root.selectedAdjustX = adjustX
    }

    /**
     * @brief 判断指定定位点的坐标字段是否当前选中。
     * @param index 定位点索引
     * @param adjustX 是否查询 x 轴字段
     * @returns 是否选中
     */
    function isFieldSelected(index, adjustX) {
        return root.selectedPointIndex === index && root.selectedAdjustX === adjustX
    }

    /**
     * @brief 重置所有定位点到默认值并清除选中状态。
     */
    function resetToDefaults() {
        root.workingPoints = root.copyPoints(root.defaultPoints())
        root.selectedPointIndex = -1
        root.selectedAdjustX = true
    }

    /**
     * @brief 从滚轮事件中提取纵向滚动增量（优先 angleDelta.y，其次 pixelDelta.y）。
     * @param event 滚轮事件对象
     * @returns 纵向滚动增量，无有效值返回 0
     */
    function wheelDeltaFromEvent(event) {
        if (event && event.angleDelta && event.angleDelta.y !== undefined && event.angleDelta.y !== 0) {
            return Number(event.angleDelta.y)
        }
        if (event && event.pixelDelta && event.pixelDelta.y !== undefined && event.pixelDelta.y !== 0) {
            return Number(event.pixelDelta.y)
        }
        return 0
    }

    /**
     * @brief 传感器坐标值显示字段组件，只读展示 x 或 y 坐标值并高亮当前选中的字段。
     */
    component SensorValueField : TextFields {
        id: control
        property real value: 0
        property bool adjustX: true
        property int pointIndex: -1
        readonly property bool isSelected: root.isFieldSelected(control.pointIndex, control.adjustX)

        type: "outlined"
        fieldHeight: Math.max(40, control.height)
        cornerRadius: 10
        contentPadding: Math.max(8, Math.round(control.height * 0.16))
        iconPadding: contentPadding
        font: Qt.font({
            family: Theme.fontFamily,
            pixelSize: Math.max(18, Math.min(28, Math.round(control.height * 0.42))),
            weight: Font.Medium
        })
        contentColor: Theme.textPrimary
        placeholderColor: root.colorWithAlpha(Theme.textMuted, 0.7)
        inactiveLabelColor: Theme.textMuted
        inactiveOutlineColor: control.isSelected
            ? root.accentColor
            : root.colorWithAlpha(Theme.textMuted, 0.55)
        activeAccentColor: root.accentColor
        forceFocused: control.isSelected
        useInputFocusForOutline: false
        focusOutlineStrokeWidth: 3
        fieldBackgroundColor: Theme.disabledBg
        labelBackgroundColor: fieldBackgroundColor
        floatPlaceholder: true
        readOnly: true
        selectByMouse: false
        text: root.toFixedString(control.value)
    }

    /**
     * @brief 提交当前定位点配置并通过回调切回主控页面。
     */
    function commitAndBack() {
        if (typeof root.applyLocalizationAndBack === "function") {
            root.applyLocalizationAndBack(root.copyPoints(root.workingPoints))
        }
    }

    Component.onCompleted: {
        if (!root.localizationPoints || root.localizationPoints.length === 0) {
            root.localizationPoints = root.defaultPoints()
        }
        root.workingPoints = root.copyPoints(root.localizationPoints)
    }

    onLocalizationPointsChanged: {
        root.workingPoints = root.copyPoints(root.localizationPoints)
    }

    ScrollView {
        id: sensorScrollView
        anchors {
            fill: parent
            topMargin: root.pageMargin
            bottomMargin: root.pageMargin
            leftMargin: root.pageMargin
            rightMargin: root.pageMargin
        }
        clip: true
        contentWidth: availableWidth
        contentHeight: rootLayout.height

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: rootLayout
            x: 0
            y: 0
            width: sensorScrollView.availableWidth
            height: Math.max(sensorScrollView.availableHeight, implicitHeight)
            spacing: 18

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 4
                implicitHeight: 46
                radius: 2
                color: root.accentColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "传感器监测"
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSectionTitle; font.weight: Font.Medium
                    color: root.accentColor
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "坐标映射到归一化腹部坐标系：横向 x in [-1, 1]，纵向 y in [0, 1]。"
                    font.pixelSize: 14
                    color: Theme.textMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: !root.stackedContent
            implicitHeight: contentRow.implicitHeight + 36
            radius: 22
            color: root.panelColor
            border.width: 1
            border.color: root.borderColor

            GridLayout {
                id: contentRow
                anchors.fill: parent
                anchors.margins: 18
                columns: root.stackedContent ? 1 : 2
                columnSpacing: 16
                rowSpacing: 16

                Rectangle {
                    id: mapCard
                    Layout.fillWidth: true
                    Layout.fillHeight: !root.stackedContent
                    Layout.minimumWidth: root.stackedContent ? 0 : 360
                    Layout.preferredWidth: root.stackedContent ? contentRow.width : Math.max(380, contentRow.width * 0.6)
                    implicitHeight: (root.stackedContent ? contentRow.width : Math.max(380, contentRow.width * 0.6)) / 2
                    radius: 18
                    color: "transparent"
                    border.width: 1
                    border.color: root.borderColor

                    Rectangle {
                        id: abdomenMap
                        anchors.fill: parent
                        anchors.margins: 14
                        radius: 14
                        color: root.colorWithAlpha(Theme.cardBg, 0.48)
                        border.width: 1
                        border.color: root.colorWithAlpha(root.accentColor, 0.24)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1
                            color: root.colorWithAlpha(Theme.textMuted, 0.35)
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 1
                            color: root.colorWithAlpha(Theme.textMuted, 0.35)
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 26
                            anchors.top: parent.top
                            anchors.topMargin: 6
                            text: "y=1"
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            text: "y=0"
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "x=-1"
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "x=1"
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }

                        Repeater {
                            model: root.workingPoints.length

                            delegate: Item {
                                required property int index
                                readonly property int pointIndex: index
                                readonly property var pointData: root.workingPoints[pointIndex]
                                readonly property real safeX: (pointData && pointData.x !== undefined) ? pointData.x : 0.0
                                readonly property real safeY: (pointData && pointData.y !== undefined) ? pointData.y : 0.0

                                visible: pointData !== undefined && pointData.x !== undefined && pointData.y !== undefined
                                width: 18
                                height: 18
                                x: ((safeX + 1.0) * 0.5) * (abdomenMap.width - width)
                                y: (1.0 - safeY) * (abdomenMap.height - height)

                                Rectangle {
                                    anchors.fill: parent
                                    radius: width / 2
                                    color: root.accentColor
                                    border.width: 1
                                    border.color: root.colorWithAlpha(Theme.textWhite, 0.7)
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.bottom
                                    anchors.topMargin: 4
                                    text: root.displayChannel(pointData && pointData.channel ? pointData.channel : "", pointIndex)
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: root.accentColor
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: root.stackedContent
                    Layout.fillHeight: !root.stackedContent
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumWidth: root.stackedContent ? 0 : 320
                    Layout.preferredWidth: root.stackedContent ? contentRow.width : Math.max(360, contentRow.width * 0.4)
                    implicitHeight: listColumnLayout.implicitHeight + 20
                    Layout.maximumWidth: root.stackedContent ? 100000 : 520
                    radius: 18
                    color: "transparent"
                    border.width: 1
                    border.color: root.borderColor

                    ColumnLayout {
                        id: listColumnLayout
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Text {
                            text: "传感器解剖位置"
                            font.pixelSize: 16
                            font.bold: true
                            color: root.accentColor
                        }

                        Column {
                            id: pointListColumn
                            Layout.fillWidth: true
                            spacing: root.pointRowSpacing

                            Repeater {
                                model: root.workingPoints.length

                                delegate: Rectangle {
                                    required property int index
                                    readonly property int rowPointIndex: index
                                    readonly property var pointData: root.workingPoints[rowPointIndex]
                                    readonly property real rowTextSize: Math.max(16, Math.min(22, Math.round(height * 0.26)))
                                    readonly property real coordinateTextSize: Math.max(18, Math.min(26, Math.round(height * 0.30)))

                                    width: pointListColumn.width
                                    height: 64
                                    radius: 12
                                        color: root.chipColor
                                        border.width: 1
                                        border.color: root.colorWithAlpha(root.accentColor, 0.22)

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 8

                                            Text {
                                                Layout.preferredWidth: Math.max(72, parent.width * 0.15)
                                                Layout.minimumWidth: 64
                                                Layout.fillHeight: true
                                                text: root.displayChannel(pointData && pointData.channel ? pointData.channel : "", rowPointIndex)
                                                elide: Text.ElideRight
                                                font.pixelSize: rowTextSize
                                                font.bold: true
                                                color: root.accentColor
                                                horizontalAlignment: Text.AlignLeft
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 128
                                                Layout.fillHeight: true
                                                text: pointData && pointData.label ? root.anatomicalLabel(pointData.label) : ""
                                                elide: Text.ElideNone
                                                fontSizeMode: Text.HorizontalFit
                                                minimumPixelSize: 14
                                                font.pixelSize: rowTextSize
                                                font.weight: Font.Medium
                                                color: Theme.textPrimary
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Text {
                                                Layout.fillHeight: true
                                                text: "x"
                                                font.pixelSize: coordinateTextSize
                                                font.bold: true
                                                Layout.preferredWidth: 18
                                                Layout.leftMargin: 2
                                                color: root.isFieldSelected(rowPointIndex, true)
                                                    ? root.accentColor
                                                    : Theme.textMuted
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Item {
                                                Layout.preferredWidth: Math.max(78, parent.width * 0.15)
                                                Layout.minimumWidth: 76
                                                Layout.fillHeight: true

                                                SensorValueField {
                                                    anchors.fill: parent
                                                    value: pointData ? pointData.x : 0
                                                    adjustX: true
                                                    pointIndex: rowPointIndex
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton
                                                    preventStealing: true

                                                    onPressed: root.selectField(rowPointIndex, true)

                                                    onWheel: function(event) {
                                                        if (!root.isFieldSelected(rowPointIndex, true)) {
                                                            return
                                                        }

                                                        const wheelDelta = root.wheelDeltaFromEvent(event)
                                                        if (wheelDelta === 0) {
                                                            event.accepted = true
                                                            return
                                                        }

                                                        event.accepted = true
                                                        root.adjustPointAxis(rowPointIndex, true, wheelDelta > 0 ? 1 : -1)
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillHeight: true
                                                text: "y"
                                                font.pixelSize: coordinateTextSize
                                                font.bold: true
                                                Layout.preferredWidth: 18
                                                Layout.leftMargin: 2
                                                color: root.isFieldSelected(rowPointIndex, false)
                                                    ? root.accentColor
                                                    : Theme.textMuted
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Item {
                                                Layout.preferredWidth: Math.max(78, parent.width * 0.15)
                                                Layout.minimumWidth: 76
                                                Layout.fillHeight: true

                                                SensorValueField {
                                                    anchors.fill: parent
                                                    value: pointData ? pointData.y : 0
                                                    adjustX: false
                                                    pointIndex: rowPointIndex
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton
                                                    preventStealing: true

                                                    onPressed: root.selectField(rowPointIndex, false)

                                                    onWheel: function(event) {
                                                        if (!root.isFieldSelected(rowPointIndex, false)) {
                                                            return
                                                        }

                                                        const wheelDelta = root.wheelDeltaFromEvent(event)
                                                        if (wheelDelta === 0) {
                                                            event.accepted = true
                                                            return
                                                        }

                                                        event.accepted = true
                                                        root.adjustPointAxis(rowPointIndex, false, wheelDelta > 0 ? 1 : -1)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item { Layout.fillWidth: true }

            Button {
                id: resetButton
                Layout.preferredWidth: 140
                Layout.preferredHeight: 44
                text: "重置"
                onClicked: root.resetToDefaults()

                background: Rectangle {
                    radius: 14
                    border.width: 1
                    border.color: root.colorWithAlpha(Theme.textMuted, 0.42)
                    color: resetButton.down
                        ? root.colorWithAlpha(Theme.textMuted, 0.20)
                        : root.colorWithAlpha(Theme.cardBg, resetButton.hovered ? 0.90 : 0.82)
                }

                contentItem: Text {
                    text: resetButton.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 16
                    font.bold: true
                    color: Theme.textPrimary
                }
            }

            Button {
                id: confirmButton
                Layout.preferredWidth: 140
                Layout.preferredHeight: 44
                text: "确认"
                onClicked: root.commitAndBack()

                background: Rectangle {
                    radius: 14
                    border.width: 1
                    border.color: root.colorWithAlpha(root.accentColor, 0.42)
                    color: confirmButton.down
                        ? root.colorWithAlpha(root.accentColor, 0.28)
                        : root.colorWithAlpha(root.accentColor, confirmButton.hovered ? 0.2 : 0.14)
                }

                contentItem: Text {
                    text: confirmButton.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 16
                    font.bold: true
                    color: root.accentColor
                }
            }
        }
        }
    }
}
