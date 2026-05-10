// OSDataVisualizationGraphCartesianWaveform.qml
import QtQuick
import QtGraphs
import BSSAS
Item {
    id: root

    property real layout_margin: 20
    readonly property int fixedGridCountX: 10
    readonly property int fixedGridCountY: 10
    readonly property real defaultResolutionX: 0.3
    readonly property real minResolutionX: 0.001
    readonly property real maxResolutionX: 0.3
    property real rawDataResolutionThreshold: 0.05
    readonly property real minResolutionY: 0.1
    readonly property real maxResolutionY: 1
    readonly property real resolutionStepY: 0.1
    readonly property real zoomInFactorX: 1.2
    readonly property real zoomOutFactorX: 0.8
    readonly property real panScaleFactorX: 2.0
    readonly property real defaultVisibleTimeLength: defaultResolutionX * fixedGridCountX
    property real currentResolutionX: defaultResolutionX
    property real currentResolutionY: maxResolutionY
    readonly property real visibleTimeLength: currentResolutionX * fixedGridCountX
    readonly property real visibleAmplitudeRange: currentResolutionY * fixedGridCountY
    property real displayMin: 0
    property real displayMax: currentResolutionX * fixedGridCountX
    readonly property real displayMinY: -visibleAmplitudeRange / 2
    readonly property real displayMaxY: visibleAmplitudeRange / 2
    property real dataBoundaryX: defaultVisibleTimeLength
    property bool syncingXAxisWindow: false
    property bool panningXAxis: false
    property real lastPanMouseX: 0
    readonly property real timeAxisScaleFactor: 1
    readonly property string timeAxisTitleText: "时间 / S"
    readonly property real axisDisplayMin: root.displayMin * root.timeAxisScaleFactor
    readonly property real axisDisplayMax: root.displayMax * root.timeAxisScaleFactor
    readonly property real axisResolutionX: root.currentResolutionX * root.timeAxisScaleFactor
    readonly property int axisLabelDecimalsX: root.labelDecimals(root.axisDisplayMin, root.axisResolutionX)
    readonly property int axisLabelDecimalsY: root.labelDecimals(root.displayMinY, root.currentResolutionY)
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

        for (let decimals = 0; decimals <= 3; decimals++) {
            var factor = Math.pow(10, decimals);
            if (Math.abs(absValue - Math.round(absValue * factor) / factor) < 0.000001) {
                return decimals;
            }
        }

        return 3;
    }

    function labelDecimals(anchorValue, intervalValue) {
        return Math.min(3, Math.max(decimalsForValue(anchorValue), decimalsForValue(intervalValue)));
    }

    function normalizedBoundaryX() {
        if (!Number.isFinite(dataBoundaryX)) {
            return defaultVisibleTimeLength;
        }

        return Math.max(defaultVisibleTimeLength, dataBoundaryX);
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
        displayMin = boundedMinimumX;
        displayMax = boundedMaximumX;

        syncingXAxisWindow = false;
        refreshSeries();
    }

    function updateDataBoundary() {
        dataBoundaryX = Math.max(defaultVisibleTimeLength, dataManager.importedWaveformDuration());
    }

    function resetView() {
        applyXAxisWindow(0, defaultResolutionX);
    }

    function zoomXAxis(mouseX, zoomFactor) {
        var plotArea = graphsView.plotArea;
        if (plotArea.width <= 0 || plotArea.height <= 0) {
            return;
        }

        var ratio = clamp((mouseX - plotArea.x) / plotArea.width, 0, 1);
        var boundaryX = normalizedBoundaryX();
        var focusX = clamp(displayMin + (displayMax - displayMin) * ratio, 0, boundaryX);
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

        var visibleSpanX = displayMax - displayMin;
        if (visibleSpanX <= 0) {
            return;
        }

        var deltaX = -deltaPixels / plotArea.width * visibleSpanX * panScaleFactorX;
        applyXAxisWindow(displayMin + deltaX, currentResolutionX);
    }

    function scaledPoints(points) {
        if (!points || points.length === 0 || timeAxisScaleFactor === 1) {
            return points;
        }

        return points.map(function(point) {
            return Qt.point(point.x * timeAxisScaleFactor, point.y);
        });
    }

    function refreshSeries() {
        var visiblePoints = currentResolutionX < rawDataResolutionThreshold
                ? dataManager.importedWaveformPoints(displayMin, displayMax)
                : dataManager.importedDownsampledWaveformPoints(displayMin, displayMax);
        if (!visiblePoints || visiblePoints.length === 0) {
            wave_series.clear();
            return;
        }

        wave_series.replace(root.scaledPoints(visiblePoints));
    }

    Connections {
        target: wavHandle
        function onImportDataReady() {
            root.updateDataBoundary();
            root.resetView();
        }
    }

    onTimeAxisScaleFactorChanged: refreshSeries()
    onCurrentResolutionXChanged: {
        if (!syncingXAxisWindow) {
            applyXAxisWindow(displayMin, currentResolutionX);
        }
    }

    Component.onCompleted: resetView()

    GraphsView {
        id: graphsView
        anchors.fill: parent
        marginTop: 10
        marginBottom: root.layout_margin
        marginLeft: -10
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
            labelBackgroundVisible: false
        }
        axisX: ValueAxis {
            id: axisX
            min: root.axisDisplayMin
            max: root.axisDisplayMax
            tickAnchor: root.axisDisplayMin
            tickInterval: root.axisResolutionX
            subTickCount: 4
            labelDecimals: root.axisLabelDecimalsX
            titleText: root.timeAxisTitleText
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
