/**
 * @file ScientificFilterResponseGraph.qml
 * @brief 科学滤波器响应图组件，绘制频率响应、相位和组延迟预览。
 */
import QtQuick
import BSSAS

Item {
    id: root

    property var responseData: ({})
    property int mode: 0
    property real hoverFrequencyHz: NaN
    property real hoverValue: NaN
    readonly property real plotLeft: 64
    readonly property real plotRight: 34
    readonly property real plotTop: 24
    readonly property real plotBottom: 54
    readonly property int axisFontSize: 14
    readonly property var activePoints: {
        if (!responseData)
            return []
        if (mode === 1)
            return responseData.phase || []
        if (mode === 2)
            return responseData.groupDelay || []
        return responseData.frequencyResponse || []
    }
    readonly property string yUnit: mode === 1 ? "deg" : (mode === 2 ? "ms" : "dB")

    signal cursorChanged(real frequencyHz, real value)

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function pointCoordinate(point, key) {
        if (!point)
            return NaN
        const value = point[key]
        return Number(value)
    }

    function rangeX() {
        const minimum = Number(responseData && responseData.minimumFrequencyHz !== undefined
            ? responseData.minimumFrequencyHz : 0)
        const maximum = Number(responseData && responseData.maximumFrequencyHz !== undefined
            ? responseData.maximumFrequencyHz : 5000)
        return {
            minimum: isFinite(minimum) ? minimum : 0,
            maximum: isFinite(maximum) && maximum > minimum ? maximum : 5000
        }
    }

    function rangeY() {
        let minimum = Infinity
        let maximum = -Infinity
        const points = activePoints || []
        for (let index = 0; index < points.length; ++index) {
            const value = pointCoordinate(points[index], "y")
            if (!isFinite(value))
                continue
            minimum = Math.min(minimum, value)
            maximum = Math.max(maximum, value)
        }
        if (!isFinite(minimum) || !isFinite(maximum)) {
            minimum = mode === 0 ? -80 : -180
            maximum = mode === 0 ? 10 : 180
        }
        if (Math.abs(maximum - minimum) < 0.0001) {
            minimum -= 1
            maximum += 1
        }
        const padding = Math.max(0.5, (maximum - minimum) * 0.12)
        return { minimum: minimum - padding, maximum: maximum + padding }
    }

    function plotWidth() {
        return Math.max(1, width - plotLeft - plotRight)
    }

    function plotHeight() {
        return Math.max(1, height - plotTop - plotBottom)
    }

    function mapX(value, xRange) {
        return plotLeft + (value - xRange.minimum) / (xRange.maximum - xRange.minimum) * plotWidth()
    }

    function mapY(value, yRange) {
        return plotTop + (yRange.maximum - value) / (yRange.maximum - yRange.minimum) * plotHeight()
    }

    function formatValue(value, decimals) {
        if (!isFinite(value))
            return "--"
        return Number(value).toFixed(decimals)
    }

    function updateHover(mouseX, mouseY) {
        if (mouseX < plotLeft || mouseX > width - plotRight
                || mouseY < plotTop || mouseY > height - plotBottom) {
            hoverFrequencyHz = NaN
            hoverValue = NaN
            graphCanvas.requestPaint()
            cursorChanged(hoverFrequencyHz, hoverValue)
            return
        }

        const xRange = rangeX()
        const frequencyHz = xRange.minimum +
            (mouseX - plotLeft) / plotWidth() * (xRange.maximum - xRange.minimum)
        const points = activePoints || []
        let nearestValue = NaN
        let nearestDistance = Infinity
        for (let index = 0; index < points.length; ++index) {
            const pointX = pointCoordinate(points[index], "x")
            const pointY = pointCoordinate(points[index], "y")
            const distance = Math.abs(pointX - frequencyHz)
            if (isFinite(pointX) && isFinite(pointY) && distance < nearestDistance) {
                nearestDistance = distance
                nearestValue = pointY
            }
        }
        hoverFrequencyHz = frequencyHz
        hoverValue = nearestValue
        graphCanvas.requestPaint()
        cursorChanged(hoverFrequencyHz, hoverValue)
    }

    onActivePointsChanged: graphCanvas.requestPaint()
    onModeChanged: graphCanvas.requestPaint()
    onResponseDataChanged: graphCanvas.requestPaint()
    onWidthChanged: graphCanvas.requestPaint()
    onHeightChanged: graphCanvas.requestPaint()

    Canvas {
        id: graphCanvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Threaded

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            const xRange = root.rangeX()
            const yRange = root.rangeY()
            const left = root.plotLeft
            const top = root.plotTop
            const plotWidth = root.plotWidth()
            const plotHeight = root.plotHeight()
            const right = left + plotWidth
            const bottom = top + plotHeight

            ctx.fillStyle = root.colorWithAlpha(Theme.primaryLighter, 0.28)
            ctx.fillRect(left, top, plotWidth, plotHeight)

            ctx.strokeStyle = root.colorWithAlpha(Theme.textPrimary, 0.16)
            ctx.lineWidth = 1
            ctx.font = root.axisFontSize + "px " + Theme.numberFontFamily
            ctx.fillStyle = root.colorWithAlpha(Theme.textPrimary, 0.72)
            ctx.textBaseline = "top"

            for (let index = 0; index <= 5; ++index) {
                const x = left + plotWidth * index / 5
                const frequency = xRange.minimum + (xRange.maximum - xRange.minimum) * index / 5
                ctx.beginPath()
                ctx.moveTo(x, top)
                ctx.lineTo(x, bottom)
                ctx.stroke()
                ctx.textAlign = "center"
                ctx.fillText(root.formatValue(frequency, 0), x, bottom + 12)
            }

            ctx.textBaseline = "middle"
            ctx.textAlign = "right"
            for (let index = 0; index <= 4; ++index) {
                const y = top + plotHeight * index / 4
                const value = yRange.maximum - (yRange.maximum - yRange.minimum) * index / 4
                ctx.beginPath()
                ctx.moveTo(left, y)
                ctx.lineTo(right, y)
                ctx.stroke()
                ctx.fillText(root.formatValue(value, 1), left - 12, y)
            }

            ctx.strokeStyle = root.colorWithAlpha(Theme.textPrimary, 0.62)
            ctx.lineWidth = 1.2
            ctx.beginPath()
            ctx.moveTo(left, top)
            ctx.lineTo(left, bottom)
            ctx.lineTo(right, bottom)
            ctx.stroke()

            const points = root.activePoints || []
            if (points.length > 0) {
                ctx.strokeStyle = Theme.chartHighlight
                ctx.lineWidth = 2
                ctx.beginPath()
                let hasMove = false
                for (let index = 0; index < points.length; ++index) {
                    const pointX = root.pointCoordinate(points[index], "x")
                    const pointY = root.pointCoordinate(points[index], "y")
                    if (!isFinite(pointX) || !isFinite(pointY))
                        continue
                    const x = root.mapX(pointX, xRange)
                    const y = root.mapY(pointY, yRange)
                    if (!hasMove) {
                        ctx.moveTo(x, y)
                        hasMove = true
                    } else {
                        ctx.lineTo(x, y)
                    }
                }
                ctx.stroke()
            }

            if (isFinite(root.hoverFrequencyHz) && isFinite(root.hoverValue)) {
                const hoverX = root.mapX(root.hoverFrequencyHz, xRange)
                const hoverY = root.mapY(root.hoverValue, yRange)
                ctx.strokeStyle = root.colorWithAlpha(Theme.primary, 0.7)
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.moveTo(hoverX, top)
                ctx.lineTo(hoverX, bottom)
                ctx.moveTo(left, hoverY)
                ctx.lineTo(right, hoverY)
                ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = Theme.primary
                ctx.beginPath()
                ctx.arc(hoverX, hoverY, 3.5, 0, Math.PI * 2)
                ctx.fill()
            }

            ctx.fillStyle = root.colorWithAlpha(Theme.textPrimary, 0.72)
            ctx.textAlign = "right"
            ctx.textBaseline = "top"
            ctx.fillText("Hz", right, bottom + 32)
            ctx.textAlign = "left"
            ctx.textBaseline = "bottom"
            ctx.fillText(root.yUnit, left - 42, top - 6)
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            root.updateHover(mouse.x, mouse.y)
        }
        onExited: {
            root.hoverFrequencyHz = NaN
            root.hoverValue = NaN
            graphCanvas.requestPaint()
            root.cursorChanged(root.hoverFrequencyHz, root.hoverValue)
        }
    }
}
