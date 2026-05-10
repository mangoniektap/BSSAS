# 肠鸣音信号分析系统 UI 开发规范

> 适用项目：肠鸣音信号分析系统 · 医学诊断辅助工具  
> 适用技术栈：Qt 6.10.1 / QML  
> 设计基准：当前 8 张最终界面图  
> 界面比例：16:10  
> 规范版本：v1.0

---

## 0. 规范复核说明

本规范已按照当前 8 张最终界面图进行复核与归纳，覆盖以下页面：

1. 首页
2. 软件总控 / DAQ主控
3. 软件总控 / 信号处理设置
4. 软件总控 / 传感器监测
5. 软件总控 / 文件管理
6. 时频监测
7. 辅助诊断
8. 关于

整体风格统一为：**浅色医疗科技风、低饱和蓝色主题、圆角卡片、柔和阴影、清晰留白、数据面板化布局**。

主题蓝统一收敛为：

```text
#2A78B8
```

该颜色主要用于：主按钮、开关开启状态、Tab 选中态、侧边栏选中态、重要图标、图表主线、关键交互描边。

---

# 1. 全局视觉原则

## 1.1 设计关键词

```text
医疗辅助
信号分析
干净可信
轻量科技感
浅色界面
圆角卡片
低饱和蓝色
柔和阴影
清晰层级
```

## 1.2 整体布局原则

- 软件整体比例固定为 `16:10`。
- 界面采用左侧导航 + 右侧内容区布局。
- 左侧导航宽度稳定，右侧内容区使用大圆角主容器。
- 页面之间保持相同的左侧导航结构、顶部窗口控制区、主内容卡片风格、页脚状态栏、圆角与边框强度、字体层级、主题蓝使用方式。

---

# 2. 字体规范

## 2.1 字体族

Windows 平台优先使用：

```qml
font.family: "Microsoft YaHei UI"
```

跨平台推荐：

```qml
font.family: Qt.platform.os === "windows"
             ? "Microsoft YaHei UI"
             : "Noto Sans CJK SC"
```

字体优先级：

```text
Microsoft YaHei UI
Microsoft YaHei
PingFang SC
Noto Sans CJK SC
Source Han Sans SC
Segoe UI
Arial
sans-serif
```

## 2.2 数字字体

统计数字、仪表数据、版本号可以继续使用主字体。  
如需数字更加清晰，可在数据卡片中使用：

```qml
font.family: "Segoe UI"
```

## 2.3 字号与字重

| 使用场景 | 字号 | 字重 | 推荐颜色 |
|---|---:|---:|---|
| 页面主标题 | 30-34 px | 700 | `#0B2145` |
| 首页 Hero 主标题 | 34-40 px | 700 | `#0B2145` |
| 首页 Hero 副标题 | 24-28 px | 400 / 500 | `#253A59` |
| 卡片标题 | 18-20 px | 600 | `#0B2145` |
| 分组标题 | 16-18 px | 600 | `#142B4D` |
| 普通正文 | 14-15 px | 400 | `#4F6278` |
| 说明文字 | 12-13 px | 400 | `#6F8297` |
| Placeholder | 13-14 px | 400 | `#96A6B8` |
| 侧边栏文字 | 16-18 px | 500 | `#203A5C` |
| 侧边栏选中文字 | 16-18 px | 600 | `#2A78B8` |
| Tab 文字 | 15-16 px | 500 / 600 | `#2A78B8` / `#4F6278` |
| 按钮文字 | 14-16 px | 600 | `#FFFFFF` |
| 数据大数字 | 24-32 px | 700 | `#0B2145` |
| 数据单位 | 12-14 px | 400 | `#6F8297` |
| 页脚文字 | 12-13 px | 400 | `#6F8297` |

---

# 3. 颜色规范

## 3.1 主色

| Token | 用途 | 色值 |
|---|---|---|
| `primary` | 主题蓝 / 主按钮 / 开关开启 / 选中态 | `#2A78B8` |
| `primaryHover` | 主按钮 Hover | `#246DA8` |
| `primaryPressed` | 主按钮 Pressed | `#1F6093` |
| `primaryLight` | 选中背景 / 浅蓝块 | `#EAF4FC` |
| `primaryLighter` | 极浅蓝背景 | `#F3F8FD` |
| `primaryBorder` | 浅蓝边框 | `#BBD8EE` |

## 3.2 背景色

