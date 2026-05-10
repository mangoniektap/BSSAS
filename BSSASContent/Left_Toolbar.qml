pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import BSSAS

Item {
    id: root
    signal settingClicked()

    property int display_content_selection: navColumn.currentIndex
    readonly property string iconSource: "images/home/home_sidebar_nav_icons.png"
    readonly property string logoSource: "images/home/home_bowel_sound_logo.png"
    readonly property string securityIconSource: "images/home/home_security_badge_icon.png"
    readonly property var navItems: [
        { title: "首页", selectedClip: [130, 262, 138, 130], normalClip: [130, 546, 138, 132] },
        { title: "软件总控", selectedClip: [382, 266, 138, 130], normalClip: [382, 552, 138, 132] },
        { title: "时频监测", selectedClip: [636, 270, 138, 124], normalClip: [636, 554, 138, 126] },
        { title: "病历管理", selectedClip: [888, 260, 132, 142], normalClip: [888, 548, 132, 142] },
        { title: "辅助诊断", selectedClip: [1104, 274, 140, 122], normalClip: [1104, 556, 140, 124] },
        { title: "关于", selectedClip: [1330, 260, 138, 140], normalClip: [1330, 548, 138, 140] }
    ]

    width: Math.max(244, Math.min(300, mainwindow_background.display_area.x - 40))

    anchors {
        top: parent.top
        bottom: parent.bottom
        left: parent.left
        leftMargin: 18
        topMargin: 22
        bottomMargin: 22
    }

    component SpriteIcon : Item {
        id: spriteIcon
        property string source
        property var clipRect

        Image {
            id: spriteImage
            anchors.fill: parent
            source: spriteIcon.source
            sourceClipRect: Qt.rect(spriteIcon.clipRect[0],
                                    spriteIcon.clipRect[1],
                                    spriteIcon.clipRect[2],
                                    spriteIcon.clipRect[3])
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
    }

    component NavButton : Item {
        id: navButton
        property string title
        property bool selected: false
        property var selectedClip
        property var normalClip
        signal clicked()

        implicitHeight: 58

        HoverHandler {
            id: hoverHandler
            target: navButton
        }

        TapHandler {
            id: tapHandler
            onTapped: navButton.clicked()
        }

        Rectangle {
            id: navBackground
            anchors.fill: parent
            radius: 12
            color: {
                if (tapHandler.pressed) return Theme.secondaryPressedBg
                if (navButton.selected) return Theme.primaryLight
                if (hoverHandler.hovered) return Theme.primaryLighter
                return "transparent"
            }
            border.width: navButton.selected ? 1 : 0
            border.color: Theme.primaryBorder

            Behavior on color {
                ColorAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 4
            height: 34
            radius: 2
            color: Theme.primary
            opacity: navButton.selected ? 1.0 : 0.0
            scale: navButton.selected ? 1.0 : 0.25
            transformOrigin: Item.Center

            Behavior on opacity { NumberAnimation { duration: 160 } }
            Behavior on scale {
                NumberAnimation { duration: 220; easing.type: Easing.OutBack }
            }
        }

        SpriteIcon {
            id: navIcon
            width: 25
            height: 25
            anchors {
                left: parent.left
                leftMargin: 28
                verticalCenter: parent.verticalCenter
            }
            source: root.iconSource
            clipRect: navButton.selected ? navButton.selectedClip : navButton.normalClip
            opacity: navButton.selected ? 1.0 : 0.82
            scale: navButton.selected ? 1.0 : 0.96

            Behavior on opacity { NumberAnimation { duration: 180 } }
            Behavior on scale { NumberAnimation { duration: 180 } }
        }

        Text {
            anchors {
                left: navIcon.right
                leftMargin: 18
                right: parent.right
                rightMargin: 14
                verticalCenter: parent.verticalCenter
            }
            text: navButton.title
            color: navButton.selected ? Theme.primary : Theme.textSidebar
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodyLarge
            font.weight: navButton.selected ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
            renderType: Text.NativeRendering

            Behavior on color { ColorAnimation { duration: 180 } }
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: 24
        color: Theme.contentBg
        border.width: 1
        border.color: Theme.borderLight
    }

    MultiEffect {
        anchors.fill: panel
        source: panel
        shadowEnabled: true
        shadowColor: Theme.shadowCard
        shadowBlur: 0.28
        shadowVerticalOffset: 4
        z: -1
    }

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 18
            rightMargin: 18
            topMargin: 24
            bottomMargin: 26
        }
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            spacing: 12

            SpriteIcon {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                source: root.logoSource
                clipRect: [452, 128, 680, 704]
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: "肠鸣音信号分析系统"
                    color: Theme.textTitle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }

                Text {
                    Layout.fillWidth: true
                    text: "医学诊断辅助工具"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }

        ColumnLayout {
            id: navColumn
            property int currentIndex: 0

            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: root.navItems

                NavButton {
                    Layout.fillWidth: true
                    title: modelData.title
                    selected: navColumn.currentIndex === index
                    selectedClip: modelData.selectedClip
                    normalClip: modelData.normalClip
                    onClicked: navColumn.currentIndex = index
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            radius: 12
            color: Theme.primaryLighter
            border.width: 1
            border.color: Theme.border

            SpriteIcon {
                id: securityIcon
                width: 28
                height: 28
                anchors {
                    left: parent.left
                    leftMargin: 16
                    verticalCenter: parent.verticalCenter
                }
                source: root.securityIconSource
                clipRect: [530, 184, 526, 622]
            }

            Column {
                anchors {
                    left: securityIcon.right
                    leftMargin: 12
                    right: arrow.left
                    rightMargin: 8
                    verticalCenter: parent.verticalCenter
                }
                spacing: 4

                Text {
                    width: parent.width
                    text: "数据安全 · 合规可信"
                    color: Theme.primary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }

                Text {
                    width: parent.width
                    text: "本地处理 · 隐私保护"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTiny
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }
            }

            Text {
                id: arrow
                anchors {
                    right: parent.right
                    rightMargin: 14
                    verticalCenter: parent.verticalCenter
                }
                text: "›"
                color: Theme.primary
                font.family: Theme.fontFamily
                font.pixelSize: 28
                font.weight: Font.Light
                renderType: Text.NativeRendering
            }
        }
    }
}
