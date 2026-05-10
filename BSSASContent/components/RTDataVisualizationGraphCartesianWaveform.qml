// RTDataVisualizationGraphCartesianWaveform.qml
import QtQuick
import QtGraphs
import BSSAS
Item {
    id: root

    property real layout_margin: 20
    readonly property int fixedGridCountX: 10
    readonly property int fixedGridCountY: 10
    readonly property real fixedVisibleTimeLength: 3.0
    readonly property real defaultResolutionX: fixedVisibleTimeLength / fixedGridCountX
    property real rawDataResolutionThreshold: 0.05
    readonly property real minResolutionY: 0.1
    readonly property real maxResolutionY: 1
    readonly property real resolutionStepY: 0.1
    property real currentResolutionX: defaultResolutionX
    property real currentResolutionY: maxResolutionY
    readonly property real visibleTimeLength: fixedVisibleTimeLength
    readonly property real visibleAmplitudeRange: currentResolutionY * fixedGridCountY
    readonly property real timeAxisScaleFactor: 1
    readonly property string timeAxisTitleText: "时间 / S"
    property real fromTimestamp: 0
    property real toTimestamp: fixedVisibleTimeLength
    readonly property real displayMinY: -visibleAmplitudeRange / 2
    readonly property real displayMaxY: visibleAmplitudeRange / 2
    readonly property real axisDisplayMin: root.fromTimestamp * root.timeAxisScaleFactor
    readonly property real axisDisplayMax: root.toTimestamp * root.timeAxisScaleFactor
    readonly property real axisResolutionX: root.defaultResolutionX * root.timeAxisScaleFactor
    readonly property int axisLabelDecimalsX: root.labelDecimals(root.axisDisplayMin, root.axisResolutionX)
    readonly property int axisLabelDecimalsY: root.labelDecimals(root.displayMinY, root.currentResolutionY)
    property var downsampledData: []
    property int currentChannelIndex: 0

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

    function clampYResolution(resolution) {
        var safeResolution = Number.isFinite(resolution) ? resolution : maxResolutionY;
        return clamp(roundToStep(safeResolution, resolutionStepY), minResolutionY, maxResolutionY);
    }

    function resetView() {
        currentResolutionX = defaultResolutionX;
        updateRealtimeWindow(0);
    }

    function refreshSeries() {
        var visiblePoints = currentResolutionX < rawDataResolutionThreshold
                ? dataManager.realtimeWaveformPointsForChannel(currentChannelIndex, fromTimestamp, toTimestamp)
                : dataManager.realtimeDownsampledWaveformPoints(fromTimestamp, toTimestamp);
        if (!visiblePoints || visiblePoints.length === 0) {
            wave_series.clear();
            return;
        }

        wave_series.replace(root.scaledPoints(visiblePoints));
    }

    function clearWaveForm() {
        wave_series.clear();
        resetView();
    }

    function clearSeriesOnly() {
        wave_series.clear();
    }

    function adjustYResolution(deltaSteps) {
        if (!Number.isFinite(deltaSteps) || deltaSteps === 0) {
            return;
        }

        currentResolutionY = clampYResolution(currentResolutionY + deltaSteps * resolutionStepY);
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

    function updateRealtimeWindow(latestTimestamp) {
        var safeLatestTimestamp = Number.isFinite(latestTimestamp) ? Math.max(0, latestTimestamp) : 0;
        var windowEnd = Math.max(fixedVisibleTimeLength, safeLatestTimestamp);
        toTimestamp = windowEnd;
        fromTimestamp = Math.max(0, windowEnd - fixedVisibleTimeLength);
        refreshSeries();
    }

    function scaledPoints(points) {
        if (!points || points.length === 0 || timeAxisScaleFactor === 1) {
            return points;
        }

        return points.map(function(point) {
            return Qt.point(point.x * timeAxisScaleFactor, point.y);
        });
    }

    function appendRealtimeBatch() {
        if (!downsampledData || downsampledData.length === 0) {
            return;
        }

        updateRealtimeWindow(dataManager.realtimeWaveformDurationForChannel(currentChannelIndex));
    }

    onDownsampledDataChanged: appendRealtimeBatch()
    onCurrentChannelIndexChanged: refreshSeries()
    onTimeAxisScaleFactorChanged: refreshSeries()

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
        acceptedButtons: Qt.NoButton
        onWheel: function(wheel) {
            if (wheel.angleDelta.y === 0) {
                return;
            }

            if (root.isPointInYAxisLabelArea(wheel.x, wheel.y)) {
                wheel.accepted = true;
                root.adjustYResolution(wheel.angleDelta.y > 0 ? -1 : 1);
            }
        }
    }
}
