/**
 * @file About.qml
 * @brief 关于页面。展示系统海报、信息卡片、设计理念与页脚状态。
 */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS

Page {
    id: root

    property bool pageActive: true
    readonly property string assetBase: "qrc:/qt/qml/BSSASContent/image resources/about/"
    readonly property real posterAspectRatio: 2171 / 576
    readonly property real waveAspectRatio: 1084 / 396
    readonly property real waveImageScale: compactCards ? 0.96 : 1.08
    readonly property int outerMargin: Math.max(14, Math.min(22, width * 0.018))
    readonly property int cardSpacing: 16
    readonly property bool compactCards: Constants.isCompactContent(width, height) || width < 980
    readonly property real posterWidth: Math.max(0, width - outerMargin * 2)
    readonly property real posterHeight: posterWidth / posterAspectRatio
    readonly property int informationCardHeight: compactCards ? 318 : Math.max(270, Math.min(318, height * 0.34))
    readonly property int designPanelHeight: compactCards ? 290 : Math.max(142, Math.min(166, height * 0.18))
    readonly property int footerHeight: 30
    readonly property var abilityItems: [
        {
            "title": "实时采集",
            "description": "多通道高保真信号采集",
            "icon": "realtime_acquisition_icon.svg"
        },
        {
            "title": "时频分析",
            "description": "时域、频域及时频域分析",
            "icon": "time_frequency_analysis_icon.svg"
        },
        {
            "title": "病历管理",
            "description": "数据存储与病例信息管理",
            "icon": "medical_record_management_icon.svg"
        },
        {
            "title": "辅助诊断",
            "description": "智能识别与异常提示",
            "icon": "auxiliary_diagnosis_icon.svg"
        }
    ]
    readonly property var versionItems: [
        {
            "label": "软件版本",
            "value": "v26.10.17"
        },
        {
            "label": "模型版本",
            "value": "v1.0"
        },
        {
            "label": "更新日期",
            "value": "2026"
        }
    ]
    readonly property var supportItems: [
        {
            "title": "开发团队",
            "value": "医学信号智能分析团队",
            "icon": "development_team_icon.svg"
        },
        {
            "title": "联系邮箱",
            "value": "support@bowelsound.mangonienie",
            "icon": "support_email_icon.svg"
        },
        {
            "title": "官方网站",
            "value": "www.bowelsound.cn",
            "icon": "official_website_icon.svg"
        },
        {
            "title": "工作时间",
            "value": "周一至周五 9:00 - 18:00",
            "icon": "working_hours_icon.svg"
        }
    ]
    readonly property var designItems: [
        {
            "title": "安全可靠",
            "description": "本地处理，隐私保护",
            "icon": "security_reliability_icon.svg"
        },
        {
            "title": "轻量高效",
            "description": "性能优化，流畅体验",
            "icon": "lightweight_efficiency_icon.svg"
        },
        {
            "title": "本地部署",
            "description": "离线运行，独立可控",
            "icon": "local_deployment_icon.svg"
        },
        {
            "title": "易于扩展",
            "description": "模块化设计，灵活扩展",
            "icon": "extensible_architecture_icon.svg"
        }
    ]

    background: Rectangle {
        color: "transparent"
    }

    function asset(fileName) {
        return root.assetBase + fileName;
    }

    /**
     * @brief 信息卡片标题区域，统一处理标题图标、文字字号与间距。
     */
    component CardHeader: RowLayout {
        id: cardHeader
        required property string title
        required property string iconName

        Layout.fillWidth: true
        Layout.preferredHeight: 32
        spacing: 10

        Image {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            source: root.asset(cardHeader.iconName)
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Text {
            Layout.fillWidth: true
            text: cardHeader.title
            color: Theme.textTitle
            font.family: Theme.fontFamily
            font.pixelSize: 18
            font.bold: true
            elide: Text.ElideRight
            renderType: Text.NativeRendering
        }
    }

    /**
     * @brief 四个主体卡片的统一容器，保持构想图中的白底、细边框与小圆角。
     */
    component InformationCard: Rectangle {
        id: informationCard
        required property string title
        required property string iconName
        default property alias bodyData: bodySlot.data

        Layout.fillWidth: true
        Layout.preferredHeight: root.informationCardHeight
        radius: 14
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.border
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            CardHeader {
                title: informationCard.title
                iconName: informationCard.iconName
            }

            Item {
                id: bodySlot
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    /**
     * @brief 核心能力图标的承载背景，避免图标直接漂浮在文本旁。
     */
    component IconBadge: Rectangle {
        id: iconBadge
        required property string iconName
        property int iconSize: 35

        radius: 10
        color: Theme.primaryLight
        border.width: 1
        border.color: Theme.borderLight

        Image {
            anchors.centerIn: parent
            width: iconBadge.iconSize
            height: iconBadge.iconSize
            source: root.asset(iconBadge.iconName)
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
    }

    /**
     * @brief 核心能力单行条目，图标顺序按需求固定为 realtime_acquisition_icon、time_frequency_analysis_icon、medical_record_management_icon、auxiliary_diagnosis_icon。
     */
    component AbilityRow: RowLayout {
        id: abilityRow
        required property string title
        required property string description
        required property string iconName

        Layout.fillWidth: true
        Layout.preferredHeight: 50
        spacing: 12

        IconBadge {
            Layout.preferredWidth: 42
            Layout.preferredHeight: 42
            iconName: abilityRow.iconName
            iconSize: 25
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: abilityRow.title
                color: Theme.textTitle
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: abilityRow.description
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: 12
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    /**
     * @brief 版本信息条目，右侧版本号使用数字字体突出。
     */
    component VersionRow: Rectangle {
        id: versionRow
        required property string label
        required property string value

        Layout.fillWidth: true
        Layout.preferredHeight: 46
        radius: 9
        color: Theme.primaryLighter

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: versionRow.label
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: 14
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                text: versionRow.value
                color: Theme.textTitle
                font.family: Theme.numberFontFamily
                font.pixelSize: 15
                font.bold: true
                renderType: Text.NativeRendering
            }
        }
    }

    /**
     * @brief 技术支持条目，图标顺序按需求固定为 development_team_icon、support_email_icon、official_website_icon、working_hours_icon。
     */
    component SupportRow: RowLayout {
        id: supportRow
        required property string title
        required property string value
        required property string iconName

        Layout.fillWidth: true
        Layout.preferredHeight: 48
        spacing: 11

        Image {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            Layout.alignment: Qt.AlignVCenter
            source: root.asset(supportRow.iconName)
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: supportRow.title
                color: Theme.textTitle
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: supportRow.value
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: 12
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    /**
     * @brief 设计理念价值卡片；图标固定为 45px，对应构想图底部四个独立价值点。
     */
    component DesignValueCard: Rectangle {
        id: designValueCard
        required property string title
        required property string description
        required property string iconName

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 128
        radius: 12
        color: Theme.textWhite
        border.width: 1
        border.color: Theme.borderLight

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 5

            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 45
                Layout.preferredHeight: 45
                source: root.asset(designValueCard.iconName)
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Text {
                Layout.fillWidth: true
                text: designValueCard.title
                color: Theme.textTitle
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: designValueCard.description
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    Flickable {
        id: aboutFlickable
        anchors {
            fill: parent
            topMargin: root.outerMargin
            bottomMargin: root.outerMargin
            leftMargin: root.outerMargin
            rightMargin: root.outerMargin
        }
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            x: 0
            y: 0
            width: parent.width
            height: Math.max(aboutFlickable.height, implicitHeight)
            spacing: root.cardSpacing

            /**
             * 海报沿用原资源的顶部区域裁切，和构想图首屏比例一致。
             */
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.posterHeight

                Image {
                    id: posterImage
                    anchors.fill: parent
                    source: root.asset("header_poster.png")
                    sourceClipRect: Qt.rect(0, 74, 2171, 576)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
            }

            GridLayout {
                id: informationGrid
                Layout.fillWidth: true
                columns: root.compactCards ? 2 : 4
                columnSpacing: root.cardSpacing
                rowSpacing: root.cardSpacing

                InformationCard {
                    title: "系统简介"
                    iconName: "system_intro_icon.svg"

                    Item {
                        anchors.fill: parent

                        Text {
                            id: introDescriptionText
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "肠鸣音信号分析系统是一款专注于肠鸣音信号采集、分析与识别的专业软件，集成了时域、频域及时频域分析方法，结合智能识别模型，为临床提供评估与辅助诊断支持。"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBodyLarge
                            lineHeight: 1.45
                            wrapMode: Text.WordWrap
                            renderType: Text.NativeRendering
                        }

                        Item {
                            id: introWaveBox
                            anchors {
                                top: introDescriptionText.bottom
                                topMargin: root.compactCards ? 8 : 12
                                left: parent.left
                                right: parent.right
                                bottom: parent.bottom
                            }
                            clip: true

                            Image {
                                anchors.centerIn: parent
                                width: Math.min(parent.width * root.waveImageScale,
                                                parent.height * root.waveAspectRatio)
                                height: width / root.waveAspectRatio
                                source: root.asset("intro_waveform_strip.png")
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }
                        }
                    }
                }

                InformationCard {
                    title: "核心能力"
                    iconName: "core_capabilities_icon.svg"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Repeater {
                            model: root.abilityItems

                            delegate: AbilityRow {
                                required property var modelData

                                title: modelData.title
                                description: modelData.description
                                iconName: modelData.icon
                            }
                        }
                    }
                }

                InformationCard {
                    title: "版本信息"
                    iconName: "version_info_icon.svg"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12

                        Repeater {
                            model: root.versionItems

                            delegate: VersionRow {
                                required property var modelData

                                label: modelData.label
                                value: modelData.value
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.divider
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                radius: 11
                                color: Theme.success

                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: Theme.textWhite
                                    font.pixelSize: 15
                                    font.bold: true
                                    renderType: Text.NativeRendering
                                }
                            }

                            Text {
                                text: "当前已是最新版本"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                renderType: Text.NativeRendering
                            }
                        }
                    }
                }

                InformationCard {
                    title: "技术支持"
                    iconName: "technical_support_headset_icon.svg"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 9

                        Repeater {
                            model: root.supportItems

                            delegate: SupportRow {
                                required property var modelData

                                title: modelData.title
                                value: modelData.value
                                iconName: modelData.icon
                            }
                        }
                    }
                }
            }

            /**
             * 设计理念区块补齐构想图底部内容；标题图标使用 design_philosophy_icon.svg，尺寸固定 40px。
             */
            Rectangle {
                id: designPanel
                Layout.fillWidth: true
                Layout.preferredHeight: root.designPanelHeight
                radius: 14
                color: Theme.textWhite
                border.width: 1
                border.color: Theme.border
                clip: true

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 22

                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.compactCards ? Math.max(280, designPanel.width * 0.42) : Math.max(440, designPanel.width * 0.39)
                        Layout.maximumWidth: root.compactCards ? 420 : 560
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Image {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                source: root.asset("design_philosophy_icon.svg")
                                sourceSize.width: width
                                sourceSize.height: height
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "设计理念"
                                color: Theme.textTitle
                                font.family: Theme.fontFamily
                                font.pixelSize: 18
                                font.bold: true
                                elide: Text.ElideRight
                                renderType: Text.NativeRendering
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "我们致力于通过先进的信号处理与人工智能技术，让科技赋能医疗新价值，让消化健康评估更精准、便捷与可及。\n以专业、可信赖的工具，赋能医疗工作者，助力每一次更精准的临床决策。"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                            renderType: Text.NativeRendering
                        }
                    }

                    GridLayout {
                        id: designValueGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: root.compactCards ? 2 : 4
                        columnSpacing: 16
                        rowSpacing: 10

                        Repeater {
                            model: root.designItems

                            delegate: DesignValueCard {
                                required property var modelData

                                title: modelData.title
                                description: modelData.description
                                iconName: modelData.icon
                            }
                        }
                    }
                }
            }
        }
    }
}
