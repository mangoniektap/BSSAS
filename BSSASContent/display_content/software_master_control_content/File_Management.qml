pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MangoComponent
import BSSAS

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
    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function extractFileName(filePath) {
        if (!filePath || filePath.length === 0)
            return ""

        const normalizedPath = filePath.replace(/\\/g, "/")
        const pathSegments = normalizedPath.split("/")
        return pathSegments.length > 0 ? pathSegments[pathSegments.length - 1] : filePath
    }

    function toLocalFilePath(fileUrl) {
        const raw = fileUrl ? fileUrl.toString() : ""
        return raw.length > 0 ? decodeURIComponent(raw).replace(/^file:\/\/\//, "") : ""
    }

    function finishImportWorkflow() {
        root.pendingImportedAnalysis = false
        root.importStatusText = ""
    }

    function openAnalysisReport() {
        if (analysisReportLoader.item) {
            root.pendingReportOpen = false
            analysisReportLoader.item["open"]()
            return
        }

        root.pendingReportOpen = true
        analysisReportLoader.active = true
    }

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

    function setAllImportProcessing(enabledValue) {
        signalPreprocessing.setAllImportProcessingEnabled(!!enabledValue)
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
        leftPadding: 30
        topPadding: 30
        rightPadding: 30
        bottomPadding: 30

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
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
                        Layout.preferredHeight: 38
                        spacing: 10

                        Text {
                            text: qsTr("\u6ee4\u6ce2\u5668\u7c7b\u578b")
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
                width: root.pageRoot ? (root.pageRoot.width * 3) / 4 : 0
                height: root.pageRoot ? root.pageRoot.height : 0
                x: root.pageRoot ? root.pageRoot.x : 0
                y: root.pageRoot ? root.pageRoot.y - 200 : -200
            }
        }

        onLoaded: {
            if (!root.pendingReportOpen || !item)
                return

            root.pendingReportOpen = false
            item["open"]()
        }
    }

    Item {
        anchors.fill: parent
        visible: root.pendingImportedAnalysis || root.pendingRealtimeSave
        z: 1000

        Rectangle {
            anchors.fill: parent
            color: root.colorWithAlpha(Theme.cardBg, 0.84)
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Rectangle {
            width: 240
            height: 220
            anchors.centerIn: parent
            radius: 24
            color: root.colorWithAlpha(Theme.primaryLighter, 0.96)
            border.width: 1
            border.color: root.colorWithAlpha(Theme.primaryBorder, 0.45)

            Column {
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
}
