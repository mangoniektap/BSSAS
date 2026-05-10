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
    readonly property var recognitionModels: [
        { key: "hubert", title: "HuBERT", description: "聚类预测语音模型" },
        { key: "wav2vec", title: "Wav2Vec 2.0", description: "对比学习语音表示" }
    ]
    readonly property var currentModelItem: root.modelEntryForKey(root.currentModelKey)
    readonly property string currentModelText: currentModelItem.title
    readonly property color titleColor: Theme.textTitle
    readonly property color mutedColor: Theme.textMuted
    readonly property color outlineColor: Theme.border
    readonly property color hoverColor: Theme.primaryLighter
    readonly property color accentColor: Theme.primary
    readonly property color softBlue: Theme.primaryLight
    readonly property color softRed: Theme.dangerBg
    readonly property color softGray: Theme.pageBg
    readonly property color resultHeadingColor: Theme.textTitle
    readonly property color resultBodyColor: Theme.textSecondary
    readonly property color resultLabelColor: Theme.textMuted
    readonly property color resultAccentTextColor: Theme.primary
    readonly property color resultCardColor: Theme.pageBg
    readonly property string recognitionServiceHostPortExample: "127.0.0.1:8000"
    readonly property bool serviceConfigured: AppState.recognitionServiceHostPort.trim().length > 0
        && AppState.recognitionApiKey.trim().length > 0

    component StatCard: Rectangle {
        id: statCard
        required property string label
        required property string value
        property bool highlight: false
        Layout.fillWidth: true
        implicitHeight: 92
        radius: 18
        color: statCard.highlight ? Theme.primaryLight : root.resultCardColor
        border.width: 1
        border.color: statCard.highlight ? Theme.primaryBorder : Theme.border
        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            Text { text: statCard.label; color: root.resultLabelColor; font.pixelSize: 13; font.family: Theme.fontFamily; wrapMode: Text.WordWrap }
            Text { text: statCard.value; color: statCard.highlight ? root.resultAccentTextColor : root.resultHeadingColor; font.pixelSize: 24; font.bold: true; font.family: Theme.fontFamily; wrapMode: Text.WrapAnywhere }
        }
    }

    function colorWithAlpha(colorValue, alpha) {
        const c = Qt.color(colorValue)
        return Qt.rgba(c.r, c.g, c.b, alpha)
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
        const separatorIndex = normalizedValue.search(/[\/?#]/)
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
        return normalizedValue.length > 0 && !/[\/\s?#]/.test(normalizedValue)
    }

    function applySelectedModel(modelKey) {
        root.currentModelKey = root.normalizedModelKey(modelKey)
        modelMenuPopup.close()
    }

    function openModelMenu() {
        if (root.recognitionBusy) {
            return
        }

        const point = modelSelectorCard.mapToItem(root, 0, modelSelectorCard.height + 10)
        modelMenuPopup.x = Math.max(18, Math.min(point.x, root.width - modelMenuPopup.width - 18))
        modelMenuPopup.y = point.y
        modelMenuPopup.open()
    }

    function formatSeconds(value) {
        const n = Number(value)
        return isFinite(n) ? n.toFixed(2) + "" : "--"
    }

    function formatPercent(value) {
        const n = Number(value)
        return isFinite(n) ? (n * 100).toFixed(2) + "%" : "--"
    }

    function formatInt(value) {
        const n = Number(value)
        return isFinite(n) ? String(Math.round(n)) : "--"
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
    function summaryObject() { const r = resultObject(); return r.summary || ({}) }
    function windowAnalysisObject() { const r = resultObject(); return r.window_analysis || ({}) }
    function eventsModel() { const r = resultObject(); return r.events || [] }
    function segmentsModel() { const r = resultObject(); return r.segments || [] }
    function voteDetailsModel() { return windowAnalysisObject().vote_details || [] }
    function headlineText() { const s = summaryObject(); const r = resultObject(); return s.headline || r.summary_text || "识别结果" }
    function summaryText() { const s = summaryObject(); const r = resultObject(); return s.summary_text || r.summary_text || "" }
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
            pageToast.showError("")
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

    ToastNotification { id: pageToast; duration: 2200; topMargin: 24 }

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

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 60
        anchors.topMargin: 40
        anchors.rightMargin: 30
        anchors.bottomMargin: 30
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            Item {
                id: modelSelectorBox
                Layout.preferredWidth: Math.max(280, Math.min(360, root.width - 110))
                Layout.preferredHeight: 68

                Rectangle {
                    id: modelSelectorCard
                    anchors.fill: parent
                    radius: 28
                    color: modelSelectorMouse.pressed
                        ? Theme.pageBg
                        : ((modelSelectorMouse.containsMouse || modelMenuPopup.visible) ? Theme.pageBg : Theme.textWhite)
                    border.width: 1
                    border.color: modelMenuPopup.visible ? Theme.primaryBorder : Theme.border

                    Behavior on border.color {
                        ColorAnimation {
                            duration: 140
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 12

                        Image {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            source: "qrc:/qt/qml/BSSASContent/images/whirlwind.png"
                            fillMode: Image.PreserveAspectFit
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: root.fixedDisplayText
                                color: root.titleColor
                                font.family: Theme.fontFamily
                                font.pixelSize: 21
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.currentModelText + (root.serviceConfigured ? "  |  已配置" : "  |  未配置")
                                color: root.serviceConfigured ? root.accentColor : root.mutedColor
                                font.pixelSize: 12
                                font.family: Theme.fontFamily
                                elide: Text.ElideRight
                            }
                        }

                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            source: "qrc:/qt/qml/BSSASContent/images/down.png"
                            fillMode: Image.PreserveAspectFit
                            rotation: modelMenuPopup.visible ? 180 : 0
                            opacity: modelSelectorMouse.enabled ? 0.9 : 0.45

                            Behavior on rotation {
                                NumberAnimation {
                                    duration: 160
                                }
                            }
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
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 32
            ColumnLayout {
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                spacing: 10
                Rectangle {
                    Layout.alignment: Qt.AlignLeft
                    implicitWidth: verboseRow.implicitWidth + 24
                    implicitHeight: 52
                    radius: 16
                    color: root.verbose ? root.softBlue : (verboseMouse.containsMouse ? root.hoverColor : Theme.textWhite)
                    border.width: 1; border.color: root.verbose ? Theme.primaryBorder : root.outlineColor
                    RowLayout {
                        id: verboseRow
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        CheckBoxes { checked: root.verbose; text: "verbose"; interactive: false }
                        Text { text: root.verbose ? "true" : "false"; color: root.verbose ? root.accentColor : root.mutedColor; font.pixelSize: 14; font.bold: true }
                    }
                    MouseArea {
                        id: verboseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !root.recognitionBusy
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.verbose = !root.verbose
                    }
                }
                Rectangle {
                    Layout.alignment: Qt.AlignLeft
                    implicitWidth: includeRow.implicitWidth + 24
                    implicitHeight: 52
                    radius: 16
                    color: root.includeProbs ? root.softBlue : (includeMouse.containsMouse ? root.hoverColor : Theme.textWhite)
                    border.width: 1; border.color: root.includeProbs ? Theme.primaryBorder : root.outlineColor
                    RowLayout {
                        id: includeRow
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        CheckBoxes { checked: root.includeProbs; text: "include_probs"; interactive: false }
                        Text { text: root.includeProbs ? "true" : "false"; color: root.includeProbs ? root.accentColor : root.mutedColor; font.pixelSize: 14; font.bold: true }
                    }
                    MouseArea {
                        id: includeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !root.recognitionBusy
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.includeProbs = !root.includeProbs
                    }
                }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: 10
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 82
                    Rectangle { anchors.fill: parent; radius: width / 2; color: uploadMouse.pressed ? Theme.secondaryPressedBg : (uploadMouse.containsMouse ? Theme.primaryLight : "transparent"); border.width: 1; border.color: uploadMouse.containsMouse ? Theme.primaryBorder : "transparent"
                        Image { anchors.centerIn: parent; width: 34; height: 34; source: "qrc:/qt/qml/BSSASContent/images/upload.png"; opacity: uploadMouse.enabled ? 1 : 0.45 }
                    }
                    MouseArea { id: uploadMouse; anchors.fill: parent; hoverEnabled: true; enabled: !root.recognitionBusy; onClicked: root.requestUpload() }
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: "上传 WAV"; color: root.titleColor; font.pixelSize: 14; font.bold: true }
                Text { Layout.preferredWidth: 220; horizontalAlignment: Text.AlignHCenter; text: root.serviceConfigured ? "上传 WAV 文件开始识别" : "请先配置服务 URL 和 API Key"; color: root.mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 26
            color: Theme.textWhite
            border.width: 1
            border.color: root.outlineColor
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 22; spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle { Layout.alignment: Qt.AlignVCenter; implicitWidth: 4; implicitHeight: 20; radius: 2; color: root.accentColor }
                    Text { Layout.fillWidth: true; text: "识别状态"; color: root.titleColor; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                    Rectangle {
                        visible: root.recognitionLastRequestId.length > 0
                        radius: 16; color: root.softBlue; border.width: 1; border.color: Theme.primaryBorder; implicitHeight: 34; implicitWidth: requestTag.implicitWidth + 22
                        Text { id: requestTag; anchors.centerIn: parent; text: "request_id: " + root.recognitionLastRequestId; color: Theme.primaryPressed; font.pixelSize: 12; font.bold: true }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.outlineColor }
                ScrollView {
                    id: resultScroll
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    Column {
                        width: resultScroll.availableWidth
                        spacing: 14
                        Rectangle {
                            visible: root.recognitionErrorMessage.length > 0
                            width: parent.width; radius: 18; color: root.softRed; border.width: 1; border.color: Theme.dangerBorder; implicitHeight: errorText.implicitHeight + 28
                            Text { id: errorText; anchors.fill: parent; anchors.margins: 14; text: root.recognitionErrorMessage + (root.recognitionHasResult ? "\n已有可用结果" : ""); color: Theme.dangerText; wrapMode: Text.WordWrap; font.pixelSize: 13 }
                        }
                        Column {
                            visible: root.recognitionHasResult
                            width: parent.width
                            spacing: 14
                            Rectangle {
                                width: parent.width; radius: 20; color: root.softGray; border.width: 1; border.color: root.outlineColor; implicitHeight: summaryCol.implicitHeight + 28
                                Column { id: summaryCol; x: 14; y: 14; width: parent.width - 28; spacing: 12
                                    Text { width: parent.width; text: root.headlineText(); color: root.resultHeadingColor; font.pixelSize: 24; font.bold: true; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
                                    Text { width: parent.width; visible: root.summaryText().length > 0; text: root.summaryText(); color: root.resultBodyColor; font.pixelSize: 14; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
                                    Flow { width: parent.width; spacing: 8
                                        Rectangle { radius: 16; color: root.softBlue; border.width: 1; border.color: Theme.primaryBorder; height: 32; width: Math.max(filePill.implicitWidth + 18, 80); Text { id: filePill; anchors.centerIn: parent; text: "文件: " + root.fileName(root.lastRequestedFilePath); color: Theme.primaryPressed; font.pixelSize: 12; font.bold: true } }
                                        Rectangle { radius: 16; color: root.softBlue; border.width: 1; border.color: Theme.primaryBorder; height: 32; width: Math.max(modelPill.implicitWidth + 18, 80); Text { id: modelPill; anchors.centerIn: parent; text: "模型: " + root.modelEntryForKey(root.lastRequestedModelKey).title; color: Theme.primaryPressed; font.pixelSize: 12; font.bold: true } }
                                    }
                                    GridLayout { width: parent.width; columns: width >= 900 ? 4 : 2; columnSpacing: 10; rowSpacing: 10
                                        StatCard { label: ""; value: String(root.resultObject().final_class_name || "--"); highlight: true }
                                        StatCard { label: "置信度"; value: root.formatPercent(root.resultObject().confidence) }
                                        StatCard { label: "音频时长"; value: root.formatSeconds(root.resultObject().audio_duration_sec) }
                                        StatCard { label: "事件数"; value: root.formatInt(root.resultObject().event_count) }
                                    }
                                }
                            }
                            Rectangle {
                                width: parent.width; radius: 20; color: Theme.textWhite; border.width: 1; border.color: root.outlineColor; implicitHeight: eventsCol.implicitHeight + 28
                                Column { id: eventsCol; x: 14; y: 14; width: parent.width - 28; spacing: 12
                                    Text { text: "事件列表"; color: root.titleColor; font.pixelSize: 18; font.bold: true; font.family: Theme.fontFamily }
                                    Text { visible: root.eventsModel().length === 0; text: "暂无可展示事件"; color: root.mutedColor; font.pixelSize: 13 }
                                    Repeater {
                                        model: root.eventsModel()
                                        delegate: Rectangle {
                                            id: eventCard
                                            required property var modelData
                                            width: eventsCol.width; radius: 16; color: root.resultCardColor; border.width: 1; border.color: Theme.border; implicitHeight: cardCol.implicitHeight + 24
                                            Column { id: cardCol; x: 12; y: 12; width: parent.width - 24; spacing: 8
                                                Text { width: parent.width; text: "#" + root.formatInt(eventCard.modelData.event_index) + "  " + String(eventCard.modelData.class_name || "--") + "  " + root.formatSeconds(eventCard.modelData.start_sec) + " - " + root.formatSeconds(eventCard.modelData.end_sec); color: root.resultHeadingColor; font.pixelSize: 15; font.bold: true; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
                                                Text { text: "时长 " + root.formatSeconds(eventCard.modelData.duration_sec) + "  |  平均置信度 " + root.formatPercent(eventCard.modelData.avg_confidence) + "  |  峰值置信度 " + root.formatPercent(eventCard.modelData.peak_confidence) + "  |  窗口数 " + root.formatInt(eventCard.modelData.window_count); color: root.mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                visible: root.verbose
                                width: parent.width; radius: 20; color: Theme.textWhite; border.width: 1; border.color: root.outlineColor; implicitHeight: debugCol.implicitHeight + 28
                                Column { id: debugCol; x: 14; y: 14; width: parent.width - 28; spacing: 12
                                    Text { text: "详细分析"; color: root.titleColor; font.pixelSize: 18; font.bold: true; font.family: Theme.fontFamily }
                                    GridLayout { width: parent.width; columns: width >= 720 ? 3 : 1; columnSpacing: 10; rowSpacing: 10
                                        StatCard { label: ""; value: root.formatInt(root.windowAnalysisObject().segment_count) }
                                        StatCard { label: ""; value: String((root.windowAnalysisObject().window_majority_class || {}).class_name || "--") }
                                        StatCard { label: "置信度"; value: root.formatPercent((root.windowAnalysisObject().window_majority_class || {}).confidence) }
                                    }
                                    Repeater {
                                        model: root.voteDetailsModel()
                                        delegate: Text { id: voteDetailText; required property var modelData; Layout.fillWidth: true; text: String(voteDetailText.modelData.class_name || "--") + "  |  票数 " + root.formatInt(voteDetailText.modelData.vote_count) + "  |  占比 " + root.formatPercent(voteDetailText.modelData.vote_ratio) + "  |  平均置信度 " + root.formatPercent(voteDetailText.modelData.avg_confidence); color: root.mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                                    }
                                    Repeater {
                                        model: root.segmentsModel()
                                        delegate: Text { id: segmentText; required property var modelData; Layout.fillWidth: true; text: "#" + root.formatInt(segmentText.modelData.index) + "  " + String(segmentText.modelData.class_name || "--") + "  " + root.formatSeconds(segmentText.modelData.start_sec) + " - " + root.formatSeconds(segmentText.modelData.end_sec) + "  |  置信度 " + root.formatPercent(segmentText.modelData.confidence); color: root.titleColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                                    }
                                }
                            }
                        }
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
        width: Math.min(326, root.width - 36)
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onAboutToShow: {
            const point = modelSelectorCard.mapToItem(root, 0, modelSelectorCard.height + 10)
            x = Math.max(18, Math.min(point.x, root.width - width - 18))
            y = point.y
        }

        background: Rectangle {
            radius: 24
            color: Theme.textWhite
            border.width: 1
            border.color: Theme.border
        }

        contentItem: Item {
            implicitHeight: modelMenuColumn.implicitHeight + 28

            Column {
                id: modelMenuColumn
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                Text {
                    width: parent.width
                    text: "最新"
                    color: Theme.textWeak
                    font.pixelSize: 12
                    font.family: Theme.fontFamily
                }

                Repeater {
                    model: root.recognitionModels

                    delegate: Rectangle {
                        id: modelOptionCard
                        required property var modelData
                        readonly property bool selected: modelData.key === root.currentModelKey
                        width: modelMenuColumn.width
                        radius: 18
                        color: modelOptionMouse.containsMouse ? Theme.primaryLighter : "transparent"
                        implicitHeight: 78

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            anchors.topMargin: 10
                            anchors.bottomMargin: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelOptionCard.modelData.title
                                    color: root.titleColor
                                    font.pixelSize: 19
                                    font.family: Theme.fontFamily
                                }

                                Text {
                                    text: modelOptionCard.modelData.description
                                    color: root.mutedColor
                                    font.pixelSize: 12
                                    font.family: Theme.fontFamily
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                visible: modelOptionCard.selected
                                text: "✓"
                                color: root.titleColor
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }

                        MouseArea {
                            id: modelOptionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.applySelectedModel(modelOptionCard.modelData.key)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    radius: 0.5
                    color: Theme.divider
                }

                Rectangle {
                    width: parent.width
                    radius: 16
                    color: configEntryMouse.containsMouse ? Theme.primaryLighter : "transparent"
                    implicitHeight: 54

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        text: "配置..."
                        color: root.titleColor
                        font.pixelSize: 16
                        font.family: Theme.fontFamily
                    }

                    MouseArea {
                        id: configEntryMouse
                        anchors.fill: parent
                        hoverEnabled: true
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
        background: Rectangle { radius: 24; color: Theme.textWhite; border.width: 1; border.color: root.outlineColor }
        contentItem: Item {
            implicitHeight: popupCol.implicitHeight + 44
            ColumnLayout {
                id: popupCol
                anchors.fill: parent; anchors.margins: 22; spacing: 14
                Text { Layout.fillWidth: true; text: "推理服务配置"; color: root.titleColor; font.pixelSize: 26; font.bold: true; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
                TextFields { id: recognitionServiceHostPortField; Layout.fillWidth: true; type: "outlined"; label: "推理服务URL"; placeholderText: ""; supportingText: "示例: " + root.recognitionServiceHostPortExample; selectByMouse: true; labelBackgroundColor: Theme.textWhite; errorText: "" }
                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: root.softGray
                    border.width: 1
                    border.color: root.outlineColor
                    implicitHeight: servicePreviewColumn.implicitHeight + 24

                    Column {
                        id: servicePreviewColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Text {
                            width: parent.width
                            text: "当前模型: " + root.currentModelText
                            color: root.titleColor
                            font.pixelSize: 14
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: root.isValidHostPort(recognitionServiceHostPortField.text)
                                ? ("接口预览: " + root.buildRecognitionServiceUrl(
                                    recognitionServiceHostPortField.text,
                                    root.currentModelKey))
                                : "接口预览: 请输入有效的服务地址"
                            color: root.mutedColor
                            font.pixelSize: 13
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                TextFields { id: recognitionApiKeyField; Layout.fillWidth: true; type: "outlined"; label: "API Key"; placeholderText: ""; selectByMouse: true; labelBackgroundColor: Theme.textWhite; errorText: "" }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button { text: "取消"; onClicked: configPopup.close() }
                    Button {
                        text: "保存配置"
                        enabled: root.isValidHostPort(recognitionServiceHostPortField.text) && recognitionApiKeyField.text.trim().length > 0
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

    Item {
        anchors.fill: parent
        visible: root.recognitionBusy
        z: 1001

        Rectangle {
            anchors.fill: parent
            color: Theme.overlayDim
        }

        Rectangle {
            width: Math.min(root.width - 48, 380)
            anchors.centerIn: parent
            radius: 28
            color: Theme.cardBg
            border.width: 1
            border.color: Theme.border
            implicitHeight: recognitionBusyColumn.implicitHeight + 40

            Column {
                id: recognitionBusyColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                StandbyAnimation {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 78
                    height: 78
                    blockColor: root.accentColor
                    running: root.recognitionBusy
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.recognitionStatusMessage.length > 0 ? root.recognitionStatusMessage : "识别中..."
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }

                ProgressBar {
                    width: parent.width
                    indeterminate: root.recognitionTotalUploadBytes <= 0
                    from: 0
                    to: 1
                    value: root.recognitionTotalUploadBytes > 0 ? root.recognitionUploadProgress : 0
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.uploadProgressLabel()
                    color: Theme.textMuted
                    font.pixelSize: 13
                }

                Row {
                    width: parent.width
                    spacing: 12
                    layoutDirection: Qt.RightToLeft

                    Button {
                        id: cancelRecognitionButton
                        width: 132
                        height: 44
                        text: "取消"
                        onClicked: {
                            if (recognitionServiceManager) {
                                recognitionServiceManager.cancelRecognition()
                            }
                        }

                        contentItem: Text {
                            text: cancelRecognitionButton.text
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 22
                            color: Theme.primaryLighter
                            border.width: 1
                            border.color: Theme.border
                        }
                    }
                }
            }
        }
    }
}
