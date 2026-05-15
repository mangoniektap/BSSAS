/**
 * @file AppState.qml
 * @brief 全局应用状态单例。集中管理信号处理与识别服务的持久化配置。
 */
pragma Singleton
import QtQuick
import Qt.labs.settings
import QtCore

QtObject {
    id: root

    readonly property real defaultAnalysisTimeLength: 0.2
    readonly property var analysisTimeLengthOptions: [0.05, 0.1, 0.15, 0.2, 0.3, 0.5, 1, 5, 10]
    readonly property var analysisTimeLengthLabels: [
        "0.05秒",
        "0.1秒",
        "0.15秒",
        "0.2秒",
        "0.3秒",
        "0.5秒",
        "1秒",
        "5秒",
        "10秒"
    ]

    function analysisTimeLengthIndex(value) {
        const numericValue = Number(value)
        if (!isFinite(numericValue))
            return 3

        for (let index = 0; index < root.analysisTimeLengthOptions.length; ++index) {
            if (Math.abs(root.analysisTimeLengthOptions[index] - numericValue) < 0.0001)
                return index
        }

        return 3
    }

    function analysisTimeLengthValue(index) {
        if (index >= 0 && index < root.analysisTimeLengthOptions.length)
            return root.analysisTimeLengthOptions[index]

        return root.defaultAnalysisTimeLength
    }

    function normalizedAnalysisTimeLength(value) {
        return root.analysisTimeLengthValue(root.analysisTimeLengthIndex(value))
    }

    property Settings signalProcessingSettings: Settings {
        id: signalProcessingSettings
        category: "SignalProcessing"

        property int noiseReductionMode: 0
        property real gain: 100
        property real analysisTimeLength: 0.2
        property real importAnalysisTimeLength: 0.2
    }

    property Settings recognitionServiceSettings: Settings {
        id: recognitionServiceSettings
        category: "RecognitionService"

        property string recognitionServiceHostPort: ""
        property string recognitionApiKey: ""
    }

    property alias noiseReductionMode: signalProcessingSettings.noiseReductionMode
    property alias gain: signalProcessingSettings.gain
    property alias analysisTimeLength: signalProcessingSettings.analysisTimeLength
    property alias importAnalysisTimeLength: signalProcessingSettings.importAnalysisTimeLength
    property alias recognitionServiceHostPort: recognitionServiceSettings.recognitionServiceHostPort
    property alias recognitionApiKey: recognitionServiceSettings.recognitionApiKey
}
