import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BSSAS

Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    clip: false

    property var searchResults: []
    property bool hasSearched: false
    property var overviewStats: ({})
    readonly property int overviewRefreshIntervalMs: 10000
    readonly property int recentSignalMetricHeight: 200
    readonly property int recentSignalWaveformClipLeft: 40
    readonly property int recentSignalWaveformClipTop: 218
    readonly property int recentSignalWaveformClipWidth: 1540
    readonly property int recentSignalWaveformClipHeight: 560
    readonly property int systemStatusIconSize: 90
    readonly property int systemStatusPanelSpacing: 10
    readonly property int systemStatusTileSpacing: 10
    readonly property int systemStatusTileHeight: 220
    readonly property int systemStatusCardContentSpacing: 10
    readonly property var overviewCards: [
        {
            "title": "今日采集",
            "value": root.safeCount(root.overviewStats.todayCollected),
            "deltaCurrent": root.safeCount(root.overviewStats.todayCollected),
            "deltaPrevious": root.safeCount(root.overviewStats.todayCollectedYesterday),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/overview_today_collection_icon.png",
            "highlight": false,
            "warning": false
        },
        {
            "title": "已分析",
            "value": root.safeCount(root.overviewStats.analyzed),
            "deltaCurrent": root.safeCount(root.overviewStats.analyzedToday),
            "deltaPrevious": root.safeCount(root.overviewStats.analyzedYesterday),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/overview_analyzed_records_icon.png",
            "highlight": false,
            "warning": false
        },
        {
            "title": "异常提示",
            "value": root.safeCount(root.overviewStats.abnormal),
            "deltaCurrent": root.safeCount(root.overviewStats.abnormalToday),
            "deltaPrevious": root.safeCount(root.overviewStats.abnormalYesterday),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/overview_abnormal_alert_icon.png",
            "highlight": false,
            "warning": true
        },
        {
            "title": "数据库",
            "value": root.safeCount(root.overviewStats.databaseRecords),
            "deltaCurrent": root.safeCount(root.overviewStats.databaseToday),
            "deltaPrevious": root.safeCount(root.overviewStats.databaseYesterday),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/overview_database_records_icon.png",
            "highlight": false,
            "warning": false
        }
    ]
    readonly property var recentSignalMetrics: [
        { "label": "信号时长", "value": "30.0", "unit": "s" },
        { "label": "有效能量", "value": "0.78", "unit": "μV²·s" },
        { "label": "主频", "value": "2.35", "unit": "Hz" },
        { "label": "信噪比", "value": "18.6", "unit": "dB" }
    ]
    readonly property var systemStatusCards: [
        {
            "title": "采集设备",
            "value": systemStatusMonitor.deviceStatusText,
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/status_acquisition_device_icon.png",
            "valueColor": root.deviceStatusColor(),
            "showDot": true
        },
        {
            "title": "存储空间",
            "value": root.formatPercent(systemStatusMonitor.storageUsagePercent),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/status_storage_usage_icon.png",
            "valueColor": root.percentStatusColor(systemStatusMonitor.storageUsagePercent),
            "showDot": false
        },
        {
            "title": "内存占用",
            "value": root.formatPercent(systemStatusMonitor.memoryUsagePercent),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/status_memory_usage_icon.png",
            "valueColor": root.percentStatusColor(systemStatusMonitor.memoryUsagePercent),
            "showDot": false
        },
        {
            "title": "CPU占用",
            "value": root.formatPercent(systemStatusMonitor.cpuUsagePercent),
            "icon": "qrc:/qt/qml/BSSASContent/image resources/home page/status_cpu_usage_icon.png",
            "valueColor": root.percentStatusColor(systemStatusMonitor.cpuUsagePercent),
            "showDot": false
        }
    ]
    property string helperMessage: "可在目录 D:/BSSASdatabase 中检索文件；如不存在则使用 %LOCALAPPDATA%/BSSASdatabase。"

    function safeCount(value) {
        const numberValue = Number(value)
        if (isNaN(numberValue) || numberValue < 0) {
            return 0
        }

        return Math.round(numberValue)
    }

    function formatCount(value) {
        return root.safeCount(value).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    }

    function formatPercent(value) {
        const numberValue = Number(value)
        if (isNaN(numberValue) || numberValue < 0) {
            return "--"
        }

        return Math.round(Math.max(0, Math.min(100, numberValue))) + "%"
    }

    function percentStatusColor(value) {
        const numberValue = Number(value)
        if (isNaN(numberValue)) {
            return Theme.textWeak
        }
        if (numberValue >= 85) {
            return Theme.danger
        }
        if (numberValue >= 70) {
            return Theme.warning
        }

        return Theme.primary
    }

    function deviceStatusColor() {
        return systemStatusMonitor.deviceConnected ? Theme.success : Theme.warning
    }

    function metricDelta(current, previous) {
        const currentCount = root.safeCount(current)
        const previousCount = root.safeCount(previous)
        const difference = currentCount - previousCount

        if (previousCount === 0) {
            if (currentCount === 0) {
                return { "text": "持平", "trend": 0 }
            }

            return {
                "text": "新增 " + root.formatCount(currentCount),
                "trend": 1
            }
        }

        if (difference === 0) {
            return { "text": "持平", "trend": 0 }
        }

        return {
            "text": (difference > 0 ? "↑ " : "↓ ") +
                    Math.round(Math.abs(difference) * 100 / previousCount) + "%",
            "trend": difference > 0 ? 1 : -1
        }
    }

    function metricDeltaColor(cardData, deltaInfo) {
        if (deltaInfo.trend === 0) {
            return Theme.textWeak
        }

        if (cardData.warning) {
            return Theme.danger
        }

        return deltaInfo.trend > 0 ? Theme.success : Theme.danger
    }

    function refreshOverviewStats() {
        overviewStats = databaseManager.homeOverviewStats()
    }

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

        root.refreshOverviewStats()

        if (searchResults.length === 0) {
            helperMessage = "未找到与“" + keyword + "”相关的文件。"
            return
        }

        helperMessage = "共找到 " + searchResults.length + " 个匹配文件。"
    }

    function itemContainsRootPosition(item, rootPosition) {
        const localPosition = item.mapFromItem(root, rootPosition.x, rootPosition.y)
        return localPosition.x >= 0
                && localPosition.x <= item.width
                && localPosition.y >= 0
                && localPosition.y <= item.height
    }

    Component.onCompleted: root.refreshOverviewStats()

    Timer {
        interval: root.overviewRefreshIntervalMs
        running: true
        repeat: true
        onTriggered: root.refreshOverviewStats()
    }

    TapHandler {
        target: root
        enabled: hasSearched
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint, button) {
            const clickPosition = eventPoint.position
            if (root.itemContainsRootPosition(searchContainer, clickPosition)
                    || root.itemContainsRootPosition(resultRevealArea, clickPosition)) {
                return
            }

            root.hasSearched = false
        }
    }

    Image {
        id: heroImage
        anchors {
            top: parent.top
            topMargin: 18
            left: parent.left
            leftMargin: 20
            right: parent.right
            rightMargin: 20
        }
        height: Math.min(width / (2508 / 627), parent.height * 0.36)
        fillMode: Image.PreserveAspectFit
        source: "qrc:/qt/qml/BSSASContent/image resources/home page/hero_intestine_waveform.png"
        smooth: true
        mipmap: true
        z: 0
    }

    Rectangle {
        id: searchShadow
        width: searchContainer.width
        height: searchContainer.height
        radius: searchContainer.radius
        x: searchContainer.x + 2
        y: searchContainer.y + 4
        color: Theme.controlPressedLayer
        z: searchContainer.z - 1
    }

    Rectangle {
        id: searchContainer
        width: Math.max(280, Math.min(heroImage.width * 0.38, 520))
        height: Math.max(44, Math.min(heroImage.height * 0.17, 58))
        radius: height / 2
        x: heroImage.x + heroImage.width * 0.05
        y: heroImage.y + heroImage.height * 0.70
        color: Theme.textWhite
        border.color: Theme.borderLight
        border.width: 1
        z: 2

        Rectangle {
            id: inputBackground
            anchors.fill: parent
            anchors.margins: 0
            radius: height / 2
            color: Theme.textWhite
            property real searchButtonSize: Math.max(44, height - 10)
            property real searchButtonInset: 10

            TextInput {
                id: searchField
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: inputBackground.searchButtonSize +
                                     inputBackground.searchButtonInset + 14
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textTitle
                font.pixelSize: Math.max(16, Math.min(20, parent.height * 0.38))
                selectionColor: Theme.orange
                selectByMouse: true
                clip: true
                renderType: Text.NativeRendering
                Keys.onReturnPressed: root.performSearch()
                Keys.onEnterPressed: root.performSearch()
            }

            Text {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: inputBackground.searchButtonSize +
                                     inputBackground.searchButtonInset + 14
                verticalAlignment: Text.AlignVCenter
                text: "搜索患者/记录/分析结果..."
                color: Theme.textWeak
                font.pixelSize: Math.max(16, Math.min(20, parent.height * 0.38))
                visible: searchField.length === 0 && !searchField.activeFocus
                renderType: Text.NativeRendering
            }

            Item {
                id: searchAction
                anchors {
                    right: parent.right
                    rightMargin: inputBackground.searchButtonInset
                    verticalCenter: parent.verticalCenter
                }
                width: inputBackground.searchButtonSize
                height: inputBackground.searchButtonSize

                HoverHandler {
                    id: searchHover
                    target: searchAction
                }

                TapHandler {
                    id: searchTap
                    onTapped: root.performSearch()
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: searchImage.width + 16
                    height: width
                    radius: width / 2
                    color: {
                        if (searchTap.pressed) return Theme.controlPressedLayer
                        if (searchHover.hovered) return Theme.controlHoverLayer
                        return "transparent"
                    }

                    Behavior on color {
                        ColorAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Image {
                    id: searchImage
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.8)
                    height: width
                    fillMode: Image.PreserveAspectFit
                    source: "qrc:/qt/qml/BSSASContent/images/search.png"
                    visible: false
                    smooth: true
                    mipmap: true
                }

                MultiEffect {
                    anchors.centerIn: parent
                    width: searchImage.width
                    height: searchImage.height
                    source: searchImage
                    transformOrigin: Item.Center
                    colorization: searchHover.hovered ? 0.45 : 0.0
                    colorizationColor: Theme.textWhite
                    brightness: searchHover.hovered ? 0.1 : 0.0
                    contrast: searchHover.hovered ? 1.1 : 1.0
                    scale: searchTap.pressed ? 0.96 : (searchHover.hovered ? 1.15 : 1.0)

                    Behavior on colorization { NumberAnimation { duration: 150 } }
                    Behavior on brightness { NumberAnimation { duration: 150 } }
                    Behavior on contrast { NumberAnimation { duration: 150 } }
                    Behavior on scale {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: inputBackground.searchButtonSize +
                                     inputBackground.searchButtonInset + 6
                acceptedButtons: Qt.LeftButton
                onClicked: searchField.forceActiveFocus()
            }
        }
    }

    GridLayout {
        id: overviewCardGrid
        anchors {
            top: heroImage.bottom
            topMargin: 16
            left: heroImage.left
            right: heroImage.right
        }
        columns: width < 760 ? 2 : 4
        columnSpacing: 18
        rowSpacing: 14
        height: implicitHeight
        z: 1
        readonly property real cardWidth: Math.max(
                                              0,
                                              (width - columnSpacing * (columns - 1)) / columns)
        readonly property real cardHeight: Math.max(
                                               96,
                                               Math.min(124, root.height * 0.14))

        Repeater {
            model: root.overviewCards

            delegate: Rectangle {
                id: metricCard
                property var cardData: modelData
                readonly property real contentMargin: Math.max(14, Math.min(20, height * 0.16))
                readonly property real iconBoxSize: Math.min(78, height - contentMargin * 2)
                readonly property var deltaInfo: root.metricDelta(
                                                     cardData.deltaCurrent,
                                                     cardData.deltaPrevious)

                Layout.preferredWidth: overviewCardGrid.cardWidth
                Layout.preferredHeight: overviewCardGrid.cardHeight
                radius: 18
                color: Theme.textWhite
                border.color: cardData.highlight ? Theme.primaryBorder : Theme.borderLight
                border.width: 1
                clip: true

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Math.max(0, parent.radius - 1)
                    color: cardData.highlight
                           ? Theme.withAlpha(Theme.primary, 0.035)
                           : "transparent"
                }

                Item {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        bottom: parent.bottom
                        leftMargin: metricCard.contentMargin
                        rightMargin: metricCard.contentMargin
                    }
                    readonly property real contentSpacing: Math.max(
                                                               12,
                                                               Math.min(18, metricCard.width * 0.05))

                    Item {
                        id: metricIconBox
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                        }
                        width: metricCard.iconBoxSize
                        height: metricCard.iconBoxSize

                        Image {
                            anchors.centerIn: parent
                            width: parent.width
                            height: parent.height
                            source: metricCard.cardData.icon
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }

                    ColumnLayout {
                        id: metricTextColumn
                        anchors {
                            left: metricIconBox.right
                            leftMargin: parent.contentSpacing
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: metricCard.cardData.title
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Math.max(13, Math.min(15, metricCard.height * 0.14))
                            font.bold: true
                            renderType: Text.NativeRendering
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: root.formatCount(metricCard.cardData.value)
                                color: Theme.textTitle
                                font.family: Theme.numberFontFamily
                                font.pixelSize: Math.max(
                                                    24,
                                                    Math.min(32, metricCard.height * 0.31))
                                font.bold: true
                                renderType: Text.NativeRendering
                            }

                            Text {
                                text: "例"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Math.max(13, Math.min(15, metricCard.height * 0.15))
                                renderType: Text.NativeRendering
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "较昨日"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Math.max(12, Math.min(14, metricCard.height * 0.13))
                                renderType: Text.NativeRendering
                            }

                            Text {
                                Layout.fillWidth: true
                                text: metricCard.deltaInfo.text
                                elide: Text.ElideRight
                                color: root.metricDeltaColor(
                                           metricCard.cardData,
                                           metricCard.deltaInfo)
                                font.family: Theme.fontFamily
                                font.pixelSize: Math.max(12, Math.min(14, metricCard.height * 0.13))
                                font.bold: metricCard.deltaInfo.trend !== 0
                                renderType: Text.NativeRendering
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        id: homeDetailPanels
        anchors {
            top: overviewCardGrid.bottom
            topMargin: 16
            left: heroImage.left
            right: heroImage.right
            bottom: parent.bottom
            bottomMargin: 18
        }
        spacing: 16
        z: 1

        Rectangle {
            id: recentSignalPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: homeDetailPanels.width * 0.56
            Layout.minimumHeight: 190
            radius: 22
            color: Theme.textWhite
            border.color: Theme.border
            border.width: 1
            clip: true

            ColumnLayout {
                anchors {
                    fill: parent
                    margins: 18
                }
                spacing: 12

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32

                    Rectangle {
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                        }
                        width: 5
                        height: 22
                        radius: 3
                        color: Theme.primary
                    }

                    Text {
                        anchors {
                            left: parent.left
                            leftMargin: 16
                            verticalCenter: parent.verticalCenter
                        }
                        text: "近期信号概览"
                        color: Theme.textTitle
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.bold: true
                        renderType: Text.NativeRendering
                    }
                }

                Image {
                    id: recentSignalWaveform
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 72
                    source: "qrc:/qt/qml/BSSASContent/image resources/home page/recent_signal_waveform_preview.png"
                    sourceClipRect: Qt.rect(root.recentSignalWaveformClipLeft,
                                            root.recentSignalWaveformClipTop,
                                            root.recentSignalWaveformClipWidth,
                                            root.recentSignalWaveformClipHeight)
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    smooth: true
                    mipmap: true
                }

                GridLayout {
                    id: recentSignalMetricGrid
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.recentSignalMetricHeight
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: root.recentSignalMetrics

                        delegate: Rectangle {
                            property var metricData: modelData
                            readonly property real metricContentMargin: Math.max(
                                                                            20,
                                                                            Math.min(36, width * 0.18))

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: Theme.pageBg
                            border.color: Theme.borderLight
                            border.width: 1

                            Column {
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    leftMargin: parent.metricContentMargin
                                    rightMargin: parent.metricContentMargin
                                    verticalCenter: parent.verticalCenter
                                }
                                spacing: 4

                                Text {
                                    width: parent.width
                                    text: metricData.label
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignLeft
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    renderType: Text.NativeRendering
                                }

                                Item {
                                    width: parent.width
                                    height: metricValueText.implicitHeight

                                    Text {
                                        id: metricValueText
                                        anchors.left: parent.left
                                        text: metricData.value
                                        color: Theme.textTitle
                                        font.family: Theme.numberFontFamily
                                        font.pixelSize: 21
                                        font.bold: true
                                        renderType: Text.NativeRendering
                                    }

                                    Text {
                                        anchors {
                                            left: metricValueText.right
                                            leftMargin: 5
                                            baseline: metricValueText.baseline
                                        }
                                        text: metricData.unit
                                        elide: Text.ElideRight
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        renderType: Text.NativeRendering
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: systemStatusPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: homeDetailPanels.width * 0.44
            Layout.minimumHeight: 190
            radius: 22
            color: Theme.textWhite
            border.color: Theme.border
            border.width: 1
            clip: true

            ColumnLayout {
                anchors {
                    fill: parent
                    margins: 18
                }
                spacing: root.systemStatusPanelSpacing

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32

                    Rectangle {
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                        }
                        width: 5
                        height: 22
                        radius: 3
                        color: Theme.primary
                    }

                    Text {
                        anchors {
                            left: parent.left
                            leftMargin: 16
                            verticalCenter: parent.verticalCenter
                        }
                        text: "系统状态"
                        color: Theme.textTitle
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.bold: true
                        renderType: Text.NativeRendering
                    }
                }

                Grid {
                    id: systemStatusGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: rowCount * tileHeight +
                                            spacing * (rowCount - 1)
                    columns: width < 420 ? 2 : 4
                    spacing: root.systemStatusTileSpacing
                    readonly property int rowCount: Math.ceil(root.systemStatusCards.length / columns)
                    readonly property real tileHeight: Math.min(
                                                           root.systemStatusTileHeight,
                                                           Math.max(
                                                               128,
                                                               (systemStatusPanel.height - 86 -
                                                                spacing * (rowCount - 1)) /
                                                               rowCount))

                    Repeater {
                        model: root.systemStatusCards

                        delegate: Rectangle {
                            property var statusData: modelData

                            width: (systemStatusGrid.width -
                                    systemStatusGrid.spacing * (systemStatusGrid.columns - 1)) /
                                   systemStatusGrid.columns
                            height: systemStatusGrid.tileHeight
                            radius: 10
                            color: Theme.pageBg
                            border.color: Theme.borderLight
                            border.width: 1
                            clip: true

                            Column {
                                anchors {
                                    centerIn: parent
                                }
                                width: parent.width - 18
                                spacing: root.systemStatusCardContentSpacing

                                Image {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: root.systemStatusIconSize
                                    height: root.systemStatusIconSize
                                    source: statusData.icon
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                }

                                Text {
                                    width: parent.width
                                    text: statusData.title
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 14
                                    renderType: Text.NativeRendering
                                }

                                Row {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    spacing: 6

                                    Text {
                                        text: statusData.value
                                        color: statusData.valueColor
                                        font.family: Theme.numberFontFamily
                                        font.pixelSize: Math.max(18, Math.min(24, parent.height * 0.23))
                                        font.bold: true
                                        renderType: Text.NativeRendering
                                    }

                                    Rectangle {
                                        visible: statusData.showDot
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: statusData.valueColor
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: resultRevealArea
        visible: hasSearched || height > 0
        width: Math.min(searchContainer.width, parent.width - popupMargin * 2)
        height: hasSearched ? targetHeight : 0
        clip: true
        x: Math.max(popupMargin,
                    Math.min(searchContainer.x, parent.width - width - popupMargin))
        y: searchContainer.y + searchContainer.height + 12
        z: 3
        readonly property real popupMargin: 20
        readonly property real targetHeight: Math.max(
                                                 190,
                                                 Math.min(
                                                     parent.height - y - 18,
                                                     resultsList.contentHeight + 148))

        Behavior on height {
            NumberAnimation {
                duration: 480
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            id: resultPanel
            width: parent.width
            height: resultRevealArea.targetHeight
            radius: 28
            y: hasSearched ? 0 : -32
            opacity: hasSearched ? 1 : 0
            color: Theme.pageBg
            border.color: Theme.border
            border.width: 1

            Behavior on y {
                NumberAnimation {
                    duration: 520
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 240
                    easing.type: Easing.OutCubic
                }
            }

            Text {
                id: resultTitle
                anchors {
                    top: parent.top
                    topMargin: 22
                    left: parent.left
                    leftMargin: 24
                }
                text: ""
                font.pixelSize: 24
                font.bold: true
                color: Theme.textTitle
                renderType: Text.NativeRendering
            }

            Rectangle {
                id: resultCountBadge
                anchors {
                    right: parent.right
                    rightMargin: 24
                    verticalCenter: resultTitle.verticalCenter
                }
                height: 30
                width: Math.max(72, resultCountText.implicitWidth + 26)
                radius: height / 2
                color: Theme.primaryLight

                Text {
                    id: resultCountText
                    anchors.centerIn: parent
                    text: hasSearched ? (searchResults.length + " ") : "0 "
                    font.pixelSize: 14
                    font.bold: true
                    color: Theme.primary
                    renderType: Text.NativeRendering
                }
            }

            Text {
                id: helperText
                anchors {
                    top: resultTitle.bottom
                    topMargin: 10
                    left: resultTitle.left
                    right: parent.right
                    rightMargin: 24
                }
                text: helperMessage
                wrapMode: Text.Wrap
                font.pixelSize: 15
                color: Theme.textMuted
                renderType: Text.NativeRendering
            }

            Text {
                id: databasePathText
                anchors {
                    top: helperText.bottom
                    topMargin: 8
                    left: resultTitle.left
                    right: parent.right
                    rightMargin: 24
                }
                text: "数据库路径：" + databaseManager.databaseFilePath
                elide: Text.ElideMiddle
                font.pixelSize: 13
                color: Theme.textWeak
                renderType: Text.NativeRendering
            }

            Rectangle {
                id: divider
                anchors {
                    top: databasePathText.bottom
                    topMargin: 14
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
                    top: divider.bottom
                    topMargin: 14
                    left: parent.left
                    leftMargin: 18
                    right: parent.right
                    rightMargin: 12
                    bottom: parent.bottom
                    bottomMargin: 16
                }
                clip: true
                model: searchResults
                spacing: 12
                visible: searchResults.length > 0

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOff
                }

                delegate: Rectangle {
                    property var resultItem: modelData

                    width: resultsList.width - 12
                    height: resultColumn.implicitHeight + 28
                    radius: 20
                    color: Theme.pageBg
                    border.color: Theme.border
                    border.width: 1

                    Column {
                        id: resultColumn
                        anchors {
                            fill: parent
                            margins: 14
                        }
                        spacing: 10

                        Row {
                            width: parent.width
                            spacing: 10

                            Text {
                                width: parent.width - typeBadge.width - 16
                                text: resultItem.fileName
                                elide: Text.ElideMiddle
                                font.pixelSize: 18
                                font.bold: true
                                color: Theme.textTitle
                                renderType: Text.NativeRendering
                            }

                            Rectangle {
                                id: typeBadge
                                width: Math.max(52, typeLabelText.implicitWidth + 18)
                                height: 28
                                radius: 14
                                color: Theme.primaryLight

                                Text {
                                    id: typeLabelText
                                    anchors.centerIn: parent
                                    text: resultItem.typeLabel
                                    font.pixelSize: 13
                                    font.bold: true
                                    color: Theme.primary
                                    renderType: Text.NativeRendering
                                }
                            }
                        }

                        TextEdit {
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: true
                            wrapMode: TextEdit.WrapAnywhere
                            textFormat: TextEdit.PlainText
                            text: "分类：" + resultItem.category +
                                  "    路径：" + resultItem.relativePath
                            font.pixelSize: 13
                            color: Theme.textMuted
                            selectedTextColor: Theme.textWhite
                            selectionColor: Theme.primary
                            cursorVisible: activeFocus
                            renderType: Text.NativeRendering
                        }

                        TextEdit {
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: true
                            wrapMode: TextEdit.WrapAnywhere
                            textFormat: TextEdit.PlainText
                            text: "绝对路径：" + resultItem.absolutePath
                            font.pixelSize: 14
                            color: Theme.textSecondary
                            selectedTextColor: Theme.textWhite
                            selectionColor: Theme.primary
                            cursorVisible: activeFocus
                            renderType: Text.NativeRendering
                        }
                    }
                }
            }

            Item {
                anchors {
                    top: divider.bottom
                    topMargin: 14
                    left: parent.left
                    leftMargin: 24
                    right: parent.right
                    rightMargin: 24
                    bottom: parent.bottom
                    bottomMargin: 20
                }
                visible: searchResults.length === 0

                Text {
                    anchors.centerIn: parent
                    width: parent.width * 0.7
                    horizontalAlignment: Text.AlignHCenter
                    text: helperMessage
                    wrapMode: Text.Wrap
                    font.pixelSize: 18
                    color: Theme.textWeak
                    renderType: Text.NativeRendering
                }
            }
        }
    }
}
