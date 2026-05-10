#include "GenerateManager.h"

#include "DataManager.h"
#include "DatabaseStoragePaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QMetaType>
#include <QPointF>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QVariantList>
#include <QVersionNumber>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct NumericStats {
    int count = 0;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
};

struct EventMetrics {
    double averageDurationMs = 0.0;
    double averageIntervalSeconds = 0.0;
    double peakAmplitude = 0.0;
    double eventRatePerMinute = 0.0;
    double rmsEnergy = 0.0;
    double burstRate = 0.0;
    double totalDurationMs = 0.0;
};

struct SpectrumMetrics {
    bool valid = false;
    double dominantFrequency = 0.0;
    double frequencyRangeMin = 0.0;
    double frequencyRangeMax = 0.0;
    double spectralCentroid = 0.0;
    double spectralEntropy = 0.0;
    double highLowRatio = 0.0;
};

QString formatInteger(qint64 value)
{
    return QLocale::system().toString(value);
}

QString formatNumber(double value, int precision = 4)
{
    return QLocale::system().toString(value, 'f', precision);
}

QString formatStatValue(const NumericStats& stats, double value, int precision = 4)
{
    if (stats.count <= 0) {
        return QStringLiteral("无");
    }

    return formatNumber(value, precision);
}

NumericStats computeStats(const QVariantList& values)
{
    NumericStats stats;
    double sum = 0.0;

    for (const QVariant& value : values) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) {
            continue;
        }

        if (stats.count == 0) {
            stats.min = number;
            stats.max = number;
        } else {
            stats.min = std::min(stats.min, number);
            stats.max = std::max(stats.max, number);
        }

        sum += number;
        ++stats.count;
    }

    if (stats.count > 0) {
        stats.mean = sum / static_cast<double>(stats.count);
    }

    return stats;
}

double meanOfValues(const QVector<double>& values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }

    return sum / static_cast<double>(values.size());
}

QVector<QPointF> extractPointSeries(const QVariantList& values)
{
    QVector<QPointF> points;
    points.reserve(values.size());

    for (const QVariant& value : values) {
        const QVariantMap pointMap = value.toMap();
        bool okX = false;
        bool okY = false;
        const double x = pointMap.value(QStringLiteral("x")).toDouble(&okX);
        const double y = pointMap.value(QStringLiteral("y")).toDouble(&okY);
        if (!okX || !okY) {
            continue;
        }

        points.append(QPointF(x, y));
    }

    return points;
}

double weightedFrequencyAtRatio(const QVector<QPointF>& points, double ratio)
{
    double totalWeight = 0.0;
    for (const QPointF& point : points) {
        totalWeight += std::max(0.0, point.y());
    }

    if (totalWeight <= 0.0) {
        return 0.0;
    }

    const double threshold = totalWeight * std::clamp(ratio, 0.0, 1.0);
    double accumulatedWeight = 0.0;
    for (const QPointF& point : points) {
        accumulatedWeight += std::max(0.0, point.y());
        if (accumulatedWeight >= threshold) {
            return point.x();
        }
    }

    return points.isEmpty() ? 0.0 : points.constLast().x();
}

SpectrumMetrics computeSpectrumMetricsFromPoints(const QVariantList& spectrumPoints)
{
    const QVector<QPointF> rawPoints = extractPointSeries(spectrumPoints);
    QVector<QPointF> sanitizedPoints;
    sanitizedPoints.reserve(rawPoints.size());

    for (const QPointF& point : rawPoints) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            continue;
        }

        sanitizedPoints.append(QPointF(std::max(0.0, point.x()), std::abs(point.y())));
    }

    if (sanitizedPoints.isEmpty()) {
        return {};
    }

    std::sort(
        sanitizedPoints.begin(),
        sanitizedPoints.end(),
        [](const QPointF& left, const QPointF& right) {
            return left.x() < right.x();
        });

    double totalWeight = 0.0;
    for (const QPointF& point : sanitizedPoints) {
        totalWeight += point.y();
    }

    if (totalWeight <= 0.0) {
        return {};
    }

    const auto dominantIterator = std::max_element(
        sanitizedPoints.cbegin(),
        sanitizedPoints.cend(),
        [](const QPointF& left, const QPointF& right) {
            return left.y() < right.y();
        });

    double spectralCentroid = 0.0;
    double lowWeight = 0.0;
    QVector<double> probabilities;
    probabilities.reserve(sanitizedPoints.size());

    for (const QPointF& point : sanitizedPoints) {
        spectralCentroid += point.x() * point.y();
        if (point.x() <= 300.0) {
            lowWeight += point.y();
        }

        if (point.y() > 0.0) {
            probabilities.append(point.y() / totalWeight);
        }
    }

    spectralCentroid /= totalWeight;

    double spectralEntropy = 0.0;
    if (probabilities.size() > 1) {
        for (double probability : probabilities) {
            spectralEntropy -= probability * std::log(probability);
        }
        spectralEntropy /= std::log(static_cast<double>(probabilities.size()));
    }

    SpectrumMetrics metrics;
    metrics.valid = true;
    metrics.dominantFrequency = dominantIterator != sanitizedPoints.cend()
        ? dominantIterator->x()
        : 0.0;
    metrics.frequencyRangeMin = weightedFrequencyAtRatio(sanitizedPoints, 0.05);
    metrics.frequencyRangeMax = weightedFrequencyAtRatio(sanitizedPoints, 0.95);
    metrics.spectralCentroid = spectralCentroid;
    metrics.spectralEntropy = spectralEntropy;
    metrics.highLowRatio = lowWeight > 0.0
        ? std::max(0.0, totalWeight - lowWeight) / lowWeight
        : 0.0;
    return metrics;
}

