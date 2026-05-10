// DataVisualizationGraphCartesianSpectrum.qml
import QtQuick
import QtGraphs
import BSSAS

Item {
    id: root

    property real layout_margin: 20
    readonly property int fixedGridCountX: 5
    readonly property int fixedGridCountY: 5
    readonly property real defaultResolutionX: 600
    readonly property real minResolutionX: 20
    readonly property real maxResolutionX: 600
    readonly property real minResolutionY: 0.1
    readonly property real maxResolutionY: 1
    readonly property real resolutionStepY: 0.1
    readonly property real zoomInFactorX: 1.2
    readonly property real zoomOutFactorX: 0.8
    readonly property real maxFrequencyBoundaryX: 3000
    readonly property real defaultVisibleFrequencyRange: defaultResolutionX * fixedGridCountX
    property real currentResolutionX: defaultResolutionX
    property real currentResolutionY: maxResolutionY
    readonly property real visibleFrequencyRange: currentResolutionX * fixedGridCountX
    readonly property real visibleAmplitudeRange: currentResolutionY * fixedGridCountY
    property real displayMinX: 0
    property real displayMaxX: currentResolutionX * fixedGridCountX
    readonly property real displayMinY: 0
    readonly property real displayMaxY: visibleAmplitudeRange
    property real dataBoundaryX: defaultVisibleFrequencyRange
    property bool syncingXAxisWindow: false
    property bool panningXAxis: false
    property real lastPanMouseX: 0
    readonly property int axisLabelDecimalsX: 0
    readonly property int axisLabelDecimalsY: labelDecimals(displayMinY, currentResolutionY)
    property bool useImportedData: false
    property real linkedTimeCenterSeconds: 0
    property var dftData: []
    property var currentSpectrumTimeRange: ({})

    function colorWithAlpha(sourceColor, alphaValue) {
        const color = Qt.color(sourceColor)
        return Qt.rgba(color.r, color.g, color.b, alphaValue)
    }

    function clamp(value, minimum, maximum) {
        return Math.min(Math.max(value, minimum), maximum);
    }

    function roundToStep(value, step) {
        if (!Number.isFinite(value) || !Number.isFinite(step) || step <= 0) {
            return value;
        }

        return Math.round(Math.round(value / step) * step * 1000) / 1000;
    }

    function decimalsForValue(value) {
        var absValue = Math.abs(value);

        for (let decimals = 0; decimals <= 4; decimals++) {
            var factor = Math.pow(10, decimals);
            if (Math.abs(absValue - Math.round(absValue * factor) / factor) < 0.000001) {
                return decimals;
            }
        }

        return 4;
    }

    function labelDecimals(anchorValue, intervalValue) {
        return Math.min(1, Math.max(decimalsForValue(anchorValue), decimalsForValue(intervalValue)));
    }

    function padNumber(value) {
        return value.toString().padStart(2, "0");
    }

    function formatTimestamp(seconds) {
        if (!Number.isFinite(seconds) || seconds < 0) {
            return "--";
        }

        var totalCentiseconds = Math.round(seconds * 100);
        var hours = Math.floor(totalCentiseconds / 360000);
        var remainingCentiseconds = totalCentiseconds % 360000;
        var minutes = Math.floor(remainingCentiseconds / 6000);
        remainingCentiseconds %= 6000;
        var wholeSeconds = Math.floor(remainingCentiseconds / 100);
        var centiseconds = remainingCentiseconds % 100;

        if (hours > 0) {
            return padNumber(hours) + ":"
                    + padNumber(minutes) + ":"
                    + padNumber(wholeSeconds) + "."
                    + padNumber(centiseconds);
        }

        return padNumber(minutes) + ":"
                + padNumber(wholeSeconds) + "."
                + padNumber(centiseconds);
    }

    function currentSpectrumTimeRangeText() {
        if (!currentSpectrumTimeRange || currentSpectrumTimeRange.valid !== true) {
            return "";
        }

        var fromSeconds = Number(currentSpectrumTimeRange.fromSeconds);
        var toSeconds = Number(currentSpectrumTimeRange.toSeconds);
        if (!Number.isFinite(fromSeconds) || !Number.isFinite(toSeconds) || toSeconds < fromSeconds) {
            return "";
        }

        return "当前时段: "
                + formatTimestamp(fromSeconds)
                + " - "
                + formatTimestamp(toSeconds);
    }

    function normalizedBoundaryX() {
        var safeBoundaryX = Number.isFinite(dataBoundaryX) ? dataBoundaryX : defaultVisibleFrequencyRange;
        return clamp(safeBoundaryX, defaultVisibleFrequencyRange, maxFrequencyBoundaryX);
    }

    function clampYResolution(resolution) {
        var safeResolution = Number.isFinite(resolution) ? resolution : maxResolutionY;
        return clamp(roundToStep(safeResolution, resolutionStepY), minResolutionY, maxResolutionY);
    }

    function clampResolution(resolution, boundaryX) {
        var safeBoundaryX = Number.isFinite(boundaryX) ? boundaryX : normalizedBoundaryX();
        var safeResolution = Number.isFinite(resolution) ? resolution : defaultResolutionX;
        var boundaryLimitedResolution = Math.max(minResolutionX, safeBoundaryX / fixedGridCountX);
        var boundedMaxResolution = Math.min(maxResolutionX, boundaryLimitedResolution);
        return clamp(safeResolution, minResolutionX, boundedMaxResolution);
    }

    function applyXAxisWindow(minimumX, resolutionX) {
        if (syncingXAxisWindow) {
            return;
        }

        syncingXAxisWindow = true;
        updateDataBoundary();

        var boundaryX = normalizedBoundaryX();
        var boundedResolutionX = clampResolution(resolutionX, boundaryX);
        var spanX = boundedResolutionX * fixedGridCountX;
        var maxMinimumX = Math.max(0, boundaryX - spanX);
        var safeMinimumX = Number.isFinite(minimumX) ? minimumX : 0;
        var boundedMinimumX = clamp(safeMinimumX, 0, maxMinimumX);
        var boundedMaximumX = Math.min(boundaryX, boundedMinimumX + spanX);

        boundedMinimumX = Math.max(0, boundedMaximumX - spanX);

        dataBoundaryX = boundaryX;
        currentResolutionX = boundedResolutionX;
        displayMinX = boundedMinimumX;
        displayMaxX = boundedMaximumX;

        syncingXAxisWindow = false;
        refreshSeries();
    }

    function updateDataBoundary() {
        var spectrumBoundary = useImportedData
            ? dataManager.importedSpectrumBoundary()
            : maxFrequencyBoundaryX;
        dataBoundaryX = Math.min(maxFrequencyBoundaryX,
                                 Math.max(defaultVisibleFrequencyRange, spectrumBoundary));
    }

    function resetView() {
        applyXAxisWindow(0, defaultResolutionX);
    }

    function clearSpectrum() {
        currentSpectrumTimeRange = ({});
        syncingXAxisWindow = true;
        updateDataBoundary();
        currentResolutionX = defaultResolutionX;
        displayMinX = 0;
        displayMaxX = Math.min(normalizedBoundaryX(), defaultVisibleFrequencyRange);
        syncingXAxisWindow = false;
        wave_series.clear();
    }

    function zoomXAxis(mouseX, zoomFactor) {
        var plotArea = graphsView.plotArea;
        if (plotArea.width <= 0 || plotArea.height <= 0) {
            return;
        }

        var ratio = clamp((mouseX - plotArea.x) / plotArea.width, 0, 1);
        var boundaryX = normalizedBoundaryX();
        var focusX = clamp(displayMinX + (displayMaxX - displayMinX) * ratio, 0, boundaryX);
        var nextResolutionX = clampResolution(currentResolutionX * zoomFactor, boundaryX);
        var nextSpanX = nextResolutionX * fixedGridCountX;

        applyXAxisWindow(focusX - nextSpanX * ratio, nextResolutionX);
    }

    function adjustYResolution(deltaSteps) {
        if (!Number.isFinite(deltaSteps) || deltaSteps === 0) {
            return;
        }

        currentResolutionY = clampYResolution(currentResolutionY + deltaSteps * resolutionStepY);
    }

    function isPointInPlotArea(x, y) {
        var plotArea = graphsView.plotArea;
        if (plotArea.width <= 0 || plotArea.height <= 0) {
            return false;
        }

        return x >= plotArea.x && x <= plotArea.x + plotArea.width
                && y >= plotArea.y && y <= plotArea.y + plotArea.height;
    }

    function isPointInYAxisLabelArea(x, y) {
        var plotArea = graphsView.plotArea;
        if (plotArea.width <= 0 || plotArea.height <= 0) {
            return false;
        }

        var labelAreaRight = Math.max(0, plotArea.x);
        if (labelAreaRight <= 0) {
            return false;
        }

        return x >= 0 && x <= labelAreaRight
                && y >= plotArea.y && y <= plotArea.y + plotArea.height;
    }

    function panXAxis(deltaPixels) {
        var plotArea = graphsView.plotArea;
        if (plotArea.width <= 0) {
            return;
        }

        var visibleSpanX = displayMaxX - displayMinX;
        if (visibleSpanX <= 0) {
            return;
        }

        var deltaX = -deltaPixels / plotArea.width * visibleSpanX;
        applyXAxisWindow(displayMinX + deltaX, currentResolutionX);
    }

    function refreshSeries() {
        var visiblePoints = useImportedData
                ? dataManager.importedSpectrumPointsAtTime(displayMinX, displayMaxX, linkedTimeCenterSeconds)
                : signalDFTCalculation.realtimeStftPointsAtTime(displayMinX, displayMaxX, linkedTimeCenterSeconds);
        if (!visiblePoints || visiblePoints.length === 0) {
            currentSpectrumTimeRange = ({});
            wave_series.clear();
            return;
        }

        currentSpectrumTimeRange = useImportedData
                ? dataManager.importedSpectrumTimeRangeAtTime(linkedTimeCenterSeconds)
                : signalDFTCalculation.realtimeStftTimeRangeAtTime(linkedTimeCenterSeconds);
        wave_series.replace(visiblePoints);
    }

    onDftDataChanged: {
        if (!useImportedData) {
            applyXAxisWindow(displayMinX, currentResolutionX);
        }
    }
    onLinkedTimeCenterSecondsChanged: {
        refreshSeries();
    }
    onCurrentResolutionXChanged: {
        if (!syncingXAxisWindow) {
            applyXAxisWindow(displayMinX, currentResolutionX);
        }
    }

    Component.onCompleted: resetView()

    GraphsView {
        id: graphsView
        anchors.fill: parent
        marginTop: 10
        marginBottom: root.layout_margin
        marginLeft: -15
        marginRight: 25
        theme: GraphsTheme {
            backgroundVisible: false
            seriesColors: ["blue"]
            gridVisible: false
            grid.mainColor: "black"
            grid.mainWidth: 0.1
            grid.subColor: "black"
            grid.subWidth: 0.05
            axisX.mainWidth: 1
            axisY.mainWidth: 1
            axisX.mainColor: "black"
            axisY.mainColor: "black"
            axisX.subColor: "black"
            axisY.subColor: "black"
            axisX.labelTextColor: "black"
            axisY.labelTextColor: "black"
            axisXLabelFont: Qt.font({ family: Theme.numberFontFamily, pixelSize: 15 })
            axisYLabelFont: Qt.font({ family: Theme.numberFontFamily })
            labelBackgroundVisible: true
        }
        axisX: ValueAxis {
            id: axisX
            min: root.displayMinX
            max: root.displayMaxX
            tickAnchor: root.displayMinX
            tickInterval: root.currentResolutionX
            subTickCount: 4
            labelDecimals: root.axisLabelDecimalsX
            titleText: "频率 / Hz"
            titleColor: "black"
            titleFont: Qt.font({ family: Theme.fontFamily, pixelSize: 20 })
        }

        axisY: ValueAxis {
            id: axisY
            min: root.displayMinY
            max: root.displayMaxY
            tickAnchor: 0
            tickInterval: root.currentResolutionY
            subTickCount: 0
            labelDecimals: root.axisLabelDecimalsY
        }

        LineSeries {
            id: wave_series
            capStyle: Qt.RoundCap
            width: 0.5
            color: Theme.chartHighlight
        }

        Connections {
            target: signalDFTCalculation
            function onImportedDftProcessingFinished() {
                if (!root.useImportedData) {
                    return;
                }

                root.updateDataBoundary()
                root.resetView()
            }
        }
    }

    Rectangle {
        id: timeRangeBadge
        visible: timeRangeLabel.text.length > 0
        z: 2
        x: Math.max(root.layout_margin,
                    graphsView.plotArea.x + graphsView.plotArea.width - width - 10)
        y: Math.max(10, graphsView.plotArea.y + 10)
        radius: height / 2
        color: root.colorWithAlpha(Theme.primaryLighter, 0.92)
        border.width: 1
        border.color: root.colorWithAlpha(Theme.border, 0.7)
        implicitWidth: timeRangeLabel.implicitWidth + 20
        implicitHeight: timeRangeLabel.implicitHeight + 10

        Text {
            id: timeRangeLabel
            anchors.centerIn: parent
            text: root.currentSpectrumTimeRangeText()
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            if (!root.isPointInPlotArea(mouse.x, mouse.y)) {
                mouse.accepted = false;
                return;
            }

            root.panningXAxis = true;
            root.lastPanMouseX = mouse.x;
            mouse.accepted = true;
        }
        onPositionChanged: function(mouse) {
            if (!root.panningXAxis) {
                return;
            }

            root.panXAxis(mouse.x - root.lastPanMouseX);
            root.lastPanMouseX = mouse.x;
            mouse.accepted = true;
        }
        onReleased: root.panningXAxis = false
        onCanceled: root.panningXAxis = false
        onWheel: function(wheel) {
            if (wheel.angleDelta.y === 0) {
                return;
            }

            if (root.isPointInYAxisLabelArea(wheel.x, wheel.y)) {
                wheel.accepted = true;
                root.adjustYResolution(wheel.angleDelta.y > 0 ? -1 : 1);
                return;
            }

            var plotArea = graphsView.plotArea;
            if (plotArea.width <= 0 || plotArea.height <= 0) {
                return;
            }

            if (wheel.x < plotArea.x || wheel.x > plotArea.x + plotArea.width
                    || wheel.y < plotArea.y || wheel.y > plotArea.y + plotArea.height) {
                return;
            }

            wheel.accepted = true;
            root.zoomXAxis(wheel.x, wheel.angleDelta.y > 0 ? 1 / root.zoomInFactorX : 1 / root.zoomOutFactorX);
        }
    }
}
