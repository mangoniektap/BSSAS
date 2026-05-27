/**
 * @file ScientificFilterConfigPopup.qml
 * @brief 科学滤波器配置弹窗，负责 IIR 参数编辑和响应预览。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MangoComponent
import BSSAS

Popup {
    id: root

    property string titleText: qsTr("科学滤波器")
    property int sampleRate: 12000
    property bool filterEnabled: false
    property int prototype: 1
    property int filterType: 0
    property int order: 2
    property real cutoffFrequencyHz: 500
    property real lowCutoffFrequencyHz: 80
    property real highCutoffFrequencyHz: 600
    property real transitionBandwidthHz: 50
    property real stopbandAttenuationDb: 60
    property real passbandRippleDb: 0.5
    property int graphMode: 0
    readonly property real minimumFrequencyHz: 20
    readonly property real maximumPreviewFrequencyHz: 5000
    readonly property real minimumGapHz: 1
    readonly property real minimumBandwidthHz: 1
    readonly property real minimumStopbandAttenuationDb: 1
    readonly property real maximumStopbandAttenuationDb: 180
    readonly property real minimumPassbandRippleDb: 0.01
    readonly property real maximumPassbandRippleDb: 20
    readonly property real maximumFrequencyHz: Math.max(
        minimumFrequencyHz,
        Math.min(maximumPreviewFrequencyHz, sampleRate / 2 - minimumGapHz))
    readonly property bool usesBandEdges: filterType === 2 || filterType === 3
    readonly property bool stopbandEditable: prototype === 3
    readonly property bool passbandEditable: prototype === 2 || prototype === 3
    readonly property string cutoffFrequencyError: usesBandEdges
        ? ""
        : frequencyRangeError(qsTr("截止频率"), cutoffFrequencyHz)
    readonly property string lowCutoffFrequencyError: usesBandEdges
        ? frequencyRangeError(qsTr("低端频率"), lowCutoffFrequencyHz)
        : ""
    readonly property string highCutoffFrequencyError: usesBandEdges
        ? frequencyRangeError(qsTr("高端频率"), highCutoffFrequencyHz)
        : ""
    readonly property string bandEdgeRelationError: bandEdgeError()
    readonly property string transitionBandwidthError: transitionError()
    readonly property string stopbandAttenuationError: stopbandEditable
        ? valueRangeError(
              qsTr("阻带衰减"),
              stopbandAttenuationDb,
              minimumStopbandAttenuationDb,
              maximumStopbandAttenuationDb,
              qsTr("dB"),
              0)
        : ""
    readonly property string passbandRippleError: passbandEditable
        ? valueRangeError(
              qsTr("通带纹波"),
              passbandRippleDb,
              minimumPassbandRippleDb,
              maximumPassbandRippleDb,
              qsTr("dB"),
              2)
        : ""
    readonly property string validationMessage: firstValidationMessage()
    readonly property bool configurationValid: validationMessage.length === 0
    readonly property var currentConfig: ({
        enabled: filterEnabled,
        prototype: prototype,
        filterType: filterType,
        order: order,
        cutoffFrequencyHz: cutoffFrequencyHz,
        lowCutoffFrequencyHz: lowCutoffFrequencyHz,
        highCutoffFrequencyHz: highCutoffFrequencyHz,
        transitionBandwidthHz: transitionBandwidthHz,
        stopbandAttenuationDb: stopbandAttenuationDb,
        passbandRippleDb: passbandRippleDb
    })
    readonly property var responseData: signalPreprocessing.scientificFilterResponse(
        currentConfig,
        sampleRate)
    readonly property real actualRippleDb: Number(responseData.actualRippleDb || 0)
    readonly property string cursorText: {
        if (!isFinite(responseGraph.hoverFrequencyHz) || !isFinite(responseGraph.hoverValue))
            return qsTr("频率 -- Hz    数值 --")
        return qsTr("频率 %1 Hz    数值 %2 %3")
            .arg(responseGraph.hoverFrequencyHz.toFixed(1))
            .arg(responseGraph.hoverValue.toFixed(graphMode === 0 ? 2 : 3))
            .arg(responseGraph.yUnit)
    }

    signal saveRequested(var config)

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function normalizedNumber(value, fallback) {
        const numericValue = Number(value)
        return isFinite(numericValue) ? numericValue : fallback
    }

    function clamp(value, minimumValue, maximumValue) {
        return Math.max(minimumValue, Math.min(maximumValue, value))
    }

    function formatRangeValue(value, decimals) {
        return Number(value).toFixed(decimals)
    }

    function valueRangeError(label, value, minimumValue, maximumValue, unit, decimals) {
        const numericValue = Number(value)
        if (!isFinite(numericValue))
            return qsTr("%1需为有效数字").arg(label)
        if (numericValue < minimumValue || numericValue > maximumValue) {
            return qsTr("%1需位于 %2-%3 %4")
                .arg(label)
                .arg(formatRangeValue(minimumValue, decimals))
                .arg(formatRangeValue(maximumValue, decimals))
                .arg(unit)
        }
        return ""
    }

    function frequencyRangeError(label, value) {
        return valueRangeError(
            label,
            value,
            minimumFrequencyHz,
            maximumFrequencyHz,
            qsTr("Hz"),
            0)
    }

    function bandEdgeError() {
        if (!usesBandEdges ||
                lowCutoffFrequencyError.length > 0 ||
                highCutoffFrequencyError.length > 0) {
            return ""
        }
        if (highCutoffFrequencyHz - lowCutoffFrequencyHz < minimumGapHz) {
            return qsTr("高端频率需至少比低端频率高 %1 Hz")
                .arg(formatRangeValue(minimumGapHz, 0))
        }
        return ""
    }

    function transitionError() {
        const rangeError = valueRangeError(
            qsTr("过渡带宽"),
            transitionBandwidthHz,
            minimumBandwidthHz,
            maximumFrequencyHz,
            qsTr("Hz"),
            0)
        if (rangeError.length > 0)
            return rangeError

        const transitionHz = Number(transitionBandwidthHz)
        if (!usesBandEdges) {
            if (cutoffFrequencyError.length > 0)
                return ""
            if (filterType === 0 &&
                    cutoffFrequencyHz - transitionHz < minimumFrequencyHz) {
                return qsTr("低通需满足：截止频率 - 过渡带宽 ≥ %1 Hz")
                    .arg(formatRangeValue(minimumFrequencyHz, 0))
            }
            if (filterType === 1 &&
                    cutoffFrequencyHz + transitionHz > maximumFrequencyHz) {
                return qsTr("高通需满足：截止频率 + 过渡带宽 ≤ %1 Hz")
                    .arg(formatRangeValue(maximumFrequencyHz, 0))
            }
            return ""
        }

        if (lowCutoffFrequencyError.length > 0 ||
                highCutoffFrequencyError.length > 0 ||
                bandEdgeRelationError.length > 0) {
            return ""
        }
        if (filterType === 2 &&
                lowCutoffFrequencyHz + transitionHz >= highCutoffFrequencyHz - transitionHz) {
            return qsTr("带通需满足：低端频率 + 过渡带宽 < 高端频率 - 过渡带宽")
        }
        if (filterType === 3 &&
                (lowCutoffFrequencyHz - transitionHz < minimumFrequencyHz ||
                 highCutoffFrequencyHz + transitionHz > maximumFrequencyHz)) {
            return qsTr("带阻需在低端左侧和高端右侧预留完整过渡带")
        }
        return ""
    }

    function firstValidationMessage() {
        if (cutoffFrequencyError.length > 0)
            return cutoffFrequencyError
        if (lowCutoffFrequencyError.length > 0)
            return lowCutoffFrequencyError
        if (highCutoffFrequencyError.length > 0)
            return highCutoffFrequencyError
        if (bandEdgeRelationError.length > 0)
            return bandEdgeRelationError
        if (transitionBandwidthError.length > 0)
            return transitionBandwidthError
        if (stopbandAttenuationError.length > 0)
            return stopbandAttenuationError
        if (passbandRippleError.length > 0)
            return passbandRippleError

        const responseError = responseData && responseData.error
            ? String(responseData.error)
            : ""
        return responseError
    }

    function constrainLinkedFrequencies() {
        if (!usesBandEdges)
            return
        if (!isFinite(lowCutoffFrequencyHz) || !isFinite(highCutoffFrequencyHz))
            return
        if (highCutoffFrequencyHz <= lowCutoffFrequencyHz &&
                lowCutoffFrequencyHz + minimumGapHz <= maximumFrequencyHz) {
            highCutoffFrequencyHz = lowCutoffFrequencyHz + minimumGapHz
        } else if (highCutoffFrequencyHz <= lowCutoffFrequencyHz &&
                   highCutoffFrequencyHz - minimumGapHz >= minimumFrequencyHz) {
            lowCutoffFrequencyHz = highCutoffFrequencyHz - minimumGapHz
        }
    }

    function loadFrom(config) {
        filterEnabled = !!config.enabled
        prototype = Math.max(0, Math.min(3, Math.round(root.normalizedNumber(config.prototype, 1))))
        filterType = Math.max(0, Math.min(3, Math.round(root.normalizedNumber(config.filterType, 0))))
        order = Math.max(1, Math.min(12, Math.round(root.normalizedNumber(config.order, 2))))
        cutoffFrequencyHz = root.normalizedNumber(config.cutoffFrequencyHz, 500)
        lowCutoffFrequencyHz = root.normalizedNumber(config.lowCutoffFrequencyHz, 80)
        highCutoffFrequencyHz = root.normalizedNumber(config.highCutoffFrequencyHz, 600)
        transitionBandwidthHz = root.normalizedNumber(config.transitionBandwidthHz, 50)
        stopbandAttenuationDb = root.normalizedNumber(config.stopbandAttenuationDb, 60)
        passbandRippleDb = root.normalizedNumber(config.passbandRippleDb, 0.5)
        graphMode = 0
    }

    function resetDefaults() {
        loadFrom({
            enabled: false,
            prototype: 1,
            filterType: 0,
            order: 2,
            cutoffFrequencyHz: 500,
            lowCutoffFrequencyHz: 80,
            highCutoffFrequencyHz: 600,
            transitionBandwidthHz: 50,
            stopbandAttenuationDb: 60,
            passbandRippleDb: 0.5
        })
    }

    function saveAndClose() {
        if (!configurationValid)
            return
        saveRequested(currentConfig)
        close()
    }

    onLowCutoffFrequencyHzChanged: {
        constrainLinkedFrequencies()
    }
    onHighCutoffFrequencyHzChanged: constrainLinkedFrequencies()
    onFilterTypeChanged: constrainLinkedFrequencies()
    onSampleRateChanged: constrainLinkedFrequencies()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.max(720, Math.min((parent ? parent.width : 1100) - 80, 980))
    height: Math.max(640, Math.min((parent ? parent.height : 820) - 48, 820))
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: BlurGlass {
        blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
        blurAmount: 64
        borderRadius: 25
        overlayOpacity: 0.3
    }

    background: Rectangle {
        radius: 22
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.border
    }

    component NumericField: ColumnLayout {
        id: numericField

        property string labelText: ""
        property real value: 0
        property bool editable: true
        property int decimals: 1
        property real minimumValue: 0
        property real maximumValue: root.maximumFrequencyHz
        property real stepSize: decimals > 1 ? 1 / Math.pow(10, decimals) : 1
        property real dragStepSize: stepSize * 10
        property real dragPixelsPerStep: 8
        property real fieldWidth: 132
        property string errorText: ""
        property real dragStartX: 0
        property real dragStartValue: 0
        property int dragAppliedSteps: 0
        property bool dragActive: false
        readonly property bool hasError: errorText.length > 0
        signal valueEdited(real value)

        function formatValue(value) {
            const numericValue = Number(value)
            if (!isFinite(numericValue))
                return ""
            return numericValue.toFixed(decimals)
        }

        function wheelDeltaFromEvent(wheel) {
            if (wheel.angleDelta && wheel.angleDelta.y !== 0)
                return wheel.angleDelta.y
            if (wheel.pixelDelta && wheel.pixelDelta.y !== 0)
                return wheel.pixelDelta.y
            return 0
        }

        function commitText() {
            const trimmedText = String(numericInput.text).trim()
            numericField.valueEdited(trimmedText.length > 0 ? Number(trimmedText) : NaN)
        }

        function currentNumericValue() {
            const currentTextValue = Number(numericInput.text)
            if (isFinite(currentTextValue))
                return currentTextValue

            const currentValue = Number(value)
            return isFinite(currentValue) ? currentValue : minimumValue
        }

        function applyAdjustedValue(nextValue) {
            numericField.valueEdited(nextValue)
            numericInput.text = formatValue(nextValue)
            numericInput.forceActiveFocus()
            numericInput.selectAll()
        }

        function adjustByAmount(amount) {
            if (!editable || amount === 0)
                return

            const nextValue = root.clamp(
                currentNumericValue() + amount,
                minimumValue,
                maximumValue)
            applyAdjustedValue(nextValue)
        }

        function adjustByWheel(delta) {
            if (delta === 0)
                return

            adjustByAmount((delta > 0 ? 1 : -1) * stepSize)
        }

        function beginDrag(mouseX) {
            dragStartX = mouseX
            dragStartValue = currentNumericValue()
            dragAppliedSteps = 0
            dragActive = false
            numericInput.forceActiveFocus()
            numericInput.selectAll()
        }

        function updateDrag(mouseX) {
            const nextSteps = Math.trunc((mouseX - dragStartX) / Math.max(1, dragPixelsPerStep))
            if (nextSteps === dragAppliedSteps)
                return

            dragActive = true
            dragAppliedSteps = nextSteps
            applyAdjustedValue(root.clamp(
                dragStartValue + nextSteps * dragStepSize,
                minimumValue,
                maximumValue))
        }

        spacing: 5

        Text {
            text: numericField.labelText
            color: numericField.hasError
                ? Theme.danger
                : (numericField.editable ? Theme.textPrimary : root.colorWithAlpha(Theme.textPrimary, 0.38))
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }

        TextField {
            id: numericInput

            Layout.preferredWidth: numericField.fieldWidth
            Layout.preferredHeight: 40
            enabled: numericField.editable
            text: numericField.formatValue(numericField.value)
            color: enabled ? Theme.textPrimary : root.colorWithAlpha(Theme.textPrimary, 0.38)
            horizontalAlignment: TextInput.AlignHCenter
            selectByMouse: true
            selectionColor: Theme.primary
            selectedTextColor: Theme.textWhite
            validator: DoubleValidator {
                bottom: numericField.minimumValue
                top: numericField.maximumValue
                decimals: 3
                notation: DoubleValidator.StandardNotation
            }
            background: Rectangle {
                radius: 8
                border.width: 1
                border.color: numericField.hasError
                    ? Theme.danger
                    : (numericInput.activeFocus
                    ? Theme.primary
                    : root.colorWithAlpha(Theme.primary, numericField.editable ? 0.38 : 0.14))
                color: numericField.editable
                    ? (numericField.hasError ? root.colorWithAlpha(Theme.danger, 0.04) : "transparent")
                    : root.colorWithAlpha(Theme.textPrimary, 0.04)
            }

            onEditingFinished: numericField.commitText()

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                enabled: numericField.editable
                hoverEnabled: true
                preventStealing: true
                cursorShape: pressed ? Qt.SizeHorCursor : Qt.IBeamCursor
                onPressed: function(mouse) {
                    numericField.beginDrag(mouse.x)
                }
                onPositionChanged: function(mouse) {
                    if (pressed)
                        numericField.updateDrag(mouse.x)
                }
                onReleased: {
                    if (!numericField.dragActive)
                        numericInput.selectAll()
                }
                onClicked: {
                    numericInput.forceActiveFocus()
                    numericInput.selectAll()
                }
                onWheel: function(wheel) {
                    if (!numericInput.activeFocus)
                        return

                    const wheelDelta = numericField.wheelDeltaFromEvent(wheel)
                    if (wheelDelta === 0)
                        return

                    numericField.adjustByWheel(wheelDelta)
                    wheel.accepted = true
                }
            }
        }

        onValueChanged: {
            if (!numericInput.activeFocus)
                numericInput.text = formatValue(value)
        }
        onEditableChanged: {
            if (!numericInput.activeFocus)
                numericInput.text = formatValue(value)
        }
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: root.titleText
                color: Theme.textTitle
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("启用")
                color: Theme.textPrimary
                font.pixelSize: 14
            }

            ToggleSwitch {
                checked: root.filterEnabled
                onCheckedChanged: root.filterEnabled = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: [qsTr("频率响应（分贝）"), qsTr("相位（度）"), qsTr("组延迟（毫秒）")]

                Button {
                    id: modeButton

                    required property int index
                    required property string modelData

                    Layout.preferredHeight: 34
                    hoverEnabled: true
                    text: modelData
                    onClicked: root.graphMode = index

                    contentItem: Text {
                        text: modeButton.text
                        color: root.graphMode === modeButton.index ? Theme.textWhite : Theme.primary
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 8
                        color: root.graphMode === modeButton.index
                            ? Theme.primary
                            : (modeButton.hovered ? root.colorWithAlpha(Theme.primary, 0.08) : "transparent")
                        border.width: 1
                        border.color: Theme.primary
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.cursorText
                color: Theme.textMuted
                font.family: Theme.numberFontFamily
                font.pixelSize: 13
            }
        }

        ScientificFilterResponseGraph {
            id: responseGraph
            Layout.fillWidth: true
            Layout.preferredHeight: 450
            responseData: root.responseData
            mode: root.graphMode
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Text {
                text: qsTr("滤波器种类")
                color: Theme.textPrimary
                font.pixelSize: 14
            }

            Select {
                Layout.preferredWidth: 170
                Layout.preferredHeight: 42
                currentIndex: root.prototype
                model: [qsTr("贝塞尔"), qsTr("巴特沃斯"), qsTr("切比雪夫 I 型"), qsTr("椭圆")]
                delegateHeight: 30
                visibleCount: 4
                textColor: Theme.textPrimary
                outlineColor: Theme.primaryBorder
                activeOutlineColor: Theme.primary
                indicatorColor: Theme.primary
                popupColor: Theme.textWhite
                popupBorderColor: Theme.primaryBorder
                optionHighlightColor: root.colorWithAlpha(Theme.primary, 0.12)
                onCurrentIndexChanged: root.prototype = currentIndex
            }

            Text {
                text: qsTr("滤波类型")
                color: Theme.textPrimary
                font.pixelSize: 14
            }

            Select {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 42
                currentIndex: root.filterType
                model: [qsTr("低通"), qsTr("高通"), qsTr("带通"), qsTr("带阻")]
                delegateHeight: 30
                visibleCount: 4
                textColor: Theme.textPrimary
                outlineColor: Theme.primaryBorder
                activeOutlineColor: Theme.primary
                indicatorColor: Theme.primary
                popupColor: Theme.textWhite
                popupBorderColor: Theme.primaryBorder
                optionHighlightColor: root.colorWithAlpha(Theme.primary, 0.12)
                onCurrentIndexChanged: root.filterType = currentIndex
            }

            Text {
                text: qsTr("滤波器阶数")
                color: Theme.textPrimary
                font.pixelSize: 14
            }

            Select {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 42
                currentIndex: Math.max(0, root.order - 1)
                model: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
                delegateHeight: 28
                visibleCount: 6
                textColor: Theme.textPrimary
                outlineColor: Theme.primaryBorder
                activeOutlineColor: Theme.primary
                indicatorColor: Theme.primary
                popupColor: Theme.textWhite
                popupBorderColor: Theme.primaryBorder
                optionHighlightColor: root.colorWithAlpha(Theme.primary, 0.12)
                onCurrentIndexChanged: root.order = currentIndex + 1
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            NumericField {
                labelText: root.usesBandEdges ? qsTr("低端频率 Hz") : qsTr("截止频率 Hz")
                value: root.usesBandEdges ? root.lowCutoffFrequencyHz : root.cutoffFrequencyHz
                minimumValue: root.minimumFrequencyHz
                maximumValue: root.maximumFrequencyHz
                stepSize: 1
                dragStepSize: 10
                errorText: root.usesBandEdges ? root.lowCutoffFrequencyError : root.cutoffFrequencyError
                onValueEdited: function(value) {
                    if (root.usesBandEdges)
                        root.lowCutoffFrequencyHz = value
                    else
                        root.cutoffFrequencyHz = value
                }
            }

            NumericField {
                labelText: qsTr("高端频率 Hz")
                value: root.highCutoffFrequencyHz
                editable: root.usesBandEdges
                minimumValue: root.minimumFrequencyHz
                maximumValue: root.maximumFrequencyHz
                stepSize: 1
                dragStepSize: 10
                errorText: root.highCutoffFrequencyError.length > 0
                    ? root.highCutoffFrequencyError
                    : root.bandEdgeRelationError
                onValueEdited: function(value) { root.highCutoffFrequencyHz = value }
            }

            NumericField {
                labelText: qsTr("过渡带宽 Hz")
                value: root.transitionBandwidthHz
                minimumValue: root.minimumBandwidthHz
                maximumValue: root.maximumFrequencyHz
                stepSize: 1
                dragStepSize: 10
                errorText: root.transitionBandwidthError
                onValueEdited: function(value) { root.transitionBandwidthHz = value }
            }

            NumericField {
                labelText: qsTr("阻带衰减 dB")
                value: root.stopbandAttenuationDb
                editable: root.stopbandEditable
                minimumValue: root.minimumStopbandAttenuationDb
                maximumValue: root.maximumStopbandAttenuationDb
                stepSize: 1
                dragStepSize: 5
                errorText: root.stopbandAttenuationError
                onValueEdited: function(value) { root.stopbandAttenuationDb = value }
            }

            NumericField {
                labelText: qsTr("通带纹波 dB")
                value: root.passbandRippleDb
                editable: root.passbandEditable
                minimumValue: root.minimumPassbandRippleDb
                maximumValue: root.maximumPassbandRippleDb
                stepSize: 0.01
                dragStepSize: 0.1
                decimals: 2
                errorText: root.passbandRippleError
                onValueEdited: function(value) { root.passbandRippleDb = value }
            }

            NumericField {
                labelText: qsTr("实际纹波 dB")
                value: root.actualRippleDb
                editable: false
                decimals: 3
                maximumValue: 200
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            text: root.validationMessage
            color: Theme.danger
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ThemedButton {
                text: qsTr("恢复默认")
                onClicked: root.resetDefaults()
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("取消")
                onClicked: root.close()
            }

            ThemedButton {
                text: qsTr("保存")
                variant: "primary"
                enabled: root.configurationValid
                onClicked: root.saveAndClose()
            }
        }
    }
}