| Token | 用途 | 色值 |
|---|---|---|
| `appBg` | 软件最外层背景 | `#F1F3F6` |
| `pageBg` | 页面浅底色 | `#F8FBFF` |
| `contentBg` | 内容区背景 | `#FFFFFF` |
| `cardBg` | 卡片背景 | `#FFFFFF` |
| `inputBg` | 输入框背景 | `#FFFFFF` |
| `heroBgStart` | 首页 Hero 渐变起始 | `#F7FBFF` |
| `heroBgEnd` | 首页 Hero 渐变结束 | `#EEF7FF` |
| `disabledBg` | 禁用控件背景 | `#E8EEF5` |

## 3.3 文字色

| Token | 用途 | 色值 |
|---|---|---|
| `textTitle` | 页面标题 / 强标题 | `#0B2145` |
| `textPrimary` | 一级正文 | `#142B4D` |
| `textSecondary` | 普通正文 | `#4F6278` |
| `textMuted` | 次级说明 | `#6F8297` |
| `textWeak` | 弱提示 | `#8A9AAF` |
| `textPlaceholder` | 输入提示 | `#96A6B8` |
| `textDisabled` | 禁用文字 | `#B5C2D0` |
| `textWhite` | 白色文字 | `#FFFFFF` |

## 3.4 边框与分割线

| Token | 用途 | 色值 |
|---|---|---|
| `border` | 普通边框 | `#DDEAF6` |
| `borderLight` | 轻边框 | `#EAF1F8` |
| `divider` | 分割线 | `#E6EEF6` |
| `gridLine` | 图表网格线 | `#E3ECF5` |
| `axisLine` | 图表坐标轴 | `#C8D6E4` |

## 3.5 状态色

| Token | 用途 | 色值 |
|---|---|---|
| `success` | 正常 / 成功 | `#19B97B` |
| `successDark` | 成功强调 | `#12A86E` |
| `successBg` | 成功浅背景 | `#E9F9F2` |
| `warning` | 警告 | `#FF9F2E` |
| `warningBg` | 警告浅背景 | `#FFF5E8` |
| `danger` | 异常 / 错误 | `#EF4444` |
| `dangerBg` | 异常浅背景 | `#FEECEC` |
| `infoBg` | 信息提示背景 | `#EAF4FC` |

## 3.6 辅助强调色

这些颜色主要来自首页数据卡片与状态模块，用于区分不同数据类型，不作为主交互色。

| Token | 用途 | 色值 |
|---|---|---|
| `teal` | 已分析 / 饼图类状态 | `#18B7B4` |
| `tealBg` | 青绿色浅背景 | `#E8FAF8` |
| `purple` | 异常提示 | `#7A55D9` |
| `purpleBg` | 紫色浅背景 | `#F2EEFF` |
| `orange` | 数据库 / CPU 等提示 | `#FF8A1F` |
| `orangeBg` | 橙色浅背景 | `#FFF1E6` |

---

# 4. 圆角规范

| 组件 | 圆角 |
|---|---:|
| 软件主窗口 | 24-28 px |
| 主内容大容器 | 24 px |
| 页面 Hero 卡片 | 18-22 px |
| 普通卡片 | 14-18 px |
| 小状态卡 / 信息块 | 12-14 px |
| 按钮 | 8-12 px |
| 输入框 / 搜索框 | 20-26 px |
| Tab 胶囊按钮 | 10-14 px |
| Badge | 12-16 px |
| Switch 开关 | 高度的一半 |

---

# 5. 阴影规范

整体阴影应轻，不使用强烈投影。

## 5.1 卡片阴影

```text
shadowColor: #12000000
shadowBlur: 0.25 - 0.35
shadowVerticalOffset: 3 - 6
```

## 5.2 悬浮卡片阴影

用于首页统计卡片、顶部功能卡片：

```text
shadowColor: #18000000
shadowBlur: 0.35 - 0.45
shadowVerticalOffset: 4 - 8
```

## 5.3 QML 示例

```qml
MultiEffect {
    source: card
    shadowEnabled: true
    shadowColor: "#14000000"
    shadowBlur: 0.35
    shadowVerticalOffset: 4
}
```

---

# 6. 主布局规范

## 6.1 窗口

```text
窗口比例: 16:10
窗口背景: #F1F3F6
窗口圆角: 24-28 px
```

## 6.2 左侧导航

