/**
 * @file ColumnTabBar.qml
 * @brief 垂直列式标签栏组件，基于 TabBar 实现垂直排列的选项卡切换。
 */
import QtQuick
import QtQuick.Controls

TabBar {
    id: root
    
    // 确保有隐含尺寸，防止在 Layout 中坍塌
    implicitWidth: 100 
    implicitHeight: contentItem.contentHeight 

    contentItem: ListView {
        model: root.contentModel
        spacing: root.spacing
        orientation: ListView.Vertical
        interactive: false

    }
}

