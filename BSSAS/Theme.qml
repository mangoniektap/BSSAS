pragma Singleton
import QtQuick

QtObject {
    id: theme

    readonly property string fontFamily: Qt.platform.os === "windows"
                                         ? "Microsoft YaHei UI"
                                         : "Noto Sans CJK SC"
    readonly property string numberFontFamily: Qt.platform.os === "windows"
                                               ? "Segoe UI"
                                               : fontFamily
    readonly property string iconFontFamily: "Material Symbols Outlined"

    readonly property color primary: "#2A78B8"
    readonly property color primaryHover: "#246DA8"
    readonly property color primaryPressed: "#1F6093"
    readonly property color primaryLight: "#EAF4FC"
    readonly property color primaryLighter: "#F3F8FD"
    readonly property color primaryBorder: "#BBD8EE"
    readonly property color secondaryPressedBg: "#DCEEFF"

    readonly property color appBg: "#F1F3F6"
    readonly property color pageBg: "#F8FBFF"
    readonly property color contentBg: "#FFFFFF"
    readonly property color cardBg: "#FFFFFF"
    readonly property color inputBg: "#FFFFFF"
    readonly property color disabledBg: "#E8EEF5"
    readonly property color heroBgStart: "#F7FBFF"
    readonly property color heroBgEnd: "#EEF7FF"

    readonly property color border: "#DDEAF6"
    readonly property color borderLight: "#EAF1F8"
    readonly property color divider: "#E6EEF6"
    readonly property color gridLine: "#E3ECF5"
    readonly property color axisLine: "#C8D6E4"

    readonly property color textTitle: "#0B2145"
    readonly property color textPrimary: "#142B4D"
    readonly property color textSecondary: "#4F6278"
    readonly property color textMuted: "#6F8297"
    readonly property color textWeak: "#8A9AAF"
    readonly property color textPlaceholder: "#96A6B8"
    readonly property color textDisabled: "#B5C2D0"
    readonly property color textWhite: "#FFFFFF"
    readonly property color textSidebar: "#203A5C"
    readonly property color textHeroSubtitle: "#253A59"
    readonly property color shortcutText: "#7A8BA0"

    readonly property color success: "#19B97B"
    readonly property color successDark: "#12A86E"
    readonly property color successBg: "#E9F9F2"
    readonly property color warning: "#FF9F2E"
    readonly property color warningBg: "#FFF5E8"
    readonly property color danger: "#EF4444"
    readonly property color dangerBg: "#FEECEC"
    readonly property color dangerBorder: "#F5B7B7"
    readonly property color dangerText: "#7F1D1D"
    readonly property color infoBg: "#EAF4FC"

    readonly property color teal: "#18B7B4"
    readonly property color tealBg: "#E8FAF8"
    readonly property color purple: "#7A55D9"
    readonly property color purpleBg: "#F2EEFF"
    readonly property color orange: "#FF8A1F"
    readonly property color orangeBg: "#FFF1E6"

    readonly property color toggleOff: "#C9D6E2"
    readonly property color toggleOffBorder: "#B8C8D8"
    readonly property color shadowCard: "#12000000"
    readonly property color shadowFloating: "#18000000"
    readonly property color overlayDim: "#5408141E"
    readonly property color controlHoverLayer: "#0D000000"
    readonly property color controlPressedLayer: "#14000000"
    readonly property color chartHighlight: "#00FF9D"
    readonly property color popoverBg: "#CC1E1E1E"
    readonly property color popoverBorder: "#33FFFFFF"

    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 16
    readonly property int radiusXLarge: 24
    readonly property int radiusWindow: 28

    readonly property int fontPageTitle: 32
    readonly property int fontHeroTitle: 38
    readonly property int fontHeroSubtitle: 26
    readonly property int fontCardTitle: 18
    readonly property int fontSectionTitle: 16
    readonly property int fontBody: 14
    readonly property int fontBodyLarge: 16
    readonly property int fontSmall: 12
    readonly property int fontTiny: 11
    readonly property int fontNumber: 28

    function withAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }
}