EventMetrics computeEventMetrics(
    const QVariantList& events,
    const QVariantList& waveformPoints,
    double totalDurationSeconds)
{
    QVector<double> durationsMs;
    QVector<double> peakAmplitudes;
    QVector<double> rmsValues;
    QVector<double> startTimes;
    QVector<double> endTimes;

    durationsMs.reserve(events.size());
    peakAmplitudes.reserve(events.size());
    rmsValues.reserve(events.size());
    startTimes.reserve(events.size());
    endTimes.reserve(events.size());

    for (const QVariant& eventValue : events) {
        const QVariantMap event = eventValue.toMap();

        bool okDuration = false;
        const double durationMs = event.value(QStringLiteral("durationMs")).toDouble(&okDuration);
        if (okDuration && std::isfinite(durationMs)) {
            durationsMs.append(durationMs);
        }

        bool okPeak = false;
        const double peakAmplitude =
            std::abs(event.value(QStringLiteral("peakAmplitude")).toDouble(&okPeak));
        if (okPeak && std::isfinite(peakAmplitude)) {
            peakAmplitudes.append(peakAmplitude);
        }

        bool okRms = false;
        const double rms = std::abs(event.value(QStringLiteral("rms")).toDouble(&okRms));
        if (okRms && std::isfinite(rms)) {
            rmsValues.append(rms);
        }

        bool okStart = false;
        const double startSeconds =
            event.value(QStringLiteral("startSeconds")).toDouble(&okStart);
        if (okStart && std::isfinite(startSeconds)) {
            startTimes.append(startSeconds);
        }

        bool okEnd = false;
        const double endSeconds = event.value(QStringLiteral("endSeconds")).toDouble(&okEnd);
        if (okEnd && std::isfinite(endSeconds)) {
            endTimes.append(endSeconds);
        }
    }

    QVector<double> intervalValues;
    const int intervalCount = std::min(startTimes.size(), endTimes.size());
    intervalValues.reserve(std::max(0, intervalCount - 1));
    for (int index = 1; index < intervalCount; ++index) {
        intervalValues.append(std::max(0.0, startTimes[index] - endTimes[index - 1]));
    }

    EventMetrics metrics;
    for (double durationMs : durationsMs) {
        metrics.totalDurationMs += durationMs;
    }

    metrics.averageDurationMs = meanOfValues(durationsMs);
    metrics.averageIntervalSeconds = meanOfValues(intervalValues);
    metrics.peakAmplitude = peakAmplitudes.isEmpty()
        ? 0.0
        : *std::max_element(peakAmplitudes.cbegin(), peakAmplitudes.cend());

    if (totalDurationSeconds > 0.0) {
        metrics.eventRatePerMinute =
            static_cast<double>(events.size()) * 60.0 / totalDurationSeconds;
        metrics.burstRate =
            metrics.totalDurationMs / (totalDurationSeconds * 1000.0) * 100.0;
    }

    QVector<double> squaredRmsValues;
    squaredRmsValues.reserve(rmsValues.size());
    for (double rms : rmsValues) {
        squaredRmsValues.append(rms * rms);
    }
    metrics.rmsEnergy = meanOfValues(squaredRmsValues);

    if (metrics.rmsEnergy <= 0.0) {
        const QVector<QPointF> points = extractPointSeries(waveformPoints);
        QVector<double> squaredAmplitudes;
        squaredAmplitudes.reserve(points.size());
        for (const QPointF& point : points) {
            const double amplitude = std::abs(point.y());
            squaredAmplitudes.append(amplitude * amplitude);
            metrics.peakAmplitude = std::max(metrics.peakAmplitude, amplitude);
        }
        metrics.rmsEnergy = meanOfValues(squaredAmplitudes);
    }

    if (metrics.averageIntervalSeconds <= 0.0 && totalDurationSeconds > 0.0 && events.isEmpty()) {
        metrics.averageIntervalSeconds = totalDurationSeconds;
    }

    return metrics;
}

QString frequencyDeviationLabel(double value)
{
    if (value < 4.0) {
        return QStringLiteral("Low");
    }
    if (value > 5.0) {
        return QStringLiteral("High");
    }
    return QStringLiteral("Normal");
}

QString rmsDeviationLabel(double value)
{
    if (value < 0.001) {
        return QStringLiteral("Low");
    }
    if (value > 1.0) {
        return QStringLiteral("High");
    }
    return QStringLiteral("Normal");
}

QString intervalDeviationLabel(double value)
{
    if (value < 12.0) {
        return QStringLiteral("Short");
    }
    if (value > 15.0) {
        return QStringLiteral("Long");
    }
    return QStringLiteral("Normal");
}

QVector<QString> eventFeatureKeys()
{
    return {
        QStringLiteral("durationMs"),
        QStringLiteral("peakAmplitude"),
        QStringLiteral("peakToPeakAmplitude"),
        QStringLiteral("rms"),
        QStringLiteral("peakLogEnergy"),
        QStringLiteral("energyIntegral"),
        QStringLiteral("envelopePeak"),
        QStringLiteral("envelopeSlopeMean"),
        QStringLiteral("transientPeak"),
        QStringLiteral("transientMean"),
        QStringLiteral("kurtosisMean"),
        QStringLiteral("spectralEntropyMean"),
        QStringLiteral("spectralEntropyStd"),
        QStringLiteral("spectralCentroidMean"),
        QStringLiteral("bandwidthMean"),
        QStringLiteral("rolloffMean"),
        QStringLiteral("dominantFreqMean"),
        QStringLiteral("subbandLowEnergy"),
        QStringLiteral("subbandMidEnergy"),
        QStringLiteral("subbandHighEnergy"),
        QStringLiteral("subbandEntropy"),
        QStringLiteral("multiscaleEnergy1"),
        QStringLiteral("multiscaleEnergy2"),
        QStringLiteral("multiscaleEnergy3"),
        QStringLiteral("waveletEntropyMean"),
        QStringLiteral("logMel1Mean"),
        QStringLiteral("logMel2Mean"),
        QStringLiteral("logMel3Mean"),
        QStringLiteral("fdWeakMean"),
        QStringLiteral("candidateDensity"),
        QStringLiteral("candidateScoreMean"),
        QStringLiteral("maxCandidateScore"),
        QStringLiteral("silenceContrast"),
        QStringLiteral("eventScore"),
        QStringLiteral("stftEntropy200ms")
    };
}

double safeZScore(double value, double mean, double std)
{
    if (std <= 1e-12) {
        return 0.0;
    }
    return (value - mean) / std;
}

double safeMinMax(double value, double min, double max)
{
    const double range = max - min;
    if (range <= 1e-12) {
        return 0.0;
    }
    return (value - min) / range;
}

