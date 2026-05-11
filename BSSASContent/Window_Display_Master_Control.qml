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
    readonly property var pageSources: [
        "./display_content/Home.qml",
        "./display_content/Software_Master_Control.qml",
        "./display_content/Temporal_Spectral_Monitoring.qml",
        "./display_content/Medical_Record_Management.qml",
        "./display_content/Auxiliary_Diagnosis.qml",
        "./display_content/About.qml"
    ]

    function animateCurrentPage() {
        enterAnim.stop()
        pageContainer.opacity = 0
        pageContainer.y = 50
        enterAnim.start()
    }

    Item {
        id: pageContainer
        x: 0
        y: 0
        width: parent.width
        height: parent.height
        opacity: 1

        Repeater {
            id: pageCacheRepeater
            model: window_display_master_control.pageSources

            delegate: Item {
                id: pageSlot
                anchors.fill: parent
                visible: isCurrent
                z: isCurrent ? 1 : 0

                property bool isCurrent: index === window_display_master_control.currentIndex
                property bool keepAlive: isCurrent
                property string loadError: ""

                function syncPageActive() {
                    if (pageLoader.item) {
                        pageLoader.item.pageActive = pageSlot.isCurrent
                    }
                }

                onIsCurrentChanged: {
                    if (isCurrent) {
                        cacheReleaseTimer.stop()
                        keepAlive = true
                        if (pageLoader.item) {
                            syncPageActive()
                            window_display_master_control.animateCurrentPage()
                        }
                    } else {
                        syncPageActive()
                        if (keepAlive) {
                            cacheReleaseTimer.restart()
                        }
                    }
                }

                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    active: pageSlot.keepAlive
                    source: modelData
                    visible: status === Loader.Ready

                    onLoaded: {
                        pageSlot.loadError = ""
                        pageSlot.syncPageActive()
                        if (pageSlot.isCurrent) {
                            window_display_master_control.animateCurrentPage()
                        }
                    }

                    onStatusChanged: {
                        if (status === Loader.Error) {
                            pageSlot.loadError = "Failed to load page source: " + source
                            console.error(pageSlot.loadError)
                            if (pageSlot.isCurrent) {
                                window_display_master_control.animateCurrentPage()
                            }
                        }
                    }
                }

                Timer {
                    id: cacheReleaseTimer
                    interval: Math.max(1, Constants.pageCacheRetentionMs)
                    repeat: false

                    onTriggered: {
                        if (!pageSlot.isCurrent) {
                            pageSlot.syncPageActive()
                            pageSlot.keepAlive = false
                            pageSlot.loadError = ""
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: pageSlot.isCurrent && pageLoader.status === Loader.Error
                    color: Theme.dangerBg
                    border.width: 1
                    border.color: Theme.dangerBorder

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: pageSlot.loadError.length > 0
                            ? pageSlot.loadError
                            : "Failed to load page"
                        color: Theme.dangerText
                    }
                }
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
