/**
 * @file Auxiliary_Diagnosis.qml
 * @brief 辅助诊断页面。按工作流构想图组织模型选择、上传识别和结果摘要。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BSSAS
import MangoComponent
import BSSASSettingsStorage

Item {
    id: root
    anchors.fill: parent
    clip: true

    property bool pageActive: true
    property bool verbose: false
    property bool includeProbs: false
    property string selectedUploadFilePath: ""
    property string lastRequestedFilePath: ""
    property bool recognitionBusy: false
    property bool recognitionHasResult: false
    property string recognitionStatusMessage: ""
    property string recognitionErrorMessage: ""
    property real recognitionUploadedBytes: 0
    property real recognitionTotalUploadBytes: 0
    property real recognitionUploadProgress: 0
    property var recognitionResult: ({})
    property string recognitionLastRequestId: ""
    property string currentModelKey: "hubert"
    property string lastRequestedModelKey: "hubert"

    readonly property string fixedDisplayText: "肠鸣音识别"
    readonly property string assetBase: "qrc:/qt/qml/BSSASContent/image resources/auxiliary diagnosis/"
    readonly property bool compactLayout: width < 980
    readonly property int outerMargin: Math.max(12, Math.min(18, width * 0.015))
    readonly property int contentMargin: Math.max(18, Math.min(26, width * 0.022))
    readonly property int topPanelHeight: compactLayout
        ? Math.min(390, Math.max(320, height * 0.42))
        : Math.min(260, Math.max(218, height * 0.31))
    readonly property int footerHeight: 28
    readonly property var recognitionModels: [
        { key: "hubert", title: "HuBERT", description: "聚类预测语音模型" },
        { key: "wav2vec", title: "Wav2Vec 2.0", description: "对比学习语音表示" }
    ]
    readonly property var currentModelItem: root.modelEntryForKey(root.currentModelKey)
    readonly property string currentModelText: currentModelItem.title
    readonly property string recognitionServiceHostPortExample: "127.0.0.1:8000"
    readonly property bool serviceConfigured: AppState.recognitionServiceHostPort.trim().length > 0
        && AppState.recognitionApiKey.trim().length > 0
    readonly property var workflowSteps: [
        {
            title: "上传文件",
            subtitle: "接收音频文件",
            icon: root.asset("upload_audio_step_icon.svg")
        },
        {
            title: "信号预处理",
            subtitle: "降噪与分段处理",
            icon: root.asset("signal_preprocessing_step_icon.svg")
        },
        {
            title: "特征编码",
            subtitle: "提取关键特征",
            icon: root.asset("feature_encoding_step_icon.svg")
        },
        {
            title: "异常识别",
            subtitle: "模型推理识别",
            icon: root.asset("abnormal_recognition_step_icon.svg")
        },
        {
            title: "生成建议",
            subtitle: "输出诊断建议",
            icon: root.asset("diagnosis_advice_step_icon.svg")
        }
    ]

    onWidthChanged: {
        if (modelMenuPopup.visible) {
            root.updateModelMenuGeometry()
        }
    }

    onHeightChanged: {
        if (modelMenuPopup.visible) {
            root.updateModelMenuGeometry()
        }
    }

    component IconBadge: Rectangle {
        id: iconBadge
        property string source: ""
        property real iconSize: Math.min(width, height) * 0.58
        property color fillColor: Theme.primaryLight
        property color strokeColor: Theme.borderLight

        radius: Math.min(width, height) * 0.28
        color: fillColor
        border.width: 1
        border.color: strokeColor

        Image {
            anchors.centerIn: parent
            width: iconBadge.iconSize
            height: iconBadge.iconSize
            source: iconBadge.source
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
    }

    component SectionHeader: Item {
        id: sectionHeader
        required property string title

        implicitHeight: 34

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 5
            height: 22
            radius: 3
            color: Theme.primary
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: sectionHeader.title
            color: Theme.textTitle
            font.family: Theme.fontFamily
            font.pixelSize: 18
            font.bold: true
            renderType: Text.NativeRendering
        }
    }

    component SwitchOption: Item {
        id: switchOption
        required property string label
        required property bool checked
        property bool optionEnabled: true
        signal toggled()

        implicitWidth: 184
        implicitHeight: 84

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            text: switchOption.label
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 16
            renderType: Text.NativeRendering
        }

        ToggleSwitch {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: -4
            checked: switchOption.checked
            enabled: switchOption.optionEnabled
            interactive: false
            showIcon: false
        }

        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: Math.max(54, switchValueText.implicitWidth + 24)
            height: 28
            radius: 14
            color: Theme.pageBg
            border.width: 1
            border.color: Theme.borderLight

            Text {
                id: switchValueText
                anchors.centerIn: parent
                text: switchOption.checked ? "true" : "false"
                color: switchOption.checked ? Theme.primary : Theme.textMuted
                font.family: Theme.numberFontFamily
                font.pixelSize: 13
                font.bold: true
                renderType: Text.NativeRendering
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            enabled: switchOption.optionEnabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: switchOption.toggled()
        }
    }

    component WorkflowNode: Item {
        id: workflowNode
        required property string title
        required property string subtitle
        required property string iconSource
        required property int stepIndex
        property bool active: false

        implicitHeight: 124

        Rectangle {
            id: stepIcon
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: 66
            height: 66
            radius: 33
            color: workflowNode.active ? Theme.primaryLight : Theme.pageBg
            border.width: 1
            border.color: workflowNode.active ? Theme.primaryBorder : Theme.borderLight

            Image {
                anchors.centerIn: parent
                width: 34
                height: 34
                source: workflowNode.iconSource
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                opacity: workflowNode.active ? 1 : 0.76
            }
        }

        Text {
            anchors.top: stepIcon.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            horizontalAlignment: Text.AlignHCenter
            text: workflowNode.title
            color: Theme.textTitle
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.bold: true
            elide: Text.ElideRight
            renderType: Text.NativeRendering
        }

        Text {
            anchors.top: stepIcon.bottom
            anchors.topMargin: 34
            anchors.left: parent.left
            anchors.right: parent.right
            horizontalAlignment: Text.AlignHCenter
            text: workflowNode.subtitle
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 12
            elide: Text.ElideRight
            renderType: Text.NativeRendering
        }
    }

    component SummaryItem: Rectangle {
        id: summaryItem
        required property string title
        required property string value
        required property string stateText
        required property string iconSource
        property color stateColor: Theme.textMuted

        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(78, Math.min(92, root.height * 0.095))
        radius: 14
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.borderLight

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 14
            spacing: 14

            IconBadge {
                Layout.preferredWidth: 54
                Layout.preferredHeight: 54
                iconSize: 32
                source: summaryItem.iconSource
                fillColor: Theme.primaryLight
                strokeColor: "transparent"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: summaryItem.title
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }

                Text {
                    Layout.fillWidth: true
                    text: summaryItem.value
                    color: Theme.textTitle
                    font.family: summaryItem.value === "--" ? Theme.numberFontFamily : Theme.fontFamily
                    font.pixelSize: summaryItem.value.length > 24 ? 15 : 18
                    font.bold: true
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }
            }

            Text {
                Layout.preferredWidth: Math.max(52, implicitWidth)
                horizontalAlignment: Text.AlignRight
                text: summaryItem.stateText
                color: summaryItem.stateColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    component InfoStrip: Rectangle {
        id: infoStrip
        required property string text

        Layout.fillWidth: true
        Layout.preferredHeight: 38
        radius: 11
        color: Theme.pageBg

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Image {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: root.asset("info_notice_icon.svg")
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
            }

            Text {
                Layout.fillWidth: true
                text: infoStrip.text
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    function asset(fileName) {
        return root.assetBase + fileName
    }

    function colorWithAlpha(colorValue, alpha) {
        const color = Qt.color(colorValue)
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    function toLocalFilePath(fileUrl) {
        const raw = fileUrl ? fileUrl.toString() : ""
        return raw.length > 0 ? decodeURIComponent(raw).replace(/^file:\/\/\//, "") : ""
    }

    function fileName(path) {
        const value = (path || "").replace(/\\/g, "/")
        const parts = value.split("/")
        return parts.length > 0 ? parts[parts.length - 1] : value
    }

    function normalizedModelKey(value) {
        return String(value || "").toLowerCase() === "wav2vec" ? "wav2vec" : "hubert"
    }

    function modelEntryForKey(modelKey) {
        const normalizedKey = normalizedModelKey(modelKey)
        for (let index = 0; index < root.recognitionModels.length; index += 1) {
            const item = root.recognitionModels[index]
            if (item.key === normalizedKey) {
                return item
            }
        }
        return root.recognitionModels[0]
    }

    function normalizeHostPort(value) {
        let normalizedValue = (value || "").trim()
        normalizedValue = normalizedValue.replace(/^https?:\/\//i, "")
        const separatorIndex = normalizedValue.search(/[\/\?#]/)
        if (separatorIndex >= 0) {
            normalizedValue = normalizedValue.slice(0, separatorIndex)
        }
        return normalizedValue.trim()
    }

    function buildRecognitionServiceUrl(hostPort, modelKey) {
        const normalizedHostPort = root.normalizeHostPort(hostPort)
        if (normalizedHostPort.length === 0) {
            return ""
        }
        return "http://" + normalizedHostPort + "/recognition/" + root.normalizedModelKey(modelKey)
    }

    function isValidHostPort(value) {
        const normalizedValue = root.normalizeHostPort(value)
        return normalizedValue.length > 0 && !/[\/\s\?#]/.test(normalizedValue)
    }

    function applySelectedModel(modelKey) {
        root.currentModelKey = root.normalizedModelKey(modelKey)
        modelMenuPopup.close()
    }

    /**
     * @brief 计算模型下拉框位置和尺寸，保证其跟随触发卡片并留在页面可视范围内。
     */
    function updateModelMenuGeometry() {
        const margin = 18
        const targetWidth = Math.max(
            Math.min(root.width - margin * 2, 300),
            Math.min(modelSelectorCard.width, root.width - margin * 2))
        modelMenuPopup.width = targetWidth
        modelMenuPopup.height = Math.min(modelMenuColumn.implicitHeight + 28, root.height - margin * 2)

        const belowPoint = modelSelectorCard.mapToItem(root, 0, modelSelectorCard.height + 8)
        const abovePoint = modelSelectorCard.mapToItem(root, 0, -modelMenuPopup.height - 8)
        modelMenuPopup.x = Math.max(margin, Math.min(belowPoint.x, root.width - modelMenuPopup.width - margin))
        modelMenuPopup.y = belowPoint.y + modelMenuPopup.height <= root.height - margin
            ? belowPoint.y
            : Math.max(margin, abovePoint.y)
    }

    function openModelMenu() {
        if (root.recognitionBusy) {
            return
        }
        root.updateModelMenuGeometry()
        modelMenuPopup.open()
    }

    function formatSeconds(value) {
        const numberValue = Number(value)
        return isFinite(numberValue) ? numberValue.toFixed(2) + " s" : "--"
    }

    function formatPercent(value) {
        const numberValue = Number(value)
        return isFinite(numberValue) ? (numberValue * 100).toFixed(2) + "%" : "--"
    }

    function formatInt(value) {
        const numberValue = Number(value)
        return isFinite(numberValue) ? String(Math.round(numberValue)) : "--"
    }

    function formatBytes(bytes) {
        const size = Number(bytes)
        if (!isFinite(size) || size <= 0) {
            return "未知"
        }

        const units = ["B", "KB", "MB", "GB"]
        let value = size
        let unitIndex = 0
        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024
            unitIndex += 1
        }
        return value.toFixed(unitIndex === 0 ? 0 : 2) + " " + units[unitIndex]
    }

    function uploadProgressLabel() {
        if (root.recognitionTotalUploadBytes > 0) {
            const percent = Math.max(0, Math.min(100, Math.round(root.recognitionUploadProgress * 100)))
            return percent + "% (" + formatBytes(root.recognitionUploadedBytes) + " / " +
                formatBytes(root.recognitionTotalUploadBytes) + ")"
        }

        if (root.recognitionUploadedBytes > 0) {
            return "已上传 " + formatBytes(root.recognitionUploadedBytes)
        }

        return "准备上传..."
    }

    function resultObject() { return root.recognitionResult || ({}) }
    function summaryObject() { const result = resultObject(); return result.summary || ({}) }
    function windowAnalysisObject() { const result = resultObject(); return result.window_analysis || ({}) }
    function headlineText() { const summary = summaryObject(); const result = resultObject(); return summary.headline || result.summary_text || "识别结果" }
    function summaryText() { const summary = summaryObject(); const result = resultObject(); return summary.summary_text || result.summary_text || "" }

    function resultClassText() {
        if (!root.recognitionHasResult) {
            return "--"
        }
        return String(root.resultObject().final_class_name || root.headlineText() || "--")
    }

    function confidenceText() {
        return root.recognitionHasResult ? root.formatPercent(root.resultObject().confidence) : "--"
    }

    function audioDurationText() {
        return root.recognitionHasResult ? root.formatSeconds(root.resultObject().audio_duration_sec) : "--"
    }

    function adviceText() {
        if (!root.recognitionHasResult) {
            return "--"
        }

        const summary = root.summaryObject()
        return summary.diagnosis_advice
            || summary.recommendation
            || root.summaryText()
            || "请结合临床信息复核识别结果。"
    }

    function statusTitleText() {
        if (root.recognitionErrorMessage.length > 0) {
            return "识别流程异常"
        }
        if (root.recognitionBusy) {
            return root.recognitionStatusMessage.length > 0 ? root.recognitionStatusMessage : "正在识别肠鸣音"
        }
        if (root.recognitionHasResult) {
            return root.headlineText()
        }
        return "等待上传音频开始识别"
    }

    function statusDescriptionText() {
        if (root.recognitionErrorMessage.length > 0) {
            return root.recognitionErrorMessage
        }
        if (root.recognitionBusy) {
            return root.uploadProgressLabel()
        }
        if (root.recognitionHasResult) {
            const file = root.fileName(root.lastRequestedFilePath)
            return file.length > 0 ? ("已完成 " + file + " 的识别流程") : "识别完成，结果已生成。"
        }
        return "请上传 WAV 格式的肠鸣音音频文件，系统将自动完成识别流程。"
    }

    function workflowActive(index) {
        if (root.recognitionHasResult) {
            return true
        }
        if (root.recognitionBusy) {
            const progress = root.recognitionTotalUploadBytes > 0 ? root.recognitionUploadProgress : 0.35
            return index <= Math.max(0, Math.min(4, Math.floor(progress * 5)))
        }
        return false
    }

    function syncRecognitionState() {
        const manager = recognitionServiceManager
        if (!manager) {
            root.recognitionBusy = false
            root.recognitionHasResult = false
            root.recognitionStatusMessage = ""
            root.recognitionErrorMessage = ""
            root.recognitionUploadedBytes = 0
            root.recognitionTotalUploadBytes = 0
            root.recognitionUploadProgress = 0
            root.recognitionResult = ({})
            root.recognitionLastRequestId = ""
            return
        }

        root.recognitionBusy = manager.busy
        root.recognitionHasResult = manager.hasResult
        root.recognitionStatusMessage = manager.statusMessage || ""
        root.recognitionErrorMessage = manager.errorMessage || ""
        root.recognitionUploadedBytes = Number(manager.uploadedBytes || 0)
        root.recognitionTotalUploadBytes = Number(manager.totalUploadBytes || 0)
        root.recognitionUploadProgress = Number(manager.uploadProgress || 0)
        root.recognitionResult = manager.result || ({})
        root.recognitionLastRequestId = manager.lastRequestId || ""
    }

    function openConfigDialog() {
        if (modelMenuPopup.visible) {
            modelMenuPopup.close()
        }
        configPopup.open()
    }

    function requestUpload() {
        if (root.recognitionBusy) {
            return
        }
        if (!serviceConfigured) {
            pageToast.showError("请先配置推理服务")
            openConfigDialog()
            return
        }
        uploadFileDialog.open()
    }

    function startRecognition(path) {
        const localPath = (path || "").trim()
        if (localPath.length === 0) {
            return
        }
        const endpointUrl = root.buildRecognitionServiceUrl(
            AppState.recognitionServiceHostPort,
            root.currentModelKey)
        root.lastRequestedFilePath = localPath
        root.selectedUploadFilePath = localPath
        root.lastRequestedModelKey = root.currentModelKey
        if (recognitionServiceManager) {
            recognitionServiceManager.recognizeFile(
                localPath,
                endpointUrl,
                AppState.recognitionApiKey,
                root.verbose,
                root.includeProbs)
        }
    }

    ToastNotification {
        id: pageToast
        duration: 2200
        topMargin: 24
    }

    Component.onCompleted: root.syncRecognitionState()

    Connections {
        target: recognitionServiceManager
        function onBusyChanged() { root.syncRecognitionState() }
        function onResultChanged() { root.syncRecognitionState() }
        function onStatusMessageChanged() { root.syncRecognitionState() }
        function onErrorMessageChanged() { root.syncRecognitionState() }
        function onUploadProgressChanged() { root.syncRecognitionState() }
        function onLastRequestIdChanged() { root.syncRecognitionState() }
        function onRecognitionSucceeded() { pageToast.showSuccess("识别完成") }
        function onRecognitionFailed(message) { if (message.length > 0) pageToast.showError(message) }
    }

    Rectangle {
        id: pageFrame
        anchors.fill: parent
        anchors.margins: root.outerMargin
        radius: 28
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.border
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.contentMargin
            spacing: 14

            Rectangle {
                id: topPanel
                Layout.fillWidth: true
                Layout.preferredHeight: root.topPanelHeight
                radius: 22
                color: Theme.heroBgStart
                border.width: 1
                border.color: Theme.border
                clip: true

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Math.max(0, parent.radius - 1)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#FBFDFF" }
                        GradientStop { position: 1.0; color: "#F2F8FF" }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Math.max(20, Math.min(30, topPanel.width * 0.025))
                    spacing: Math.max(16, Math.min(22, topPanel.height * 0.08))

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "辅助诊断"
                            color: Theme.textTitle
                            font.family: Theme.fontFamily
                            font.pixelSize: Math.max(30, Math.min(38, topPanel.height * 0.18))
                            font.bold: true
                            renderType: Text.NativeRendering
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "基于模型的肠鸣音识别与诊断建议生成"
                            color: Theme.textHeroSubtitle
                            font.family: Theme.fontFamily
                            font.pixelSize: Math.max(17, Math.min(22, topPanel.height * 0.1))
                            renderType: Text.NativeRendering
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: root.compactLayout ? 1 : 3
                        columnSpacing: 28
                        rowSpacing: 12

                        Rectangle {
                            id: modelSelectorCard
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 360
                            Layout.minimumHeight: 104
                            radius: 16
                            color: modelSelectorMouse.pressed
                                ? Theme.pageBg
                                : ((modelSelectorMouse.containsMouse || modelMenuPopup.visible) ? Theme.primaryLighter : Theme.textWhite)
                            border.width: 1
                            border.color: modelMenuPopup.visible ? Theme.primaryBorder : Theme.borderLight

                            Behavior on color { ColorAnimation { duration: 150 } }
                            Behavior on border.color { ColorAnimation { duration: 150 } }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 22
                                anchors.rightMargin: 18
                                spacing: 20

                                IconBadge {
                                    Layout.preferredWidth: 72
                                    Layout.preferredHeight: 72
                                    iconSize: 40
                                    source: root.asset("model_inference_icon.svg")
                                    fillColor: Theme.primaryLight
                                    strokeColor: "transparent"
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 12

                                    Text {
                                        Layout.fillWidth: true
                                        text: root.fixedDisplayText
                                        color: Theme.textTitle
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 21
                                        font.bold: true
                                        elide: Text.ElideRight
                                        renderType: Text.NativeRendering
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            text: root.currentModelText
                                            color: Theme.textPrimary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            elide: Text.ElideRight
                                            renderType: Text.NativeRendering
                                        }

                                        Text {
                                            text: "|"
                                            color: Theme.textMuted
                                            font.pixelSize: 14
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: root.serviceConfigured ? "已配置" : "未配置"
                                            color: root.serviceConfigured ? Theme.success : Theme.warning
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            font.bold: true
                                            elide: Text.ElideRight
                                            renderType: Text.NativeRendering
                                        }
                                    }
                                }

                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelMenuPopup.visible ? "⌃" : "⌄"
                                    color: Theme.textMuted
                                    font.pixelSize: 18
                                    font.bold: true
                                    opacity: root.recognitionBusy ? 0.4 : 1
                                }
                            }

                            MouseArea {
                                id: modelSelectorMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !root.recognitionBusy
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (modelMenuPopup.visible) {
                                        modelMenuPopup.close()
                                    } else {
                                        root.openModelMenu()
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: switchPanel
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 430
                            Layout.minimumHeight: 104
                            radius: 16
                            color: Theme.textWhite
                            border.width: 1
                            border.color: Theme.borderLight

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 26
                                anchors.rightMargin: 26
                                anchors.topMargin: 18
                                anchors.bottomMargin: 18
                                spacing: 24

                                SwitchOption {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    label: "verbose"
                                    checked: root.verbose
                                    optionEnabled: !root.recognitionBusy
                                    onToggled: root.verbose = !root.verbose
                                }

                                Rectangle {
                                    Layout.preferredWidth: 1
                                    Layout.fillHeight: true
                                    color: Theme.divider
                                }

                                SwitchOption {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    label: "include_probs"
                                    checked: root.includeProbs
                                    optionEnabled: !root.recognitionBusy
                                    onToggled: root.includeProbs = !root.includeProbs
                                }
                            }
                        }

                        Rectangle {
                            id: uploadPanel
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 350
                            Layout.minimumHeight: 104
                            radius: 16
                            color: uploadMouse.pressed
                                ? Theme.secondaryPressedBg
                                : (uploadMouse.containsMouse ? Theme.primaryLighter : Theme.textWhite)
                            border.width: 1
                            border.color: Theme.borderLight

                            Behavior on color { ColorAnimation { duration: 150 } }

                            Rectangle {
                                id: dashedUploadFrame
                                anchors.fill: parent
                                anchors.margins: 14
                                radius: 14
                                color: "transparent"

                                Canvas {
                                    id: dashedUploadBorder
                                    anchors.fill: parent
                                    antialiasing: true
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        const radius = dashedUploadFrame.radius
                                        const inset = 1
                                        const right = width - inset
                                        const bottom = height - inset

                                        ctx.clearRect(0, 0, width, height)
                                        ctx.beginPath()
                                        ctx.setLineDash([7, 5])
                                        ctx.lineWidth = 1.3
                                        ctx.strokeStyle = Theme.primary
                                        ctx.moveTo(radius + inset, inset)
                                        ctx.lineTo(right - radius, inset)
                                        ctx.quadraticCurveTo(right, inset, right, radius + inset)
                                        ctx.lineTo(right, bottom - radius)
                                        ctx.quadraticCurveTo(right, bottom, right - radius, bottom)
                                        ctx.lineTo(radius + inset, bottom)
                                        ctx.quadraticCurveTo(inset, bottom, inset, bottom - radius)
                                        ctx.lineTo(inset, radius + inset)
                                        ctx.quadraticCurveTo(inset, inset, radius + inset, inset)
                                        ctx.stroke()
                                    }
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                }
                            }

                            RowLayout {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 54, 248)
                                spacing: 18

                                IconBadge {
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 64
                                    iconSize: 42
                                    source: root.asset("upload_audio_step_icon.svg")
                                    fillColor: Theme.primaryLight
                                    strokeColor: "transparent"
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: "上传 WAV"
                                        color: Theme.textTitle
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 20
                                        font.bold: true
                                        elide: Text.ElideRight
                                        renderType: Text.NativeRendering
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "上传 WAV 文件开始识别"
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                        renderType: Text.NativeRendering
                                    }
                                }
                            }

                            MouseArea {
                                id: uploadMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !root.recognitionBusy
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.requestUpload()
                            }
                        }
                    }
                }
            }

            RowLayout {
                id: diagnosisBody
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Rectangle {
                    id: statusPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 730
                    radius: 16
                    color: Theme.textWhite
                    border.width: 1
                    border.color: Theme.border
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        SectionHeader {
                            Layout.fillWidth: true
                            title: "识别状态"
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 14
                            color: Theme.textWhite
                            border.width: 1
                            border.color: Theme.primaryBorder
                            clip: true

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 150

                                    Column {
                                        anchors.centerIn: parent
                                        width: Math.min(parent.width * 0.82, 620)
                                        spacing: 10

                                        Item {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width * 0.40
                                            height: parent.width * 0.40
                                            clip: true
                                            opacity: root.recognitionErrorMessage.length > 0 ? 0.45 : 1

                                            Image {
                                                /**
                                                 * 避免 sourceClipRect 在部分 Qt/PNG 解码路径下渲染为黑块；
                                                 * 先缩小完整图，再用容器裁掉外侧留白，保证中心波形和圆环不被切断。
                                                 */
                                                anchors.centerIn: parent
                                                width: parent.width * 1.70
                                                height: parent.height * 1.70
                                                source: root.asset("recognition_status_waveform.png")
                                                sourceSize.width: width
                                                sourceSize.height: height
                                                fillMode: Image.PreserveAspectFit
                                                smooth: true
                                                mipmap: true
                                            }
                                        }

                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: root.statusTitleText()
                                            color: root.recognitionErrorMessage.length > 0 ? Theme.danger : Theme.textTitle
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 20
                                            font.bold: true
                                            wrapMode: Text.WordWrap
                                            renderType: Text.NativeRendering
                                        }

                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: root.statusDescriptionText()
                                            color: root.recognitionErrorMessage.length > 0 ? Theme.dangerText : Theme.textSecondary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            maximumLineCount: 2
                                            wrapMode: Text.WordWrap
                                            elide: Text.ElideRight
                                            renderType: Text.NativeRendering
                                        }

                                        ProgressBar {
                                            width: Math.min(parent.width, 420)
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            visible: root.recognitionBusy
                                            indeterminate: root.recognitionTotalUploadBytes <= 0
                                            from: 0
                                            to: 1
                                            value: root.recognitionTotalUploadBytes > 0 ? root.recognitionUploadProgress : 0
                                        }
                                    }
                                }

                                Item {
                                    id: workflowTrack
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 124

                                    Canvas {
                                        id: workflowConnectorCanvas
                                        anchors.fill: parent
                                        z: 0
                                        antialiasing: true

                                        /**
                                         * @brief 绘制流程节点之间的虚线箭头，避免用文本横线导致箭头缺失。
                                         */
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            const nodeCount = root.workflowSteps.length
                                            const spacing = workflowNodeRow.spacing
                                            const iconRadius = 33
                                            const connectorPadding = 14
                                            const nodeWidth = (width - spacing * (nodeCount - 1)) / nodeCount
                                            const y = 33

                                            ctx.clearRect(0, 0, width, height)
                                            ctx.lineWidth = 1.3
                                            ctx.strokeStyle = Theme.primaryBorder
                                            ctx.fillStyle = Theme.primaryBorder
                                            ctx.setLineDash([7, 6])

                                            for (let index = 0; index < nodeCount - 1; index += 1) {
                                                const currentCenterX = index * (nodeWidth + spacing) + nodeWidth / 2
                                                const nextCenterX = (index + 1) * (nodeWidth + spacing) + nodeWidth / 2
                                                const startX = currentCenterX + iconRadius + connectorPadding
                                                const endX = nextCenterX - iconRadius - connectorPadding

                                                if (endX <= startX + 12) {
                                                    continue
                                                }

                                                ctx.beginPath()
                                                ctx.moveTo(startX, y)
                                                ctx.lineTo(endX - 8, y)
                                                ctx.stroke()

                                                ctx.setLineDash([])
                                                ctx.beginPath()
                                                ctx.moveTo(endX, y)
                                                ctx.lineTo(endX - 8, y - 4)
                                                ctx.lineTo(endX - 8, y + 4)
                                                ctx.closePath()
                                                ctx.fill()
                                                ctx.setLineDash([7, 6])
                                            }
                                        }

                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                    }

                                    RowLayout {
                                        id: workflowNodeRow
                                        anchors.fill: parent
                                        z: 1
                                        spacing: 8

                                        Repeater {
                                            model: root.workflowSteps

                                            delegate: WorkflowNode {
                                                required property var modelData
                                                required property int index

                                                Layout.fillWidth: true
                                                title: modelData.title
                                                subtitle: modelData.subtitle
                                                iconSource: modelData.icon
                                                stepIndex: index
                                                active: root.workflowActive(index)
                                            }
                                        }
                                    }
                                }

                                InfoStrip {
                                    text: "支持单通道 WAV 格式，采样率建议 8kHz 或 16kHz，时长不超过 5 分钟。"
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: summaryPanel
                    Layout.fillHeight: true
                    Layout.preferredWidth: Math.max(330, Math.min(430, diagnosisBody.width * 0.34))
                    Layout.minimumWidth: 310
                    radius: 16
                    color: Theme.textWhite
                    border.width: 1
                    border.color: Theme.border
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        SectionHeader {
                            Layout.fillWidth: true
                            title: "识别摘要"
                        }

                        SummaryItem {
                            title: "识别结果"
                            value: root.resultClassText()
                            stateText: root.recognitionHasResult ? "已识别" : "待识别"
                            stateColor: root.recognitionHasResult ? Theme.success : Theme.textMuted
                            iconSource: root.asset("recognition_result_icon.svg")
                        }

                        SummaryItem {
                            title: "置信度"
                            value: root.confidenceText()
                            stateText: root.recognitionHasResult ? "已计算" : "待计算"
                            stateColor: root.recognitionHasResult ? Theme.primary : Theme.textMuted
                            iconSource: root.asset("confidence_score_icon.svg")
                        }

                        SummaryItem {
                            title: "预计时长"
                            value: root.audioDurationText()
                            stateText: root.recognitionHasResult ? "已完成" : "--"
                            stateColor: root.recognitionHasResult ? Theme.success : Theme.textMuted
                            iconSource: root.asset("estimated_duration_icon.svg")
                        }

                        SummaryItem {
                            title: "诊断建议"
                            value: root.adviceText()
                            stateText: root.recognitionHasResult ? "已生成" : "待生成"
                            stateColor: root.recognitionHasResult ? Theme.primary : Theme.textMuted
                            iconSource: root.asset("diagnosis_advice_icon.svg")
                        }

                        Item {
                            Layout.fillHeight: true
                        }

                        InfoStrip {
                            text: root.recognitionHasResult
                                ? "识别结果仅作辅助参考，请结合临床评估使用。"
                                : "上传文件后，系统将自动开始识别并生成结果。"
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.footerHeight

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 26

                    Text {
                        text: "© 2024 肠鸣音信号分析系统 · 医学诊断辅助工具"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                    }

                    RowLayout {
                        spacing: 6

                        Image {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: root.asset("confidence_score_icon.svg")
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            text: "系统运行正常"
                            color: Theme.success
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            renderType: Text.NativeRendering
                        }
                    }

                    Text {
                        text: "版本: 1.0.0"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                    }
                }
            }
        }
    }

    FileDialog {
        id: uploadFileDialog
        title: "选择 WAV 文件"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV (*.wav)"]
        onAccepted: root.startRecognition(root.toLocalFilePath(uploadFileDialog.selectedFile))
    }

    Popup {
        id: modelMenuPopup
        parent: root
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: root.updateModelMenuGeometry()
        onOpened: root.updateModelMenuGeometry()

        background: Rectangle {
            radius: 18
            color: Theme.textWhite
            border.width: 1
            border.color: Theme.border
        }

        contentItem: Flickable {
            clip: true
            contentWidth: width
            contentHeight: modelMenuColumn.implicitHeight + 28
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            Column {
                id: modelMenuColumn
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 6

                Text {
                    width: parent.width
                    text: "识别模型"
                    color: Theme.textWeak
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    renderType: Text.NativeRendering
                }

                Repeater {
                    model: root.recognitionModels

                    delegate: Rectangle {
                        id: modelOptionCard
                        required property var modelData
                        readonly property bool selected: modelData.key === root.currentModelKey

                        width: modelMenuColumn.width
                        height: 76
                        radius: 14
                        color: modelOptionMouse.containsMouse ? Theme.primaryLighter : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            IconBadge {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                iconSize: 25
                                source: root.asset("model_inference_icon.svg")
                                fillColor: modelOptionCard.selected ? Theme.primaryLight : Theme.pageBg
                                strokeColor: "transparent"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: modelOptionCard.modelData.title
                                    color: Theme.textTitle
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 17
                                    font.bold: true
                                    elide: Text.ElideRight
                                    renderType: Text.NativeRendering
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelOptionCard.modelData.description
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    renderType: Text.NativeRendering
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                visible: modelOptionCard.selected
                                text: "✓"
                                color: Theme.primary
                                font.pixelSize: 22
                                font.bold: true
                            }
                        }

                        MouseArea {
                            id: modelOptionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.applySelectedModel(modelOptionCard.modelData.key)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.divider
                }

                Rectangle {
                    width: parent.width
                    height: 56
                    radius: 14
                    color: configEntryMouse.containsMouse ? Theme.primaryLighter : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Image {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            source: root.asset("info_notice_icon.svg")
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "服务配置"
                            color: Theme.textTitle
                            font.family: Theme.fontFamily
                            font.pixelSize: 16
                            renderType: Text.NativeRendering
                        }
                    }

                    MouseArea {
                        id: configEntryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            modelMenuPopup.close()
                            root.openConfigDialog()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: configPopup
        parent: root
        anchors.centerIn: parent
        width: Math.min(root.width - 60, 540)
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: {
            recognitionServiceHostPortField.text = AppState.recognitionServiceHostPort
            recognitionApiKeyField.text = AppState.recognitionApiKey
        }
        background: Rectangle {
            radius: 24
            color: Theme.textWhite
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Item {
            implicitHeight: popupCol.implicitHeight + 44

            ColumnLayout {
                id: popupCol
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: "推理服务配置"
                    color: Theme.textTitle
                    font.pixelSize: 26
                    font.bold: true
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    renderType: Text.NativeRendering
                }

                TextFields {
                    id: recognitionServiceHostPortField
                    Layout.fillWidth: true
                    type: "outlined"
                    label: "推理服务URL"
                    placeholderText: ""
                    supportingText: "示例: " + root.recognitionServiceHostPortExample
                    selectByMouse: true
                    labelBackgroundColor: Theme.textWhite
                    errorText: ""
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.borderLight
                    implicitHeight: servicePreviewColumn.implicitHeight + 24

                    Column {
                        id: servicePreviewColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Text {
                            width: parent.width
                            text: "当前模型: " + root.currentModelText
                            color: Theme.textTitle
                            font.pixelSize: 14
                            font.bold: true
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontFamily
                            renderType: Text.NativeRendering
                        }

                        Text {
                            width: parent.width
                            text: root.isValidHostPort(recognitionServiceHostPortField.text)
                                ? ("接口预览: " + root.buildRecognitionServiceUrl(
                                    recognitionServiceHostPortField.text,
                                    root.currentModelKey))
                                : "接口预览: 请输入有效的服务地址"
                            color: Theme.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.WrapAnywhere
                            font.family: Theme.fontFamily
                            renderType: Text.NativeRendering
                        }
                    }
                }

                TextFields {
                    id: recognitionApiKeyField
                    Layout.fillWidth: true
                    type: "outlined"
                    label: "API Key"
                    placeholderText: ""
                    selectByMouse: true
                    labelBackgroundColor: Theme.textWhite
                    errorText: ""
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "取消"
                        onClicked: configPopup.close()
                    }

                    Button {
                        text: "保存配置"
                        enabled: root.isValidHostPort(recognitionServiceHostPortField.text) &&
                            recognitionApiKeyField.text.trim().length > 0
                        onClicked: {
                            AppState.recognitionServiceHostPort = root.normalizeHostPort(recognitionServiceHostPortField.text)
                            AppState.recognitionApiKey = recognitionApiKeyField.text.trim()
                            configPopup.close()
                            pageToast.showSuccess("保存成功")
                        }
                    }
                }
            }
        }
    }

}