QVariantMap buildEventFeatureExport(const QVariantList& events)
{
    const QVector<QString> featureKeys = eventFeatureKeys();
    QVariantMap exportMap;
    exportMap.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    exportMap.insert(QStringLiteral("featureOrder"), QVariantList {});
    exportMap.insert(QStringLiteral("featureStats"), QVariantList {});
    exportMap.insert(QStringLiteral("events"), QVariantList {});
    exportMap.insert(QStringLiteral("matrixRaw"), QVariantList {});
    exportMap.insert(QStringLiteral("matrixZScore"), QVariantList {});
    exportMap.insert(QStringLiteral("matrixMinMax"), QVariantList {});
    exportMap.insert(QStringLiteral("eventCount"), events.size());
    exportMap.insert(QStringLiteral("featureCount"), featureKeys.size());
    exportMap.insert(QStringLiteral("normalization"), QVariantMap {
        { QStringLiteral("zscore"), QStringLiteral("(x-mean)/std") },
        { QStringLiteral("minmax"), QStringLiteral("(x-min)/(max-min)") }
    });

    if (events.isEmpty()) {
        QVariantList featureOrder;
        featureOrder.reserve(featureKeys.size());
        for (const QString& key : featureKeys) {
            featureOrder.append(key);
        }
        exportMap.insert(QStringLiteral("featureOrder"), featureOrder);
        return exportMap;
    }

    QVector<double> means(featureKeys.size(), 0.0);
    QVector<double> mins(featureKeys.size(), std::numeric_limits<double>::max());
    QVector<double> maxs(featureKeys.size(), std::numeric_limits<double>::lowest());
    QVector<QVector<double>> rawRows;
    rawRows.reserve(events.size());

    for (const QVariant& eventValue : events) {
        const QVariantMap event = eventValue.toMap();
        QVector<double> row;
        row.reserve(featureKeys.size());
        for (int index = 0; index < featureKeys.size(); ++index) {
            const double value = event.value(featureKeys[index]).toDouble();
            row.append(value);
            means[index] += value;
            mins[index] = std::min(mins[index], value);
            maxs[index] = std::max(maxs[index], value);
        }
        rawRows.append(row);
    }

    const int rowCount = rawRows.size() > 0 ? static_cast<int>(rawRows.size()) : 1;
    for (int index = 0; index < means.size(); ++index) {
        means[index] /= static_cast<double>(rowCount);
    }

    QVector<double> stds(featureKeys.size(), 0.0);
    for (const QVector<double>& row : rawRows) {
        for (int index = 0; index < row.size(); ++index) {
            const double centered = row[index] - means[index];
            stds[index] += centered * centered;
        }
    }
    const int varianceDenominator =
        rawRows.size() > 1 ? static_cast<int>(rawRows.size()) - 1 : 1;
    for (int index = 0; index < stds.size(); ++index) {
        stds[index] = std::sqrt(stds[index] / static_cast<double>(varianceDenominator));
    }

    QVariantList featureOrder;
    QVariantList featureStats;
    featureOrder.reserve(featureKeys.size());
    featureStats.reserve(featureKeys.size());
    for (int index = 0; index < featureKeys.size(); ++index) {
        featureOrder.append(featureKeys[index]);
        featureStats.append(QVariantMap {
            { QStringLiteral("name"), featureKeys[index] },
            { QStringLiteral("mean"), means[index] },
            { QStringLiteral("std"), stds[index] },
            { QStringLiteral("min"), mins[index] },
            { QStringLiteral("max"), maxs[index] }
        });
    }

    QVariantList eventList;
    QVariantList matrixRaw;
    QVariantList matrixZ;
    QVariantList matrixMinMax;
    eventList.reserve(rawRows.size());
    matrixRaw.reserve(rawRows.size());
    matrixZ.reserve(rawRows.size());
    matrixMinMax.reserve(rawRows.size());

    for (int rowIndex = 0; rowIndex < rawRows.size(); ++rowIndex) {
        const QVariantMap event = events[rowIndex].toMap();
        const QVector<double>& row = rawRows[rowIndex];

        QVariantMap rawFeatureMap;
        QVariantMap zFeatureMap;
        QVariantMap minMaxFeatureMap;
        QVariantList rawRow;
        QVariantList zRow;
        QVariantList minMaxRow;
        rawRow.reserve(row.size());
        zRow.reserve(row.size());
        minMaxRow.reserve(row.size());

        for (int index = 0; index < row.size(); ++index) {
            const QString& key = featureKeys[index];
            const double raw = row[index];
            const double z = safeZScore(raw, means[index], stds[index]);
            const double minMax = safeMinMax(raw, mins[index], maxs[index]);

            rawFeatureMap.insert(key, raw);
            zFeatureMap.insert(key, z);
            minMaxFeatureMap.insert(key, minMax);
            rawRow.append(raw);
            zRow.append(z);
            minMaxRow.append(minMax);
        }

        eventList.append(QVariantMap {
            { QStringLiteral("eventIndex"), event.value(QStringLiteral("eventIndex")) },
            { QStringLiteral("isRecognized"), event.value(QStringLiteral("isRecognized")) },
            { QStringLiteral("startSeconds"), event.value(QStringLiteral("startSeconds")) },
            { QStringLiteral("endSeconds"), event.value(QStringLiteral("endSeconds")) },
            { QStringLiteral("durationMs"), event.value(QStringLiteral("durationMs")) },
            { QStringLiteral("rawFeatures"), rawFeatureMap },
            { QStringLiteral("zscoreFeatures"), zFeatureMap },
            { QStringLiteral("minmaxFeatures"), minMaxFeatureMap }
        });

        matrixRaw.append(rawRow);
        matrixZ.append(zRow);
        matrixMinMax.append(minMaxRow);
    }

    exportMap.insert(QStringLiteral("featureOrder"), featureOrder);
    exportMap.insert(QStringLiteral("featureStats"), featureStats);
    exportMap.insert(QStringLiteral("events"), eventList);
    exportMap.insert(QStringLiteral("matrixRaw"), matrixRaw);
    exportMap.insert(QStringLiteral("matrixZScore"), matrixZ);
    exportMap.insert(QStringLiteral("matrixMinMax"), matrixMinMax);
    return exportMap;
}

QString buildSeriesPreview(const QVariantList& values, int maxItems = 12, int precision = 4)
{
    if (values.isEmpty()) {
        return QStringLiteral("无");
    }

    QStringList previewParts;
    const qsizetype previewCount =
        std::min<qsizetype>(static_cast<qsizetype>(maxItems), values.size());
    previewParts.reserve(
        static_cast<int>(previewCount) + (values.size() > previewCount ? 1 : 0));

    for (qsizetype index = 0; index < previewCount; ++index) {
        bool ok = false;
        const double number = values.at(index).toDouble(&ok);
        previewParts.append(ok ? formatNumber(number, precision) : QStringLiteral("N/A"));
    }

    if (values.size() > previewCount) {
        previewParts.append(QStringLiteral("..."));
    }

    return previewParts.join(QStringLiteral(", "));
}

int resolveRecognizedEventCount(const QVariantMap& featureValues, const QVariantList& segments)
{
    int recognizedEventCount =
        featureValues.value(QStringLiteral("bowelSoundOccurrenceCount")).toInt();
    if (recognizedEventCount <= 0) {
        recognizedEventCount =
            featureValues.value(QStringLiteral("recognizedEventCount")).toInt();
    }
    if (recognizedEventCount <= 0 && !segments.isEmpty()) {
        recognizedEventCount = segments.size();
    }

    return recognizedEventCount;
}

double resolveRecognizedTotalDurationMs(
    const QVariantMap& featureValues,
    const QVariantList& segments)
{
    const double cachedDurationMs =
        featureValues.value(QStringLiteral("recognizedTotalDurationMs")).toDouble();
    if (cachedDurationMs > 0.0) {
        return cachedDurationMs;
    }

    double totalDurationMs = 0.0;
    for (const QVariant& segmentValue : segments) {
        totalDurationMs += segmentValue.toMap().value(QStringLiteral("durationMs")).toDouble();
    }

    return totalDurationMs;
}

QString buildRecognizedSegmentsRows(const QVariantList& segments)
{
    if (segments.isEmpty()) {
        return QStringLiteral(
            "<tr><td colspan=\"6\" class=\"empty\">未识别到肠鸣音事件</td></tr>");
    }

    QString rows;
    QTextStream stream(&rows);
    for (int index = 0; index < segments.size(); ++index) {
        const QVariantMap segment = segments.at(index).toMap();
        const qint64 startSample = segment.value(QStringLiteral("startSample")).toLongLong();
        const qint64 endSample = segment.value(QStringLiteral("endSample")).toLongLong();
        const double startSeconds =
            segment.value(QStringLiteral("startSeconds")).toDouble();
        const double endSeconds =
            segment.value(QStringLiteral("endSeconds")).toDouble();
        const double durationMs =
            segment.value(QStringLiteral("durationMs")).toDouble();

        stream << "<tr>";
        stream << "<td>" << formatInteger(index + 1) << "</td>";
        stream << "<td>" << formatInteger(startSample) << "</td>";
        stream << "<td>" << formatInteger(endSample) << "</td>";
        stream << "<td>" << formatNumber(startSeconds, 3) << "</td>";
        stream << "<td>" << formatNumber(endSeconds, 3) << "</td>";
        stream << "<td>" << formatNumber(durationMs, 3) << "</td>";
        stream << "</tr>";
    }

    return rows;
}

