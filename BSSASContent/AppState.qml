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
        property int importAdaptiveNoiseReductionLevel: 1
        property bool importAdaptiveNoiseReductionHighPassFilterEnabled: false
        property bool importAdaptiveNoiseReductionAutomaticGainControlEnabled: false
        property bool importAdaptiveNoiseReductionTransientSuppressionEnabled: false
        property bool importScientificFilterEnabled: false
        property int importScientificFilterPrototype: 1
        property int importScientificFilterType: 0
        property int importScientificFilterOrder: 2
        property real importScientificFilterCutoffFrequencyHz: 500
        property real importScientificFilterLowCutoffFrequencyHz: 80
        property real importScientificFilterHighCutoffFrequencyHz: 600
        property real importScientificFilterTransitionBandwidthHz: 50
        property real importScientificFilterStopbandAttenuationDb: 60
        property real importScientificFilterPassbandRippleDb: 0.5
        property int realtimeAdaptiveNoiseReductionLevel: 1
        property bool realtimeAdaptiveNoiseReductionHighPassFilterEnabled: false
        property bool realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled: false
        property bool realtimeAdaptiveNoiseReductionTransientSuppressionEnabled: false
        property bool realtimeScientificFilterEnabled: false
        property int realtimeScientificFilterPrototype: 1
        property int realtimeScientificFilterType: 0
        property int realtimeScientificFilterOrder: 2
        property real realtimeScientificFilterCutoffFrequencyHz: 500
        property real realtimeScientificFilterLowCutoffFrequencyHz: 80
        property real realtimeScientificFilterHighCutoffFrequencyHz: 600
        property real realtimeScientificFilterTransitionBandwidthHz: 50
        property real realtimeScientificFilterStopbandAttenuationDb: 60
        property real realtimeScientificFilterPassbandRippleDb: 0.5
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
    property alias importAdaptiveNoiseReductionLevel: signalProcessingSettings.importAdaptiveNoiseReductionLevel
    property alias importAdaptiveNoiseReductionHighPassFilterEnabled: signalProcessingSettings.importAdaptiveNoiseReductionHighPassFilterEnabled
    property alias importAdaptiveNoiseReductionAutomaticGainControlEnabled: signalProcessingSettings.importAdaptiveNoiseReductionAutomaticGainControlEnabled
    property alias importAdaptiveNoiseReductionTransientSuppressionEnabled: signalProcessingSettings.importAdaptiveNoiseReductionTransientSuppressionEnabled
    property alias importScientificFilterEnabled: signalProcessingSettings.importScientificFilterEnabled
    property alias importScientificFilterPrototype: signalProcessingSettings.importScientificFilterPrototype
    property alias importScientificFilterType: signalProcessingSettings.importScientificFilterType
    property alias importScientificFilterOrder: signalProcessingSettings.importScientificFilterOrder
    property alias importScientificFilterCutoffFrequencyHz: signalProcessingSettings.importScientificFilterCutoffFrequencyHz
    property alias importScientificFilterLowCutoffFrequencyHz: signalProcessingSettings.importScientificFilterLowCutoffFrequencyHz
    property alias importScientificFilterHighCutoffFrequencyHz: signalProcessingSettings.importScientificFilterHighCutoffFrequencyHz
    property alias importScientificFilterTransitionBandwidthHz: signalProcessingSettings.importScientificFilterTransitionBandwidthHz
    property alias importScientificFilterStopbandAttenuationDb: signalProcessingSettings.importScientificFilterStopbandAttenuationDb
    property alias importScientificFilterPassbandRippleDb: signalProcessingSettings.importScientificFilterPassbandRippleDb
    property alias realtimeAdaptiveNoiseReductionLevel: signalProcessingSettings.realtimeAdaptiveNoiseReductionLevel
    property alias realtimeAdaptiveNoiseReductionHighPassFilterEnabled: signalProcessingSettings.realtimeAdaptiveNoiseReductionHighPassFilterEnabled
    property alias realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled: signalProcessingSettings.realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled
    property alias realtimeAdaptiveNoiseReductionTransientSuppressionEnabled: signalProcessingSettings.realtimeAdaptiveNoiseReductionTransientSuppressionEnabled
    property alias realtimeScientificFilterEnabled: signalProcessingSettings.realtimeScientificFilterEnabled
    property alias realtimeScientificFilterPrototype: signalProcessingSettings.realtimeScientificFilterPrototype
    property alias realtimeScientificFilterType: signalProcessingSettings.realtimeScientificFilterType
    property alias realtimeScientificFilterOrder: signalProcessingSettings.realtimeScientificFilterOrder
    property alias realtimeScientificFilterCutoffFrequencyHz: signalProcessingSettings.realtimeScientificFilterCutoffFrequencyHz
    property alias realtimeScientificFilterLowCutoffFrequencyHz: signalProcessingSettings.realtimeScientificFilterLowCutoffFrequencyHz
    property alias realtimeScientificFilterHighCutoffFrequencyHz: signalProcessingSettings.realtimeScientificFilterHighCutoffFrequencyHz
    property alias realtimeScientificFilterTransitionBandwidthHz: signalProcessingSettings.realtimeScientificFilterTransitionBandwidthHz
    property alias realtimeScientificFilterStopbandAttenuationDb: signalProcessingSettings.realtimeScientificFilterStopbandAttenuationDb
    property alias realtimeScientificFilterPassbandRippleDb: signalProcessingSettings.realtimeScientificFilterPassbandRippleDb
    property alias recognitionServiceHostPort: recognitionServiceSettings.recognitionServiceHostPort
    property alias recognitionApiKey: recognitionServiceSettings.recognitionApiKey
}
