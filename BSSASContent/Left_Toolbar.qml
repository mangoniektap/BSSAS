import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS
import MangoComponent

Item {
    id: root
    signal settingClicked()

    property alias display_content_selection: sidebar.currentIndex
    property int navFrameTopSpacing: 20

    width: Math.floor(parent.width * 0.225) - 80

    anchors {
        top: parent.top
        bottom: parent.bottom
        left: parent.left
        margins: 20
    }

    Item {
        anchors.fill: parent

        Item {
            id: brandHeader
            height: 130
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: 10
            }

            RowLayout {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                }
                spacing: 12

                Image {
                    id: brandLogo
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 90
                    source: "qrc:/qt/qml/BSSASContent/image resources/home page/brand_bowel_sound_logo.png"
                    sourceClipRect: Qt.rect(390, 96, 820, 820)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: "肠鸣音信号分析系统"
                        color: Theme.primary
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeroSubtitle
                        font.weight: Font.DemiBold
                        renderType: Text.NativeRendering
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "医学诊断辅助工具"
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        font.family: Theme.fontHeroSubtitle
                        font.pixelSize: Theme.fontCardTitle
                        font.weight: Font.Normal
                        renderType: Text.NativeRendering
                    }
                }
            }
        }

        Item {
            id: navFrameContainer
            height: parent.height * 0.5
            anchors {
                top: brandHeader.bottom
                topMargin: 40
                left: parent.left
                right: parent.right
            }

            Rectangle {
                id: navFrame
                anchors.fill: parent
                radius: Theme.radiusXLarge
                color: Theme.cardBg
                border.width: 1
                border.color: Theme.borderLight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Math.max(0, parent.radius - 1)
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                }
            }

            ColumnTabBar {
                id: sidebar
                anchors {
                    fill: parent
                    margins: 12
                }
                spacing: 10
                currentIndex: 3

                background: Rectangle { color: "transparent" }

                Repeater {
                    model: [
                        {
                            "title": "首页",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_home.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_home_selected.svg"
                        },
                        {
                            "title": "软件总控",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_master_control.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_master_control_selected.svg"
                        },
                        {
                            "title": "时频监测",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_temporal_spectral_monitoring.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_temporal_spectral_monitoring_selected.svg"
                        },
                        {
                            "title": "病历管理",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_medical_record_management.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_medical_record_management_selected.svg"
                        },
                        {
                            "title": "辅助诊断",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_auxiliary_diagnosis.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_auxiliary_diagnosis_selected.svg"
                        },
                        {
                            "title": "关于",
                            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_about.svg",
                            "selectedIcon": "qrc:/qt/qml/BSSASContent/image resources/home page/nav_about_selected.svg"
                        }
                    ]

                    TabButton {
                        id: control
                        required property var modelData

                        text: control.modelData.title
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontPageTitle; font.weight: Font.Medium
                        implicitHeight: (sidebar.height - sidebar.spacing * 5) / 6
                        width: sidebar.width

                        contentItem: Item {
                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: Math.max(18, control.width * 0.12)
                                    rightMargin: 16
                                }
                                spacing: 14

                                Image {
                                    readonly property real navIconSize: Math.max(24, Math.min(32, control.height * 0.34))
                                    Layout.preferredWidth: navIconSize
                                    Layout.preferredHeight: navIconSize
                                    source: control.checked ? control.modelData.selectedIcon : control.modelData.icon
                                    sourceSize.width: width
                                    sourceSize.height: height
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: control.text
                                    font: control.font
                                    color: control.checked ? Theme.textPrimary : Theme.textWeak
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    renderType: Text.NativeRendering

                                    Behavior on color {
                                        ColorAnimation { duration: 200 }
                                    }
                                }
                            }
                        }

                        background: Rectangle {
                            radius: 12
                            color: {
                                if (control.pressed) return Theme.secondaryPressedBg
                                if (control.checked) return Theme.primaryLight
                                if (control.hovered) return Theme.primaryLighter
                                return "transparent"
                            }

                            Behavior on color {
                                ColorAnimation {
                                    duration: 250
                                    easing.type: Easing.OutCubic
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                height: parent.height * 0.5
                                width: 3
                                radius: 1.5
                                color: Theme.primary
                                scale: control.checked ? 1 : 0

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 400
                                        easing.type: Easing.OutBack
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Image {
            id: securityBadgeIcon
            width: parent.width * 1.5
            height: width * 0.375
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: 120
            }
            source: "qrc:/qt/qml/BSSASContent/image resources/home page/security_badge_banner.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
    }
}