建议宽度：

```text
260-300 px
```

样式：

```text
背景: #FFFFFF / #F8FBFF
菜单普通文字: #203A5C
菜单普通图标: #4F6278
菜单选中背景: #EAF4FC
菜单选中文字: #2A78B8
菜单选中竖条: #2A78B8
底部安全卡背景: #F3F8FD
底部安全卡边框: #DDEAF6
```

菜单项：

```text
首页
软件总控
时频监测
病历管理
辅助诊断
关于
```

选中菜单项示例：

```qml
Rectangle {
    height: 54
    radius: 12
    color: selected ? Theme.primaryLight : "transparent"

    Rectangle {
        visible: selected
        width: 4
        height: 28
        radius: 2
        color: Theme.primary
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        text: menuText
        color: selected ? Theme.primary : Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: 16
        font.weight: selected ? Font.DemiBold : Font.Medium
    }
}
```

## 6.3 主内容容器

```text
背景: #FFFFFF
边框: #DDEAF6
圆角: 24 px
内边距: 24-32 px
```

---

# 7. 按钮规范

## 7.1 主按钮

用于：

```text
开始采集
启动DAQ
确认
保存为 WAV 文件
选择并导入 WAV
```

样式：

```text
背景: #2A78B8
Hover: #246DA8
Pressed: #1F6093
文字: #FFFFFF
圆角: 10 px
高度: 36-44 px
```

QML 示例：

```qml
Rectangle {
    id: primaryButton
    height: 40
    radius: 10
    color: pressed ? Theme.primaryPressed
                   : hovered ? Theme.primaryHover
                             : Theme.primary

    Text {
        anchors.centerIn: parent
        text: "开始采集"
        color: Theme.textWhite
        font.family: Theme.fontFamily
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }
}
```

## 7.2 次级描边按钮

用于：

```text
保存数据
打开分析报告
刷新设备
初始化设备
配置传感器
重置
停止DAQ
```

样式：

```text
背景: #FFFFFF
边框: #2A78B8
文字: #2A78B8
Hover 背景: #EAF4FC
Pressed 背景: #DCEEFF
圆角: 10 px
```

```qml
Rectangle {
    height: 38
    radius: 10
    color: hovered ? Theme.primaryLight : "#FFFFFF"
    border.color: Theme.primary
    border.width: 1

    Text {
        anchors.centerIn: parent
        text: "保存数据"
        color: Theme.primary
        font.family: Theme.fontFamily
        font.pixelSize: 14
        font.weight: Font.DemiBold
    }
}
```

## 7.3 危险按钮

用于：

```text
停止DAQ
异常操作确认
```

建议在本套界面中弱化红色，使用描边按钮，避免大面积红色破坏医疗浅色风格。

```text
文字 / 边框: #EF4444
背景: #FFFFFF
Hover 背景: #FEECEC
```

---

# 8. 开关 / Toggle 规范

```text
开启轨道: #2A78B8
关闭轨道: #C9D6E2
滑块: #FFFFFF
关闭边框: #B8C8D8
高度: 24 px
宽度: 44-50 px
```

QML 示例：

```qml
Rectangle {
    width: 46
    height: 24
    radius: 12
    color: checked ? Theme.primary : "#C9D6E2"

    Rectangle {
        width: 20
        height: 20
        radius: 10
        color: "#FFFFFF"
        anchors.verticalCenter: parent.verticalCenter
        x: checked ? parent.width - width - 2 : 2
    }
}
```

---

# 9. 输入框与搜索框规范

## 9.1 首页搜索框

样式：

```text
背景: #FFFFFF
边框: #DDEAF6
圆角: 24-26 px
高度: 48-52 px
Placeholder: #96A6B8
输入文字: #142B4D
图标色: #6F8297
快捷键背景: #F3F8FD
快捷键边框: #DDEAF6
快捷键文字: #7A8BA0
```

QML 示例：

```qml
Rectangle {
    width: 470
    height: 50
    radius: 25
    color: Theme.inputBg
    border.color: Theme.border
    border.width: 1

    TextInput {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 48
        width: parent.width - 140
        font.family: Theme.fontFamily
        font.pixelSize: 14
        color: Theme.textPrimary
        selectionColor: Theme.primaryBorder
        placeholderText: "搜索患者/记录/分析结果..."
        placeholderTextColor: Theme.textPlaceholder
    }

    Rectangle {
        width: 58
        height: 26
        radius: 8
        color: Theme.primaryLighter
        border.color: Theme.border
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter

        Text {
            anchors.centerIn: parent
            text: "Ctrl + K"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }
}
```

