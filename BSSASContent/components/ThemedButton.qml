/**
 * @file ThemedButton.qml
 * @brief 弹窗和表单使用的统一主题按钮。
 */
import QtQuick
import QtQuick.Controls
import BSSAS

Button {
    id: root

    property string variant: "secondary"
    property real minimumButtonWidth: 88
    property real buttonHeight: 42
    readonly property bool primaryVariant: variant === "primary"

    hoverEnabled: true
    implicitWidth: Math.max(minimumButtonWidth, buttonLabel.implicitWidth + leftPadding + rightPadding)
    implicitHeight: buttonHeight
    leftPadding: 22
    rightPadding: 22
    topPadding: 0
    bottomPadding: 0

    contentItem: Text {
        id: buttonLabel

        text: root.text
        color: {
            if (!root.enabled)
                return Theme.textDisabled
            return root.primaryVariant ? Theme.textWhite : Theme.primary
        }
        font.family: Theme.fontFamily
        font.pixelSize: 14
        font.bold: root.primaryVariant
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: height / 2
        border.width: 1
        border.color: {
            if (!root.enabled)
                return Theme.border
            return root.primaryVariant ? Theme.primary : Theme.primaryBorder
        }
        color: {
            if (!root.enabled)
                return Theme.disabledBg
            if (root.primaryVariant) {
                if (root.down)
                    return Theme.primaryPressed
                if (root.hovered)
                    return Theme.primaryHover
                return Theme.primary
            }
            if (root.down)
                return Theme.secondaryPressedBg
            if (root.hovered)
                return Theme.primaryLight
            return Theme.textWhite
        }
    }
}
