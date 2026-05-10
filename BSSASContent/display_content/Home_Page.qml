pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BSSAS

Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    clip: true

    property var searchResults: []
    property bool hasSearched: false
    property string helperMessage: "可在目录 D:/BSSASdatabase 中检索文件；如不存在则使用 %LOCALAPPDATA%/BSSASdatabase。"
    readonly property int pagePadding: 22
    readonly property int sectionSpacing: 18
    readonly property string searchIconSource: "images/search.png"
    readonly property string heroImageSource: "images/home/home_hero_intestine_waveform.png"
    readonly property string metricIconSource: "images/home/home_metric_card_icons.png"
    readonly property string waveformSource: "images/home/home_recent_signal_waveform.png"
    readonly property string statusIconSource: "images/home/home_system_status_icons.png"

    function performSearch() {
        const keyword = searchField.text.trim()
        hasSearched = true

        if (keyword.length === 0) {
            searchResults = []
            helperMessage = "请输入关键字。"
            return
        }

        searchResults = databaseManager.searchFiles(keyword)

        if (databaseManager.lastError.length > 0) {
            helperMessage = databaseManager.lastError
            return
        }

        if (searchResults.length === 0) {
            helperMessage = "未找到与“" + keyword + "”相关的文件。"
            return
        }

        helperMessage = "共找到 " + searchResults.length + " 个匹配文件。"
    }

    component SpriteIcon : Item {
        id: spriteIcon
        property string source
        property var clipRect

        Image {
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

    component Card : Item {
        id: card
        property color backgroundColor: Theme.cardBg
        property color borderColor: Theme.borderLight
        property real cardRadius: Theme.radiusLarge
        property real shadowBlur: 0.28
        property int shadowOffset: 4
        default property alias content: contentLayer.data

        Rectangle {
            id: cardBackground
            anchors.fill: parent
            radius: card.cardRadius
            color: card.backgroundColor
            border.width: 1
            border.color: card.borderColor
        }

        MultiEffect {
            anchors.fill: cardBackground
            source: cardBackground
            shadowEnabled: true
            shadowColor: Theme.shadowCard
            shadowBlur: card.shadowBlur
            shadowVerticalOffset: card.shadowOffset
            z: -1
        }

        Item {
            id: contentLayer
            anchors.fill: parent
        }
    }

    component MetricCard : Card {
        id: metricCard
        property string title
        property string value
        property string unit
        property string deltaText
        property color deltaColor: Theme.successDark
        property var iconClip

        cardRadius: Theme.radiusLarge
        shadowBlur: 0.34
        shadowOffset: 5

        SpriteIcon {
            id: metricIcon
            width: 58
            height: 58
            anchors {
                left: parent.left
                leftMargin: 18
                verticalCenter: parent.verticalCenter
            }
            source: root.metricIconSource
            clipRect: metricCard.iconClip
        }

        Column {
            anchors {
                left: metricIcon.right
                leftMargin: 18
                right: parent.right
                rightMargin: 14
                verticalCenter: parent.verticalCenter
            }
            spacing: 5

            Text {
                width: parent.width
                text: metricCard.title
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            RowLayout {
                width: parent.width
                height: valueText.implicitHeight
                spacing: 8

                Text {
                    id: valueText
                    text: metricCard.value
                    color: Theme.textTitle
                    font.family: Theme.numberFontFamily
                    font.pixelSize: 29
                    font.weight: Font.Bold
                    renderType: Text.NativeRendering
                }

                Text {
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 4
                    text: metricCard.unit
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    renderType: Text.NativeRendering
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Text {
                    text: "较昨日"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    renderType: Text.NativeRendering
                }

                Text {
                    text: metricCard.deltaText
                    color: metricCard.deltaColor
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    renderType: Text.NativeRendering
                }
            }
        }
    }

    component StatusTile : Card {
        id: statusTile
        property string title
        property string value
        property color valueColor: Theme.primary
        property var iconClip

        cardRadius: Theme.radiusMedium
        backgroundColor: Theme.primaryLighter
        borderColor: Theme.border
        shadowBlur: 0.18
        shadowOffset: 2

        Column {
            anchors {
                fill: parent
                margins: 12
            }
            spacing: 8

            SpriteIcon {
                width: 50
                height: 50
                anchors.horizontalCenter: parent.horizontalCenter
                source: root.statusIconSource
                clipRect: statusTile.iconClip
            }

            Text {
                width: parent.width
                text: statusTile.title
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                width: parent.width
                text: statusTile.value
                horizontalAlignment: Text.AlignHCenter
                color: statusTile.valueColor
                font.family: Theme.numberFontFamily
                font.pixelSize: 20
                font.weight: Font.Bold
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }

    component SectionTitle : Row {
        id: sectionTitle
        property string title

        spacing: 10

        Rectangle {
            width: 4
            height: 22
            radius: 2
            color: Theme.primary
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: sectionTitle.title
            color: Theme.textTitle
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCardTitle
            font.weight: Font.DemiBold
            renderType: Text.NativeRendering
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        anchors.margins: root.pagePadding
        clip: true
        contentWidth: availableWidth

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: contentColumn
            width: pageScroll.availableWidth
            spacing: root.sectionSpacing

            Item {
                id: heroCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(228, Math.min(292, root.height * 0.33))

                Rectangle {
                    id: heroBackground
                    anchors.fill: parent
                    radius: 20
                    border.width: 1
                    border.color: Theme.primaryBorder
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.heroBgStart }
                        GradientStop { position: 1.0; color: Theme.heroBgEnd }
                    }
                }

                MultiEffect {
                    anchors.fill: heroBackground
                    source: heroBackground
                    shadowEnabled: true
                    shadowColor: Theme.shadowFloating
                    shadowBlur: 0.32
                    shadowVerticalOffset: 4
                    z: -1
                }

                Image {
                    anchors {
                        right: parent.right
                        rightMargin: 4
                        verticalCenter: parent.verticalCenter
                    }
                    width: Math.min(parent.width * 0.62, 640)
                    height: parent.height * 0.94
                    source: root.heroImageSource
                    sourceClipRect: Qt.rect(0, 120, 1586, 640)
                    fillMode: Image.PreserveAspectFit
                    opacity: 0.96
                    smooth: true
                    mipmap: true
                }

                Column {
                    anchors {
                        left: parent.left
                        leftMargin: 52
                        top: parent.top
                        topMargin: 52
                    }
                    width: Math.min(parent.width * 0.47, 520)
                    spacing: 16

                    Text {
                        width: parent.width
                        text: "专注肠鸣音信号分析"
                        color: Theme.textTitle
                        font.family: Theme.fontFamily
                        font.pixelSize: Math.min(Theme.fontHeroTitle, 36)
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                        renderType: Text.NativeRendering
                    }

                    Text {
                        width: parent.width
                        text: "助力消化健康评估与诊断"
                        color: Theme.textHeroSubtitle
                        font.family: Theme.fontFamily
                        font.pixelSize: 24
                        font.weight: Font.Normal
                        wrapMode: Text.Wrap
                        renderType: Text.NativeRendering
                    }

                    Rectangle {
                        id: searchContainer
                        width: Math.min(parent.width, 460)
                        height: 50
                        radius: height / 2
                        color: Theme.contentBg
                        border.width: 1
                        border.color: searchField.activeFocus ? Theme.primaryBorder : Theme.border

                        MultiEffect {
                            anchors.fill: parent
                            source: parent
                            shadowEnabled: true
                            shadowColor: Theme.shadowCard
                            shadowBlur: 0.22
                            shadowVerticalOffset: 3
                            z: -1
                        }

                        Image {
                            id: searchIcon
                            width: 22
                            height: 22
                            anchors {
                                left: parent.left
                                leftMargin: 18
                                verticalCenter: parent.verticalCenter
                            }
                            source: root.searchIconSource
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }

                        TextInput {
                            id: searchField
                            anchors {
                                left: searchIcon.right
                                leftMargin: 12
                                right: shortcutBadge.left
                                rightMargin: 10
                                verticalCenter: parent.verticalCenter
                            }
                            height: parent.height
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textTitle
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            selectionColor: Theme.primary
                            selectedTextColor: Theme.textWhite
                            selectByMouse: true
                            clip: true
                            renderType: Text.NativeRendering
                            Keys.onReturnPressed: root.performSearch()
                            Keys.onEnterPressed: root.performSearch()
                        }

                        Text {
                            anchors {
                                left: searchField.left
                                right: searchField.right
                                verticalCenter: parent.verticalCenter
                            }
                            text: "搜索患者/记录/分析结果..."
                            color: Theme.textPlaceholder
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                            visible: searchField.text.length === 0 && !searchField.activeFocus
                            renderType: Text.NativeRendering
                        }

                        Rectangle {
                            id: shortcutBadge
                            anchors {
                                right: searchButton.left
                                rightMargin: 8
                                verticalCenter: parent.verticalCenter
                            }
                            width: shortcutText.implicitWidth + 16
                            height: 26
                            radius: 8
                            color: Theme.primaryLighter
                            border.width: 1
                            border.color: Theme.border

                            Text {
                                id: shortcutText
                                anchors.centerIn: parent
                                text: "Ctrl + K"
                                color: Theme.shortcutText
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                                renderType: Text.NativeRendering
                            }
                        }

                        Rectangle {
                            id: searchButton
                            anchors {
                                right: parent.right
                                rightMargin: 8
                                verticalCenter: parent.verticalCenter
                            }
                            width: 34
                            height: 34
                            radius: width / 2
                            color: searchButtonTap.pressed ? Theme.primaryPressed
                                                           : searchButtonHover.hovered ? Theme.primaryHover
                                                                                       : Theme.primary

                            HoverHandler { id: searchButtonHover }
                            TapHandler {
                                id: searchButtonTap
                                onTapped: root.performSearch()
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "›"
                                color: Theme.textWhite
                                font.family: Theme.fontFamily
                                font.pixelSize: 25
                                font.weight: Font.DemiBold
                                renderType: Text.NativeRendering
                            }

                            Behavior on color { ColorAnimation { duration: 150 } }
                        }

                        MouseArea {
                            anchors {
                                fill: parent
                                rightMargin: 54
                            }
                            acceptedButtons: Qt.LeftButton
                            onClicked: searchField.forceActiveFocus()
                        }
                    }
                }
            }

            Card {
                id: resultPanel
                Layout.fillWidth: true
                Layout.preferredHeight: root.hasSearched
                                        ? Math.min(360, Math.max(190, resultsList.contentHeight + 150))
                                        : 0
                visible: root.hasSearched
                opacity: root.hasSearched ? 1.0 : 0.0
                cardRadius: Theme.radiusLarge
                backgroundColor: Theme.pageBg
                borderColor: Theme.border

                Behavior on opacity {
                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                }

                Text {
                    id: resultTitle
                    anchors {
                        top: parent.top
                        topMargin: 20
                        left: parent.left
                        leftMargin: 24
                    }
                    text: "搜索结果"
                    color: Theme.textTitle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCardTitle
                    font.weight: Font.DemiBold
                    renderType: Text.NativeRendering
                }

                Rectangle {
                    anchors {
                        right: parent.right
                        rightMargin: 24
                        verticalCenter: resultTitle.verticalCenter
                    }
                    width: Math.max(72, resultCountText.implicitWidth + 26)
                    height: 30
                    radius: height / 2
                    color: Theme.primaryLight

                    Text {
                        id: resultCountText
                        anchors.centerIn: parent
                        text: root.searchResults.length + " 项"
                        color: Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        renderType: Text.NativeRendering
                    }
                }

                Text {
                    id: helperText
                    anchors {
                        top: resultTitle.bottom
                        topMargin: 8
                        left: resultTitle.left
                        right: parent.right
                        rightMargin: 24
                    }
                    text: root.helperMessage
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    renderType: Text.NativeRendering
                }

                Text {
                    id: databasePathText
                    anchors {
                        top: helperText.bottom
                        topMargin: 6
                        left: resultTitle.left
                        right: parent.right
                        rightMargin: 24
                    }
                    text: "数据库路径：" + databaseManager.databaseFilePath
                    elide: Text.ElideMiddle
                    color: Theme.textWeak
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    renderType: Text.NativeRendering
                }

                Rectangle {
                    id: resultDivider
                    anchors {
                        top: databasePathText.bottom
                        topMargin: 12
                        left: parent.left
                        leftMargin: 24
                        right: parent.right
                        rightMargin: 24
                    }
                    height: 1
                    color: Theme.divider
                }

                ListView {
                    id: resultsList
                    anchors {
                        top: resultDivider.bottom
                        topMargin: 12
                        left: parent.left
                        leftMargin: 18
                        right: parent.right
                        rightMargin: 12
                        bottom: parent.bottom
                        bottomMargin: 16
                    }
                    clip: true
                    model: root.searchResults
                    spacing: 10
                    visible: root.searchResults.length > 0

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        property var resultItem: modelData

                        width: resultsList.width - 10
                        height: 82
                        radius: Theme.radiusMedium
                        color: Theme.contentBg
                        border.width: 1
                        border.color: Theme.borderLight

                        Text {
                            id: fileNameText
                            anchors {
                                top: parent.top
                                topMargin: 12
                                left: parent.left
                                leftMargin: 16
                                right: typeBadge.left
                                rightMargin: 12
                            }
                            text: resultItem.fileName || ""
                            color: Theme.textTitle
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBodyLarge
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            renderType: Text.NativeRendering
                        }

                        Rectangle {
                            id: typeBadge
                            anchors {
                                right: parent.right
                                rightMargin: 14
                                verticalCenter: fileNameText.verticalCenter
                            }
                            width: Math.max(52, typeLabelText.implicitWidth + 18)
                            height: 26
                            radius: height / 2
                            color: Theme.primaryLight

                            Text {
                                id: typeLabelText
                                anchors.centerIn: parent
                                text: resultItem.typeLabel || ""
                                color: Theme.primary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
                                renderType: Text.NativeRendering
                            }
                        }

                        Text {
                            anchors {
                                top: fileNameText.bottom
                                topMargin: 7
                                left: fileNameText.left
                                right: parent.right
                                rightMargin: 16
                            }
                            text: "分类：" + (resultItem.category || "") + "    路径：" + (resultItem.relativePath || resultItem.absolutePath || "")
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                            renderType: Text.NativeRendering
                        }
                    }
                }

                Text {
                    anchors {
                        top: resultDivider.bottom
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 22
                    }
                    visible: root.searchResults.length === 0
                    text: root.helperMessage
                    color: Theme.textWeak
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodyLarge
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    renderType: Text.NativeRendering
                }
            }

            GridLayout {
                id: metricGrid
                Layout.fillWidth: true
                columns: width > 880 ? 4 : 2
                columnSpacing: root.sectionSpacing
                rowSpacing: root.sectionSpacing

                Repeater {
                    model: [
                        { title: "今日采集", value: "18", unit: "例", delta: "↑ 12%", color: Theme.successDark, clip: [58, 310, 326, 328] },
                        { title: "已分析", value: "152", unit: "例", delta: "↑ 8%", color: Theme.successDark, clip: [438, 310, 326, 328] },
                        { title: "异常提示", value: "23", unit: "例", delta: "↓ 5%", color: Theme.danger, clip: [824, 310, 326, 328] },
                        { title: "数据库", value: "3,246", unit: "例", delta: "↑ 15%", color: Theme.successDark, clip: [1214, 310, 326, 328] }
                    ]

                    MetricCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 118
                        title: modelData.title
                        value: modelData.value
                        unit: modelData.unit
                        deltaText: modelData.delta
                        deltaColor: modelData.color
                        iconClip: modelData.clip
                    }
                }
            }

            GridLayout {
                id: overviewGrid
                Layout.fillWidth: true
                columns: width > 780 ? 2 : 1
                columnSpacing: root.sectionSpacing
                rowSpacing: root.sectionSpacing

                Card {
                    id: signalCard
                    Layout.fillWidth: true
                    Layout.preferredWidth: overviewGrid.width * 0.54
                    Layout.preferredHeight: 320
                    cardRadius: Theme.radiusLarge
                    borderColor: Theme.border

                    SectionTitle {
                        anchors {
                            top: parent.top
                            topMargin: 22
                            left: parent.left
                            leftMargin: 22
                        }
                        title: "近期信号概览"
                    }

                    Rectangle {
                        anchors {
                            top: parent.top
                            topMargin: 18
                            right: parent.right
                            rightMargin: 22
                        }
                        width: 120
                        height: 34
                        radius: 10
                        color: Theme.primaryLighter
                        border.width: 1
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: "肠鸣音信号"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            renderType: Text.NativeRendering
                        }
                    }

                    Image {
                        id: waveformImage
                        anchors {
                            top: parent.top
                            topMargin: 72
                            left: parent.left
                            leftMargin: 24
                            right: parent.right
                            rightMargin: 24
                        }
                        height: 144
                        source: root.waveformSource
                        sourceClipRect: Qt.rect(40, 246, 1508, 480)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }

                    GridLayout {
                        anchors {
                            left: parent.left
                            leftMargin: 22
                            right: parent.right
                            rightMargin: 22
                            bottom: parent.bottom
                            bottomMargin: 18
                        }
                        columns: 4
                        columnSpacing: 12

                        Repeater {
                            model: [
                                { label: "信号时长", value: "30.0 s" },
                                { label: "有效能量", value: "0.78 uV²·s" },
                                { label: "主频", value: "2.35 Hz" },
                                { label: "信噪比", value: "18.6 dB" }
                            ]

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 64
                                radius: Theme.radiusMedium
                                color: Theme.pageBg
                                border.width: 1
                                border.color: Theme.borderLight

                                Column {
                                    anchors.centerIn: parent
                                    width: parent.width - 10
                                    spacing: 4

                                    Text {
                                        width: parent.width
                                        text: modelData.label
                                        horizontalAlignment: Text.AlignHCenter
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                        renderType: Text.NativeRendering
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.value
                                        horizontalAlignment: Text.AlignHCenter
                                        color: Theme.textTitle
                                        font.family: Theme.numberFontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.Bold
                                        elide: Text.ElideRight
                                        renderType: Text.NativeRendering
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    id: statusCard
                    Layout.fillWidth: true
                    Layout.preferredWidth: overviewGrid.width * 0.46
                    Layout.preferredHeight: 320
                    cardRadius: Theme.radiusLarge
                    borderColor: Theme.border

                    SectionTitle {
                        anchors {
                            top: parent.top
                            topMargin: 22
                            left: parent.left
                            leftMargin: 22
                        }
                        title: "系统状态"
                    }

                    GridLayout {
                        anchors {
                            left: parent.left
                            leftMargin: 22
                            right: parent.right
                            rightMargin: 22
                            top: parent.top
                            topMargin: 80
                            bottom: parent.bottom
                            bottomMargin: 24
                        }
                        columns: 4
                        columnSpacing: 10

                        Repeater {
                            model: [
                                { title: "采集设备", value: "正常 ●", color: Theme.successDark, clip: [124, 340, 244, 250] },
                                { title: "存储空间", value: "68%", color: Theme.primary, clip: [492, 340, 250, 250] },
                                { title: "内存占用", value: "42%", color: Theme.primary, clip: [842, 340, 232, 248] },
                                { title: "CPU占用", value: "28%", color: Theme.textTitle, clip: [1224, 340, 246, 252] }
                            ]

                            StatusTile {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                title: modelData.title
                                value: modelData.value
                                valueColor: modelData.color
                                iconClip: modelData.clip
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Layout.topMargin: 2

                Item { Layout.fillWidth: true }

                Text {
                    text: "© 2024 肠鸣音信号分析系统 · 医学诊断辅助工具"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    renderType: Text.NativeRendering
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 14
                    color: Theme.divider
                }

                Text {
                    text: "系统运行正常"
                    color: Theme.successDark
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    renderType: Text.NativeRendering
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 14
                    color: Theme.divider
                }

                Text {
                    text: "版本: " + Constants.version
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    renderType: Text.NativeRendering
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: searchField.forceActiveFocus()
    }
}
