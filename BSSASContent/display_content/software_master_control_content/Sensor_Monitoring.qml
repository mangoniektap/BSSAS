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

    function colorWithAlpha(sourceColor, alphaValue) {
        const c = Qt.color(sourceColor)
        return Qt.rgba(c.r, c.g, c.b, alphaValue)
    }

    function clamp(value, minimumValue, maximumValue) {
        return Math.max(minimumValue, Math.min(maximumValue, value))
    }

    function toFixedString(value) {
        return Number(value).toFixed(2)
    }

    function toChineseNumber(value) {
        const numbers = ["一", "二", "三", "四", "五", "六", "七", "八", "九", "十"]
        const n = Number(value)
        if (!isNaN(n) && n >= 1 && n <= numbers.length) {
            return numbers[n - 1]
        }
        return String(value)
    }

    function displayChannel(channelText, pointIndex) {
        const source = channelText ? String(channelText) : ("CH" + (pointIndex + 1))
        const matched = /^CH(\d+)$/i.exec(source)
        if (matched && matched.length > 1) {
            return "通道" + root.toChineseNumber(Number(matched[1]))
        }
        return source
    }

    function anatomicalLabel(labelText) {
        const source = labelText ? String(labelText) : ""
        const matched = /^[^（）()]+[（(]([^（）()]+)[）)]$/.exec(source)
        return matched && matched.length > 1 ? matched[1] : source
    }

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

    function updatePoint(index, xValue, yValue) {
        const nextPoints = root.copyPoints(root.workingPoints)
        if (index < 0 || index >= nextPoints.length) {
            return
        }

        nextPoints[index].x = root.clamp(Number(xValue), -1.0, 1.0)
        nextPoints[index].y = root.clamp(Number(yValue), 0.0, 1.0)
        root.workingPoints = nextPoints
    }

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

    function selectField(index, adjustX) {
        root.selectedPointIndex = index
        root.selectedAdjustX = adjustX
    }

    function isFieldSelected(index, adjustX) {
        return root.selectedPointIndex === index && root.selectedAdjustX === adjustX
    }

    function resetToDefaults() {
        root.workingPoints = root.copyPoints(root.defaultPoints())
        root.selectedPointIndex = -1
        root.selectedAdjustX = true
    }

    function wheelDeltaFromEvent(event) {
        if (event && event.angleDelta && event.angleDelta.y !== undefined && event.angleDelta.y !== 0) {
            return Number(event.angleDelta.y)
        }
        if (event && event.pixelDelta && event.pixelDelta.y !== undefined && event.pixelDelta.y !== 0) {
            return Number(event.pixelDelta.y)
        }
        return 0
    }

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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
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
            Layout.fillHeight: true
            radius: 22
            color: root.panelColor
            border.width: 1
            border.color: root.borderColor

            RowLayout {
                id: contentRow
                anchors.fill: parent
                anchors.margins: 18
                spacing: 16

                Rectangle {
                    id: mapCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 360
                    Layout.preferredWidth: Math.max(380, contentRow.width * 0.6)
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
                    Layout.fillHeight: true
                    Layout.minimumWidth: 320
                    Layout.preferredWidth: Math.max(360, contentRow.width * 0.4)
                    Layout.maximumWidth: 520
                    radius: 18
                    color: "transparent"
                    border.width: 1
                    border.color: root.borderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Text {
                            text: "传感器解剖位置"
                            font.pixelSize: 16
                            font.bold: true
                            color: root.accentColor
                        }

                        ScrollView {
                            id: pointsScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AlwaysOff
                            }

                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AlwaysOff
                            }

                            Column {
                                id: pointListColumn
                                width: pointsScroll.availableWidth
                                height: Math.max(
                                    pointsScroll.availableHeight,
                                    root.workingPoints.length * 64 +
                                        Math.max(0, root.workingPoints.length - 1) * spacing)
                                spacing: root.pointRowSpacing

                                Repeater {
                                    model: root.workingPoints.length

                                    delegate: Rectangle {
                                        required property int index
                                        readonly property int rowPointIndex: index
                                        readonly property var pointData: root.workingPoints[rowPointIndex]
                                        readonly property real rowTextSize: Math.max(16, Math.min(22, Math.round(height * 0.26)))
                                        readonly property real coordinateTextSize: Math.max(18, Math.min(26, Math.round(height * 0.30)))

                                        width: parent ? parent.width : 440
                                        height: Math.max(
                                            64,
                                            (pointListColumn.height -
                                                Math.max(0, root.workingPoints.length - 1) * pointListColumn.spacing) /
                                                Math.max(1, root.workingPoints.length))
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