QString buildRecognizedEventParameterRows(const QVariantList& events)
{
    if (events.isEmpty()) {
        return QStringLiteral(
            "<tr><td colspan=\"12\" class=\"empty\">未识别到肠鸣音事件</td></tr>");
    }

    QString rows;
    QTextStream stream(&rows);
    for (int index = 0; index < events.size(); ++index) {
        const QVariantMap event = events.at(index).toMap();

        stream << "<tr>";
        stream << "<td>"
               << formatInteger(event.value(QStringLiteral("eventIndex")).toLongLong())
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("durationMs")).toDouble(), 3)
               << "</td>";
        stream << "<td>"
               << formatInteger(event.value(QStringLiteral("sampleCount")).toLongLong())
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("peakAmplitude")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("peakToPeakAmplitude")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("meanAmplitude")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatNumber(
                      event.value(QStringLiteral("meanAbsoluteAmplitude")).toDouble(),
                      4)
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("rms")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatInteger(event.value(QStringLiteral("frameCount")).toLongLong())
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("steMean")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("zcrMean")).toDouble(), 4)
               << "</td>";
        stream << "<td>"
               << formatNumber(event.value(QStringLiteral("fdMean")).toDouble(), 4)
               << "</td>";
        stream << "</tr>";
    }

    return rows;
}

QVariant normalizeVariantForJson(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    if (value.metaType().id() == QMetaType::QPointF || value.canConvert<QPointF>()) {
        const QPointF point = value.toPointF();
        QVariantMap pointMap;
        pointMap.insert(QStringLiteral("x"), point.x());
        pointMap.insert(QStringLiteral("y"), point.y());
        return pointMap;
    }

    if (value.metaType().id() == QMetaType::QStringList) {
        QVariantList list;
        const QStringList stringList = value.toStringList();
        list.reserve(stringList.size());
        for (const QString& item : stringList) {
            list.append(item);
        }
        return list;
    }

    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList normalizedList;
        const QVariantList list = value.toList();
        normalizedList.reserve(list.size());
        for (const QVariant& item : list) {
            normalizedList.append(normalizeVariantForJson(item));
        }
        return normalizedList;
    }

    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap normalizedMap;
        const QVariantMap map = value.toMap();
        for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
            normalizedMap.insert(iterator.key(), normalizeVariantForJson(iterator.value()));
        }
        return normalizedMap;
    }

    return value;
}

QVariant mergeJsonTemplate(const QVariant& templateValue, const QVariant& payloadValue)
{
    const QVariant normalizedPayload = normalizeVariantForJson(payloadValue);

    if (templateValue.metaType().id() == QMetaType::QVariantMap &&
        normalizedPayload.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap mergedMap = templateValue.toMap();
        const QVariantMap payloadMap = normalizedPayload.toMap();
        for (auto iterator = payloadMap.cbegin(); iterator != payloadMap.cend(); ++iterator) {
            if (mergedMap.contains(iterator.key())) {
                mergedMap.insert(
                    iterator.key(),
                    mergeJsonTemplate(mergedMap.value(iterator.key()), iterator.value()));
            } else {
                mergedMap.insert(iterator.key(), iterator.value());
            }
        }
        return mergedMap;
    }

    return normalizedPayload;
}

QVariantMap buildTemplateContext(const QVariantMap& featureValues, const QDateTime& generatedAt)
{
    const int sampleRate = featureValues.value(QStringLiteral("sampleRate")).toInt();
    const int sampleCount = featureValues.value(QStringLiteral("sampleCount")).toInt();
    const double signalDurationSeconds =
        sampleRate > 0
            ? static_cast<double>(sampleCount) / static_cast<double>(sampleRate)
            : 0.0;

    const QVariantList steValues = featureValues.value(QStringLiteral("steValues")).toList();
    const QVariantList zcrValues = featureValues.value(QStringLiteral("zcrValues")).toList();
    const QVariantList fdValues = featureValues.value(QStringLiteral("fdValues")).toList();
    const QVariantList recognizedSegments =
        featureValues.value(QStringLiteral("recognizedSegments")).toList();
    const QVariantList candidateSegments =
        featureValues.value(QStringLiteral("candidateSegments")).toList();

    const NumericStats steStats = computeStats(steValues);
    const NumericStats zcrStats = computeStats(zcrValues);
    const NumericStats fdStats = computeStats(fdValues);

    const int recognizedEventCount =
        resolveRecognizedEventCount(featureValues, recognizedSegments);
    const bool hasBowelSound =
        featureValues.value(QStringLiteral("hasBowelSound")).toBool();
    const double recognizedTotalDurationMs =
        resolveRecognizedTotalDurationMs(featureValues, recognizedSegments);

    QStringList structureFields = featureValues.keys();
    structureFields.sort(Qt::CaseInsensitive);

    QString analysisNote;
    if (hasBowelSound) {
        analysisNote = QStringLiteral(
                           "本次分析识别到 %1 个肠鸣音事件，累计识别时长约 %2 ms。")
                           .arg(formatInteger(recognizedEventCount))
                           .arg(formatNumber(recognizedTotalDurationMs, 3));
    } else {
        analysisNote = QStringLiteral(
            "本次分析未识别到满足当前阈值条件的肠鸣音事件。");
    }

    QVariantMap context;
    context.insert(
        QStringLiteral("generated_at"),
        generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    context.insert(QStringLiteral("data_source"), QStringLiteral("导入 WAV 文件"));
    context.insert(
        QStringLiteral("sample_rate"),
        sampleRate > 0
            ? formatInteger(sampleRate) + QStringLiteral(" Hz")
            : QStringLiteral("未知"));
    context.insert(QStringLiteral("sample_count"), formatInteger(sampleCount));
    context.insert(
        QStringLiteral("signal_duration_seconds"),
        formatNumber(signalDurationSeconds, 3) + QStringLiteral(" s"));
    context.insert(
        QStringLiteral("has_bowel_sound"),
        hasBowelSound ? QStringLiteral("是") : QStringLiteral("否"));
    context.insert(
        QStringLiteral("recognized_event_count"),
        formatInteger(recognizedEventCount));
    context.insert(
        QStringLiteral("recognized_total_duration_ms"),
        formatNumber(recognizedTotalDurationMs, 3) + QStringLiteral(" ms"));
    context.insert(
        QStringLiteral("candidate_event_count"),
        formatInteger(candidateSegments.size()));
    context.insert(
        QStringLiteral("feature_frame_count"),
        formatInteger(steStats.count));
    context.insert(
        QStringLiteral("frame_length_ms"),
        formatNumber(featureValues.value(QStringLiteral("frameLengthMs")).toDouble(), 3) +
            QStringLiteral(" ms"));
    context.insert(
        QStringLiteral("frame_shift_ms"),
        formatNumber(featureValues.value(QStringLiteral("frameShiftMs")).toDouble(), 3) +
            QStringLiteral(" ms"));
    context.insert(
        QStringLiteral("max_silence_ms"),
        formatNumber(featureValues.value(QStringLiteral("maxSilenceMs")).toDouble(), 3) +
            QStringLiteral(" ms"));
    context.insert(
        QStringLiteral("threshold_t"),
        formatNumber(featureValues.value(QStringLiteral("thresholdT")).toDouble(), 4));
    context.insert(
        QStringLiteral("ste_threshold"),
        formatNumber(featureValues.value(QStringLiteral("steThreshold")).toDouble(), 4));
    context.insert(
        QStringLiteral("zcr_threshold"),
        formatNumber(featureValues.value(QStringLiteral("zcrThreshold")).toDouble(), 4));
    context.insert(
        QStringLiteral("fd_threshold"),
        formatNumber(featureValues.value(QStringLiteral("fdThreshold")).toDouble(), 4));
    context.insert(
        QStringLiteral("structure_fields"),
        structureFields.join(QStringLiteral(", ")).toHtmlEscaped());
    context.insert(
        QStringLiteral("analysis_note"),
        analysisNote.toHtmlEscaped());
    context.insert(
        QStringLiteral("recognized_segments_rows"),
        buildRecognizedSegmentsRows(recognizedSegments));
    context.insert(
        QStringLiteral("recognized_event_parameter_rows"),
        buildRecognizedEventParameterRows(recognizedSegments));
    context.insert(
        QStringLiteral("ste_frame_count"),
        formatInteger(steStats.count));
    context.insert(
        QStringLiteral("ste_min"),
        formatStatValue(steStats, steStats.min, 4));
    context.insert(
        QStringLiteral("ste_max"),
        formatStatValue(steStats, steStats.max, 4));
    context.insert(
        QStringLiteral("ste_mean"),
        formatStatValue(steStats, steStats.mean, 4));
    context.insert(
        QStringLiteral("ste_preview"),
        buildSeriesPreview(steValues).toHtmlEscaped());
    context.insert(
        QStringLiteral("zcr_frame_count"),
        formatInteger(zcrStats.count));
    context.insert(
        QStringLiteral("zcr_min"),
        formatStatValue(zcrStats, zcrStats.min, 4));
    context.insert(
        QStringLiteral("zcr_max"),
        formatStatValue(zcrStats, zcrStats.max, 4));
    context.insert(
        QStringLiteral("zcr_mean"),
        formatStatValue(zcrStats, zcrStats.mean, 4));
    context.insert(
        QStringLiteral("zcr_preview"),
        buildSeriesPreview(zcrValues).toHtmlEscaped());
    context.insert(
        QStringLiteral("fd_frame_count"),
        formatInteger(fdStats.count));
    context.insert(
        QStringLiteral("fd_min"),
        formatStatValue(fdStats, fdStats.min, 4));
    context.insert(
        QStringLiteral("fd_max"),
        formatStatValue(fdStats, fdStats.max, 4));
    context.insert(
        QStringLiteral("fd_mean"),
        formatStatValue(fdStats, fdStats.mean, 4));
    context.insert(
        QStringLiteral("fd_preview"),
        buildSeriesPreview(fdValues).toHtmlEscaped());

    return context;
}
} // namespace

