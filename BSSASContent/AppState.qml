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

    property Settings signalProcessingSettings: Settings {
        id: signalProcessingSettings
        category: "SignalProcessing"

        property int noiseReductionMode: 0
        property real gain: 100
        property int analysisTimeLength: 0
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
    property alias recognitionServiceHostPort: recognitionServiceSettings.recognitionServiceHostPort
    property alias recognitionApiKey: recognitionServiceSettings.recognitionApiKey
}
