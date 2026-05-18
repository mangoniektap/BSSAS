/**
 * @file AdaptiveNoiseReductionConfigPopup.qml
 * @brief WebRTC adaptive noise reduction configuration popup.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS

Popup {
    id: root

    property string title: "自适应降噪参数"
    property int level: 1
    property bool highPassFilterEnabled: false
    property bool automaticGainControlEnabled: false
    property bool transientSuppressionEnabled: false

    signal accepted(int level, bool highPassFilterEnabled,
                    bool automaticGainControlEnabled,
                    bool transientSuppressionEnabled)

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function normalizedLevel(value) {
        const numericValue = Number(value)
        if (!isFinite(numericValue))
            return 1
        return Math.max(0, Math.min(3, Math.round(numericValue)))
    }

    function openWithValues(levelValue, highPassValue, agcValue, transientValue) {
        root.level = root.normalizedLevel(levelValue)
        root.highPassFilterEnabled = !!highPassValue
        root.automaticGainControlEnabled = !!agcValue
        root.transientSuppressionEnabled = !!transientValue
        levelSelect.currentIndex = root.level
        highPassSwitch.checked = root.highPassFilterEnabled
        agcSwitch.checked = root.automaticGainControlEnabled
        transientSwitch.checked = root.transientSuppressionEnabled
        root.open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min((parent ? parent.width : 640) - 60, 560)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 24
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.border
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
                text: root.title
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
                    text: "降噪强度"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                }

                Item {
                    Layout.fillWidth: true
                }

                Select {
                    id: levelSelect
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 46
                    currentIndex: root.level
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
                    onCurrentIndexChanged: root.level = root.normalizedLevel(currentIndex)
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

                ConfigSwitchRow {
                    label: "高通滤波"
                    checked: root.highPassFilterEnabled
                    onToggled: function(checkedValue) {
                        root.highPassFilterEnabled = checkedValue
                    }
                }

                ConfigSwitchRow {
                    label: "自动增益控制"
                    checked: root.automaticGainControlEnabled
                    onToggled: function(checkedValue) {
                        root.automaticGainControlEnabled = checkedValue
                    }
                }

                ConfigSwitchRow {
                    label: "瞬态抑制"
                    checked: root.transientSuppressionEnabled
                    onToggled: function(checkedValue) {
                        root.transientSuppressionEnabled = checkedValue
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "恢复默认"
                    onClicked: root.openWithValues(1, false, false, false)
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "取消"
                    onClicked: root.close()
                }

                Button {
                    text: "保存"
                    onClicked: {
                        root.accepted(
                            root.normalizedLevel(root.level),
                            root.highPassFilterEnabled,
                            root.automaticGainControlEnabled,
                            root.transientSuppressionEnabled)
                        root.close()
                    }
                }
            }
        }
    }

    component ConfigSwitchRow : Item {
        id: switchRow

        property string label: ""
        property bool checked: false
        signal toggled(bool checkedValue)

        Layout.fillWidth: true
        Layout.preferredHeight: 38
        implicitHeight: 38

        MouseArea {
            anchors.fill: parent
            z: 1
            cursorShape: Qt.PointingHandCursor
            onClicked: switchRow.toggled(!switchRow.checked)
        }

        RowLayout {
            anchors.fill: parent
            z: 0
            spacing: 12

            Text {
                text: switchRow.label
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }

            Item {
                Layout.fillWidth: true
            }

            ToggleSwitch {
                checked: switchRow.checked
                interactive: false
            }
        }
    }
}
