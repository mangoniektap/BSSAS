/**
 * @file RTDataVisualizationGraphCartesianWaveform.qml
 * @brief 实时数据波形图笛卡尔可视化组件，用于显示实时采集的波形数据，支持固定时间窗口滚动显示。
 */
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
    property real windowAnchorTimestamp: 0
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

    /**
     * @brief 将值钳制在指定范围内
     * @param value 输入值
     * @param minimum 下限
     * @param maximum 上限
     * @returns 钳制后的值
     */
    function clamp(value, minimum, maximum) {
        return Math.min(Math.max(value, minimum), maximum);
    }

    /**
     * @brief 将值按指定步长四舍五入
     * @param value 输入值
     * @param step 步长 (必须为有限正数)
     * @returns 舍入后的值；输入无效时返回原值
     */
    function roundToStep(value, step) {
        if (!Number.isFinite(value) || !Number.isFinite(step) || step <= 0) {
            return value;
        }

        return Math.round(Math.round(value / step) * step * 1000) / 1000;
    }

    /**
     * @brief 计算数值的有效小数位数
     * @param value 输入数值
     * @returns 小数位数 (0~3)
     */
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

    /**
     * @brief 计算坐标轴标签的合适小数位数
     * @param anchorValue 锚点值
     * @param intervalValue 刻度间隔值
     * @returns 小数位数 (最多 3 位)
     */
    function labelDecimals(anchorValue, intervalValue) {
        return Math.min(3, Math.max(decimalsForValue(anchorValue), decimalsForValue(intervalValue)));
    }

    /**
     * @brief 钳制 Y 轴分辨率到合法范围
     * @param resolution 期望分辨率
     * @returns 经步长舍入并钳制后的分辨率
     */
    function clampYResolution(resolution) {
        var safeResolution = Number.isFinite(resolution) ? resolution : maxResolutionY;
        return clamp(roundToStep(safeResolution, resolutionStepY), minResolutionY, maxResolutionY);
    }

    /**
     * @brief 重置视图到默认状态
     */
    function resetView() {
        currentResolutionX = defaultResolutionX;
        windowAnchorTimestamp = 0;
        updateRealtimeWindow(0);
    }

    function restartDisplayWindow(startTimestamp) {
        var safeStartTimestamp = Number.isFinite(startTimestamp) ? Math.max(0, startTimestamp) : 0;
        windowAnchorTimestamp = safeStartTimestamp;
        fromTimestamp = safeStartTimestamp;
        toTimestamp = safeStartTimestamp + fixedVisibleTimeLength;
        wave_series.clear();
    }

    function pointsInsideCurrentWindow(points) {
        if (!points || points.length === 0) {
            return [];
        }

        var filteredPoints = [];
        for (let index = 0; index < points.length; ++index) {
            const point = points[index];
            if (!point || !Number.isFinite(point.x)) {
                continue;
            }

            if (point.x >= fromTimestamp && point.x <= toTimestamp) {
                filteredPoints.push(point);
            }
        }

        return filteredPoints;
    }

    /**
     * @brief 刷新实时波形数据序列
     * @details 根据当前分辨率和通道索引，从 dataManager 获取数据点并更新图表。
     */
    function refreshSeries() {
        var visiblePoints = currentResolutionX < rawDataResolutionThreshold
                ? dataManager.realtimeWaveformPointsForChannel(currentChannelIndex, fromTimestamp, toTimestamp)
                : dataManager.realtimeDownsampledWaveformPoints(fromTimestamp, toTimestamp);
        visiblePoints = root.pointsInsideCurrentWindow(visiblePoints);
        if (!visiblePoints || visiblePoints.length === 0) {
            wave_series.clear();
            return;
        }

        wave_series.replace(root.scaledPoints(visiblePoints));
    }

    /**
     * @brief 清空波形图并重置视图
     */
    function clearWaveForm() {
        wave_series.clear();
        resetView();
    }

    /**
     * @brief 仅清空数据序列，不重置视图
     */
    function clearSeriesOnly() {
        wave_series.clear();
    }

    /**
     * @brief 调整 Y 轴分辨率（幅度缩放）
     * @param deltaSteps 步数变化量 (正数放大，负数缩小)
     */
    function adjustYResolution(deltaSteps) {
        if (!Number.isFinite(deltaSteps) || deltaSteps === 0) {
            return;
        }

        currentResolutionY = clampYResolution(currentResolutionY + deltaSteps * resolutionStepY);
    }

    /**
     * @brief 判断点是否位于 Y 轴标签区域内（用于 Y 轴缩放）
     * @param x 点的 X 坐标
     * @param y 点的 Y 坐标
     * @returns 点在 Y 轴标签区域内返回 true
     */
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

    /**
     * @brief 更新实时时间窗口，使图表滚动到最新数据
     * @param latestTimestamp 最新数据时间戳 (秒)
     */
    function updateRealtimeWindow(latestTimestamp) {
        var safeLatestTimestamp = Number.isFinite(latestTimestamp) ? Math.max(0, latestTimestamp) : 0;
        var safeAnchorTimestamp = Number.isFinite(windowAnchorTimestamp) ? Math.max(0, windowAnchorTimestamp) : 0;
        var anchoredWindowEnd = safeAnchorTimestamp + fixedVisibleTimeLength;
        var windowEnd = safeLatestTimestamp <= anchoredWindowEnd
                ? anchoredWindowEnd
                : safeLatestTimestamp;
        toTimestamp = windowEnd;
        fromTimestamp = safeLatestTimestamp <= anchoredWindowEnd
                ? safeAnchorTimestamp
                : Math.max(0, windowEnd - fixedVisibleTimeLength);
        refreshSeries();
    }

    /**
     * @brief 对数据点集应用时间轴比例缩放
     * @param points 原始数据点数组
     * @returns 缩放后的数据点数组；比例因子为 1 时返回原数组
     */
    function scaledPoints(points) {
        if (!points || points.length === 0 || timeAxisScaleFactor === 1) {
            return points;
        }

        return points.map(function(point) {
            return Qt.point(point.x * timeAxisScaleFactor, point.y);
        });
    }

    /**
     * @brief 追加实时数据批次并更新时间窗口
     * @details 在收到新的降采样数据后，更新视图窗口并刷新显示。
     */
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
