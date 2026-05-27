/**
 * @file Temporal_Spectral_Monitoring.qml
 * @brief 时频谱监测页面。对多通道实时采集信号进行时域波形与频域谱图可视化，支持通道切换与数据保存。
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import MangoComponent
import BSSAS

Page {
    id: root

    property bool pageActive: true
    property real layout_margin: 20
    property real leftPanelSpacing: layout_margin / 2
    property int leftPanelWidth: 170
    property int summaryCardHeight: 110
    property int actionButtonHeight: 40
    readonly property bool isPlotting: daqManager.isReading
    property bool pendingRealtimeSave: false
    property bool jointReportExportEnabled: false
    readonly property bool canSaveRealtimeData: dataManager.realtimeCollectionAvailable
        && !root.isPlotting
        && !root.pendingRealtimeSave
    property int currentChannelIndex: 0
    readonly property int monitorVolumePercent: Math.round(realtimeAudioMonitor.volume * 100)
    readonly property var channelNames: ["通道一", "通道二", "通道三", "通道四", "通道五", "通道六", "通道七"]
    readonly property var activeChannels: daqManager.activeChannels
    readonly property var inactiveChannelIndices: {
        const inactive = []
        if (!activeChannels || activeChannels.length !== channelNames.length) {
            return inactive
        }
        for (let i = 0; i < activeChannels.length; ++i) {
            if (!activeChannels[i]) {
                inactive.push(i)
            }
        }
        return inactive
    }

    anchors.fill: parent
    readonly property color _summaryCardColor: Theme.primaryLighter
    readonly property color _channelPanelColor: Theme.pageBg
    readonly property color _channelHoverColor: colorWithAlpha(Theme.primary, 0.08)
    readonly property color _channelSelectedColor: colorWithAlpha(Theme.primary, 0.16)
    readonly property color _channelPressedColor: colorWithAlpha(Theme.primary, 0.24)
    readonly property color _actionButtonColor: Theme.primary
    readonly property color _actionButtonActiveColor: Theme.primaryLighter

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
     * @brief 调整实时监听音量，范围限制在 0~100%。
     * @param delta 音量增量，正数增大、负数减小
     */
    function adjustMonitorVolume(delta) {
        realtimeAudioMonitor.setVolume(Math.max(0, Math.min(1, realtimeAudioMonitor.volume + delta)))
    }

    /**
     * @brief 从文件路径中提取文件名。
     * @param filePath 完整文件路径
     * @returns 文件名，空路径返回空字符串
     */
    function extractFileName(filePath) {
        if (!filePath || filePath.length === 0) {
            return ""
        }

        const normalizedPath = filePath.replace(/\\/g, "/")
        const pathSegments = normalizedPath.split("/")
        return pathSegments.length > 0 ? pathSegments[pathSegments.length - 1] : filePath
    }

    function toLocalFilePath(fileUrl) {
        const raw = fileUrl ? fileUrl.toString() : ""
        return raw.length > 0 ? decodeURIComponent(raw).replace(/^file:\/\/\//, "") : ""
    }

    /**
     * @brief 获取第一个已激活通道的索引，无激活通道时返回 -1。
     * @returns 首个激活通道索引
     */
    function firstActiveChannelIndex() {
        if (!root.activeChannels || root.activeChannels.length !== root.channelNames.length) {
            return 0
        }
        for (let i = 0; i < root.activeChannels.length; ++i) {
            if (root.activeChannels[i]) {
                return i
            }
        }
        return -1
    }

    function restartRealtimeWaveformWindow(channelIndex) {
        if (!root.isPlotting || channelIndex < 0) {
            return
        }

        const latestTimestamp = dataManager.realtimeWaveformDurationForChannel(channelIndex)
        rawTimeWaveform.restartDisplayWindow(
            Number.isFinite(latestTimestamp) ? latestTimestamp : 0)
    }

    /**
     * @brief 确保当前选中通道为激活状态；若当前通道已关闭则自动切换至首个激活通道。
     * @param showToast 是否显示切换提示
     */
    function ensureActiveChannelSelected(showToast) {
        const activeIndex = firstActiveChannelIndex()
        if (activeIndex < 0) {
            left_radio_button.selectedIndices = []
            return
        }

        if (root.currentChannelIndex >= 0 &&
            root.currentChannelIndex < root.activeChannels.length &&
            root.activeChannels[root.currentChannelIndex]) {
            left_radio_button.selectedIndices = [root.currentChannelIndex]
            return
        }

        const channelChanged = root.currentChannelIndex !== activeIndex
        if (channelChanged) {
            root.restartRealtimeWaveformWindow(activeIndex)
        }

        left_radio_button.selectedIndices = [activeIndex]
        signalPreprocessing.setCurrentChannel(activeIndex)
        root.currentChannelIndex = activeIndex

        if (showToast) {
            channelToast.showSuccess("当前通道已关闭，自动切换为：" + root.channelNames[activeIndex])
        }
    }

    /**
     * @brief 启动实时采集：初始化数据库、清空波形与频谱，依次启动缓冲读取、信号预处理、降采样及 DFT 计算。
     */
    function startRealtimeCollection() {
        if (!daqManager.isCollecting) {
            channelToast.showError("请先在软件总控中启动DAQ")
            return
        }

        if (daqManager.isReading) {
            return
        }

        dataManager.initializeDatabase()
        rawTimeWaveform.clearWaveForm()
        rawFrequencySpectrum.clearSpectrum()
        daqManager.startBufferReading()
        signalPreprocessing.startPreprocessing()
        adaptiveDownsampling.startDownsamplingProcessing()
        signalDFTCalculation.startDFTProcessing()
        console.log("开始绘制")
    }

    /**
     * @brief 停止实时采集：依次停止预处理、降采样、DFT 计算及缓冲读取，完成当前采集记录。
     * @param needToast 是否在弹出的 Toast 中提示停止信息
     */
    function stopRealtimeCollection(needToast) {
        realtimeAudioMonitor.setEnabled(false)
        signalPreprocessing.stopPreprocessing()
        adaptiveDownsampling.stopDownsamplingProcessing()
        signalDFTCalculation.stopDFTProcessing()
        daqManager.stopBufferReading()
        dataManager.collectionCompleted()
        console.log("停止绘制")
        if (needToast) {
            channelToast.showError("DAQ已停止，时频采集已自动停止")
        }
    }

    background: Rectangle {
        color: "transparent"
    }

    Connections {
        target: daqManager
        function onCollectionChanged() {
            if (!daqManager.isCollecting && root.isPlotting) {
                root.stopRealtimeCollection(true)
            }
        }

        function onActiveChannelsChanged() {
            root.ensureActiveChannelSelected(true)
        }
    }

    Connections {
        target: wavHandle

        function onSaveCompleted(outputPath) {
            root.pendingRealtimeSave = false
            const fileName = root.extractFileName(outputPath)
            channelToast.showSuccess(fileName.length > 0 ? "WAV 保存成功：" + fileName : "WAV 保存成功")
        }

        function onSaveFailed(errorMessage) {
            root.pendingRealtimeSave = false
            channelToast.showError(errorMessage.length > 0 ? "WAV 保存失败：" + errorMessage : "WAV 保存失败")
        }
    }

    Connections {
        target: jointExportManager

        function onFailed(errorMessage) {
            root.pendingRealtimeSave = false
            if (errorMessage.length > 0) {
                channelToast.showError(errorMessage)
            }
        }
    }

    Item {
        id: leftPanel
        anchors {
            top: parent.top
            bottom: parent.bottom
            left: parent.left
            margins: root.layout_margin
        }
        width: root.leftPanelWidth

        DirectionAwareHoverCard {
            id: topic_card
            containerColor: root._summaryCardColor
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            height: root.summaryCardHeight

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 10

                Text {
                    text: "📡 实时采集信号"
                    font.pixelSize: 15
                    color: Theme.textPrimary
                }

                Text {
                    text: "📈 采集数据可视化"
                    font.pixelSize: 15
                    color: Theme.textPrimary
                }

                Text {
                    text: "🔍 多通道时频监测"
                    font.pixelSize: 15
                    color: Theme.textPrimary
                }
            }
        }

        RadioButton {
            id: left_radio_button
            x: 0
            y: topic_card.height + root.leftPanelSpacing
            width: leftPanel.width
            height: implicitHeight
            containerColor: root._channelPanelColor
            hoverColor: root._channelHoverColor
            selectedColor: root._channelSelectedColor
            pressedColor: root._channelPressedColor
            textColor: Theme.textPrimary
            checkmarkColor: Theme.primary
            disabledIndices: root.inactiveChannelIndices
            model: [
                { text: "通道一" },
                { text: "通道二" },
                { text: "通道三" },
                { text: "通道四" },
                { text: "通道五" },
                { text: "通道六" },
                { text: "通道七" }
            ]

            onSelectionChanged: function(selectedIndices, selectedData) {
                if (selectedIndices.length === 0) {
                    const defaultChannelIndex = root.firstActiveChannelIndex()
                    if (defaultChannelIndex < 0) {
                        return
                    }
                    const channelChanged = root.currentChannelIndex !== defaultChannelIndex
                    if (channelChanged) {
                        root.restartRealtimeWaveformWindow(defaultChannelIndex)
                    }
                    left_radio_button.selectedIndices = [defaultChannelIndex]
                    signalPreprocessing.setCurrentChannel(defaultChannelIndex)
                    root.currentChannelIndex = defaultChannelIndex
                    channelToast.showSuccess("未选择通道，默认显示：" + root.channelNames[defaultChannelIndex])
                } else {
                    const channelIndex = selectedIndices[0]
                    if (channelIndex < 0 || channelIndex >= root.activeChannels.length || !root.activeChannels[channelIndex]) {
                        root.ensureActiveChannelSelected(false)
                        return
                    }
                    const channelChanged = root.currentChannelIndex !== channelIndex

                    if (channelChanged) {
                        root.restartRealtimeWaveformWindow(channelIndex)
                    }

                    signalPreprocessing.setCurrentChannel(channelIndex)
                    root.currentChannelIndex = channelIndex
                    channelToast.showSuccess("已切换为：" + root.channelNames[channelIndex])
                }
            }
        }

        Component.onCompleted: {
            root.ensureActiveChannelSelected(false)
        }

        Item {
            id: actionButtons
            anchors {
                left: parent.left
                right: parent.right
                top: left_radio_button.bottom
                topMargin: root.leftPanelSpacing
            }
            height: root.actionButtonHeight * 2 + 142

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Button {
                    id: savePlotButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.actionButtonHeight
                    enabled: root.canSaveRealtimeData
                    text: "保存数据"
                    hoverEnabled: true

                    contentItem: Text {
                        text: savePlotButton.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 15
                        font.bold: true
                        color: savePlotButton.enabled
                            ? Theme.textWhite
                            : root.colorWithAlpha(Theme.textWhite, 0.65)
                    }

                    background: Rectangle {
                        radius: height / 2
                        color: !savePlotButton.enabled
                            ? root.colorWithAlpha(root._actionButtonColor, 0.38)
                            : (savePlotButton.down
                                ? Qt.darker(root._actionButtonColor, 1.08)
                                : (savePlotButton.hovered
                                    ? Qt.darker(root._actionButtonColor, 1.04)
                                    : root._actionButtonColor))
                    }

                    onClicked: {
                        if (!root.canSaveRealtimeData) {
                            return
                        }
                        if (root.jointReportExportEnabled) {
                            jointExportFolderDialog.open()
                            return
                        }

                        root.pendingRealtimeSave = true
                        wavHandle.startSaveAsWav()
                    }
                }

                Rectangle {
                    id: jointExportOption
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 8
                    color: root.jointReportExportEnabled
                        ? root.colorWithAlpha(Theme.primary, 0.10)
                        : "transparent"
                    border.width: 1
                    border.color: root.jointReportExportEnabled
                        ? root.colorWithAlpha(Theme.primary, 0.32)
                        : root.colorWithAlpha(Theme.primaryBorder, 0.45)

                    CheckBoxes {
                        id: jointExportCheck
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        text: qsTr("联合导出报告")
                        checked: root.jointReportExportEnabled
                        interactive: false
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.jointReportExportEnabled = !root.jointReportExportEnabled
                    }
                }

                Button {
                    id: startPlotButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.actionButtonHeight
                    enabled: !root.pendingRealtimeSave
                    text: root.isPlotting ? "停止采集" : "开始采集"
                    hoverEnabled: true

                    contentItem: Text {
                        text: startPlotButton.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 15
                        font.bold: true
                        color: root.isPlotting ? Theme.primary : Theme.textWhite
                    }

                    background: Rectangle {
                        radius: height / 2
                        color: startPlotButton.down
                            ? (root.isPlotting
                                ? Theme.pageBg
                                : Qt.darker(root._actionButtonColor, 1.08))
                            : (root.isPlotting
                                ? root._actionButtonActiveColor
                                : (startPlotButton.hovered
                                    ? Qt.darker(root._actionButtonColor, 1.04)
                                    : root._actionButtonColor))
                        border.width: root.isPlotting ? 1 : 0
                        border.color: root.colorWithAlpha(Theme.primary, 0.28)
                    }

                    onClicked: {
                        if (root.isPlotting) {
                            root.stopRealtimeCollection(false)
                        } else {
                            root.startRealtimeCollection()
                        }
                    }
                }

                Rectangle {
                    id: realtimeMonitorCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    radius: 8
                    enabled: root.isPlotting
                    clip: true
                    color: realtimeAudioMonitor.enabled
                        ? root.colorWithAlpha(Theme.primary, 0.10)
                        : Theme.pageBg
                    border.width: 1
                    border.color: realtimeAudioMonitor.enabled
                        ? root.colorWithAlpha(Theme.primary, 0.30)
                        : Theme.border
                    opacity: enabled ? 1.0 : 0.62

                    Behavior on color {
                        ColorAnimation {
                            duration: 160
                        }
                    }

                    MouseArea {
                        id: monitorWheelArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onWheel: function(wheel) {
                            if (!realtimeMonitorCard.enabled) {
                                return
                            }

                            root.adjustMonitorVolume(wheel.angleDelta.y >= 0 ? 0.05 : -0.05)
                            wheel.accepted = true
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "实时监听"
                                font.pixelSize: 14
                                font.bold: true
                                color: Theme.textPrimary
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Item {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: monitorSwitch.implicitWidth
                                implicitHeight: monitorSwitch.implicitHeight

                                ToggleSwitch {
                                    id: monitorSwitch
                                    anchors.centerIn: parent
                                    checked: realtimeAudioMonitor.enabled
                                    enabled: realtimeMonitorCard.enabled
                                    interactive: false
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: realtimeMonitorCard.enabled
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: realtimeAudioMonitor.setEnabled(!realtimeAudioMonitor.enabled)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: root.currentChannelIndex >= 0 && root.currentChannelIndex < root.channelNames.length
                                    ? root.channelNames[root.currentChannelIndex]
                                    : ""
                                font.pixelSize: 12
                                color: Theme.textMuted
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                text: root.monitorVolumePercent + "%"
                                font.pixelSize: 12
                                font.bold: true
                                color: realtimeAudioMonitor.enabled ? Theme.primary : Theme.textMuted
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 6
                            radius: 3
                            color: root.colorWithAlpha(Theme.primary, 0.14)

                            Rectangle {
                                width: parent.width * realtimeAudioMonitor.volume
                                height: parent.height
                                radius: parent.radius
                                color: realtimeAudioMonitor.enabled ? Theme.primary : Theme.textMuted

                                Behavior on width {
                                    NumberAnimation {
                                        duration: 120
                                    }
                                }
                            }
                        }
                    }

                    ToolTip.visible: monitorWheelArea.containsMouse && realtimeMonitorCard.enabled
                    ToolTip.delay: 400
                    ToolTip.text: "鼠标滚轮调节音量"
                }
            }
        }
    }

    ToastNotification {
        id: channelToast
        duration: 2000
    }

    FolderDialog {
        id: jointExportFolderDialog
        title: qsTr("选择联合导出保存目录")

        onAccepted: {
            if (!root.canSaveRealtimeData) {
                return
            }

            const localDirectoryPath = root.toLocalFilePath(selectedFolder).trim()
            if (localDirectoryPath.length === 0) {
                return
            }

            const started = jointExportManager.beginExport(localDirectoryPath)
            if (!started) {
                root.pendingRealtimeSave = false
                return
            }

            root.pendingRealtimeSave = true
            left_toolbar.display_content_selection = 3
        }
    }

    TabBar {
        id: waveform_spectrum_choose
        anchors {
            top: parent.top
            topMargin: root.layout_margin
            left: leftPanel.right
            right: parent.right
        }
        height: 35
        spacing: 20
        currentIndex: 0

        background: Rectangle {
            color: "transparent"
        }

        Material.accent: "transparent"

        Repeater {
            id: waveformTabRepeater
            model: ListModel {
                ListElement { btnText: "Time-domain waveform" }
                ListElement { btnText: "Frequency-domain spectrum" }
            }

            TabButton {
                id: tabBtn
                text: model.btnText
                width: implicitWidth + 10

                contentItem: Text {
                    text: tabBtn.text
                    font.family: Theme.numberFontFamily
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textMuted

                    Behavior on color {
                        ColorAnimation {
                            duration: 200
                        }
                    }
                }

                background: Item {}
            }
        }
    }

    Rectangle {
        id: choosing_instruction
        height: 3
        radius: 1.5
        color: Theme.primary
        anchors.top: waveform_spectrum_choose.bottom

        readonly property Item targetItem: (
            waveform_spectrum_choose.currentIndex >= 0 &&
            waveform_spectrum_choose.currentIndex < waveformTabRepeater.count
        ) ? waveformTabRepeater.itemAt(waveform_spectrum_choose.currentIndex) : null

        width: targetItem ? targetItem.width * 0.6 : 0
        x: targetItem ? (
            targetItem.x +
            waveform_spectrum_choose.x +
            waveform_spectrum_choose.contentItem.x +
            (targetItem.width - width) / 2
        ) : waveform_spectrum_choose.x

        Behavior on x {
            SpringAnimation {
                spring: 3.5
                damping: 0.3
                epsilon: 0.5
            }
        }

        Behavior on width {
            NumberAnimation {
                duration: 100
                easing.type: Easing.OutQuint
            }
        }
    }

    Rectangle {
        id: separatorLine
        anchors {
            top: waveform_spectrum_choose.bottom
            topMargin: 7
            left: leftPanel.right
            leftMargin: root.layout_margin
            right: waveform_spectrum_choose.right
            rightMargin: root.layout_margin
        }
        height: 1
        color: Theme.border
    }

    SwipeView {
        id: mainSwipeView
        anchors {
            top: separatorLine.bottom
            topMargin: root.layout_margin / 2
            left: leftPanel.right
            leftMargin: root.layout_margin / 2
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        interactive: false
        currentIndex: (count > 0)
            ? Math.max(0, Math.min(waveform_spectrum_choose.currentIndex, count - 1))
            : 0

        RTDataVisualizationGraphCartesianWaveform {
            id: rawTimeWaveform
            currentResolutionX: 0.3
            rawDataResolutionThreshold: adaptiveDownsampling.rawDataResolutionThreshold
            downsampledData: adaptiveDownsampling.downsampledData
            currentChannelIndex: root.currentChannelIndex
        }

        DataVisualizationGraphCartesianSpectrum {
            id: rawFrequencySpectrum
            dftData: signalDFTCalculation.dftData
            linkedTimeCenterSeconds: (rawTimeWaveform.fromTimestamp + rawTimeWaveform.toTimestamp) / 2
        }

        /*
         * 预留：去噪后时域波形与频域频谱视图（暂未启用）
        DataVisualizationGraphCartesian {
            id: denoisedTimeWaveform
        }

        DataVisualizationGraphCartesian {
            id: denoisedFrequencySpectrum
        }
        */
    }

    Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 240
        height: 220
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: root.pendingRealtimeSave

        Overlay.modal: BlurGlass {
            blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
            blurAmount: 64
            borderRadius: 25
            overlayOpacity: 0.3
        }

        background: Rectangle {
            radius: 24
            color: root.colorWithAlpha(Theme.primaryLighter, 0.96)
            border.width: 1
            border.color: root.colorWithAlpha(Theme.primaryBorder, 0.45)
        }

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 20

            StandbyAnimation {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 78
                height: 78
                blockColor: Theme.primary
                running: root.pendingRealtimeSave
            }

            Text {
                width: 180
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("实时数据保存处理中...")
                color: Theme.textPrimary
                font.pixelSize: 16
            }
        }
    }
}
