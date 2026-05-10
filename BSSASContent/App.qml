import QtQuick
import QtQuick.Controls
import BSSAS
import MangoComponent

Window {
    id: rootwindow
    property alias mainWindowBackground: mainwindow_background
    property bool settingsSidebarOpen: false
    width: Constants.width
    height: Constants.height
    x: Constants.x
    y: Constants.y

    color: "transparent"
    title: "肠鸣音信号分析系统医学辅助诊断工具"
    flags: Qt.FramelessWindowHint | Qt.Window | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint
    visible: true

    MainWindow_Background {
        id: mainwindow_background
    }

    ToastNotification {
        id: appToast
        duration: 3200
        topMargin: 24
    }

    UpdateDialog {
        id: updateDialog
        z: 60
    }

    Connections {
        target: updateManager

        function onPromptUpdateDialog() {
            updateDialog.open()
        }

        function onToastRequested(message, error) {
            if (error) {
                appToast.showError(message)
            } else {
                appToast.showSuccess(message)
            }
        }
    }

    Component.onCompleted: Qt.callLater(function() {
        updateManager.checkForUpdatesOnStartup()
    })

    Item {
        x: 0
        y: 0
        width: parent.width
        height: mainwindow_background.display_area.y

        DragHandler {
            id: window_drag
            acceptedButtons: Qt.LeftButton
            grabPermissions: PointerHandler.TakeOverForbidden | PointerHandler.ApprovesTakeOverByAnything

            onActiveChanged: {
                if (active) {
                    if (rootwindow.visibility === Window.FullScreen) {
                        rootwindow.showNormal()
                        dragExitTimer.start()
                    } else {
                        rootwindow.startSystemMove()
                    }
                }
            }
        }

        Timer {
            id: dragExitTimer
            interval: 16
            running: false
            repeat: false

            onTriggered: {
                if (rootwindow.visibility !== Window.Minimized) {
                    rootwindow.startSystemMove()
                }
            }
        }
    }

    Left_Toolbar {
        id: left_toolbar
    }

    Window_Display_Master_Control {
    }

    SettingsSidebar {
        id: settings_sidebar
        z: 20
        opened: rootwindow.settingsSidebarOpen
        onCloseRequested: rootwindow.settingsSidebarOpen = false
    }

    Right_Top_Toolbar {
        id: right_top_toolbar
        z: 30
        opacity: rootwindow.settingsSidebarOpen ? 0.0 : 1.0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
        onSettingClicked: rootwindow.settingsSidebarOpen = !rootwindow.settingsSidebarOpen
        onMinimizeClicked: rootwindow.showMinimized()
        onFullscreenClicked: {
            if (rootwindow.visibility === Window.FullScreen) {
                rootwindow.showNormal()
            } else {
                rootwindow.showFullScreen()
            }
        }
        onCloseClicked: rootwindow.close()
    }
}
