/**
 * @file Medical_Record_Management.qml
 * @brief 病历管理页面。统一管理受试者信息录入、采集参数配置、算法分析结果与综合结论表单。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BSSAS
import MangoComponent

Page {
    id: root

    property bool pageActive: true
    property real pageMargin: 30
    property date recordingDate: new Date()
    property int recordingHour: new Date().getHours()
    property int recordingMinute: new Date().getMinutes()

    /**
     * @brief 数字补零为两位，如 3 -> "03"。
     * @param value 待补零的数字
     * @returns 两位数字字符串
     */
    function padNumber(value) {
        return value.toString().padStart(2, "0")
    }

    /**
     * @brief 格式化采集日期为 "yyyy 年 MM 月 dd 日" 格式。
     * @param value Date 对象
     * @returns 格式化后的日期字符串
     */
    function formatRecordingDate(value) {
        return Qt.formatDate(value, "yyyy 年 MM 月 dd 日")
    }

    /**
     * @brief 格式化采集时间为 "HH 时 MM 分" 格式。
     * @param hour 小时
     * @param minute 分钟
     * @returns 格式化后的时间字符串
     */
    function formatRecordingTime(hour, minute) {
        return padNumber(hour) + " 时 " + padNumber(minute) + " 分"
    }

    /**
     * @brief 解析选择框与文本框的组合值：优先取文本框输入，为空时取选择框显示文本。
     * @param selectControl 下拉选择框控件
     * @param textControl 文本输入框控件
     * @returns 最终的有效取值字符串
     */
    function resolvedSelectValue(selectControl, textControl) {
        const customText = textControl.text.trim()
        return customText.length > 0 ? customText : selectControl.displayText
    }

    function textValue(control) {
        return control.text.trim()
    }

    function fileName(filePath) {
        if (!filePath || filePath.length === 0) {
            return ""
        }
        const normalizedPath = filePath.replace(/\\/g, "/")
        const pathSegments = normalizedPath.split("/")
        return pathSegments.length > 0 ? pathSegments[pathSegments.length - 1] : filePath
    }

    function optionalSelectValue(selectControl, textControl) {
        const customText = textControl.text.trim()
        if (customText.length > 0) {
            return customText
        }
        return selectControl.currentIndex <= 0 ? "" : selectControl.displayText
    }

    function optionValue(index, values) {
        return index >= 0 && index < values.length ? values[index] : ""
    }

    function minutesToSecondsText(valueText) {
        const value = Number(valueText)
        if (!Number.isFinite(value) || valueText.trim().length === 0) {
            return ""
        }
        const seconds = value * 60
        return Number.isInteger(seconds) ? seconds.toString() : seconds.toFixed(1)
    }

    function collectFormData() {
        const durationSeconds = root.minutesToSecondsText(durationField.text)
        const eatingStatus = root.optionValue(
            eatingStatusSelect.currentIndex,
            ["Fasting", "Postprandial", "Postoperative"])
        const bodyPositionOptions = [
            { body: "Supine", side: "", other: "" },
            { body: "Sitting", side: "", other: "" },
            { body: "Lateral Decubitus", side: "Left", other: "" },
            { body: "Lateral Decubitus", side: "Right", other: "" },
            { body: "Other", side: "", other: bodyPositionSelect.displayText }
        ]
        const bodyPosition = bodyPositionOptions[Math.max(
            0,
            Math.min(bodyPositionSelect.currentIndex, bodyPositionOptions.length - 1))]
        const subjectStatus = root.optionValue(
            subjectStatusSelect.currentIndex,
            ["Awake", "Sedated", "Other"])
        const eatingHours = root.textValue(eatingHoursField)

        return {
            "subject_id": root.textValue(subjectIdField),
            "sex": maleCheck.checked ? "Male" : (femaleCheck.checked ? "Female" : ""),
            "age": root.textValue(ageField),
            "height": root.textValue(heightField),
            "weight": root.textValue(weightField),
            "medical_history": root.optionalSelectValue(medicalHistorySelect, medicalHistoryField),
            "symptoms": root.optionalSelectValue(symptomSelect, symptomField),
            "record_year": Qt.formatDate(root.recordingDate, "yyyy"),
            "record_month": Qt.formatDate(root.recordingDate, "MM"),
            "record_day": Qt.formatDate(root.recordingDate, "dd"),
            "examiner": root.textValue(examinerField),
            "remarks": root.textValue(remarksField),
            "sampling_rate": root.textValue(samplingRateField),
            "bit_depth": root.textValue(bitDepthField),
            "env_type": root.optionValue(
                envTypeSelect.currentIndex,
                ["Quiet Laboratory", "Ward", "Other"]),
            "env_other": root.textValue(envOtherField),
            "specific_point": root.textValue(specificPointField),
            "duration_per_quadrant": durationSeconds,
            "total_recording_duration": durationSeconds,
            "is_continuous": true,
            "interval_s": "",
            "eating_status": eatingStatus,
            "fasting_h": eatingStatus === "Fasting" ? eatingHours : "",
            "postprandial_h": eatingStatus === "Postprandial" ? eatingHours : "",
            "postoperative_h": eatingStatus === "Postoperative" ? eatingHours : "",
            "body_position": bodyPosition.body,
            "lateral_side": bodyPosition.side,
            "position_other": bodyPosition.other,
            "subject_status": subjectStatus,
            "subject_status_other": subjectStatus === "Other" ? subjectStatusSelect.displayText : "",
            "eval_overall": root.optionValue(overallEvalSelect.currentIndex, ["Normal", "Abnormal"]),
            "feature_tip": root.optionValue(
                featureTipSelect.currentIndex,
                ["Decreased", "Normal", "Increased", "Abnormal"]),
            "clinical_analysis": clinicalAnalysisArea.text.trim(),
            "conclusion_text": conclusionArea.text.trim(),
            "recommendation_text": recommendationArea.text.trim(),
            "appendix_content": appendixArea.text.trim(),
            "associated_diseases": [],
            "quadrants": []
        }
    }

    function clearForm() {
        subjectIdField.text = ""
        maleCheck.checked = false
        femaleCheck.checked = false
        ageField.text = ""
        heightField.text = ""
        weightField.text = ""
        medicalHistorySelect.currentIndex = 0
        medicalHistoryField.text = ""
        symptomSelect.currentIndex = 0
        symptomField.text = ""
        const now = new Date()
        root.recordingDate = now
        root.recordingHour = now.getHours()
        root.recordingMinute = now.getMinutes()
        examinerField.text = ""
        remarksField.text = ""
        samplingRateField.text = ""
        bitDepthField.text = ""
        envTypeSelect.currentIndex = 0
        envOtherField.text = ""
        specificPointField.text = ""
        durationField.text = ""
        eatingStatusSelect.currentIndex = 0
        eatingHoursField.text = ""
        bodyPositionSelect.currentIndex = 0
        subjectStatusSelect.currentIndex = 0
        overallEvalSelect.currentIndex = 0
        featureTipSelect.currentIndex = 0
        clinicalAnalysisArea.text = ""
        conclusionArea.text = ""
        recommendationArea.text = ""
        appendixArea.text = ""
    }

    /**
     * @brief 表单文本输入框组件，预置样式、圆角、颜色与浮动占位符。
     */
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

    /**
     * @brief 表单多行文本域组件，带自定义占位符叠加层与焦点高亮边框。
     */
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

    /**
     * @brief 日期/时间选择触发按钮，展示格式化文本并在点击时打开对应选择弹窗。
     */
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

                                        CheckBoxes {
                                            id: maleCheck
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            text: "男（Male）"
                                            interactive: false
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

                                        CheckBoxes {
                                            id: femaleCheck
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            text: "女（Female）"
                                            interactive: false
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

            Item {
                Layout.fillWidth: true
                visible: jointExportManager.active
                implicitHeight: visible ? actionCard.implicitHeight : 0

                Rectangle {
                    id: actionCard
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 980)
                    implicitHeight: actionRow.implicitHeight + 28
                    radius: 18
                    color: Theme.pageBg
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        id: actionRow
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            id: clearJointExportButton
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: 40
                            text: qsTr("清空")
                            hoverEnabled: true

                            contentItem: Text {
                                text: clearJointExportButton.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                font.bold: true
                                color: Theme.primary
                            }

                            background: Rectangle {
                                radius: 20
                                color: clearJointExportButton.down
                                    ? Theme.primaryLight
                                    : (clearJointExportButton.hovered ? Theme.primaryLighter : Theme.pageBg)
                                border.width: 1
                                border.color: Theme.primaryBorder
                            }

                            onClicked: root.clearForm()
                        }

                        Button {
                            id: saveJointExportButton
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: 40
                            enabled: !jointExportManager.saving
                            text: qsTr("保存")
                            hoverEnabled: true

                            contentItem: Text {
                                text: saveJointExportButton.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                font.bold: true
                                color: Theme.textWhite
                            }

                            background: Rectangle {
                                radius: 20
                                color: saveJointExportButton.enabled
                                    ? (saveJointExportButton.down
                                        ? Qt.darker(Theme.primary, 1.08)
                                        : (saveJointExportButton.hovered
                                            ? Qt.darker(Theme.primary, 1.04)
                                            : Theme.primary))
                                    : Theme.disabledBg
                            }

                            onClicked: {
                                jointExportManager.saveWithMedicalRecord(root.collectFormData())
                            }
                        }
                    }
                }
            }
        }
    }

    ToastNotification {
        id: pageToast
        duration: 2400
    }

    Connections {
        target: jointExportManager

        function onCompleted(outputDirectory) {
            const folderName = root.fileName(outputDirectory)
            pageToast.showSuccess(
                folderName.length > 0
                    ? qsTr("联合导出完成：") + folderName
                    : qsTr("联合导出完成"))
        }

        function onFailed(errorMessage) {
            pageToast.showError(
                errorMessage.length > 0 ? errorMessage : qsTr("联合导出失败"))
        }
    }

    Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 260
        height: 220
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: jointExportManager.saving

        Overlay.modal: BlurGlass {
            blurSource: ApplicationWindow.window ? ApplicationWindow.window.contentItem : null
            blurAmount: 64
            borderRadius: 25
            overlayOpacity: 0.3
        }

        background: Rectangle {
            radius: 24
            color: Theme.primaryLighter
            border.width: 1
            border.color: Theme.primaryBorder
        }

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 20

            StandbyAnimation {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 78
                height: 78
                blockColor: Theme.primary
                running: jointExportManager.saving
            }

            Text {
                width: 200
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: jointExportManager.statusMessage.length > 0
                    ? jointExportManager.statusMessage
                    : qsTr("联合导出处理中...")
                color: Theme.textPrimary
                font.pixelSize: 16
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