namespace {
QStringList runtimeRootCandidates()
{
    const QString applicationDirectory =
        QFileInfo(QCoreApplication::applicationDirPath()).absoluteFilePath();

    QStringList candidateRoots {
        applicationDirectory,
        QDir(applicationDirectory).filePath(QStringLiteral("distribution"))
    };

    QStringList normalizedRoots;
    for (const QString& candidateRoot : candidateRoots) {
        const QString normalizedRoot = QDir::cleanPath(candidateRoot);
        if (!normalizedRoots.contains(normalizedRoot)) {
            normalizedRoots.append(normalizedRoot);
        }
    }

    return normalizedRoots;
}

QString resolveTemplateJsonPath(const QString& reportKind)
{
    const QString normalizedReportKind = reportKind.trimmed().toUpper();
    const QString templateFileName = normalizedReportKind + QStringLiteral(".json");

    QStringList candidatePaths;
    const QStringList candidateRoots = runtimeRootCandidates();
    for (const QString& candidateRoot : candidateRoots) {
        candidatePaths.append(
            QDir(candidateRoot).filePath(
                QStringLiteral("Generate/DataChunk/") + templateFileName));
    }

    for (const QString& candidatePath : candidatePaths) {
        if (QFileInfo::exists(candidatePath)) {
            return QFileInfo(candidatePath).absoluteFilePath();
        }
    }

    return {};
}

bool createOrUpdateTemporaryJsonFromTemplate(
    const QVariantMap& reportPayload,
    const QString& reportKind,
    const QString& preferredJsonPath,
    QString* jsonFilePath,
    QString* errorMessage)
{
    const QString normalizedReportKind = reportKind.trimmed().toUpper();

    if (jsonFilePath == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告 JSON 临时文件输出参数无效");
        }
        return false;
    }

    const QString templateJsonPath = resolveTemplateJsonPath(reportKind);
    if (templateJsonPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未找到报告 JSON 模板: %1")
                                .arg(normalizedReportKind);
        }
        return false;
    }

    QDir tempDirectory(DatabaseStoragePaths::TEMPORARY_FILE_PATH);
    if (!tempDirectory.exists() && !QDir().mkpath(tempDirectory.path())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建临时目录: %1")
                                .arg(DatabaseStoragePaths::TEMPORARY_FILE_PATH);
        }
        return false;
    }

    QFile templateFile(templateJsonPath);
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("无法读取报告 JSON 模板: %1").arg(templateJsonPath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument templateDocument =
        QJsonDocument::fromJson(templateFile.readAll(), &parseError);
    templateFile.close();

    if (parseError.error != QJsonParseError::NoError || !templateDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告 JSON 模板格式无效: %1")
                                .arg(parseError.errorString());
        }
        return false;
    }

    QString outputJsonPath = preferredJsonPath;
    if (outputJsonPath.isEmpty()) {
        QTemporaryFile temporaryJsonFile(
            tempDirectory.filePath(normalizedReportKind + QStringLiteral("_XXXXXX.json")));
        temporaryJsonFile.setAutoRemove(false);
        if (!temporaryJsonFile.open()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    QStringLiteral("无法创建临时报告 JSON 文件: %1")
                        .arg(tempDirectory.path());
            }
            return false;
        }

        outputJsonPath = temporaryJsonFile.fileName();
        temporaryJsonFile.close();
    }

    const QVariant mergedPayload = mergeJsonTemplate(templateDocument.toVariant(), reportPayload);
    const QJsonDocument document =
        QJsonDocument::fromVariant(mergedPayload);

    QFile outputJsonFile(outputJsonPath);
    if (!outputJsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("无法写入报告 JSON 文件: %1")
                    .arg(outputJsonPath);
        }
        return false;
    }

    const QByteArray bytes = document.toJson(QJsonDocument::Indented);
    if (outputJsonFile.write(bytes) != bytes.size()) {
        outputJsonFile.close();
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("无法完整写入报告 JSON 文件: %1")
                    .arg(outputJsonPath);
        }
        return false;
    }

    outputJsonFile.close();
    *jsonFilePath = outputJsonPath;
    return true;
}
} // namespace

