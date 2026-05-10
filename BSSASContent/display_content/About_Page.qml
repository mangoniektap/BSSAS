import QtQuick
import QtQuick.Controls
import BSSAS

Page {
    id: root

    background: Rectangle {
        color: "transparent"
    }

    Text {
        anchors.centerIn: parent
        text: "关于"
        color: Theme.textPrimary
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontPageTitle; font.weight: Font.DemiBold
        renderType: Text.NativeRendering
    }
}