## 9.2 普通输入框

```text
背景: #FFFFFF
边框: #DDEAF6
Focus 边框: #2A78B8
圆角: 8-10 px
高度: 34-40 px
```

---

# 10. Tab 规范

适用于：

```text
DAQ主控
信号处理设置
传感器监测
文件管理
```

以及时频监测中的：

```text
时域波形
频域频谱
时频图
```

样式：

```text
普通文字: #4F6278
选中文字: #2A78B8
选中下划线: #2A78B8
底部分割线: #E6EEF6
字号: 15-16 px
普通字重: 500
选中字重: 600
```

QML 示例：

```qml
Text {
    text: tabText
    color: selected ? Theme.primary : Theme.textSecondary
    font.family: Theme.fontFamily
    font.pixelSize: 15
    font.weight: selected ? Font.DemiBold : Font.Medium
}

Rectangle {
    visible: selected
    height: 2
    radius: 1
    color: Theme.primary
}
```

---

# 11. 卡片规范

## 11.1 普通卡片

```text
背景: #FFFFFF
边框: #DDEAF6
边框宽度: 1 px
圆角: 16 px
内边距: 20-24 px
```

```qml
Rectangle {
    radius: Theme.radiusLarge
    color: Theme.cardBg
    border.color: Theme.border
    border.width: 1
}
```

## 11.2 大内容卡片

用于：

```text
首页 Hero
时频监测图表区域
辅助诊断识别状态
关于页面 Hero
```

```text
背景: #FFFFFF / 浅蓝渐变
边框: #DDEAF6
圆角: 20-24 px
内边距: 24-32 px
```

## 11.3 小状态卡片

用于：

```text
系统状态
统计数据
识别摘要
版本信息
核心能力
```

```text
背景: #FFFFFF
边框: #EAF1F8
圆角: 12-14 px
内边距: 14-18 px
```

---

# 12. 图表规范

## 12.1 波形图

| 元素 | 颜色 |
|---|---|
| 波形主线 | `#2A78B8` |
| 波形浅填充 | `#BBD8EE` |
| 图表网格线 | `#E3ECF5` |
| 坐标轴线 | `#C8D6E4` |
| 坐标轴文字 | `#6F8297` |
| 图表背景 | `#FFFFFF` |

建议视觉：

```text
线宽: 1.2 - 1.6 px
网格线: 虚线 / 低透明度
波形填充: 20%-35% 透明度
```

## 12.2 坐标散点图

适用于：

```text
软件总控 / 传感器监测
```

样式：

```text
散点颜色: #2A78B8
散点标签: #2A78B8
坐标轴虚线: #C8D6E4
图表边框: #DDEAF6
背景: #FFFFFF
```

---

# 13. Badge / 状态标签规范

## 13.1 成功状态

```text
背景: #E9F9F2
文字: #12A86E
圆点: #19B97B
```

例如：

```text
系统运行正常
实时采集中
已配置
正常
```

## 13.2 等待状态

```text
背景: #F3F8FD
文字: #6F8297
边框: #DDEAF6
```

例如：

```text
待识别
待计算
待生成
```

## 13.3 异常状态

```text
背景: #FEECEC
文字: #EF4444
```

---


# 14. QML Theme 单例

建议建立：

```text
Theme.qml
```

内容如下：

