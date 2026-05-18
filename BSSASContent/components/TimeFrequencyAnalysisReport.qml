/**
 * @file TimeFrequencyAnalysisReport.qml
 * @brief 时频分析报告弹窗。展示时域波形图、频谱图及导入分析统计信息，支持报告导出、数据保存和悬停提示。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import MangoComponent
import BSSAS

Popup {
    id: root

    readonly property real toolbarAreaWidth: 56
    readonly property real toolbarGap: 12
    readonly property var importedAnalysisSummary: dataManager.importedAnalysisSummary

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    opacity: 0
    scale: 0.9

    /**
     * @brief 将颜色值与指定透明度混合
     * @param sourceColor 源颜色值
     * @param alphaValue 透明度（0.0~1.0）
     * @returns 混合后的RGBA颜色值
     */
    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    /**
     * @brief 从文件路径中提取文件名
     * @param filePath 完整文件路径
     * @returns 提取的文件名，空字符串表示路径无效
     */
    function extractFileName(filePath) {
        if (!filePath || filePath.length === 0) {
            return ""
        }

        const normalizedPath = filePath.replace(/\\/g, "/")
        const pathSegments = normalizedPath.split("/")
        return pathSegments.length > 0 ? pathSegments[pathSegments.length - 1] : filePath
    }

    /**
     * @brief 安全解析数值，无效时返回回退值
     * @param value 待解析的值
     * @param fallbackValue 解析失败时的回退值
     * @returns 有效的数值或回退值
     */
    function numericValue(value, fallbackValue) {
        const parsedValue = Number(value)
        return isFinite(parsedValue) ? parsedValue : fallbackValue
    }

    /**
     * @brief 将秒数格式化为可读时长字符串
     * @param secondsValue 秒数值
     * @returns 格式化的时长字符串，如"2分30.00秒"，无效时返回"未记录"
     */
    function formatDurationSeconds(secondsValue) {
        const seconds = root.numericValue(secondsValue, -1)
        if (seconds < 0) {
            return "未记录"
        }

        if (seconds >= 60) {
            const minutes = Math.floor(seconds / 60)
            const remainingSeconds = seconds - minutes * 60
            return minutes + " 分 " + remainingSeconds.toFixed(2) + " 秒"
        }

        return seconds >= 10 ? seconds.toFixed(2) + " 秒" : seconds.toFixed(3) + " 秒"
    }

    /**
     * @brief 将毫秒数格式化为可读时间字符串
     * @param millisecondsValue 毫秒数值
     * @returns 格式化的时间字符串，如"1.50 s"或"150 ms"，无效时返回"未记录"
     */
    function formatMilliseconds(millisecondsValue) {
        const milliseconds = root.numericValue(millisecondsValue, -1)
        if (milliseconds < 0) {
            return "未记录"
        }

        if (milliseconds >= 1000) {
            return (milliseconds / 1000).toFixed(2) + " s"
        }

        return milliseconds >= 100 ? milliseconds.toFixed(0) + " ms" : milliseconds.toFixed(1) + " ms"
    }

    /**
     * @brief 计算导入分析的总耗时，优先使用显式totalMs，否则累加各步骤耗时
     * @param summaryObject 导入分析摘要对象
     * @returns 总耗时（毫秒），无有效数据时返回-1
     */
    function importedAnalysisTotalMs(summaryObject) {
        const summary = summaryObject || {}
        const timings = summary.timings || {}
        const explicitTotal = root.numericValue(timings.totalMs, -1)
        if (explicitTotal >= 0) {
            return explicitTotal
        }

        const timingKeys = [
            "importReadMs",
            "bandpassMs",
            "notchMs",
            "adaptiveNoiseReductionMs",
            "waveletDenoisingMs",
            "transientNoiseSuppressionMs",
            "motionArtifactReductionMs",
            "downsamplingMs",
            "dftMs",
            "multiFeatureAnalysisMs"
        ]

        let total = 0
        let hasValue = false
        for (let index = 0; index < timingKeys.length; ++index) {
            const value = root.numericValue(timings[timingKeys[index]], -1)
            if (value >= 0) {
                total += value
                hasValue = true
            }
        }

        return hasValue ? total : -1
    }

    /**
     * @brief 构建导入分析摘要文本，包含音频参数和各处理步骤耗时
     * @returns 多行格式化的分析统计文本
     */
    function importedAnalysisSummaryText() {
        const summary = root.importedAnalysisSummary || {}
        const sampleCount = root.numericValue(summary.sampleCount, 0)
        const timings = summary.timings || {}
        const processing = summary.processing || {}

        if (!summary.sourceFileName && sampleCount <= 0 && Object.keys(timings).length === 0) {
            return "暂无最近一次导入分析统计。"
        }

        const lines = []
        lines.push("音频参数")
        lines.push("文件名：" + (summary.sourceFileName || "未记录"))

        const sampleRate = root.numericValue(summary.sampleRate, -1)
        if (sampleRate > 0) {
            lines.push("采样率：" + sampleRate.toFixed(0) + " Hz")
        }
        if (sampleCount > 0) {
            lines.push("样本数：" + sampleCount.toFixed(0))
        }

        const durationSeconds = root.numericValue(summary.durationSeconds, -1)
        if (durationSeconds >= 0) {
            lines.push("时长：" + root.formatDurationSeconds(durationSeconds))
        }

        const minAmplitude = root.numericValue(summary.minAmplitude, NaN)
        const maxAmplitude = root.numericValue(summary.maxAmplitude, NaN)
        if (isFinite(minAmplitude) && isFinite(maxAmplitude)) {
            lines.push("幅值范围：" + minAmplitude.toFixed(2) + " ~ " + maxAmplitude.toFixed(2) + " V")
        }

        const peakAmplitude = root.numericValue(summary.peakAmplitude, -1)
        if (peakAmplitude >= 0) {
            lines.push("峰值幅度：" + peakAmplitude.toFixed(2) + " V")
        }

        lines.push("")
        lines.push("耗时")

        const bandpassFilterType = (processing.bandpassFilterType || processing.filterType || "").toString().trim()
        if (bandpassFilterType.length > 0) {
            lines.push("带通滤波器类型：" + bandpassFilterType)
        }
        const bandpassFirOrder = root.numericValue(processing.bandpassFirOrder, -1)
        if (bandpassFilterType === "FIR" && bandpassFirOrder > 0) {
            lines.push("带通 FIR 阶数：" + bandpassFirOrder.toFixed(0))
        }
        const notchFrequencyMode = (processing.notchFrequencyMode || "").toString().trim()
        if (notchFrequencyMode.length > 0) {
            lines.push("陷波器配置：" + (notchFrequencyMode === "adaptive" ? "自适应" : "固定频率"))
        }

        const importReadMs = root.numericValue(timings.importReadMs, -1)
        if (importReadMs >= 0) {
            lines.push("导入解码：" + root.formatMilliseconds(importReadMs))
        }

        lines.push("带通滤波：" + (processing.bandpassEnabled === false
            ? "未启用"
            : root.formatMilliseconds(timings.bandpassMs)))
        lines.push("陷波滤波：" + (processing.notchEnabled === false
            ? "未启用"
            : root.formatMilliseconds(timings.notchMs)))
        lines.push("自适应降噪：" + (processing.adaptiveNoiseReductionEnabled === false
            ? "未启用"
            : root.formatMilliseconds(timings.adaptiveNoiseReductionMs)))
        lines.push("小波降噪：" + (processing.waveletDenoisingEnabled === false
            ? "未启用"
            : root.formatMilliseconds(timings.waveletDenoisingMs)))

        lines.push("\u77ac\u6001\u566a\u58f0\u6291\u5236\uff1a" + (processing.transientNoiseSuppressionEnabled === false
            ? "\u672a\u542f\u7528"
            : root.formatMilliseconds(timings.transientNoiseSuppressionMs)))

        lines.push("\u540c\u9891\u6bb5\u8fd0\u52a8\u4f2a\u5f71\u6d88\u9664\uff1a" + (processing.motionArtifactReductionEnabled === false
            ? "鏈惎鐢?"
            : root.formatMilliseconds(timings.motionArtifactReductionMs)))
        if (processing.motionArtifactReductionEnabled !== false
                && (processing.motionArtifactReductionMethod || "").length > 0) {
            lines.push("\u5904\u7406\u65b9\u6cd5\uff1a" + processing.motionArtifactReductionMethod)
        }

        const downsamplingMs = root.numericValue(timings.downsamplingMs, -1)
        if (downsamplingMs >= 0) {
            lines.push("下采样：" + root.formatMilliseconds(downsamplingMs))
        }

        const dftMs = root.numericValue(timings.dftMs, -1)
        if (dftMs >= 0) {
            lines.push("STFT/DFT：" + root.formatMilliseconds(dftMs))
        }

        const featureMs = root.numericValue(timings.multiFeatureAnalysisMs, -1)
        if (featureMs >= 0) {
            lines.push("多特征联合检测：" + root.formatMilliseconds(featureMs))
        }

        const totalMs = root.importedAnalysisTotalMs(summary)
        if (totalMs >= 0) {
            lines.push("总耗时：" + root.formatMilliseconds(totalMs))
        }

        return lines.join("\n")
    }

    onClosed: reportStatsTip.close()

    Overlay.modal: BlurGlass {
        blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
        blurAmount: 64
        borderRadius: 25
        overlayOpacity: 0.3
    }

    background: Item {}

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 0
                to: 1
                duration: 400
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: root
                property: "scale"
                from: 0.9
                to: 1
                duration: 400
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: root
                property: "y"
                from: root.y + 50
                to: root.y
                duration: 400
                easing.type: Easing.OutCubic
            }
        }
    }

    Connections {
        target: generateManager

        function onExportCompleted(pdfFilePath) {
            console.log("report exported:", pdfFilePath)
            const fileName = root.extractFileName(pdfFilePath)
            reportToast.showSuccess(fileName.length > 0
                ? "分析报告已导出：" + fileName
                : "分析报告已导出")
        }

        function onExportFailed(errorMessage) {
            console.log("report export failed:", errorMessage)
            reportToast.showError(errorMessage.length > 0
                ? "分析报告导出失败：" + errorMessage
                : "分析报告导出失败")
        }
    }

    Connections {
        target: wavHandle

        function onImportedSaveCompleted(outputPath) {
            console.log("imported wav saved:", outputPath)
            const fileName = root.extractFileName(outputPath)
            reportToast.showSuccess(fileName.length > 0
                ? "导入数据已保存：" + fileName
                : "导入数据已保存")
        }

        function onImportedSaveFailed(errorMessage) {
            console.log("imported wav save failed:", errorMessage)
            reportToast.showError(errorMessage.length > 0
                ? "导入数据保存失败：" + errorMessage
                : "导入数据保存失败")
        }
    }

    ToastNotification {
        id: reportToast
        duration: 3000
    }

    Rectangle {
        id: reportCard
        anchors {
            top: parent.top
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            rightMargin: root.toolbarAreaWidth + root.toolbarGap
        }
        color: Theme.cardBg
        radius: 10
        border.width: 1
        border.color: Theme.border
    }

    OSDataVisualizationGraphCartesianWaveform {
        id: timeWaveform
        rawDataResolutionThreshold: adaptiveDownsampling.rawDataResolutionThreshold
        anchors {
            top: reportCard.top
            topMargin: 20
            left: reportCard.left
            right: reportCard.right
        }
        height: 500
    }

    DataVisualizationGraphCartesianSpectrum {
        id: frequencySpectrum
        useImportedData: true
        linkedTimeCenterSeconds: (timeWaveform.displayMin + timeWaveform.displayMax) / 2
        anchors {
            top: timeWaveform.bottom
            left: reportCard.left
            right: reportCard.right
        }
        height: 500
    }

    Item {
        id: toolbarArea
        anchors {
            top: reportCard.top
            bottom: reportCard.bottom
            right: parent.right
        }
        width: root.toolbarAreaWidth

        Item {
            id: exportting
            anchors {
                top: parent.top
                topMargin: 28
                horizontalCenter: parent.horizontalCenter
            }
            width: 30
            height: 30
            opacity: generateManager.busy ? 0.45 : 1

            HoverHandler {
                id: exportHover
                target: exportting
                enabled: !generateManager.busy
            }

            Image {
                id: exportIMAGE
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: "qrc:/qt/qml/BSSASContent/images/export.png"
                visible: false
                smooth: true
                mipmap: true
            }

            MultiEffect {
                anchors.fill: parent
                source: exportIMAGE
                transformOrigin: Item.Center

                colorization: exportHover.hovered ? 0.45 : 0.0
                colorizationColor: Theme.textWhite
                brightness: exportHover.hovered ? 0.1 : 0.0
                contrast: exportHover.hovered ? 1.1 : 1.0

                Behavior on colorization { NumberAnimation { duration: 150 } }
                Behavior on brightness { NumberAnimation { duration: 150 } }
                Behavior on contrast { NumberAnimation { duration: 150 } }

                scale: exportHover.hovered ? 1.2 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }
            }

            TapHandler {
                enabled: !generateManager.busy
                onTapped: generateManager.startExportIdentificationAndFeatureExtractionReport()
            }
        }

        Item {
            id: saveButton
            anchors {
                top: exportting.bottom
                topMargin: 26
                horizontalCenter: parent.horizontalCenter
            }
            width: 30
            height: 30
            opacity: generateManager.busy ? 0.45 : 1

            HoverHandler {
                id: saveHover
                target: saveButton
                enabled: !generateManager.busy
            }

            Image {
                id: saveImage
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: "qrc:/qt/qml/BSSASContent/images/save.png"
                visible: false
                smooth: true
                mipmap: true
            }

            MultiEffect {
                anchors.fill: parent
                source: saveImage
                transformOrigin: Item.Center

                colorization: saveHover.hovered ? 0.45 : 0.0
                colorizationColor: Theme.textWhite
                brightness: saveHover.hovered ? 0.1 : 0.0
                contrast: saveHover.hovered ? 1.1 : 1.0

                Behavior on colorization { NumberAnimation { duration: 150 } }
                Behavior on brightness { NumberAnimation { duration: 150 } }
                Behavior on contrast { NumberAnimation { duration: 150 } }

                scale: saveHover.hovered ? 1.2 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }
            }

            TapHandler {
                enabled: !generateManager.busy
                onTapped: wavHandle.startSaveImportedAsWav()
            }
        }

        Item {
            id: reportStatsButton
            anchors {
                top: saveButton.bottom
                topMargin: 26
                horizontalCenter: parent.horizontalCenter
            }
            width: 30
            height: 30
            opacity: generateManager.busy ? 0.45 : 1

            MouseArea {
                id: reportStatsHover
                anchors.fill: parent
                enabled: !generateManager.busy
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                cursorShape: Qt.PointingHandCursor

                onContainsMouseChanged: {
                    if (containsMouse) {
                        reportStatsTipTimer.restart()
                    } else {
                        reportStatsTipTimer.stop()
                        reportStatsTip.close()
                    }
                }
            }

            Image {
                id: reportStatsImage
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: "qrc:/qt/qml/BSSASContent/images/details.png"
                visible: false
                smooth: true
                mipmap: true
            }

            MultiEffect {
                anchors.fill: parent
                source: reportStatsImage
                transformOrigin: Item.Center

                colorization: reportStatsHover.containsMouse ? 0.45 : 0.0
                colorizationColor: Theme.textWhite
                brightness: reportStatsHover.containsMouse ? 0.1 : 0.0
                contrast: reportStatsHover.containsMouse ? 1.1 : 1.0

                Behavior on colorization { NumberAnimation { duration: 150 } }
                Behavior on brightness { NumberAnimation { duration: 150 } }
                Behavior on contrast { NumberAnimation { duration: 150 } }

                scale: reportStatsHover.containsMouse ? 1.2 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Timer {
                id: reportStatsTipTimer
                interval: 60
                onTriggered: {
                    const popupWidth = reportStatsText.width
                    const popupHeight = reportStatsText.implicitHeight
                    const iconPosition = reportStatsButton.mapToItem(root.contentItem, 0, 0)
                    reportStatsTip.x = iconPosition.x + reportStatsButton.width + 16
                    reportStatsTip.y = Math.max(20, Math.min(root.height - popupHeight - 20,
                        iconPosition.y - popupHeight / 2 + reportStatsButton.height / 2))
                    reportStatsTip.open()
                }
            }

            Popup {
                id: reportStatsTip
                parent: root.contentItem
                padding: 0
                modal: false
                focus: false
                closePolicy: Popup.NoAutoClose

                enter: Transition {
                    ParallelAnimation {
                        NumberAnimation {
                            target: reportStatsTip
                            property: "opacity"
                            from: 0
                            to: 1
                            duration: 120
                            easing.type: Easing.OutQuad
                        }

                        NumberAnimation {
                            target: reportStatsTip
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
                            target: reportStatsTip
                            property: "opacity"
                            from: 1
                            to: 0
                            duration: 100
                            easing.type: Easing.InQuad
                        }

                        NumberAnimation {
                            target: reportStatsTip
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
                    id: reportStatsText
                    width: Math.min(320, root.width * 0.42)
                    padding: 12
                    text: root.importedAnalysisSummaryText()
                    color: Theme.textWhite
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            onVisibleChanged: {
                if (!visible) {
                    reportStatsTipTimer.stop()
                    reportStatsTip.close()
                }
            }

        }
    }

    Item {
        anchors.fill: parent
        visible: generateManager.busy
        z: 100

        Rectangle {
            anchors.fill: parent
            color: root.colorWithAlpha(Theme.cardBg, 0.82)
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
                    running: generateManager.busy
                }

                Text {
                    width: 180
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "正在生成报告，请稍候..."
                    color: Theme.textPrimary
                    font.pixelSize: 16
                }
            }
        }

        MouseArea {
            anchors.fill: parent
        }
    }
}
