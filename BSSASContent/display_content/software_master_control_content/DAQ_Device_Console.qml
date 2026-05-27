/**
 * @file DAQ_Device_Console.qml
 * @brief DAQ 设备控制台页面。管理设备连接/初始化、通道激活、采样率调节、高速采集模式与事件定位。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS
import MangoComponent

Item {
    id: root

    readonly property bool compactContent: Constants.isCompactContent(width, height)
    property real layout_margin: compactContent ? 20 : 30
    readonly property real controlWidth: compactContent ? 132 : 150
    property bool voiceLocalizationEnabled: false
    property var openSensorLocalizationPage: null
    property var closeVoiceLocalization: null
    property int normalSamplingRate: 12000
    readonly property bool isConnected: daqManager.isConnected
    readonly property bool isCollecting: daqManager.isCollecting
    readonly property color themeBlue: Theme.primary
    readonly property color textColor: Theme.textPrimary
    readonly property color dividerColor: colorWithAlpha(themeBlue, 0.22)
    readonly property color defaultBorderColor: Theme.primaryBorder
    readonly property color hoverFillColor: colorWithAlpha(themeBlue, 0.08)
    readonly property color pressedFillColor: colorWithAlpha(themeBlue, 0.16)
    readonly property color activeFillColor: colorWithAlpha(themeBlue, 0.14)
    readonly property color activeBorderColor: colorWithAlpha(themeBlue, 0.42)
    readonly property color connectedButtonColor: colorWithAlpha(themeBlue, 0.12)
    readonly property color connectedButtonBorderColor: colorWithAlpha(themeBlue, 0.42)
    readonly property color connectedButtonTextColor: themeBlue
    readonly property var channelLabels: [
        "通道一", "通道二", "通道三", "通道四",
        "通道五", "通道六", "通道七"
    ]
    readonly property var channelStates: daqManager.activeChannels
    readonly property bool allChannelsActive: {
        if (!channelStates || channelStates.length !== channelLabels.length)
            return false
        for (let i = 0; i < channelStates.length; ++i) {
            if (!channelStates[i])
                return false
        }
        return true
    }

    /**
     * @brief 为源颜色叠加 alpha 通道值，返回新的 rgba 颜色。
     * @param sourceColor 源颜色
     * @param alphaValue alpha 值 [0,1]
     * @returns 带 alpha 的 Qt.rgba 颜色值
     */
    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    /**
     * @brief 切换声源定位状态：已开启则关闭，未开启则跳转传感器配置页面。
     */
    function toggleVoiceLocalization() {
        if (root.voiceLocalizationEnabled) {
            if (typeof root.closeVoiceLocalization === "function")
                root.closeVoiceLocalization()
            return
        }

        if (typeof root.openSensorLocalizationPage === "function")
            root.openSensorLocalizationPage()
    }

    Component.onCompleted: {
        normalSamplingRate = dataManager.configuredSampleRate
    }

    Connections {
        target: dataManager

        function onConfiguredSampleRateChanged() {
            if (!dataManager.highSpeedCollectionMode)
                root.normalSamplingRate = dataManager.configuredSampleRate
        }
    }

    ScrollView {
        id: daqScrollView
        anchors.fill: parent
        anchors.margins: root.layout_margin
        clip: true
        contentWidth: availableWidth

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: daqDeviceConsoleColumnLayout
            width: daqScrollView.availableWidth
            spacing: root.layout_margin

            GroupBox {
                title: ""
                Layout.fillWidth: true
                Layout.preferredHeight: 210

                label: RowLayout {
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "设备连接"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: root.textColor
                    }

                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: "transparent"
                    border.width: 0
                }

                ColumnLayout {
                    id: deviceConnectionContent
                    property string displayedStatusText: daqManager.statusText
                    property string pendingStatusText: daqManager.statusText

                    function showStatusText(nextText) {
                        if (displayedStatusText === nextText)
                            return

                        pendingStatusText = nextText
                        statusTextSwapAnimation.restart()
                    }

                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    spacing: root.layout_margin / 2

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: root.layout_margin / 2

                        Text {
                            id: deviceStatusText
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            text: "设备连接状态：" + deviceConnectionContent.displayedStatusText
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                            elide: Text.ElideRight
                        }

                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: root.layout_margin / 2

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "初始化设备："
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                        }

                        Button {
                            id: initializeDeviceButton
                            text: root.isConnected ? "已初始化" : "初始化设备"
                            hoverEnabled: true
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: root.controlWidth
                            Layout.preferredHeight: 40
                            enabled: !root.isCollecting && !root.isConnected

                            contentItem: Text {
                                text: initializeDeviceButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.isConnected ? root.connectedButtonTextColor : root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                border.color: root.isConnected ? root.connectedButtonBorderColor : root.defaultBorderColor
                                border.width: 2
                                radius: 10
                                color: root.isConnected
                                    ? root.connectedButtonColor
                                    : (initializeDeviceButton.down
                                        ? root.pressedFillColor
                                        : (initializeDeviceButton.hovered ? root.hoverFillColor : "transparent"))
                            }

                            onClicked: daqManager.initializeDevice()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: root.layout_margin / 2

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "刷新设备："
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                        }

                        Button {
                            id: refreshDeviceButton
                            text: "刷新设备"
                            hoverEnabled: true
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: root.controlWidth
                            Layout.preferredHeight: 40
                            enabled: !root.isCollecting

                            contentItem: Text {
                                text: refreshDeviceButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                border.color: root.defaultBorderColor
                                border.width: 2
                                radius: 10
                                color: refreshDeviceButton.down
                                    ? root.pressedFillColor
                                    : (refreshDeviceButton.hovered ? root.hoverFillColor : "transparent")
                            }

                            onClicked: daqManager.refreshDevice()
                        }
                    }

                    Connections {
                        target: daqManager

                        function onStatusTextChanged() {
                            deviceConnectionContent.showStatusText(daqManager.statusText)
                        }
                    }

                    SequentialAnimation {
                        id: statusTextSwapAnimation

                        NumberAnimation {
                            target: deviceStatusText
                            property: "opacity"
                            to: 0.0
                            duration: 120
                            easing.type: Easing.InOutQuad
                        }

                        ScriptAction {
                            script: deviceConnectionContent.displayedStatusText = deviceConnectionContent.pendingStatusText
                        }

                        NumberAnimation {
                            target: deviceStatusText
                            property: "opacity"
                            to: 1.0
                            duration: 160
                            easing.type: Easing.OutQuad
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

            GroupBox {
                title: ""
                Layout.fillWidth: true
                Layout.preferredHeight: 220

                label: RowLayout {
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "通道管理"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: root.textColor
                    }

                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: "transparent"
                    border.width: 0
                }

                ColumnLayout {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    spacing: root.layout_margin / 2

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        spacing: root.layout_margin / 2

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "激活全部通道："
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                        }

                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: masterChannelSwitch.implicitWidth
                            implicitHeight: masterChannelSwitch.implicitHeight

                            ToggleSwitch {
                                id: masterChannelSwitch
                                anchors.centerIn: parent
                                checked: root.allChannelsActive
                                interactive: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: daqManager.setAllChannelsActive(!root.allChannelsActive)
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 18
                        rowSpacing: 12

                        Repeater {
                            model: root.channelLabels.length

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: root.channelLabels[index]
                                    font.pixelSize: 14
                                    color: root.textColor
                                }

                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitWidth: channelSwitch.implicitWidth
                                    implicitHeight: channelSwitch.implicitHeight

                                    ToggleSwitch {
                                        id: channelSwitch
                                        anchors.centerIn: parent
                                        checked: root.channelStates && index < root.channelStates.length
                                            ? !!root.channelStates[index]
                                            : true
                                        interactive: false
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const currentActive = root.channelStates && index < root.channelStates.length
                                                ? !!root.channelStates[index]
                                                : true
                                            daqManager.setChannelActive(index, !currentActive)
                                        }
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

            GroupBox {
                title: ""
                Layout.fillWidth: true
                Layout.preferredHeight: 250

                label: RowLayout {
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "设备模式"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: root.textColor
                    }

                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: "transparent"
                    border.width: 0
                }

                ColumnLayout {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    spacing: root.layout_margin / 2

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "采样率设置"
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.textColor
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: root.normalSamplingRate + "Hz"
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: samplingRateSlider.enabled
                                    ? root.themeBlue
                                    : Theme.textMuted
                            }
                        }

                        Slider {
                            id: samplingRateSlider
                            Layout.fillWidth: true
                            from: 8000
                            to: 48000
                            value: root.normalSamplingRate
                            stepSize: 1000
                            snapMode: Slider.SnapAlways
                            enabled: !root.isCollecting && !dataManager.highSpeedCollectionMode

                            onMoved: {
                                const roundedSampleRate = Math.round(value / 1000) * 1000
                                root.normalSamplingRate = roundedSampleRate
                                dataManager.configuredSampleRate = roundedSampleRate
                            }

                            background: Rectangle {
                                x: samplingRateSlider.leftPadding
                                y: samplingRateSlider.topPadding + samplingRateSlider.availableHeight / 2 - height / 2
                                width: samplingRateSlider.availableWidth
                                height: 6
                                radius: 3
                                color: colorWithAlpha(root.themeBlue, 0.2)

                                Rectangle {
                                    width: samplingRateSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: root.themeBlue
                                }
                            }

                            handle: Rectangle {
                                x: samplingRateSlider.leftPadding + samplingRateSlider.visualPosition * (samplingRateSlider.availableWidth - width)
                                y: samplingRateSlider.topPadding + samplingRateSlider.availableHeight / 2 - height / 2
                                implicitWidth: 18
                                implicitHeight: 18
                                radius: 9
                                color: Theme.textWhite
                                border.width: 3
                                border.color: root.themeBlue
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: root.layout_margin / 2

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "高速采集模式："
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                        }

                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: highSpeedCollectionSwitch.implicitWidth
                            implicitHeight: highSpeedCollectionSwitch.implicitHeight

                            ToggleSwitch {
                                id: highSpeedCollectionSwitch
                                anchors.centerIn: parent
                                checked: dataManager.highSpeedCollectionMode
                                enabled: !root.isCollecting
                                interactive: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: !root.isCollecting
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: dataManager.highSpeedCollectionMode = !dataManager.highSpeedCollectionMode
                            }
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "开启后将以150kHz采样"
                            font.pixelSize: 14
                            color: Theme.textMuted
                        }

                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        spacing: root.layout_margin / 2

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "事件定位："
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: root.textColor
                        }

                        Button {
                            id: localizationButton
                            text: root.voiceLocalizationEnabled ? "关闭定位" : "配置传感器"
                            hoverEnabled: true
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: root.controlWidth
                            Layout.preferredHeight: 40

                            contentItem: Text {
                                text: localizationButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.voiceLocalizationEnabled ? root.themeBlue : root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                border.color: root.voiceLocalizationEnabled
                                    ? root.activeBorderColor
                                    : root.defaultBorderColor
                                border.width: 2
                                radius: 10
                                color: localizationButton.down
                                    ? root.pressedFillColor
                                    : (root.voiceLocalizationEnabled
                                        ? root.activeFillColor
                                        : (localizationButton.hovered ? root.hoverFillColor : "transparent"))
                            }

                            onClicked: root.toggleVoiceLocalization()
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: root.voiceLocalizationEnabled ? "已开启" : "未开启"
                            font.pixelSize: 14
                            color: root.voiceLocalizationEnabled ? root.themeBlue : Theme.textMuted
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

            GroupBox {
                title: ""
                Layout.fillWidth: true
                Layout.preferredHeight: 200

                label: RowLayout {
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4
                        implicitHeight: 20
                        radius: 2
                        color: root.themeBlue
                    }

                    Text {
                        text: "采集状态控制"
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                        color: root.textColor
                    }

                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: "transparent"
                    border.width: 0
                }

                ColumnLayout {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    spacing: root.layout_margin

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: root.layout_margin

                        Button {
                            id: startButton
                            text: root.isCollecting ? "采集中..." : "启动DAQ"
                            hoverEnabled: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: root.controlWidth
                            enabled: root.isConnected

                            contentItem: Text {
                                text: startButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.isCollecting ? root.themeBlue : root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                border.color: root.isCollecting
                                    ? root.activeBorderColor
                                    : ((startButton.hovered || startButton.down) ? root.activeBorderColor : root.defaultBorderColor)
                                border.width: 2
                                radius: 15
                                color: root.isCollecting
                                    ? root.activeFillColor
                                    : (startButton.down
                                        ? root.pressedFillColor
                                        : (startButton.hovered ? root.hoverFillColor : "transparent"))
                            }

                            onClicked: daqManager.startHardwareCollection()
                        }

                        Button {
                            id: stopButton
                            text: "停止DAQ"
                            hoverEnabled: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: root.controlWidth
                            enabled: root.isCollecting

                            contentItem: Text {
                                text: stopButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                border.color: (stopButton.hovered || stopButton.down) ? root.activeBorderColor : root.defaultBorderColor
                                border.width: 2
                                radius: 15
                                color: stopButton.down
                                    ? root.pressedFillColor
                                    : (stopButton.hovered ? root.hoverFillColor : "transparent")
                            }

                            onClicked: daqManager.stopHardwareCollection()
                        }
                    }

                    ProgressIndicator {
                        Layout.fillWidth: true
                        value: 0.5
                        wavy: true
                        indeterminate: root.isCollecting
                    }
                }
            }
        }
    }
}