```qml
pragma Singleton
import QtQuick

QtObject {
    // Font
    readonly property string fontFamily: Qt.platform.os === "windows"
                                         ? "Microsoft YaHei UI"
                                         : "Noto Sans CJK SC"

    readonly property string numberFontFamily: Qt.platform.os === "windows"
                                               ? "Segoe UI"
                                               : fontFamily

    // Primary
    readonly property color primary: "#2A78B8"
    readonly property color primaryHover: "#246DA8"
    readonly property color primaryPressed: "#1F6093"
    readonly property color primaryLight: "#EAF4FC"
    readonly property color primaryLighter: "#F3F8FD"
    readonly property color primaryBorder: "#BBD8EE"

    // Background
    readonly property color appBg: "#F1F3F6"
    readonly property color pageBg: "#F8FBFF"
    readonly property color contentBg: "#FFFFFF"
    readonly property color cardBg: "#FFFFFF"
    readonly property color inputBg: "#FFFFFF"
    readonly property color disabledBg: "#E8EEF5"

    // Hero gradient
    readonly property color heroBgStart: "#F7FBFF"
    readonly property color heroBgEnd: "#EEF7FF"

    // Border / Divider
    readonly property color border: "#DDEAF6"
    readonly property color borderLight: "#EAF1F8"
    readonly property color divider: "#E6EEF6"
    readonly property color gridLine: "#E3ECF5"
    readonly property color axisLine: "#C8D6E4"

    // Text
    readonly property color textTitle: "#0B2145"
    readonly property color textPrimary: "#142B4D"
    readonly property color textSecondary: "#4F6278"
    readonly property color textMuted: "#6F8297"
    readonly property color textWeak: "#8A9AAF"
    readonly property color textPlaceholder: "#96A6B8"
    readonly property color textDisabled: "#B5C2D0"
    readonly property color textWhite: "#FFFFFF"

    // Status
    readonly property color success: "#19B97B"
    readonly property color successDark: "#12A86E"
    readonly property color successBg: "#E9F9F2"
    readonly property color warning: "#FF9F2E"
    readonly property color warningBg: "#FFF5E8"
    readonly property color danger: "#EF4444"
    readonly property color dangerBg: "#FEECEC"

    // Accent
    readonly property color teal: "#18B7B4"
    readonly property color tealBg: "#E8FAF8"
    readonly property color purple: "#7A55D9"
    readonly property color purpleBg: "#F2EEFF"
    readonly property color orange: "#FF8A1F"
    readonly property color orangeBg: "#FFF1E6"

    // Radius
    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 16
    readonly property int radiusXLarge: 24
    readonly property int radiusWindow: 28

    // Typography
    readonly property int fontPageTitle: 32
    readonly property int fontHeroTitle: 38
    readonly property int fontHeroSubtitle: 26
    readonly property int fontCardTitle: 18
    readonly property int fontSectionTitle: 16
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontNumber: 28
}
```

---

# 15. 组件尺寸建议

## 15.1 侧边栏

```text
宽度: 260-300 px
菜单项高度: 52-56 px
菜单项圆角: 12 px
菜单项左右内边距: 20-24 px
图标尺寸: 20-24 px
```

## 15.2 主内容区

```text
主容器外边距: 20-28 px
主容器内边距: 24-32 px
卡片间距: 16-20 px
```

## 15.3 按钮

```text
普通按钮高度: 36-40 px
主要按钮高度: 40-44 px
最小宽度: 120 px
图标与文字间距: 8 px
```

## 15.4 数据卡片

```text
高度: 90-130 px
圆角: 16 px
内边距: 18-22 px
图标块尺寸: 48-60 px
```

## 15.5 图表区域

```text
图表卡片圆角: 16-20 px
图表内边距: 20-28 px
图表网格线透明度: 45%-65%
```

---

# 16. 交互状态规范

## 16.1 Hover

| 组件 | Hover 效果 |
|---|---|
| 主按钮 | 背景变为 `#246DA8` |
| 描边按钮 | 背景变为 `#EAF4FC` |
| 菜单项 | 背景变为 `#F3F8FD` |
| 卡片 | 阴影轻微增强 |
| 输入框 | 边框变为 `#BBD8EE` |

## 16.2 Pressed

| 组件 | Pressed 效果 |
|---|---|
| 主按钮 | 背景变为 `#1F6093` |
| 描边按钮 | 背景变为 `#DCEEFF` |
| 菜单项 | 背景变为 `#EAF4FC` |

## 16.3 Disabled

```text
背景: #E8EEF5
文字: #B5C2D0
边框: #DDEAF6
```

---

# 18. 实现注意事项

1. 所有页面优先复用同一套 `Theme.qml`。
3. 页面主标题、Tab、卡片标题、按钮、开关应使用统一组件，不要每页单独写样式。
4. `#2A78B8` 是当前主题蓝，按钮、开关、选中态必须统一使用该色。
5. 页面中可保留青绿、紫色、橙色作为数据类型区分色，但不能替代主题蓝。
6. 阴影必须轻，避免出现厚重、商业后台风格。
7. 页面留白要充足，控件不要贴边。
8. QML 中尽量避免硬编码重复颜色，应引用 `Theme`。

---

