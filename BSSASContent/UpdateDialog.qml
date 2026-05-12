/**
 * @file UpdateDialog.qml
 * @brief 更新对话框。展示新版本信息、发布说明并提供下载与安装控制。
 */
import QtQuick
import QtQuick.Controls
import BSSAS

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 620, 620)
    modal: true
    focus: true
    padding: 0
    closePolicy: (updateManager.mandatoryUpdate || updateManager.downloading)
        ? Popup.NoAutoClose
        : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    /**
     * @brief 格式化字节数为可读字符串（B / KB / MB / GB）。
     * @param bytes 字节数
     * @returns 格式化后的字符串
     */
    function formatBytes(bytes) {
        if (bytes <= 0) {
            return "未知"
        }

        const units = ["B", "KB", "MB", "GB"]
        let value = bytes
        let unitIndex = 0
        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024
            unitIndex += 1
        }
        return value.toFixed(unitIndex === 0 ? 0 : 2) + " " + units[unitIndex]
    }

    /**
     * @brief 生成安装包下载进度标签文本。
     * @returns 进度描述字符串
     */
    function progressLabel() {
        if (updateManager.totalBytes > 0) {
            const percent = Math.max(0, Math.min(100, Math.round(updateManager.downloadProgress * 100)))
            return percent + "% (" + formatBytes(updateManager.downloadedBytes) + " / " +
                formatBytes(updateManager.totalBytes) + ")"
        }

        if (updateManager.downloadedBytes > 0) {
            return "已下载 " + formatBytes(updateManager.downloadedBytes)
        }

        return "准备中..."
    }

    Overlay.modal: Rectangle {
        color: Theme.overlayDim
    }

    background: Rectangle {
        radius: 28
        color: Theme.cardBg
        border.width: 1
        border.color: Theme.border
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 0
                to: 1
                duration: 220
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: root
                property: "scale"
                from: 0.94
                to: 1
                duration: 220
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 1
                to: 0
                duration: 160
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: root
                property: "scale"
                from: 1
                to: 0.97
                duration: 160
                easing.type: Easing.InCubic
            }
        }
    }

    contentItem: Item {
        implicitWidth: 572
        implicitHeight: contentColumn.implicitHeight + 48

        Column {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            Rectangle {
                implicitWidth: badgeText.implicitWidth + 24
                implicitHeight: badgeText.implicitHeight + 14
                width: implicitWidth
                height: implicitHeight
                radius: 18
                color: updateManager.mandatoryUpdate ? Theme.dangerBg : Theme.primaryLight

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: updateManager.mandatoryUpdate ? "强制更新" : "版本更新"
                    color: updateManager.mandatoryUpdate ? Theme.danger : Theme.primary
                    font.pixelSize: 13
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Text {
                width: parent.width
                text: updateManager.latestVersion.length > 0
                    ? ("发现新版本 " + updateManager.latestVersion)
                    : "发现可用更新"
                color: Theme.textPrimary
                font.pixelSize: 28
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "当前版本 " + updateManager.currentVersion +
                    (updateManager.mandatoryUpdate
                        ? "，该版本已停止支持，请尽快升级。"
                        : "，建议升级以获取最新功能和修复。")
                color: Theme.textMuted
                font.pixelSize: 15
                lineHeight: 22
                lineHeightMode: Text.FixedHeight
                wrapMode: Text.WordWrap
            }

            Rectangle {
                width: parent.width
                radius: 20
                color: Theme.primaryLighter
                border.width: 1
                border.color: Theme.border

                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        text: "更新信息"
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: "发布时间: " + (updateManager.publishTimeDisplay.length > 0
                            ? updateManager.publishTimeDisplay
                            : "未提供")
                        color: Theme.textMuted
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: "安装包大小: " + formatBytes(updateManager.fileSize)
                        color: Theme.textMuted
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: "下载地址: " + updateManager.installerUrl
                        color: Theme.textMuted
                        font.pixelSize: 14
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 10

                Text {
                    text: "更新说明"
                    color: Theme.textPrimary
                    font.pixelSize: 16
                    font.bold: true
                }

                ScrollView {
                    width: parent.width
                    height: 150
                    clip: true

                    background: Rectangle {
                        radius: 18
                        color: Theme.primaryLighter
                        border.width: 1
                        border.color: Theme.border
                    }

                    Text {
                        width: root.width - 88
                        text: updateManager.releaseNotes.length > 0
                            ? updateManager.releaseNotes
                            : "暂无更新说明"
                        color: Theme.textMuted
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        padding: 16
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8
                visible: updateManager.downloading || updateManager.statusMessage.length > 0

                Text {
                    width: parent.width
                    text: updateManager.statusMessage
                    color: updateManager.downloading ? Theme.primary : Theme.textMuted
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                ProgressBar {
                    width: parent.width
                    visible: updateManager.downloading
                    indeterminate: updateManager.totalBytes <= 0
                    from: 0
                    to: 1
                    value: updateManager.totalBytes > 0 ? updateManager.downloadProgress : 0
                }

                Text {
                    width: parent.width
                    visible: updateManager.downloading
                    text: progressLabel()
                    color: Theme.textMuted
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }

            Row {
                width: parent.width
                spacing: 12
                layoutDirection: Qt.RightToLeft

                Button {
                    width: 152
                    height: 48
                    enabled: !updateManager.checking && !updateManager.downloading
                    text: updateManager.downloading
                        ? "下载中..."
                        : (updateManager.checking ? "检查中..." : "立即更新")
                    onClicked: updateManager.startUpdate()

                    contentItem: Text {
                        text: parent.text
                        color: Theme.textWhite
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 24
                        color: parent.enabled ? Theme.primary : Theme.primaryBorder
                    }
                }

                Button {
                    visible: !updateManager.mandatoryUpdate && !updateManager.downloading
                    width: 132
                    height: 48
                    text: "稍后再说"
                    onClicked: root.close()

                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 24
                        color: Theme.primaryLighter
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }
        }
    }
}
