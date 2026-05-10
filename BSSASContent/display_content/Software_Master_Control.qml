import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import BSSAS
import "./software_master_control_content"

Page {
    property real layout_margin: 20
    property bool voiceLocalizationEnabled: false
    property var localizationPoints: [
        ({ channel: "CH1", label: "回盲部-最核心区", x: 0.52, y: 0.28 }),
        ({ channel: "CH2", label: "膀胱上方/小肠下段", x: 0.00, y: 0.22 }),
        ({ channel: "CH3", label: "乙状结肠", x: -0.50, y: 0.28 }),
        ({ channel: "CH4", label: "升结肠/小肠右", x: 0.58, y: 0.56 }),
        ({ channel: "CH5", label: "脐部", x: 0.00, y: 0.50 }),
        ({ channel: "CH6", label: "降结肠/小肠左", x: -0.58, y: 0.56 }),
        ({ channel: "CH7", label: "肝曲/升结肠上段", x: 0.62, y: 0.74 })
    ]
    id: root
    anchors.fill: parent

    function openSensorLocalizationPage() {
        control_selection.currentIndex = 2
    }

    function currentLocalizationSampleRate() {
        return Math.max(1, dataManager.configuredSampleRate)
    }

    function closeVoiceLocalization() {
        localizationIntestinalSound.stopRealtimePipeline()
        voiceLocalizationEnabled = false
    }

    function applyLocalizationAndBack(points) {
        localizationPoints = points
        localizationIntestinalSound.startRealtimePipeline(root.currentLocalizationSampleRate())
        voiceLocalizationEnabled = true
        control_selection.currentIndex = 0
    }

    Component.onCompleted: {
        voiceLocalizationEnabled = localizationIntestinalSound.running
    }

    Connections {
        target: localizationIntestinalSound

        function onRunningChanged() {
            root.voiceLocalizationEnabled = localizationIntestinalSound.running
        }
    }

    background: Rectangle {
        color: "transparent"
    }

    Text {
        id: windowTitle
        anchors {
            top: parent.top
            left: parent.left
            margins: layout_margin * 1.5
        }
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontPageTitle; font.weight: Font.DemiBold
        text: "软件总控"
        renderType: Text.NativeRendering
        color: Theme.textPrimary
    }

    TabBar {
        id: control_selection
        anchors {
            top: windowTitle.bottom
            topMargin: layout_margin
            left: windowTitle.left
            right: parent.right
            rightMargin: layout_margin
        }
        height: 35
        spacing: 20
        currentIndex: 0

        background: Rectangle {
            color: "transparent"
        }

        Material.accent: "transparent"

        Repeater {
            id: controlTabRepeater
            model: ListModel {
                ListElement { btnText: "DAQ主控" }
                ListElement { btnText: "信号处理设置" }
                ListElement { btnText: "传感器监测" }
                ListElement { btnText: "文件管理" }
            }

            TabButton {
                id: tabBtn
                text: model.btnText
                width: implicitWidth + 10

                contentItem: Text {
                    text: tabBtn.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabBtn.checked ? Theme.primary : Theme.textMuted
                    Behavior on color { ColorAnimation { duration: 200 } }
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
        anchors.top: control_selection.bottom

        readonly property Item targetItem: (
            control_selection.currentIndex >= 0 &&
            control_selection.currentIndex < controlTabRepeater.count
        ) ? controlTabRepeater.itemAt(control_selection.currentIndex) : null

        width: targetItem ? targetItem.width * 0.6 : 0
        x: targetItem ? (
            targetItem.x +
            control_selection.x +
            control_selection.contentItem.x +
            (targetItem.width - width) / 2) : control_selection.x

        Behavior on x {
            SpringAnimation {
                spring: 3.5
                damping: 0.3
                epsilon: 0.5
            }
        }

        Behavior on width {
            NumberAnimation { duration: 100; easing.type: Easing.OutQuint }
        }
    }

    Rectangle {
        id: separatorLine
        anchors {
            top: control_selection.bottom
            topMargin: 7
            left: parent.left
            leftMargin: layout_margin
            right: parent.right
            rightMargin: layout_margin
        }
        height: 1
        color: Theme.border
    }

    SwipeView {
        id: mainSwipeView
        anchors {
            top: separatorLine.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        interactive: false
        currentIndex: (count > 0)
              ? Math.max(0, Math.min(control_selection.currentIndex, count - 1))
              : 0

        DAQ_Device_Console {
            id: daq_device_console
            voiceLocalizationEnabled: root.voiceLocalizationEnabled
            openSensorLocalizationPage: root.openSensorLocalizationPage
            closeVoiceLocalization: root.closeVoiceLocalization
        }
        Signal_Processing_Settings {
            id: signal_processing_settings
        }
        Sensor_Monitoring {
            id: sensor_monitoring
            localizationPoints: root.localizationPoints
            applyLocalizationAndBack: root.applyLocalizationAndBack
        }
        File_Management {
            id: file_management
            pageRoot: root
        }
    }
}
