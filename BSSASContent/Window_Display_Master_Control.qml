import QtQuick
import BSSAS
Item {
    id: window_display_master_control
    x: mainwindow_background.display_area.x
    y: mainwindow_background.display_area.y
    width: mainwindow_background.display_area.width
    height: mainwindow_background.display_area.height
    clip: true

    property int currentIndex: left_toolbar.display_content_selection
    property string pageLoadError: ""
    readonly property var pageSources: [
        "./display_content/Home_Page.qml",
        "./display_content/Software_Master_Control.qml",
        "./display_content/Temporal_Spectral_Monitoring.qml",
        "./display_content/Medical_Record_Management.qml",
        "./display_content/Auxiliary_Diagnosis.qml",
        "./display_content/About_Page.qml"
    ]

    Item {
        id: pageContainer
        x: 0
        y: 0
        width: parent.width
        height: parent.height
        opacity: 1

        Loader {
            id: pageLoader
            anchors.fill: parent
            source: (
                window_display_master_control.currentIndex >= 0
                && window_display_master_control.currentIndex < window_display_master_control.pageSources.length
            ) ? window_display_master_control.pageSources[window_display_master_control.currentIndex] : ""

            onSourceChanged: {
                window_display_master_control.pageLoadError = ""
            }

            onStatusChanged: {
                if (status === Loader.Error) {
                    window_display_master_control.pageLoadError = "Failed to load page source: " + source
                    console.error(window_display_master_control.pageLoadError)
                }
            }

            onLoaded: {
                if (!item)
                    return

                enterAnim.stop()
                pageContainer.opacity = 0
                pageContainer.y = 50
                enterAnim.start()
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: pageLoader.status === Loader.Error
            color: Theme.dangerBg
            border.width: 1
            border.color: Theme.dangerBorder

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: window_display_master_control.pageLoadError.length > 0
                    ? window_display_master_control.pageLoadError
                    : "Failed to load page"
                color: Theme.dangerText
            }
        }
    }

    ParallelAnimation {
        id: enterAnim

        NumberAnimation {
            target: pageContainer
            property: "opacity"
            to: 1
            duration: 300
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: pageContainer
            property: "y"
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
    }
}
