import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import BSSAS
import MangoComponent
import BSSASSettingsStorage

Item {
    id: root

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function clampRealtimeGain(value) {
        return Math.max(0.5, Math.min(5.0, value))
    }

    function storedGainToRealtimeGain(value) {
        return root.clampRealtimeGain(value / 100.0)
    }

    readonly property color themeBlue: Theme.primary
    readonly property color dividerColor: root.colorWithAlpha(themeBlue, 0.22)
    readonly property color sliderTrackColor: root.colorWithAlpha(themeBlue, 0.18)
    readonly property color sliderTickColor: root.colorWithAlpha(themeBlue, 0.32)
    readonly property color selectOutlineColor: root.colorWithAlpha(themeBlue, 0.38)
    readonly property var realtimeProcessingLabels: [
        qsTr("\u5e26\u901a\u6ee4\u6ce2"),
        qsTr("\u9677\u6ce2\u6ee4\u6ce2"),
        qsTr("\u4e3b\u52a8\u566a\u58f0\u5bf9\u6d88"),
        qsTr("\u81ea\u9002\u5e94\u964d\u566a"),
        qsTr("\u5c0f\u6ce2\u964d\u566a"),
        qsTr("\u77ac\u6001\u566a\u58f0\u6291\u5236"),
        qsTr("\u540c\u9891\u6bb5\u8fd0\u52a8\u4f2a\u5f71\u6d88\u9664")
    ]

    function toggleRealtimeProcessing(index, enabledValue) {
        const hasExplicitValue = enabledValue !== undefined
        switch (index) {
        case 0:
            signalPreprocessing.realtimeBandpassEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeBandpassEnabled
            break
        case 1:
            signalPreprocessing.realtimeNotchEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeNotchEnabled
            break
        case 2:
            signalPreprocessing.realtimeActiveNoiseCancellationEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeActiveNoiseCancellationEnabled
            break
        case 3:
            signalPreprocessing.realtimeAdaptiveNoiseReductionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeAdaptiveNoiseReductionEnabled
            break
        case 4:
            signalPreprocessing.realtimeWaveletDenoisingEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeWaveletDenoisingEnabled
            break
        case 5:
            signalPreprocessing.realtimeTransientNoiseSuppressionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeTransientNoiseSuppressionEnabled
            break
        case 6:
            signalPreprocessing.realtimeMotionArtifactReductionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.realtimeMotionArtifactReductionEnabled
            break
        default:
            break
        }
    }

    function realtimeProcessingStateAt(index) {
        switch (index) {
        case 0:
            return !!signalPreprocessing.realtimeBandpassEnabled
        case 1:
            return !!signalPreprocessing.realtimeNotchEnabled
        case 2:
            return !!signalPreprocessing.realtimeActiveNoiseCancellationEnabled
        case 3:
            return !!signalPreprocessing.realtimeAdaptiveNoiseReductionEnabled
        case 4:
            return !!signalPreprocessing.realtimeWaveletDenoisingEnabled
        case 5:
            return !!signalPreprocessing.realtimeTransientNoiseSuppressionEnabled
        case 6:
            return !!signalPreprocessing.realtimeMotionArtifactReductionEnabled
        default:
            return true
        }
    }

    function setAllRealtimeProcessing(enabledValue) {
        signalPreprocessing.setAllRealtimeProcessingEnabled(!!enabledValue)
    }

    Binding {
        target: signalPreprocessing
        property: "realtimeGain"
        value: root.storedGainToRealtimeGain(AppState.gain)
    }

    ScrollView {
        id: pageScrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        leftPadding: 30
        topPadding: 30
        rightPadding: 30
        bottomPadding: 30

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ColumnLayout {
            width: pageScrollView.availableWidth
            spacing: 28

        GridLayout {
            id: controlGrid

            Layout.fillWidth: true
            columns: width >= 980 ? 2 : 1
            columnSpacing: 44
            rowSpacing: 26

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "降噪方式"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: Theme.textPrimary
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Select {
                        id: noiseReductionMethodChoose

                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 50
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        currentIndex: 0
                        delegateHeight: 30
                        visibleCount: 3
                        popupPadding: 5
                        showScrollIndicator: false
                        textColor: Theme.textPrimary
                        outlineColor: root.selectOutlineColor
                        activeOutlineColor: root.themeBlue
                        indicatorColor: root.themeBlue
                        optionHighlightColor: root.colorWithAlpha(root.themeBlue, 0.12)
                        popupColor: Theme.textWhite
                        popupBorderColor: root.selectOutlineColor
                        model: ["无降噪", "小波降噪", "谱减法", "自适应滤波"]
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    radius: 0.5
                    color: root.dividerColor
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "增益调节"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: Theme.textPrimary
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        id: gainValueText
                        text: gainSlider.value.toFixed(2) + "x"
                        font {
                            family: Theme.fontFamily
                            pixelSize: 18
                            bold: true
                        }
                        color: root.themeBlue
                        horizontalAlignment: Text.AlignRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18

                    Slider {
                        id: gainSlider

                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        from: 0.5
                        to: 5.0
                        stepSize: 0

                        Binding {
                            target: gainSlider
                            property: "value"
                            value: root.storedGainToRealtimeGain(AppState.gain)
                            when: !gainSlider.pressed
                            restoreMode: Binding.RestoreBinding
                        }

                        onMoved: AppState.gain = gainSlider.value * 100.0

                        background: Rectangle {
                            x: gainSlider.leftPadding
                            y: gainSlider.topPadding + gainSlider.availableHeight / 2 - height / 2
                            implicitHeight: 6
                            width: gainSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            color: root.sliderTrackColor

                            Rectangle {
                                width: gainSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                color: root.themeBlue
                            }

                            Rectangle {
                                x: (1.0 - gainSlider.from) / (gainSlider.to - gainSlider.from) * parent.width - width / 2
                                y: -4
                                width: 2
                                height: 14
                                radius: 1
                                color: root.sliderTickColor
                            }
                        }

                        handle: Rectangle {
                            x: gainSlider.leftPadding + gainSlider.visualPosition * (gainSlider.availableWidth - width)
                            y: gainSlider.topPadding + gainSlider.availableHeight / 2 - height / 2
                            implicitWidth: 22
                            implicitHeight: 22
                            radius: 11
                            color: Theme.textWhite
                            border.color: gainSlider.pressed ? root.themeBlue : root.selectOutlineColor
                            border.width: 2
                            scale: gainSlider.pressed ? 1.18 : 1

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 150
                                    easing.type: Easing.OutBack
                                }
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 6
                                height: 6
                                radius: 3
                                color: root.themeBlue
                                visible: !gainSlider.pressed
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    radius: 0.5
                    color: root.dividerColor
                }
            }

            ColumnLayout {
                id: analysisSection

                Layout.fillWidth: true
                Layout.columnSpan: controlGrid.columns > 1 ? 2 : 1
                spacing: 14

                property string detailTipText: "系统会按照这里设置的时间长度，对肠鸣音频信号进行单次分析和处理。"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "分析时间长度"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: Theme.textPrimary
                    }

                    Item {
                        id: analysisTimeLengthDetails

                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16

                        MouseArea {
                            id: detailsHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                            cursorShape: Qt.PointingHandCursor

                            onContainsMouseChanged: {
                                if (containsMouse) {
                                    openTipTimer.restart()
                                } else {
                                    openTipTimer.stop()
                                    detailTip.close()
                                }
                            }
                        }

                        Image {
                            id: detailsImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            source: "qrc:/qt/qml/BSSASContent/images/details.png"
                            visible: false
                            smooth: true
                        }

                        MultiEffect {
                            anchors.fill: parent
                            source: detailsImage
                            transformOrigin: Item.Center
                            colorization: detailsHover.containsMouse ? 0.45 : 0.0
                            colorizationColor: Theme.textWhite
                            brightness: detailsHover.containsMouse ? 0.1 : 0.0
                            contrast: detailsHover.containsMouse ? 1.1 : 1.0
                            scale: detailsHover.containsMouse ? 1.15 : 1.0

                            Behavior on colorization {
                                NumberAnimation {
                                    duration: 150
                                }
                            }

                            Behavior on brightness {
                                NumberAnimation {
                                    duration: 150
                                }
                            }

                            Behavior on contrast {
                                NumberAnimation {
                                    duration: 150
                                }
                            }

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 150
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }

                        Timer {
                            id: openTipTimer
                            interval: 60
                            onTriggered: {
                                const iconCenter = analysisTimeLengthDetails.mapToItem(root,
                                    analysisTimeLengthDetails.width / 2,
                                    analysisTimeLengthDetails.height / 2)
                                const popupWidth = detailTip.implicitWidth > 0 ? detailTip.implicitWidth : 260
                                detailTip.x = Math.max(30, Math.min(root.width - popupWidth - 30,
                                    iconCenter.x - popupWidth / 2))
                                detailTip.y = iconCenter.y + 16
                                detailTip.open()
                            }
                        }

                        Popup {
                            id: detailTip
                            parent: root
                            padding: 0
                            modal: false
                            focus: false
                            closePolicy: Popup.NoAutoClose

                            enter: Transition {
                                ParallelAnimation {
                                    NumberAnimation {
                                        target: detailTip
                                        property: "opacity"
                                        from: 0
                                        to: 1
                                        duration: 120
                                        easing.type: Easing.OutQuad
                                    }

                                    NumberAnimation {
                                        target: detailTip
                                        property: "scale"
                                        from: 0.92
                                        to: 1
                                        duration: 180
                                        easing.type: Easing.OutBack
                                    }
                                }
                            }

                            exit: Transition {
                                ParallelAnimation {
                                    NumberAnimation {
                                        target: detailTip
                                        property: "opacity"
                                        from: 1
                                        to: 0
                                        duration: 100
                                        easing.type: Easing.InQuad
                                    }

                                    NumberAnimation {
                                        target: detailTip
                                        property: "scale"
                                        from: 1
                                        to: 0.96
                                        duration: 100
                                        easing.type: Easing.InQuad
                                    }
                                }
                            }

                            background: Rectangle {
                                radius: 8
                                color: Theme.popoverBg
                                border.width: 1
                                border.color: Theme.popoverBorder
                            }

                            contentItem: Text {
                                width: Math.min(280, root.width * 0.38)
                                padding: 10
                                text: analysisSection.detailTipText
                                color: Theme.textWhite
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }

                        onVisibleChanged: {
                            if (!visible) {
                                openTipTimer.stop()
                                detailTip.close()
                            }
                        }

                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Select {
                        id: analysisTimeLengthChoose

                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 50
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        currentIndex: 0
                        delegateHeight: 30
                        visibleCount: 3
                        popupPadding: 5
                        showScrollIndicator: false
                        textColor: Theme.textPrimary
                        outlineColor: root.selectOutlineColor
                        activeOutlineColor: root.themeBlue
                        indicatorColor: root.themeBlue
                        optionHighlightColor: root.colorWithAlpha(root.themeBlue, 0.12)
                        popupColor: Theme.textWhite
                        popupBorderColor: root.selectOutlineColor
                        model: ["0.5秒", "1秒", "2秒", "5秒", "10秒", "30秒", "60秒"]
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    radius: 0.5
                    color: root.dividerColor
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.columnSpan: controlGrid.columns > 1 ? 2 : 1
                Layout.preferredHeight: 38
                spacing: 10

                Text {
                    text: qsTr("\u6ee4\u6ce2\u5668\u7c7b\u578b")
                    font.pixelSize: 14
                    color: Theme.textPrimary
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "IIR"
                    font.pixelSize: 14
                    font.bold: !signalPreprocessing.realtimeFirFilterEnabled
                    color: signalPreprocessing.realtimeFirFilterEnabled
                        ? root.colorWithAlpha(Theme.textPrimary, 0.62)
                        : root.themeBlue
                }

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: realtimeFilterTypeSwitch.implicitWidth
                    implicitHeight: realtimeFilterTypeSwitch.implicitHeight

                    ToggleSwitch {
                        id: realtimeFilterTypeSwitch
                        anchors.centerIn: parent
                        checked: signalPreprocessing.realtimeFirFilterEnabled
                        interactive: false
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: signalPreprocessing.realtimeFirFilterEnabled = !signalPreprocessing.realtimeFirFilterEnabled
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "FIR"
                    font.pixelSize: 14
                    font.bold: signalPreprocessing.realtimeFirFilterEnabled
                    color: signalPreprocessing.realtimeFirFilterEnabled
                        ? root.themeBlue
                        : root.colorWithAlpha(Theme.textPrimary, 0.62)
                }
            }

            ColumnLayout {
                id: realtimeProcessingSection

                Layout.fillWidth: true
                Layout.columnSpan: controlGrid.columns > 1 ? 2 : 1
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: qsTr("\u5b9e\u65f6\u91c7\u96c6\u5904\u7406")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: Theme.textPrimary
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    implicitHeight: 46

                    RowLayout {
                        anchors.fill: parent
                        spacing: 14

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("\u63a5\u5165\u5168\u90e8\u4fe1\u53f7\u5904\u7406\u7b97\u6cd5\uff1a")
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: Theme.textPrimary
                        }

                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: realtimeProcessingMasterSwitch.implicitWidth
                            implicitHeight: realtimeProcessingMasterSwitch.implicitHeight

                            ToggleSwitch {
                                id: realtimeProcessingMasterSwitch
                                anchors.centerIn: parent
                                checked: signalPreprocessing.realtimeAllProcessingEnabled
                                interactive: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setAllRealtimeProcessing(!signalPreprocessing.realtimeAllProcessingEnabled)
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 860 ? 3 : (width >= 560 ? 2 : 1)
                    columnSpacing: 18
                    rowSpacing: 12

                    Repeater {
                        model: root.realtimeProcessingLabels.length

                        RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: root.realtimeProcessingLabels[index]
                                font.pixelSize: 14
                                color: Theme.textPrimary
                            }

                            Item {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: realtimeAlgorithmSwitch.implicitWidth
                                implicitHeight: realtimeAlgorithmSwitch.implicitHeight

                                ToggleSwitch {
                                    id: realtimeAlgorithmSwitch
                                    anchors.centerIn: parent
                                    checked: root.realtimeProcessingStateAt(index)
                                    interactive: false
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggleRealtimeProcessing(index, !root.realtimeProcessingStateAt(index))
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    radius: 0.5
                    color: root.dividerColor
                }
            }
        }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