class PDFExport
{
public:
    QString exportReport(
        const QVariantMap& reportPayload,
        const QString& reportKind,
        const QString& pdfFilePath,
        QString* errorMessage) const
    {
        QString jsonFilePath;

        if (!createOrUpdateTemporaryJsonFromTemplate(
                reportPayload,
                reportKind,
            QString(),
                &jsonFilePath,
                errorMessage)) {
            QFile::remove(jsonFilePath);
            return {};
        }

        const bool exported = invokePythonExporter(jsonFilePath, pdfFilePath, errorMessage);
        QFile::remove(jsonFilePath);
        if (!exported) {
            return {};
        }

        const QFileInfo generatedPdfInfo(pdfFilePath);
        if (!generatedPdfInfo.exists() || generatedPdfInfo.size() <= 0) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    QStringLiteral("PDF 文件未成功生成: %1").arg(pdfFilePath);
            }
            return {};
        }

        return pdfFilePath;
    }

    QString exportReportFromJson(
        const QString& jsonFilePath,
        const QString& pdfFilePath,
        QString* errorMessage) const
    {
        if (jsonFilePath.isEmpty() || !QFileInfo::exists(jsonFilePath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("报告 JSON 临时文件不存在: %1")
                                    .arg(jsonFilePath);
            }
            return {};
        }

        if (!invokePythonExporter(jsonFilePath, pdfFilePath, errorMessage)) {
            return {};
        }

        const QFileInfo generatedPdfInfo(pdfFilePath);
        if (!generatedPdfInfo.exists() || generatedPdfInfo.size() <= 0) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    QStringLiteral("PDF 文件未成功生成: %1").arg(pdfFilePath);
            }
            return {};
        }

        return pdfFilePath;
    }

private:
    static QString resolveBundledPythonExecutable()
    {
        QStringList runtimeDirectoryCandidates;
        const QStringList candidateRoots = runtimeRootCandidates();
        for (const QString& candidateRoot : candidateRoots) {
            runtimeDirectoryCandidates.append(
                QDir(candidateRoot).filePath(QStringLiteral("PythonModules/Runtime")));
        }

        const QRegularExpression runtimeDirPattern(
            QStringLiteral(
                "^Python-(\\d+\\.\\d+\\.\\d+)-embed-(amd64|x64|arm64|x86)$"),
            QRegularExpression::CaseInsensitiveOption);

        QString selectedPythonPath;
        QVersionNumber selectedVersion(-1, -1, -1);

        for (const QString& runtimeRootPath : runtimeDirectoryCandidates) {
            const QDir runtimeRoot(runtimeRootPath);
            if (!runtimeRoot.exists()) {
                continue;
            }

            const QString directPythonPath =
                runtimeRoot.filePath(QStringLiteral("python.exe"));
            if (QFileInfo::exists(directPythonPath)) {
                return QFileInfo(directPythonPath).absoluteFilePath();
            }

            const QFileInfoList runtimeCandidates = runtimeRoot.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo& runtimeCandidate : runtimeCandidates) {
                const QRegularExpressionMatch match =
                    runtimeDirPattern.match(runtimeCandidate.fileName());
                if (!match.hasMatch()) {
                    continue;
                }

                const QVersionNumber currentVersion =
                    QVersionNumber::fromString(match.captured(1));
                if (currentVersion.isNull()) {
                    continue;
                }

                const QString pythonExecutablePath =
                    QDir(runtimeCandidate.absoluteFilePath())
                        .filePath(QStringLiteral("python.exe"));
                if (!QFileInfo::exists(pythonExecutablePath)) {
                    continue;
                }

                if (selectedPythonPath.isEmpty() ||
                    QVersionNumber::compare(currentVersion, selectedVersion) > 0) {
                    selectedVersion = currentVersion;
                    selectedPythonPath =
                        QFileInfo(pythonExecutablePath).absoluteFilePath();
                }
            }
        }

        return selectedPythonPath;
    }

    static QString resolvePythonExecutable()
    {
        return resolveBundledPythonExecutable();
    }

    static QString resolvePythonScriptPath()
    {
        QStringList candidatePaths;
        const QStringList candidateRoots = runtimeRootCandidates();
        for (const QString& candidateRoot : candidateRoots) {
            candidatePaths.append(
                QDir(candidateRoot).filePath(
                    QStringLiteral("PythonModules/Scripts/PDFExport.py")));
            candidatePaths.append(
                QDir(candidateRoot).filePath(
                    QStringLiteral("PythonModules/PDFExport.py")));
        }

        for (const QString& candidatePath : candidatePaths) {
            if (QFileInfo::exists(candidatePath)) {
                return QFileInfo(candidatePath).absoluteFilePath();
            }
        }

        return candidatePaths.constFirst();
    }

    static bool invokePythonExporter(
        const QString& jsonFilePath,
        const QString& pdfFilePath,
        QString* errorMessage)
    {
        const QString pythonScriptPath = resolvePythonScriptPath();
        if (!QFileInfo::exists(pythonScriptPath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                    "未找到 PDF 导出脚本: %1").arg(pythonScriptPath);
            }
            return false;
        }

        QProcess process;
        const QString pythonExecutablePath = resolvePythonExecutable();
        if (pythonExecutablePath.isEmpty() || !QFileInfo::exists(pythonExecutablePath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                    "未找到内置 Python 运行时，请检查 PythonModules/Runtime 目录");
            }
            return false;
        }

        process.setWorkingDirectory(QCoreApplication::applicationDirPath());
        process.start(
            pythonExecutablePath,
            QStringList()
                << QStringLiteral("-B")
                << pythonScriptPath
                << jsonFilePath
                << pdfFilePath);

        if (!process.waitForStarted(10000)) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    QStringLiteral("无法启动 PDF 导出脚本: %1")
                        .arg(process.errorString());
            }
            return false;
        }

        if (!process.waitForFinished(180000)) {
            process.kill();
            process.waitForFinished(5000);
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("PDF 导出脚本执行超时");
            }
            return false;
        }

        const QString stdOut =
            QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        const QString stdErr =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                                    "PDF 导出脚本执行失败。stdout: %1 stderr: %2")
                                    .arg(stdOut, stdErr);
            }
            return false;
        }

        if (!stdOut.isEmpty()) {
            qDebug() << "GenerateManager/PDFExport:" << stdOut;
        }

        if (!stdErr.isEmpty()) {
            qDebug() << "GenerateManager/PDFExport stderr:" << stdErr;
        }

        return true;
    }
};

GenerateManager::GenerateManager(QObject* parent)
    : QObject(parent)
    , m_pdfExport(std::make_unique<PDFExport>())
{
}

GenerateManager::~GenerateManager()
{
    if (m_exportThread != nullptr && m_exportThread->isRunning()) {
        m_exportThread->wait();
    }
}

bool GenerateManager::busy() const
{
    return m_busy;
}

void GenerateManager::startExportIdentificationAndFeatureExtractionReport()
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    m_exportThread = QThread::create([this]() {
        exportIdentificationAndFeatureExtractionReport();
        QMetaObject::invokeMethod(
            this,
            [this]() {
                setBusy(false);
            },
            Qt::QueuedConnection);
    });

    connect(
        m_exportThread,
        &QThread::finished,
        this,
        [this]() {
            m_exportThread = nullptr;
        });
    connect(m_exportThread, &QThread::finished, m_exportThread, &QObject::deleteLater);
    m_exportThread->start();
}

