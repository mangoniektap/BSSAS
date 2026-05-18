/**
 * @file File_Management.qml
 * @brief 文件管理页面。提供 WAV 音频导入与分析、分析报告打开、数据处理算法开关及数据保存功能。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MangoComponent
import BSSAS
import BSSASSettingsStorage

Item {
    id: root

    property Item pageRoot
    property bool pendingImportedAnalysis: false
    property bool pendingRealtimeSave: false
    property bool pendingReportOpen: false
    property string importStatusText: ""

    readonly property color themeBlue: Theme.primary
    readonly property color dividerColor: root.colorWithAlpha(themeBlue, 0.22)
    readonly property color mutedTextColor: Theme.textMuted
    readonly property color cardColor: "transparent"
    readonly property color cardBorderColor: "transparent"
    readonly property color defaultBorderColor: Theme.primaryBorder
    readonly property color hoverFillColor: root.colorWithAlpha(themeBlue, 0.08)
    readonly property color pressedFillColor: root.colorWithAlpha(themeBlue, 0.16)
    readonly property color primaryButtonColor: themeBlue
    readonly property color primaryButtonPressedColor: root.colorWithAlpha(themeBlue, 0.88)
    readonly property var importProcessingItems: [
        qsTr("\u5e26\u901a\u6ee4\u6ce2"),
        qsTr("\u9677\u6ce2\u6ee4\u6ce2"),
        qsTr("\u81ea\u9002\u5e94\u964d\u566a"),
        qsTr("\u5c0f\u6ce2\u964d\u566a"),
        qsTr("\u77ac\u6001\u566a\u58f0\u6291\u5236"),
        qsTr("\u540c\u9891\u6bb5\u8fd0\u52a8\u4f2a\u5f71\u6d88\u9664")
    ]
    readonly property bool importAllProcessingState: signalPreprocessing.importAllProcessingEnabled
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
     * @brief 从文件路径中提取文件名。
     * @param filePath 完整文件路径
     * @returns 文件名，空路径返回空字符串
     */
    function extractFileName(filePath) {
        if (!filePath || filePath.length === 0)
            return ""

        const normalizedPath = filePath.replace(/\\/g, "/")
        const pathSegments = normalizedPath.split("/")
        return pathSegments.length > 0 ? pathSegments[pathSegments.length - 1] : filePath
    }

    /**
     * @brief 将 file:// URL 转换为本地文件系统路径。
     * @param fileUrl 文件 URL
     * @returns 解码后的本地文件路径
     */
    function toLocalFilePath(fileUrl) {
        const raw = fileUrl ? fileUrl.toString() : ""
        return raw.length > 0 ? decodeURIComponent(raw).replace(/^file:\/\/\//, "") : ""
    }

    /**
     * @brief 完成导入分析工作流，重置导入状态标志与状态文本。
     */
    function finishImportWorkflow() {
        root.pendingImportedAnalysis = false
        root.importStatusText = ""
    }

    /**
     * @brief 打开分析报告：若 Loader 已加载则直接调用 open()，否则激活 Loader 延时打开。
     */
    function openAnalysisReport() {
        if (analysisReportLoader.item) {
            root.pendingReportOpen = false
            analysisReportLoader.item["open"]()
            return
        }

        root.pendingReportOpen = true
        analysisReportLoader.active = true
    }

    /**
     * @brief 切换指定导入处理算法的启用状态。
     * @param index 处理算法索引（0-5 对应带通滤波、陷波滤波、自适应降噪、小波降噪、瞬态噪声抑制、运动伪影消除）
     * @param enabledValue 显式目标状态，未传入则取反
     */
    function toggleImportProcessing(index, enabledValue) {
        const hasExplicitValue = enabledValue !== undefined
        switch (index) {
        case 0:
            signalPreprocessing.importBandpassEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importBandpassEnabled
            break
        case 1:
            signalPreprocessing.importNotchEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importNotchEnabled
            break
        case 2:
            signalPreprocessing.importAdaptiveNoiseReductionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importAdaptiveNoiseReductionEnabled
            break
        case 3:
            signalPreprocessing.importWaveletDenoisingEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importWaveletDenoisingEnabled
            break
        case 4:
            signalPreprocessing.importTransientNoiseSuppressionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importTransientNoiseSuppressionEnabled
            break
        case 5:
            signalPreprocessing.importMotionArtifactReductionEnabled = hasExplicitValue ? enabledValue : !signalPreprocessing.importMotionArtifactReductionEnabled
            break
        default:
            break
        }
    }

    /**
     * @brief 获取指定导入处理算法的当前启用状态。
     * @param index 处理算法索引（0-5）
     * @returns 启用返回 true，否则返回 false
     */
    function importProcessingStateAt(index) {
        switch (index) {
        case 0:
            return !!signalPreprocessing.importBandpassEnabled
        case 1:
            return !!signalPreprocessing.importNotchEnabled
        case 2:
            return !!signalPreprocessing.importAdaptiveNoiseReductionEnabled
        case 3:
            return !!signalPreprocessing.importWaveletDenoisingEnabled
        case 4:
            return !!signalPreprocessing.importTransientNoiseSuppressionEnabled
        case 5:
            return !!signalPreprocessing.importMotionArtifactReductionEnabled
        default:
            return true
        }
    }

    /**
     * @brief 批量设置所有导入处理算法的启用状态。
     * @param enabledValue 目标启用状态
     */
    function setAllImportProcessing(enabledValue) {
        signalPreprocessing.setAllImportProcessingEnabled(!!enabledValue)
    }

    function normalizedAdaptiveNoiseReductionLevel(value) {
        const numericValue = Number(value)
        if (!isFinite(numericValue))
            return 1
        return Math.max(0, Math.min(3, Math.round(numericValue)))
    }

    function openImportAdaptiveNoiseReductionConfig() {
        importAdaptiveNoiseReductionConfigPopup.level =
            root.normalizedAdaptiveNoiseReductionLevel(AppState.importAdaptiveNoiseReductionLevel)
        importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled =
            !!AppState.importAdaptiveNoiseReductionHighPassFilterEnabled
        importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled =
            !!AppState.importAdaptiveNoiseReductionAutomaticGainControlEnabled
        importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled =
            !!AppState.importAdaptiveNoiseReductionTransientSuppressionEnabled
        importAdaptiveNoiseReductionConfigPopup.open()
    }

    function saveImportAdaptiveNoiseReductionConfig() {
        AppState.importAdaptiveNoiseReductionLevel =
            root.normalizedAdaptiveNoiseReductionLevel(importAdaptiveNoiseReductionConfigPopup.level)
        AppState.importAdaptiveNoiseReductionHighPassFilterEnabled =
            importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled
        AppState.importAdaptiveNoiseReductionAutomaticGainControlEnabled =
            importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled
        AppState.importAdaptiveNoiseReductionTransientSuppressionEnabled =
            importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled
        importAdaptiveNoiseReductionConfigPopup.close()
    }

    function importScientificFilterConfig() {
        return {
            enabled: AppState.importScientificFilterEnabled,
            prototype: AppState.importScientificFilterPrototype,
            filterType: AppState.importScientificFilterType,
            order: AppState.importScientificFilterOrder,
            cutoffFrequencyHz: AppState.importScientificFilterCutoffFrequencyHz,
            lowCutoffFrequencyHz: AppState.importScientificFilterLowCutoffFrequencyHz,
            highCutoffFrequencyHz: AppState.importScientificFilterHighCutoffFrequencyHz,
            transitionBandwidthHz: AppState.importScientificFilterTransitionBandwidthHz,
            stopbandAttenuationDb: AppState.importScientificFilterStopbandAttenuationDb,
            passbandRippleDb: AppState.importScientificFilterPassbandRippleDb
        }
    }

    function openImportScientificFilterConfig() {
        importScientificFilterConfigPopup.loadFrom(root.importScientificFilterConfig())
        importScientificFilterConfigPopup.open()
    }

    function saveImportScientificFilterConfig(config) {
        AppState.importScientificFilterEnabled = !!config.enabled
        AppState.importScientificFilterPrototype = config.prototype
        AppState.importScientificFilterType = config.filterType
        AppState.importScientificFilterOrder = config.order
        AppState.importScientificFilterCutoffFrequencyHz = config.cutoffFrequencyHz
        AppState.importScientificFilterLowCutoffFrequencyHz = config.lowCutoffFrequencyHz
        AppState.importScientificFilterHighCutoffFrequencyHz = config.highCutoffFrequencyHz
        AppState.importScientificFilterTransitionBandwidthHz = config.transitionBandwidthHz
        AppState.importScientificFilterStopbandAttenuationDb = config.stopbandAttenuationDb
        AppState.importScientificFilterPassbandRippleDb = config.passbandRippleDb
    }

    Connections {
        target: multiFeatureJointDetection

        function onImportedAnalysisCompleted() {
            if (!root.pendingImportedAnalysis)
                return

            root.finishImportWorkflow()
            root.openAnalysisReport()
        }

        function onImportedAnalysisFailed(errorMessage) {
            root.finishImportWorkflow()
            pageToast.show(errorMessage.length > 0 ? "分析失败：" + errorMessage : "分析失败", Theme.danger)
        }
    }

    Connections {
        target: wavHandle

        function onSaveCompleted(outputPath) {
            root.pendingRealtimeSave = false
            const fileName = root.extractFileName(outputPath)
            pageToast.show(fileName.length > 0 ? "WAV 保存成功：" + fileName : "WAV 保存成功", Theme.primary)
        }

        function onSaveFailed(errorMessage) {
            root.pendingRealtimeSave = false
            pageToast.show(errorMessage.length > 0 ? "WAV 保存失败：" + errorMessage : "WAV 保存失败", Theme.danger)
        }

        function onImportDataReady() {
            if (!root.pendingImportedAnalysis)
                return

            root.importStatusText = "音频预处理中..."
            signalDFTCalculation.startImportedDftProcessing()
        }

        function onImportFailed(errorMessage) {
            root.finishImportWorkflow()
            pageToast.show(errorMessage.length > 0 ? "导入失败：" + errorMessage : "导入失败", Theme.danger)
        }
    }

    Connections {
        target: signalDFTCalculation

        function onImportedDftProcessingFinished() {
            if (!root.pendingImportedAnalysis)
                return

            root.importStatusText = "特征分析中..."
            multiFeatureJointDetection.startImportedAnalysis()
        }
    }

    ToastNotification {
        id: pageToast
        duration: 2000
        topMargin: 20
        z: 1500
    }

    ScrollView {
        id: pageScrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: mainColumn.implicitHeight
        leftPadding: 30
        topPadding: 30
        rightPadding: 30
        bottomPadding: 30

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: mainColumn
            width: pageScrollView.availableWidth
            spacing: 24

            Rectangle {
                Layout.fillWidth: true
                radius: 22
                color: root.cardColor
                border.width: 0
                border.color: root.cardBorderColor
                implicitHeight: importCardContent.implicitHeight + 48

                ColumnLayout {
                    id: importCardContent
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

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
                            text: "导入音频文件"
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: Theme.textPrimary
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "导入并开始分析"
                            font.pixelSize: 15
                            font.bold: true
                            color: Theme.textPrimary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "点击后选择一个 WAV 文件，导入完成后会自动进入分析流程。"
                            wrapMode: Text.WordWrap
                            color: root.mutedTextColor
                            font.pixelSize: 13
                        }

                        Button {
                            id: importWavFileButton
                            Layout.alignment: Qt.AlignLeft
                            Layout.preferredWidth: 220
                            Layout.preferredHeight: 44
                            hoverEnabled: true
                            enabled: !root.pendingImportedAnalysis && !root.pendingRealtimeSave
                            text: "选择并导入 WAV"

                            contentItem: Text {
                                text: importWavFileButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: Theme.textWhite
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 10
                                color: importWavFileButton.enabled
                                    ? (importWavFileButton.down ? root.primaryButtonPressedColor : root.primaryButtonColor)
                                    : root.colorWithAlpha(root.themeBlue, 0.38)
                            }

                            onClicked: fileDialog.open()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "打开分析报告"
                            font.pixelSize: 15
                            font.bold: true
                            color: Theme.textPrimary
                        }

                        Button {
                            id: openReportButton
                            Layout.alignment: Qt.AlignLeft
                            Layout.preferredWidth: 220
                            Layout.preferredHeight: 44
                            hoverEnabled: true
                            enabled: !root.pendingImportedAnalysis && !root.pendingRealtimeSave
                            text: "打开分析报告"

                            contentItem: Text {
                                text: openReportButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: Theme.textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 10
                                border.width: 2
                                border.color: root.defaultBorderColor
                                color: openReportButton.enabled
                                    ? (openReportButton.down ? root.pressedFillColor : (openReportButton.hovered ? root.hoverFillColor : "transparent"))
                                    : root.colorWithAlpha(Theme.textPrimary, 0.04)
                            }

                            onClicked: root.openAnalysisReport()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        radius: 0.5
                        color: root.dividerColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        spacing: 10

                        Text {
                            text: "分析时间长度"
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        Item { Layout.fillWidth: true }

                        Select {
                            id: importAnalysisTimeLengthChoose

                            Layout.preferredWidth: 170
                            Layout.preferredHeight: 50
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            currentIndex: AppState.analysisTimeLengthIndex(AppState.importAnalysisTimeLength)
                            delegateHeight: 30
                            visibleCount: 3
                            popupPadding: 5
                            showScrollIndicator: false
                            textColor: Theme.textPrimary
                            outlineColor: root.defaultBorderColor
                            activeOutlineColor: root.themeBlue
                            indicatorColor: root.themeBlue
                            optionHighlightColor: root.colorWithAlpha(root.themeBlue, 0.12)
                            popupColor: Theme.textWhite
                            popupBorderColor: root.defaultBorderColor
                            model: AppState.analysisTimeLengthLabels

                            onCurrentIndexChanged: {
                                const selectedValue = AppState.analysisTimeLengthValue(currentIndex)
                                if (Math.abs(AppState.importAnalysisTimeLength - selectedValue) >= 0.0001)
                                    AppState.importAnalysisTimeLength = selectedValue
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        radius: 0.5
                        color: root.dividerColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        spacing: 10

                        Text {
                            text: qsTr("带通滤波器类型")
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "IIR"
                            font.pixelSize: 14
                            font.bold: !signalPreprocessing.importFirFilterEnabled
                            color: signalPreprocessing.importFirFilterEnabled
                                ? root.colorWithAlpha(Theme.textPrimary, 0.62)
                                : root.themeBlue
                        }

                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: importFilterTypeSwitch.implicitWidth
                            implicitHeight: importFilterTypeSwitch.implicitHeight

                            ToggleSwitch {
                                id: importFilterTypeSwitch
                                anchors.centerIn: parent
                                checked: signalPreprocessing.importFirFilterEnabled
                                interactive: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: signalPreprocessing.importFirFilterEnabled = !signalPreprocessing.importFirFilterEnabled
                            }
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: "FIR"
                            font.pixelSize: 14
                            font.bold: signalPreprocessing.importFirFilterEnabled
                            color: signalPreprocessing.importFirFilterEnabled
                                ? root.themeBlue
                                : root.colorWithAlpha(Theme.textPrimary, 0.62)
                        }
                    }

                    RowLayout {
                        id: importScientificFilterConfigAnchor

                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        spacing: 10

                        Text {
                            text: qsTr("科学滤波器")
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        Text {
                            text: AppState.importScientificFilterEnabled ? qsTr("已启用") : qsTr("未启用")
                            font.pixelSize: 13
                            color: AppState.importScientificFilterEnabled
                                ? Theme.primary
                                : root.colorWithAlpha(Theme.textPrimary, 0.52)
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            id: importScientificFilterConfigButton

                            Layout.preferredWidth: 96
                            Layout.preferredHeight: 36
                            hoverEnabled: true
                            text: qsTr("配置")

                            contentItem: Text {
                                text: importScientificFilterConfigButton.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                color: Theme.primary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 18
                                border.width: 1
                                border.color: importScientificFilterConfigButton.hovered
                                    || importScientificFilterConfigPopup.visible
                                    ? Theme.primary
                                    : root.defaultBorderColor
                                color: importScientificFilterConfigButton.down
                                    ? root.colorWithAlpha(Theme.primary, 0.16)
                                    : (importScientificFilterConfigButton.hovered
                                        || importScientificFilterConfigPopup.visible
                                        ? root.colorWithAlpha(Theme.primary, 0.08)
                                        : "transparent")

                                Behavior on border.color {
                                    ColorAnimation { duration: 150 }
                                }

                                Behavior on color {
                                    ColorAnimation { duration: 150 }
                                }
                            }

                            onClicked: root.openImportScientificFilterConfig()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        spacing: 10

                        Text {
                            text: qsTr("陷波器配置")
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("固定频率")
                            font.pixelSize: 14
                            font.bold: signalPreprocessing.importNotchFrequencyMode === 0
                            color: signalPreprocessing.importNotchFrequencyMode === 0
                                ? root.themeBlue
                                : root.colorWithAlpha(Theme.textPrimary, 0.62)
                        }

                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: importNotchFrequencyModeSwitch.implicitWidth
                            implicitHeight: importNotchFrequencyModeSwitch.implicitHeight

                            ToggleSwitch {
                                id: importNotchFrequencyModeSwitch
                                anchors.centerIn: parent
                                checked: signalPreprocessing.importNotchFrequencyMode === 1
                                interactive: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: signalPreprocessing.importNotchFrequencyMode =
                                    signalPreprocessing.importNotchFrequencyMode === 1 ? 0 : 1
                            }
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("自适应")
                            font.pixelSize: 14
                            font.bold: signalPreprocessing.importNotchFrequencyMode === 1
                            color: signalPreprocessing.importNotchFrequencyMode === 1
                                ? root.themeBlue
                                : root.colorWithAlpha(Theme.textPrimary, 0.62)
                        }
                    }

                    RowLayout {
                        id: importAdaptiveNoiseReductionConfigAnchor

                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        spacing: 10

                        Text {
                            text: qsTr("自适应降噪参数")
                            font.pixelSize: 14
                            color: Theme.textPrimary
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            id: importAdaptiveNoiseReductionConfigButton

                            Layout.preferredWidth: 96
                            Layout.preferredHeight: 36
                            hoverEnabled: true
                            text: qsTr("配置")

                            contentItem: Text {
                                text: importAdaptiveNoiseReductionConfigButton.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                color: importAdaptiveNoiseReductionConfigButton.enabled
                                    ? Theme.primary
                                    : root.colorWithAlpha(Theme.textPrimary, 0.38)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 18
                                border.width: 1
                                border.color: importAdaptiveNoiseReductionConfigButton.hovered
                                    || importAdaptiveNoiseReductionConfigPopup.visible
                                    ? Theme.primary
                                    : root.defaultBorderColor
                                color: importAdaptiveNoiseReductionConfigButton.down
                                    ? root.colorWithAlpha(Theme.primary, 0.16)
                                    : (importAdaptiveNoiseReductionConfigButton.hovered
                                        || importAdaptiveNoiseReductionConfigPopup.visible
                                        ? root.colorWithAlpha(Theme.primary, 0.08)
                                        : "transparent")

                                Behavior on border.color {
                                    ColorAnimation {
                                        duration: 150
                                    }
                                }

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }
                                }
                            }

                            onClicked: root.openImportAdaptiveNoiseReductionConfig()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            spacing: 14

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: 4
                                implicitHeight: 20
                                radius: 2
                                color: root.themeBlue
                            }

                            Text {
                                text: "导入音频处理"
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: Theme.textPrimary
                            }

                            Item { Layout.fillWidth: true }
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
                                    text: "接入全部信号处理算法："
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    color: Theme.textPrimary
                                }

                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitWidth: masterImportSwitch.implicitWidth
                                    implicitHeight: masterImportSwitch.implicitHeight

                                    ToggleSwitch {
                                        id: masterImportSwitch
                                        anchors.centerIn: parent
                                        checked: root.importAllProcessingState
                                        interactive: false
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.setAllImportProcessing(!root.importAllProcessingState)
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width >= 860 ? 3 : (width >= 560 ? 2 : 1)
                            columnSpacing: 18
                            rowSpacing: 12

                            Repeater {
                                model: root.importProcessingItems.length

                                RowLayout {
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: root.importProcessingItems[index]
                                        font.pixelSize: 14
                                        color: Theme.textPrimary
                                    }

                                    Item {
                                        Layout.alignment: Qt.AlignVCenter
                                        implicitWidth: importAlgorithmSwitch.implicitWidth
                                        implicitHeight: importAlgorithmSwitch.implicitHeight

                                        ToggleSwitch {
                                            id: importAlgorithmSwitch
                                            anchors.centerIn: parent
                                            checked: root.importProcessingStateAt(index)
                                            interactive: false
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.toggleImportProcessing(index, !root.importProcessingStateAt(index))
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 22
                color: root.cardColor
                border.width: 0
                border.color: root.cardBorderColor
                implicitHeight: saveCardContent.implicitHeight + 48

                ColumnLayout {
                    id: saveCardContent
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

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
                            text: "保存数据文件"
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                            color: Theme.textPrimary
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        radius: 0.5
                        color: root.dividerColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "保存当前数据"
                            font.pixelSize: 15
                            font.bold: true
                            color: Theme.textPrimary
                        }

                        Button {
                            id: saveDataButton
                            Layout.alignment: Qt.AlignLeft
                            Layout.preferredWidth: 220
                            Layout.preferredHeight: 44
                            hoverEnabled: true
                            enabled: !root.pendingImportedAnalysis && !root.pendingRealtimeSave
                            text: "保存为 WAV 文件"

                            contentItem: Text {
                                text: saveDataButton.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                color: Theme.textWhite
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 10
                                color: saveDataButton.enabled
                                    ? (saveDataButton.down ? root.primaryButtonPressedColor : root.primaryButtonColor)
                                    : root.colorWithAlpha(root.themeBlue, 0.38)
                            }

                            onClicked: {
                                if (root.pendingRealtimeSave) {
                                    return
                                }
                                root.pendingRealtimeSave = true
                                wavHandle.startSaveAsWav()
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "选择 WAV 文件"
        nameFilters: ["WAV 文件 (*.wav)"]
        fileMode: FileDialog.OpenFile

        onAccepted: {
            const localFilePath = root.toLocalFilePath(fileDialog.selectedFile)
            root.pendingImportedAnalysis = true
            root.importStatusText = "读取 WAV 文件中..."
            wavHandle.startReadFromWav(localFilePath)
        }
    }

    Loader {
        id: analysisReportLoader
        active: false
        sourceComponent: Component {
            TimeFrequencyAnalysisReport {
                parent: Overlay.overlay
                anchors.centerIn: parent
                width: parent ? (parent.width * 3) / 4 : 800
                height: parent ? parent.height - 100 : 600
            }
        }

        onLoaded: {
            if (!root.pendingReportOpen || !item)
                return

            root.pendingReportOpen = false
            item["open"]()
        }
    }

    ScientificFilterConfigPopup {
        id: importScientificFilterConfigPopup
        titleText: qsTr("导入科学滤波器")
        sampleRate: Math.max(1, dataManager.configuredSampleRate)
        onSaveRequested: function(config) {
            root.saveImportScientificFilterConfig(config)
        }
    }

    Popup {
        id: importAdaptiveNoiseReductionConfigPopup

        property int level: 1
        property bool highPassFilterEnabled: false
        property bool automaticGainControlEnabled: false
        property bool transientSuppressionEnabled: false

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(280, Math.min((parent ? parent.width : 640) - 60, 560))
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    target: importAdaptiveNoiseReductionConfigPopup
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 120
                    easing.type: Easing.OutQuad
                }

                NumberAnimation {
                    target: importAdaptiveNoiseReductionConfigPopup
                    property: "scale"
                    from: 0.94
                    to: 1
                    duration: 180
                    easing.type: Easing.OutBack
                }
            }
        }

        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    target: importAdaptiveNoiseReductionConfigPopup
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 100
                    easing.type: Easing.InQuad
                }

                NumberAnimation {
                    target: importAdaptiveNoiseReductionConfigPopup
                    property: "scale"
                    from: 1
                    to: 0.96
                    duration: 100
                    easing.type: Easing.InQuad
                }
            }
        }

        background: BlurGlass {
            blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
            blurAmount: 64
            borderRadius: 25
            overlayOpacity: 0.3
        }

        contentItem: Item {
            implicitHeight: popupColumn.implicitHeight + 44

            ColumnLayout {
                id: popupColumn
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16

                Text {
                    Layout.fillWidth: true
                    text: qsTr("导入自适应降噪参数")
                    color: Theme.textTitle
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    spacing: 12

                    Text {
                        text: qsTr("降噪强度")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Select {
                        id: importAdaptiveNoiseReductionLevelSelect

                        Layout.preferredWidth: 180
                        Layout.preferredHeight: 46
                        currentIndex: importAdaptiveNoiseReductionConfigPopup.level
                        delegateHeight: 30
                        visibleCount: 4
                        popupPadding: 5
                        showScrollIndicator: false
                        textColor: Theme.textPrimary
                        outlineColor: Theme.primaryBorder
                        activeOutlineColor: Theme.primary
                        indicatorColor: Theme.primary
                        optionHighlightColor: root.colorWithAlpha(Theme.primary, 0.12)
                        popupColor: Theme.textWhite
                        popupBorderColor: Theme.primaryBorder
                        model: ["Low", "Moderate", "High", "VeryHigh"]
                        onCurrentIndexChanged: importAdaptiveNoiseReductionConfigPopup.level =
                            root.normalizedAdaptiveNoiseReductionLevel(currentIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: root.colorWithAlpha(Theme.primary, 0.18)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        implicitHeight: 38

                        MouseArea {
                            anchors.fill: parent
                            z: 1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled =
                                !importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled
                        }

                        RowLayout {
                            anchors.fill: parent
                            z: 0
                            spacing: 12

                            Text {
                                text: qsTr("高通滤波")
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ToggleSwitch {
                                checked: importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled
                                interactive: false
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        implicitHeight: 38

                        MouseArea {
                            anchors.fill: parent
                            z: 1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled =
                                !importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled
                        }

                        RowLayout {
                            anchors.fill: parent
                            z: 0
                            spacing: 12

                            Text {
                                text: qsTr("自动增益控制")
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ToggleSwitch {
                                checked: importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled
                                interactive: false
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        implicitHeight: 38

                        MouseArea {
                            anchors.fill: parent
                            z: 1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled =
                                !importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled
                        }

                        RowLayout {
                            anchors.fill: parent
                            z: 0
                            spacing: 12

                            Text {
                                text: qsTr("瞬态抑制")
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ToggleSwitch {
                                checked: importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled
                                interactive: false
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ThemedButton {
                        text: qsTr("恢复默认")
                        onClicked: {
                            importAdaptiveNoiseReductionConfigPopup.level = 1
                            importAdaptiveNoiseReductionConfigPopup.highPassFilterEnabled = false
                            importAdaptiveNoiseReductionConfigPopup.automaticGainControlEnabled = false
                            importAdaptiveNoiseReductionConfigPopup.transientSuppressionEnabled = false
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    ThemedButton {
                        text: qsTr("取消")
                        onClicked: importAdaptiveNoiseReductionConfigPopup.close()
                    }

                    ThemedButton {
                        text: qsTr("保存")
                        variant: "primary"
                        onClicked: root.saveImportAdaptiveNoiseReductionConfig()
                    }
                }
            }
        }
    }

    Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 240
        height: 220
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: root.pendingImportedAnalysis || root.pendingRealtimeSave

        background: BlurGlass {
            blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
            blurAmount: 64
            borderRadius: 25
            overlayOpacity: 0.3
        }

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 20

            StandbyAnimation {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 78
                height: 78
                blockColor: Theme.primary
                running: root.pendingImportedAnalysis || root.pendingRealtimeSave
            }

            Text {
                width: 180
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.pendingRealtimeSave ? qsTr("实时数据保存处理中...") : root.importStatusText
                color: Theme.textPrimary
                font.pixelSize: 16
            }
        }
    }
}
