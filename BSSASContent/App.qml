/**
 * @file App.qml
 * @brief 主窗口入口。管理无边框窗口、全局拖拽、设置侧栏以及子控件的布局与信号分发。
 */
import QtQuick
import QtQuick.Controls
import BSSAS
import BSSASSettingsStorage
import MangoComponent

Window {
    id: rootwindow
    property alias mainWindowBackground: mainwindow_background
    property bool settingsSidebarOpen: false
    width: Constants.width
    height: Constants.height
    x: Constants.x
    y: Constants.y

    color: "transparent"
    title: "肠鸣音信号分析系统医学辅助诊断工具"
    flags: Qt.FramelessWindowHint | Qt.Window | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint
    visible: true

    MainWindow_Background {
        id: mainwindow_background
    }

    ToastNotification {
        id: appToast
        duration: 3200
        topMargin: 24
    }

    UpdateDialog {
        id: updateDialog
        z: 60
    }

    Connections {
        target: updateManager

        function onPromptUpdateDialog() {
            updateDialog.open()
        }

        function onToastRequested(message, error) {
            if (error) {
                appToast.showError(message)
            } else {
                appToast.showSuccess(message)
            }
        }
    }

    Binding {
        target: signalDFTCalculation
        property: "realtimeAnalysisWindowSeconds"
        value: AppState.normalizedAnalysisTimeLength(AppState.analysisTimeLength)
    }

    Binding {
        target: signalDFTCalculation
        property: "importAnalysisWindowSeconds"
        value: AppState.normalizedAnalysisTimeLength(AppState.importAnalysisTimeLength)
    }

    Binding {
        target: signalPreprocessing
        property: "importAdaptiveNoiseReductionLevel"
        value: AppState.importAdaptiveNoiseReductionLevel
    }

    Binding {
        target: signalPreprocessing
        property: "importAdaptiveNoiseReductionHighPassFilterEnabled"
        value: AppState.importAdaptiveNoiseReductionHighPassFilterEnabled
    }

    Binding {
        target: signalPreprocessing
        property: "importAdaptiveNoiseReductionAutomaticGainControlEnabled"
        value: AppState.importAdaptiveNoiseReductionAutomaticGainControlEnabled
    }

    Binding {
        target: signalPreprocessing
        property: "importAdaptiveNoiseReductionTransientSuppressionEnabled"
        value: AppState.importAdaptiveNoiseReductionTransientSuppressionEnabled
    }

    Binding { target: signalPreprocessing; property: "importScientificFilterEnabled"; value: AppState.importScientificFilterEnabled }
    Binding { target: signalPreprocessing; property: "importScientificFilterPrototype"; value: AppState.importScientificFilterPrototype }
    Binding { target: signalPreprocessing; property: "importScientificFilterType"; value: AppState.importScientificFilterType }
    Binding { target: signalPreprocessing; property: "importScientificFilterOrder"; value: AppState.importScientificFilterOrder }
    Binding { target: signalPreprocessing; property: "importScientificFilterCutoffFrequencyHz"; value: AppState.importScientificFilterCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "importScientificFilterLowCutoffFrequencyHz"; value: AppState.importScientificFilterLowCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "importScientificFilterHighCutoffFrequencyHz"; value: AppState.importScientificFilterHighCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "importScientificFilterTransitionBandwidthHz"; value: AppState.importScientificFilterTransitionBandwidthHz }
    Binding { target: signalPreprocessing; property: "importScientificFilterStopbandAttenuationDb"; value: AppState.importScientificFilterStopbandAttenuationDb }
    Binding { target: signalPreprocessing; property: "importScientificFilterPassbandRippleDb"; value: AppState.importScientificFilterPassbandRippleDb }

    Binding {
        target: signalPreprocessing
        property: "realtimeAdaptiveNoiseReductionLevel"
        value: AppState.realtimeAdaptiveNoiseReductionLevel
    }

    Binding {
        target: signalPreprocessing
        property: "realtimeAdaptiveNoiseReductionHighPassFilterEnabled"
        value: AppState.realtimeAdaptiveNoiseReductionHighPassFilterEnabled
    }

    Binding {
        target: signalPreprocessing
        property: "realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled"
        value: AppState.realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled
    }

    Binding {
        target: signalPreprocessing
        property: "realtimeAdaptiveNoiseReductionTransientSuppressionEnabled"
        value: AppState.realtimeAdaptiveNoiseReductionTransientSuppressionEnabled
    }

    Binding { target: signalPreprocessing; property: "realtimeScientificFilterEnabled"; value: AppState.realtimeScientificFilterEnabled }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterPrototype"; value: AppState.realtimeScientificFilterPrototype }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterType"; value: AppState.realtimeScientificFilterType }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterOrder"; value: AppState.realtimeScientificFilterOrder }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterCutoffFrequencyHz"; value: AppState.realtimeScientificFilterCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterLowCutoffFrequencyHz"; value: AppState.realtimeScientificFilterLowCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterHighCutoffFrequencyHz"; value: AppState.realtimeScientificFilterHighCutoffFrequencyHz }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterTransitionBandwidthHz"; value: AppState.realtimeScientificFilterTransitionBandwidthHz }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterStopbandAttenuationDb"; value: AppState.realtimeScientificFilterStopbandAttenuationDb }
    Binding { target: signalPreprocessing; property: "realtimeScientificFilterPassbandRippleDb"; value: AppState.realtimeScientificFilterPassbandRippleDb }

    Component.onCompleted: Qt.callLater(function() {
        updateManager.checkForUpdatesOnStartup()
    })

    Item {
        x: 0
        y: 0
        width: parent.width
        height: mainwindow_background.display_area.y

        DragHandler {
            id: window_drag
            acceptedButtons: Qt.LeftButton
            grabPermissions: PointerHandler.TakeOverForbidden | PointerHandler.ApprovesTakeOverByAnything

            onActiveChanged: {
                if (active) {
                    if (rootwindow.visibility === Window.FullScreen) {
                        rootwindow.showNormal()
                        dragExitTimer.start()
                    } else {
                        rootwindow.startSystemMove()
                    }
                }
            }
        }

        Timer {
            id: dragExitTimer
            interval: 16
            running: false
            repeat: false

            onTriggered: {
                if (rootwindow.visibility !== Window.Minimized) {
                    rootwindow.startSystemMove()
                }
            }
        }
    }

    Left_Toolbar {
        id: left_toolbar
    }

    Window_Display_Master_Control {
    }

    SettingsSidebar {
        id: settings_sidebar
        z: 20
        opened: rootwindow.settingsSidebarOpen
        onCloseRequested: rootwindow.settingsSidebarOpen = false
    }

    Right_Top_Toolbar {
        id: right_top_toolbar
        z: 30
        opacity: rootwindow.settingsSidebarOpen ? 0.0 : 1.0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
        onSettingClicked: rootwindow.settingsSidebarOpen = !rootwindow.settingsSidebarOpen
        onMinimizeClicked: rootwindow.showMinimized()
        onFullscreenClicked: {
            if (rootwindow.visibility === Window.FullScreen) {
                rootwindow.showNormal()
            } else {
                rootwindow.showFullScreen()
            }
        }
        onCloseClicked: rootwindow.close()
    }
}