bool GenerateManager::persistImportedAnalysisTemporaryJson(
    const QVariantMap& featureValues,
    QString* errorMessage)
{
    if (featureValues.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前没有可写入的分析结果");
        }
        return false;
    }

    const QString reportKind = QStringLiteral("AFDROIS");
    const QDateTime generatedAt = QDateTime::currentDateTime();
    const QString draftReportId =
        QStringLiteral("%1-DRAFT").arg(reportKind);

    const QVariantMap payload =
        buildReportPayload(featureValues, draftReportId, reportKind, generatedAt);
    QString jsonFilePath;
    const QString preferredPath = DataManager::instance()->importedFeatureTemporaryFilePath();
    if (!createOrUpdateTemporaryJsonFromTemplate(
            payload,
            reportKind,
            preferredPath,
            &jsonFilePath,
            errorMessage)) {
        return false;
    }

    DataManager::instance()->setImportedFeatureTemporaryFilePath(jsonFilePath);
    return true;
}

QString GenerateManager::exportIdentificationAndFeatureExtractionReport()
{
    if (!ensureOutputDirectoryExists()) {
        const QString errorMessage =
            QStringLiteral("无法创建报告输出目录: %1")
                .arg(DatabaseStoragePaths::REPORTS_PATH);
        qWarning() << "GenerateManager:" << errorMessage;
        emit exportFailed(errorMessage);
        return {};
    }

    const QString reportKind = QStringLiteral("AFDROIS");
    const QDateTime generatedAt = QDateTime::currentDateTime();
    const QString reportId = createReportId(reportKind, generatedAt.date());
    const QVariantMap featureValues = DataManager::instance()->importedFeatureValues();
    if (featureValues.isEmpty()) {
        const QString errorMessage =
            QStringLiteral("当前没有可导出的分析结果");
        qWarning() << "GenerateManager:" << errorMessage;
        emit exportFailed(errorMessage);
        return {};
    }

    const QVariantMap reportPayload =
        buildReportPayload(featureValues, reportId, reportKind, generatedAt);
    const QString pdfFilePath = createOutputFilePath(reportId, QStringLiteral("pdf"));
    const QString preferredJsonPath = DataManager::instance()->importedFeatureTemporaryFilePath();
    QString jsonFilePath;

    QString errorMessage;
    if (!createOrUpdateTemporaryJsonFromTemplate(
            reportPayload,
            reportKind,
            preferredJsonPath,
            &jsonFilePath,
            &errorMessage)) {
        qWarning() << "GenerateManager:" << errorMessage;
        emit exportFailed(errorMessage);
        return {};
    }

    DataManager::instance()->setImportedFeatureTemporaryFilePath(jsonFilePath);

    const QString exportedPdfPath =
        m_pdfExport->exportReportFromJson(jsonFilePath, pdfFilePath, &errorMessage);
    if (exportedPdfPath.isEmpty()) {
        qWarning() << "GenerateManager:" << errorMessage;
        emit exportFailed(errorMessage);
        return {};
    }

    qDebug() << "GenerateManager: report exported to" << exportedPdfPath;
    emit exportCompleted(exportedPdfPath);
    return exportedPdfPath;
}

void GenerateManager::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

bool GenerateManager::ensureOutputDirectoryExists()
{
    if (QDir(DatabaseStoragePaths::REPORTS_PATH).exists()) {
        return true;
    }

    return QDir().mkpath(DatabaseStoragePaths::REPORTS_PATH);
}

QString GenerateManager::createReportId(const QString& reportKind, const QDate& reportDate)
{
    const QString normalizedReportKind = reportKind.trimmed().toUpper();
    const QString dateCode = reportDate.toString(QStringLiteral("yyyyMMdd"));
    const QRegularExpression pattern(
        QStringLiteral("^%1-%2-(\\d{4})$")
            .arg(QRegularExpression::escape(normalizedReportKind), dateCode));

    QDir reportDirectory(DatabaseStoragePaths::REPORTS_PATH);
    const QFileInfoList existingFiles =
        reportDirectory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    int maxSequence = 0;
    for (const QFileInfo& existingFile : existingFiles) {
        const QRegularExpressionMatch match =
            pattern.match(existingFile.completeBaseName());
        if (!match.hasMatch()) {
            continue;
        }

        const int sequence = match.captured(1).toInt();
        maxSequence = std::max(maxSequence, sequence);
    }

    return QStringLiteral("%1-%2-%3")
        .arg(normalizedReportKind, dateCode, QStringLiteral("%1").arg(maxSequence + 1, 4, 10, QChar('0')));
}

QString GenerateManager::createOutputFilePath(const QString& reportId, const QString& extension)
{
    const QString safeExtension = extension.startsWith('.')
        ? extension.mid(1)
        : extension;

    QDir reportDirectory(DatabaseStoragePaths::REPORTS_PATH);
    QString filePath =
        reportDirectory.filePath(QStringLiteral("%1.%2").arg(reportId, safeExtension));

    int suffix = 1;
    while (QFileInfo::exists(filePath)) {
        filePath = reportDirectory.filePath(
            QStringLiteral("%1_%2.%3").arg(reportId).arg(suffix).arg(safeExtension));
        ++suffix;
    }

    return filePath;
}

