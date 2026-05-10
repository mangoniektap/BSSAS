import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS
import MangoComponent

Page {
    id: root

    property real pageMargin: 30
    property date recordingDate: new Date()
    property int recordingHour: new Date().getHours()
    property int recordingMinute: new Date().getMinutes()

    function padNumber(value) {
        return value.toString().padStart(2, "0")
    }

    function formatRecordingDate(value) {
        return Qt.formatDate(value, "yyyy 年 MM 月 dd 日")
    }

    function formatRecordingTime(hour, minute) {
        return padNumber(hour) + " 时 " + padNumber(minute) + " 分"
    }

    function resolvedSelectValue(selectControl, textControl) {
        const customText = textControl.text.trim()
        return customText.length > 0 ? customText : selectControl.displayText
    }

    component FormTextField : TextFields {
        type: "outlined"
        fieldHeight: 52
        cornerRadius: 14
        contentPadding: 14
        iconPadding: 14
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
        contentColor: Theme.textPrimary
        placeholderColor: Theme.textWeak
        inactiveLabelColor: Theme.textWeak
        inactiveOutlineColor: Theme.primaryBorder
        activeAccentColor: Theme.primary
        fieldBackgroundColor: enabled ? Theme.inputBg : Theme.disabledBg
        labelBackgroundColor: fieldBackgroundColor
        floatPlaceholder: true
        selectByMouse: true
    }

    component FormTextArea : TextArea {
        id: control
        implicitHeight: 110
        leftPadding: 14
        rightPadding: 14
        topPadding: 14
        bottomPadding: 14
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
        color: Theme.textPrimary
        selectByMouse: true
        wrapMode: TextArea.Wrap
        readonly property color placeholderOverlayColor: Theme.textWeak
        placeholderTextColor: "transparent"

        background: Rectangle {
            radius: 16
            color: control.enabled ? Theme.inputBg : Theme.disabledBg
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? Theme.primary : Theme.primaryBorder

            Text {
                visible: control.text.length === 0
                x: control.leftPadding
                anchors.verticalCenter: parent.verticalCenter
                width: control.width - (control.leftPadding + control.rightPadding)
                text: control.placeholderText
                color: control.placeholderOverlayColor
                font.family: control.font.family
                font.pixelSize: control.font.pixelSize
                font.weight: control.font.weight
                elide: Text.ElideRight
                clip: true
                renderType: control.renderType
                verticalAlignment: Text.AlignVCenter
            }

            Behavior on border.color {
                ColorAnimation { duration: 150 }
            }
        }
    }

    component PickerButton : Button {
        id: control
        implicitHeight: 52

        contentItem: Text {
            text: control.text
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
            color: Theme.textPrimary
            x: 14
            width: control.availableWidth - 28
            height: control.availableHeight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 14
            color: control.down ? Theme.primaryLight : Theme.inputBg
            border.width: 1
            border.color: control.hovered ? Theme.primaryBorder : Theme.primaryBorder

            Behavior on border.color {
                ColorAnimation { duration: 150 }
            }
        }
    }

    anchors.fill: parent

    background: Rectangle {
        color: "transparent"
    }

    Text {
        id: pageTitle
        anchors {
            top: parent.top
            left: parent.left
            margins: root.pageMargin
        }
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontPageTitle; font.weight: Font.Bold
        text: "基于多特征联合检测的肠鸣音分析报告"
        renderType: Text.NativeRendering
        color: Theme.primary
    }

    ScrollView {
        id: formScrollView
        anchors {
            top: pageTitle.bottom
            topMargin: 18
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: root.pageMargin
            rightMargin: root.pageMargin
            bottomMargin: root.pageMargin
        }
        clip: true
        contentWidth: availableWidth

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: scrollContent
            width: formScrollView.availableWidth
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: tipText.implicitHeight + 28
                radius: 18
                color: Theme.heroBgEnd
                border.width: 1
                border.color: Theme.primaryBorder

                Text {
                    id: tipText
                    anchors.fill: parent
                    anchors.margins: 14
                    text: "请填写受试者基础信息及检测条件。分析结果将在处理完成后自动填入，临床医师可根据分析结果填写最终结论。"
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: formCard.implicitHeight

                Rectangle {
                    id: formCard
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 980)
                    implicitHeight: formCardContent.implicitHeight + 52
                    radius: 24
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: formCardContent
                        anchors.fill: parent
                        anchors.margins: 26
                        spacing: 22

                        Text {
                            Layout.fillWidth: true
                            text: "1. 受试者信息录入"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCardTitle
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            id: formGrid
                            Layout.fillWidth: true
                            columns: formCard.width > 860 ? 2 : 1
                            columnSpacing: 24
                            rowSpacing: 20

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "受试者编号（Subject ID）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextField { id: subjectIdField; Layout.fillWidth: true; placeholderText: "请输入 Subject ID" }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "性别（Sex）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 52
                                        radius: 14
                                        color: maleCheck.checked ? Theme.primaryLight : Theme.inputBg
                                        border.width: 1
                                        border.color: maleCheck.checked ? Theme.primary : Theme.primaryBorder

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 8
                                            CheckBoxes { id: maleCheck; interactive: false }
                                            Text {
                                                Layout.fillWidth: true
                                                text: "男（Male）"
                                                color: Theme.textPrimary
                                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                maleCheck.checked = !maleCheck.checked
                                                if (maleCheck.checked)
                                                    femaleCheck.checked = false
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 52
                                        radius: 14
                                        color: femaleCheck.checked ? Theme.primaryLight : Theme.inputBg
                                        border.width: 1
                                        border.color: femaleCheck.checked ? Theme.primary : Theme.primaryBorder

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 8
                                            CheckBoxes { id: femaleCheck; interactive: false }
                                            Text {
                                                Layout.fillWidth: true
                                                text: "女（Female）"
                                                color: Theme.textPrimary
                                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                femaleCheck.checked = !femaleCheck.checked
                                                if (femaleCheck.checked)
                                                    maleCheck.checked = false
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "年龄（Age）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    FormTextField {
                                        id: ageField
                                        Layout.fillWidth: true
                                        placeholderText: "请输入年龄"
                                        validator: IntValidator { bottom: 0; top: 150 }
                                    }
                                    Text { text: "岁（Years）"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "身高 / 体重（Height / Weight）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    FormTextField {
                                        id: heightField
                                        Layout.fillWidth: true
                                        placeholderText: "身高"
                                        validator: DoubleValidator { bottom: 0; decimals: 2 }
                                    }
                                    Text { text: "cm"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                    FormTextField {
                                        id: weightField
                                        Layout.fillWidth: true
                                        placeholderText: "体重"
                                        validator: DoubleValidator { bottom: 0; decimals: 2 }
                                    }
                                    Text { text: "kg"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid.columns
                                spacing: 10
                                Text { text: "既往病史（Medical History）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 720 ? 2 : 1
                                    columnSpacing: 12
                                    rowSpacing: 12

                                    Select {
                                        id: medicalHistorySelect
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                        currentIndex: 0
                                        delegateHeight: 34
                                        visibleCount: 5
                                        popupPadding: 6
                                        textColor: Theme.textPrimary
                                        outlineColor: Theme.primaryBorder
                                        activeOutlineColor: Theme.primary
                                        indicatorColor: Theme.primary
                                        optionHighlightColor: Theme.primaryLight
                                        popupColor: Theme.inputBg
                                        popupBorderColor: Theme.primaryBorder
                                        model: ["无", "腹痛", "腹胀", "肠道病史", "术后恢复期", "慢性疾病", "其他"]
                                    }

                                    FormTextField {
                                        id: medicalHistoryField
                                        Layout.fillWidth: true
                                        placeholderText: "如未列出，请在此输入；输入内容优先"
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "当前取值：" + root.resolvedSelectValue(medicalHistorySelect, medicalHistoryField)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    wrapMode: Text.Wrap
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid.columns
                                spacing: 10
                                Text { text: "当前症状（Symptoms）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 720 ? 2 : 1
                                    columnSpacing: 12
                                    rowSpacing: 12

                                    Select {
                                        id: symptomSelect
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                        currentIndex: 0
                                        delegateHeight: 34
                                        visibleCount: 6
                                        popupPadding: 6
                                        textColor: Theme.textPrimary
                                        outlineColor: Theme.primaryBorder
                                        activeOutlineColor: Theme.primary
                                        indicatorColor: Theme.primary
                                        optionHighlightColor: Theme.primaryLight
                                        popupColor: Theme.inputBg
                                        popupBorderColor: Theme.primaryBorder
                                        model: ["无", "腹痛", "腹胀", "腹泻", "便秘", "恶心", "呕吐", "其他"]
                                    }

                                    FormTextField {
                                        id: symptomField
                                        Layout.fillWidth: true
                                        placeholderText: "如未列出，请在此输入；输入内容优先"
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "当前取值：" + root.resolvedSelectValue(symptomSelect, symptomField)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    wrapMode: Text.Wrap
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid.columns
                                spacing: 10
                                Text { text: "检查日期（Date of Recording）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 720 ? 2 : 1
                                    columnSpacing: 12
                                    rowSpacing: 12

                                    PickerButton {
                                        Layout.fillWidth: true
                                        text: root.formatRecordingDate(root.recordingDate)
                                        onClicked: {
                                            dateSelection.selectedDate = root.recordingDate
                                            dateSelection.open()
                                        }
                                    }

                                    PickerButton {
                                        Layout.fillWidth: true
                                        text: root.formatRecordingTime(root.recordingHour, root.recordingMinute)
                                        onClicked: {
                                            timeSelection.hour = root.recordingHour
                                            timeSelection.minute = root.recordingMinute
                                            timeSelection.open()
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "已选时间：" + root.formatRecordingDate(root.recordingDate) + "  " + root.formatRecordingTime(root.recordingHour, root.recordingMinute)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    wrapMode: Text.Wrap
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "检测人员（Examiner）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextField { id: examinerField; Layout.fillWidth: true; placeholderText: "请输入检测人员姓名" }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "备注（Remarks）"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextField { id: remarksField; Layout.fillWidth: true; placeholderText: "可填写补充说明" }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: summaryColumn.implicitHeight + 24
                            radius: 18
                            color: Theme.heroBgEnd
                            border.width: 1
                            border.color: Theme.primaryBorder

                            ColumnLayout {
                                id: summaryColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: "录入摘要"
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSectionTitle
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    text: "Subject ID: " + (subjectIdField.text.trim().length > 0 ? subjectIdField.text.trim() : "未填写")
                                          + " | Sex: " + (maleCheck.checked ? "男 Male" : (femaleCheck.checked ? "女 Female" : "未选择"))
                                          + " | Age: " + (ageField.text.trim().length > 0 ? ageField.text.trim() + " 岁" : "未填写")
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    text: "Medical History: " + root.resolvedSelectValue(medicalHistorySelect, medicalHistoryField)
                                          + " | Symptoms: " + root.resolvedSelectValue(symptomSelect, symptomField)
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    text: "Date of Recording: " + root.formatRecordingDate(root.recordingDate)
                                          + "  " + root.formatRecordingTime(root.recordingHour, root.recordingMinute)
                                          + " | Examiner: " + (examinerField.text.trim().length > 0 ? examinerField.text.trim() : "未填写")
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: formCard2.implicitHeight

                Rectangle {
                    id: formCard2
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 980)
                    implicitHeight: formCardContent2.implicitHeight + 52
                    radius: 24
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: formCardContent2
                        anchors.fill: parent
                        anchors.margins: 26
                        spacing: 22

                        Text {
                            Layout.fillWidth: true
                            text: "2. 采集参数"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCardTitle
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            id: formGrid2
                            Layout.fillWidth: true
                            columns: formCard2.width > 860 ? 2 : 1
                            columnSpacing: 24
                            rowSpacing: 20

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "采样率 Sampling Rate"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    FormTextField { id: samplingRateField; Layout.fillWidth: true; placeholderText: "4000"; validator: IntValidator { bottom: 0 } }
                                    Text { text: "Hz"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "位深 Bit Depth"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    FormTextField { id: bitDepthField; Layout.fillWidth: true; placeholderText: "16"; validator: IntValidator { bottom: 0 } }
                                    Text { text: "bit"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid2.columns
                                spacing: 10
                                Text { text: "采集环境 Environment Type"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 720 ? 2 : 1
                                    columnSpacing: 12
                                    rowSpacing: 12

                                    Select {
                                        id: envTypeSelect
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                        currentIndex: 0
                                        delegateHeight: 34
                                        visibleCount: 3
                                        popupPadding: 6
                                        textColor: Theme.textPrimary
                                        outlineColor: Theme.primaryBorder
                                        activeOutlineColor: Theme.primary
                                        indicatorColor: Theme.primary
                                        optionHighlightColor: Theme.primaryLight
                                        popupColor: Theme.inputBg
                                        popupBorderColor: Theme.primaryBorder
                                        model: ["实验室 Quiet Laboratory", "病房 Ward", "其他 Other"]
                                    }

                                    FormTextField {
                                        id: envOtherField
                                        Layout.fillWidth: true
                                        placeholderText: "请补充环境说明"
                                        visible: envTypeSelect.currentIndex === 2
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "采集部位 Specific Point"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextField { id: specificPointField; Layout.fillWidth: true; placeholderText: "例如：右下腹、脐周" }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "记录时长 Duration"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    FormTextField {
                                        id: durationField
                                        Layout.fillWidth: true
                                        placeholderText: "请输入记录时长"
                                        validator: DoubleValidator { bottom: 0; decimals: 1 }
                                    }
                                    Text { text: "min"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid2.columns
                                spacing: 10
                                Text { text: "进食状态 Eating Status"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 720 ? 2 : 1
                                    columnSpacing: 12
                                    rowSpacing: 12

                                    Select {
                                        id: eatingStatusSelect
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                        currentIndex: 0
                                        delegateHeight: 34
                                        visibleCount: 3
                                        popupPadding: 6
                                        textColor: Theme.textPrimary
                                        outlineColor: Theme.primaryBorder
                                        activeOutlineColor: Theme.primary
                                        indicatorColor: Theme.primary
                                        optionHighlightColor: Theme.primaryLight
                                        popupColor: Theme.inputBg
                                        popupBorderColor: Theme.primaryBorder
                                        model: ["空腹 Fasting", "餐后 Postprandial", "术后 Postoperative"]
                                    }

                                    FormTextField { id: eatingHoursField; Layout.fillWidth: true; placeholderText: "距离进食时间 (h)" }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "体位 Body Position"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                Select {
                                    id: bodyPositionSelect
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    currentIndex: 0
                                    delegateHeight: 34
                                    visibleCount: 5
                                    popupPadding: 6
                                    textColor: Theme.textPrimary
                                    outlineColor: Theme.primaryBorder
                                    activeOutlineColor: Theme.primary
                                    indicatorColor: Theme.primary
                                    optionHighlightColor: Theme.primaryLight
                                    popupColor: Theme.inputBg
                                    popupBorderColor: Theme.primaryBorder
                                    model: ["仰卧位 Supine", "坐位 Sitting", "左侧卧位 Left Lateral", "右侧卧位 Right Lateral", "其他 Other"]
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "受试者状态 Subject Status"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                Select {
                                    id: subjectStatusSelect
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    currentIndex: 0
                                    delegateHeight: 34
                                    visibleCount: 3
                                    popupPadding: 6
                                    textColor: Theme.textPrimary
                                    outlineColor: Theme.primaryBorder
                                    activeOutlineColor: Theme.primary
                                    indicatorColor: Theme.primary
                                    optionHighlightColor: Theme.primaryLight
                                    popupColor: Theme.inputBg
                                    popupBorderColor: Theme.primaryBorder
                                    model: ["清醒 Awake", "镇静 Sedated", "其他 Other"]
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: formCard3.implicitHeight

                Rectangle {
                    id: formCard3
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 980)
                    implicitHeight: formCardContent3.implicitHeight + 52
                    radius: 24
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: formCardContent3
                        anchors.fill: parent
                        anchors.margins: 26
                        spacing: 22

                        Text {
                            Layout.fillWidth: true
                            text: "3-5. 算法分析结果"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCardTitle
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 80
                            radius: 14
                            color: Theme.disabledBg
                            border.width: 1
                            border.color: Theme.borderLight

                            Text {
                                anchors.centerIn: parent
                                anchors.margins: 16
                                width: parent.width - 32
                                text: "这里预留给 AI 识别结果、频谱分析图和结构化诊断内容。"
                                color: Theme.textWeak
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: formCard4.implicitHeight

                Rectangle {
                    id: formCard4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 980)
                    implicitHeight: formCardContent4.implicitHeight + 52
                    radius: 24
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: formCardContent4
                        anchors.fill: parent
                        anchors.margins: 26
                        spacing: 22

                        Text {
                            Layout.fillWidth: true
                            text: "6-7. 综合结论"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCardTitle
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            id: formGrid4
                            Layout.fillWidth: true
                            columns: formCard4.width > 860 ? 2 : 1
                            columnSpacing: 24
                            rowSpacing: 20

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "总体评估 Overall Evaluation"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                Select {
                                    id: overallEvalSelect
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    currentIndex: 0
                                    delegateHeight: 34
                                    visibleCount: 2
                                    popupPadding: 6
                                    textColor: Theme.textPrimary
                                    outlineColor: Theme.primaryBorder
                                    activeOutlineColor: Theme.primary
                                    indicatorColor: Theme.primary
                                    optionHighlightColor: Theme.primaryLight
                                    popupColor: Theme.inputBg
                                    popupBorderColor: Theme.primaryBorder
                                    model: ["正常 Normal", "异常 Abnormal"]
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: "声学特征提示 Acoustic Feature Tips"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                Select {
                                    id: featureTipSelect
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal
                                    currentIndex: 0
                                    delegateHeight: 34
                                    visibleCount: 4
                                    popupPadding: 6
                                    textColor: Theme.textPrimary
                                    outlineColor: Theme.primaryBorder
                                    activeOutlineColor: Theme.primary
                                    indicatorColor: Theme.primary
                                    optionHighlightColor: Theme.primaryLight
                                    popupColor: Theme.inputBg
                                    popupBorderColor: Theme.primaryBorder
                                    model: ["肠鸣音偏弱", "肠鸣音正常", "肠鸣音增强", "异常肠鸣音高风险"]
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid4.columns
                                spacing: 10
                                Text { text: "临床症状分析 Clinical Symptoms Analysis"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextArea {
                                    id: clinicalAnalysisArea
                                    Layout.fillWidth: true
                                    placeholderText: "结合症状填写详细分析..."
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid4.columns
                                spacing: 10
                                Text { text: "最终结论 Final Conclusion"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextArea {
                                    id: conclusionArea
                                    Layout.fillWidth: true
                                    placeholderText: "请明确描述定位范围、主要表现与结论"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid4.columns
                                spacing: 10
                                Text { text: "建议 Recommendation"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextArea {
                                    id: recommendationArea
                                    Layout.fillWidth: true
                                    placeholderText: "填写医生建议..."
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: formGrid4.columns
                                spacing: 10
                                Text { text: "附录 Appendix"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontBody; font.weight: Font.Normal }
                                FormTextArea {
                                    id: appendixArea
                                    Layout.fillWidth: true
                                    placeholderText: "补充图表说明或附加信息..."
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    DateSelection {
        id: dateSelection
        onAccepted: function(date) {
            root.recordingDate = date
        }
    }

    TimeSelection {
        id: timeSelection
        is24Hour: true
        onAccepted: function(hour, minute) {
            root.recordingHour = hour
            root.recordingMinute = minute
        }
    }
}
