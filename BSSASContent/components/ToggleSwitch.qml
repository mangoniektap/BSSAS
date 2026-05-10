import QtQuick
import QtQuick.Layouts
import BSSAS

Item {
    id: root
    
    property bool checked: false
    property bool enabled: true
    property bool interactive: true
    
    // Custom icon support (defaults to standard check/close if not specified)
    property bool showIcon: true 
    property string icon: "✔"   // Thumb icon when checked
    
    signal clicked()
    
    implicitWidth: rowLayout.implicitWidth
    implicitHeight: Math.max(32, rowLayout.implicitHeight)
    
    // Theme Colors Lookup
    
    // Helper to resolve onSurface with alpha
    function disabledColor(alphaValue) {
        let color = Qt.color(Theme.textPrimary)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }
    
    RowLayout {
        id: rowLayout
        anchors.centerIn: parent
        spacing: 12
        
        // Switch Container (Track + Thumb)
        Item {
            implicitWidth: 52
            implicitHeight: 32
            
            // Track
            Rectangle {
                id: track
                anchors.fill: parent
                radius: 16
                
                color: {
                    if (!root.enabled) {
                        return root.checked ? root.disabledColor(0.12) : root.disabledColor(0.12)
                    }
                    return root.checked ? Theme.primary : Theme.pageBg
                }
                
                border.width: root.checked ? 0 : 2
                border.color: {
                    if (!root.enabled) {
                        return root.checked ? "transparent" : root.disabledColor(0.12)
                    }
                    return root.checked ? Theme.primary : Theme.primaryBorder
                }
                
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }
                
                // Hover effect for the track
                MouseArea {
                    anchors.fill: parent
                    enabled: root.enabled && root.interactive
                    onClicked: {
                        root.checked = !root.checked
                        root.clicked()
                    }

                    // Hover effect animation on the track
                    onHoveredChanged: {
                        if (hovered) {
                            track.scale = 1.05  // Slightly scale up the track
                            track.opacity = 1.1 // Brighten the track slightly
                        } else {
                            track.scale = 1  // Reset scale
                            track.opacity = 1  // Reset opacity
                        }
                    }
                }
            }
            
            // Thumb
            Rectangle {
                id: thumb
                width: root.checked ? 24 : 16
                height: width
                radius: width / 2
                
                anchors.verticalCenter: parent.verticalCenter
                x: root.checked ? (parent.width - width - 4) : 8
                
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                
                color: {
                    if (!root.enabled) {
                        return root.checked ? Theme.cardBg : root.disabledColor(0.38)
                    }
                    return root.checked ? Theme.textWhite : Theme.primaryBorder
                }
                
                // Icon (Checkmark)
                Text {
                    id: iconItem
                    anchors.centerIn: parent
                    text: root.icon
                    font.family: Theme.iconFontFamily
                    font.pixelSize: 16
                    visible: root.showIcon
                    
                    scale: root.checked ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
                    
                    color: {
                        if (!root.enabled) return root.disabledColor(0.38)
                        return Theme.primary
                    }
                }

                Behavior on color { ColorAnimation { duration: 150 } }
                
                // Hover effect for the thumb
                MouseArea {
                    anchors.fill: parent
                    enabled: root.enabled && root.interactive
                    onClicked: {
                        root.checked = !root.checked
                        root.clicked()
                    }

                    // Hover effect animation on the thumb
                    onHoveredChanged: {
                        if (hovered) {
                            thumb.scale = 1.05  // Slightly scale up the thumb
                            thumb.opacity = 1.1 // Brighten the thumb slightly
                        } else {
                            thumb.scale = 1  // Reset scale
                            thumb.opacity = 1  // Reset opacity
                        }
                    }
                }
            }
            
            // Ripple
           /*  Ripple {
                anchors.centerIn: thumb
                width: 40
                height: 40
                clipRadius: 20
                enabled: root.enabled
                rippleColor: root.checked ? Theme.primary : Theme.textPrimary
                
                onClicked: {
                    root.checked = !root.checked
                    root.clicked()
                }
            } */
        }
    }
}