QVariantMap GenerateManager::buildReportPayload(
    const QVariantMap& featureValues,
    const QString& reportId,
    const QString& reportKind,
    const QDateTime& generatedAt)
{
    DataManager* dataManager = DataManager::instance();

    QVariantMap templateContext = buildTemplateContext(featureValues, generatedAt);
    templateContext.insert(QStringLiteral("report_id"), reportId);
    templateContext.insert(
        QStringLiteral("record_date"),
        generatedAt.date().toString(QStringLiteral("yyyy-MM-dd")));
    const QVariantList candidateSegments =
        featureValues.value(QStringLiteral("candidateSegments")).toList();
    const QVariantList recognizedSegments =
        featureValues.value(QStringLiteral("recognizedSegments")).toList();
    const QVariantList steValues = featureValues.value(QStringLiteral("steValues")).toList();
    const QVariantList zcrValues = featureValues.value(QStringLiteral("zcrValues")).toList();
    const QVariantList fdValues = featureValues.value(QStringLiteral("fdValues")).toList();

    const NumericStats steStats = computeStats(steValues);
    const NumericStats zcrStats = computeStats(zcrValues);
    const NumericStats fdStats = computeStats(fdValues);

    const double waveformDurationSeconds = dataManager->importedWaveformDuration();
    const double spectrumBoundaryHz = dataManager->importedSpectrumBoundary();
    const QVariantList waveformPoints = normalizeVariantForJson(
        waveformDurationSeconds > 0.0
            ? dataManager->importedDownsampledWaveformPoints(0.0, waveformDurationSeconds)
            : QVariantList {}).toList();
    const QVariantList spectrumPoints = normalizeVariantForJson(
        spectrumBoundaryHz > 0.0
            ? dataManager->importedSpectrumPoints(0.0, spectrumBoundaryHz)
            : QVariantList {}).toList();

    QVariantMap waveformCache;
    waveformCache.insert(
        QStringLiteral("sampleRate"),
        dataManager->importedWaveformSampleRate());
    waveformCache.insert(QStringLiteral("durationSeconds"), waveformDurationSeconds);
    waveformCache.insert(QStringLiteral("downsampledPoints"), waveformPoints);

    QVariantMap spectrumCache;
    spectrumCache.insert(QStringLiteral("boundaryHz"), spectrumBoundaryHz);
    spectrumCache.insert(QStringLiteral("points"), spectrumPoints);

    QVariantMap featureStats;
    featureStats.insert(QStringLiteral("ste"), QVariantMap {
        { QStringLiteral("count"), steStats.count },
        { QStringLiteral("min"), steStats.count > 0 ? steStats.min : 0.0 },
        { QStringLiteral("max"), steStats.count > 0 ? steStats.max : 0.0 },
        { QStringLiteral("mean"), steStats.count > 0 ? steStats.mean : 0.0 },
        { QStringLiteral("preview"), buildSeriesPreview(steValues) }
    });
    featureStats.insert(QStringLiteral("zcr"), QVariantMap {
        { QStringLiteral("count"), zcrStats.count },
        { QStringLiteral("min"), zcrStats.count > 0 ? zcrStats.min : 0.0 },
        { QStringLiteral("max"), zcrStats.count > 0 ? zcrStats.max : 0.0 },
        { QStringLiteral("mean"), zcrStats.count > 0 ? zcrStats.mean : 0.0 },
        { QStringLiteral("preview"), buildSeriesPreview(zcrValues) }
    });
    featureStats.insert(QStringLiteral("fd"), QVariantMap {
        { QStringLiteral("count"), fdStats.count },
        { QStringLiteral("min"), fdStats.count > 0 ? fdStats.min : 0.0 },
        { QStringLiteral("max"), fdStats.count > 0 ? fdStats.max : 0.0 },
        { QStringLiteral("mean"), fdStats.count > 0 ? fdStats.mean : 0.0 },
        { QStringLiteral("preview"), buildSeriesPreview(fdValues) }
    });

    const int sampleRate = featureValues.value(QStringLiteral("sampleRate")).toInt();
    const int sampleCount = featureValues.value(QStringLiteral("sampleCount")).toInt();
    const double totalDurationSeconds = waveformDurationSeconds > 0.0
        ? waveformDurationSeconds
        : (sampleRate > 0 && sampleCount > 0
            ? static_cast<double>(sampleCount) / static_cast<double>(sampleRate)
            : 0.0);
    const int recognizedEventCount =
        resolveRecognizedEventCount(featureValues, recognizedSegments);
    const double recognizedTotalDurationMs =
        resolveRecognizedTotalDurationMs(featureValues, recognizedSegments);
    const EventMetrics eventMetrics =
        computeEventMetrics(recognizedSegments, waveformPoints, totalDurationSeconds);
    const SpectrumMetrics spectrumMetrics = computeSpectrumMetricsFromPoints(spectrumPoints);

    templateContext.insert(QStringLiteral("td_total_events"), formatInteger(recognizedEventCount));
    templateContext.insert(
        QStringLiteral("td_events_per_min"),
        formatNumber(eventMetrics.eventRatePerMinute, 2));
    templateContext.insert(
        QStringLiteral("td_avg_duration"),
        formatNumber(eventMetrics.averageDurationMs, 2));
    templateContext.insert(
        QStringLiteral("td_avg_interval"),
        formatNumber(eventMetrics.averageIntervalSeconds, 2));
    templateContext.insert(
        QStringLiteral("td_peak_amp"),
        formatNumber(eventMetrics.peakAmplitude, 4));
    templateContext.insert(
        QStringLiteral("fd_dom_freq"),
        formatNumber(spectrumMetrics.dominantFrequency, 2));
    templateContext.insert(
        QStringLiteral("fd_freq_range"),
        QStringLiteral("%1 - %2")
            .arg(formatNumber(spectrumMetrics.frequencyRangeMin, 2))
            .arg(formatNumber(spectrumMetrics.frequencyRangeMax, 2)));
    templateContext.insert(
        QStringLiteral("fd_spec_centroid"),
        formatNumber(spectrumMetrics.spectralCentroid, 2));
    templateContext.insert(
        QStringLiteral("fd_spec_entropy"),
        formatNumber(spectrumMetrics.spectralEntropy, 4));
    templateContext.insert(
        QStringLiteral("ef_rms_energy"),
        formatNumber(eventMetrics.rmsEnergy, 6));
    templateContext.insert(
        QStringLiteral("ef_burst_rate"),
        formatNumber(eventMetrics.burstRate, 2));
    templateContext.insert(
        QStringLiteral("ef_hl_ratio"),
        formatNumber(spectrumMetrics.highLowRatio, 4));
    templateContext.insert(
        QStringLiteral("sa_freq_curr"),
        formatNumber(eventMetrics.eventRatePerMinute, 2));
    templateContext.insert(
        QStringLiteral("sa_freq_dev"),
        frequencyDeviationLabel(eventMetrics.eventRatePerMinute));
    templateContext.insert(
        QStringLiteral("sa_rms_curr"),
        formatNumber(eventMetrics.rmsEnergy, 6));
    templateContext.insert(
        QStringLiteral("sa_rms_normal"),
        QStringLiteral("0.0010 - 1.0000"));
    templateContext.insert(
        QStringLiteral("sa_rms_dev"),
        rmsDeviationLabel(eventMetrics.rmsEnergy));
    templateContext.insert(
        QStringLiteral("sa_int_curr"),
        formatNumber(eventMetrics.averageIntervalSeconds, 2));
    templateContext.insert(
        QStringLiteral("sa_int_dev"),
        intervalDeviationLabel(eventMetrics.averageIntervalSeconds));
    templateContext.insert(
        QStringLiteral("recognized_event_count"),
        formatInteger(recognizedEventCount));
    templateContext.insert(
        QStringLiteral("recognized_total_duration_ms"),
        formatNumber(recognizedTotalDurationMs, 3) + QStringLiteral(" ms"));
    templateContext.insert(
        QStringLiteral("signal_duration_seconds"),
        formatNumber(totalDurationSeconds, 3) + QStringLiteral(" s"));

    QVariantMap payload;
    payload.insert(QStringLiteral("schema"), QStringLiteral("BSSAS.ReportPayload"));
    payload.insert(QStringLiteral("schemaVersion"), QStringLiteral("1.0"));
    payload.insert(QStringLiteral("reportKind"), reportKind);
    payload.insert(QStringLiteral("meta"), QVariantMap {
        { QStringLiteral("reportId"), reportId },
        { QStringLiteral("reportKind"), reportKind },
        { QStringLiteral("generatedAt"), templateContext.value(QStringLiteral("generated_at")) },
        { QStringLiteral("dataSource"), templateContext.value(QStringLiteral("data_source")) },
        { QStringLiteral("reportType"), QStringLiteral("IdentificationAndFeatureExtraction") },
        { QStringLiteral("application"), QCoreApplication::applicationName() }
    });
    payload.insert(QStringLiteral("cachedData"), QVariantMap {
        { QStringLiteral("waveform"), waveformCache },
        { QStringLiteral("spectrum"), spectrumCache },
        { QStringLiteral("featureValues"), normalizeVariantForJson(featureValues) }
    });
    payload.insert(QStringLiteral("reportData"), QVariantMap {
        { QStringLiteral("templateName"), reportKind },
        { QStringLiteral("candidateSegments"), normalizeVariantForJson(candidateSegments) },
        { QStringLiteral("recognizedSegments"), normalizeVariantForJson(recognizedSegments) },
        { QStringLiteral("featureStats"), featureStats },
        { QStringLiteral("eventFeatureExport"), buildEventFeatureExport(recognizedSegments) },
        { QStringLiteral("templateContext"), templateContext }
    });

    return payload;
}
