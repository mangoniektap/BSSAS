/** @file AdaptiveDownsampling.cpp
 *  @brief 自适应降采样模块实现。使用 Largest-Triangle-Three-Buckets (LTTB) 算法对时域波形数据进行降采样，
 *         通过工作线程定时处理原始数据，生成适合 UI 波形显示的降采样数据点。
 */

#include "AdaptiveDownsampling.h"

#include "DataManager.h"
#include "SignalPreprocessing.h"

#include <QThread>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

QVector<QPointF> indexedPoints(const QVector<float>& source)
{
    QVector<QPointF> points;
    points.reserve(source.size());

    for (int index = 0; index < source.size(); ++index) {
        points.append(QPointF(index, source[index]));
    }

    return points;
}

QVector<QPointF> scalePointX(
    const QVector<QPointF>& source,
    double xScale,
    double xOffset = 0.0)
{
    QVector<QPointF> points;
    points.reserve(source.size());

    for (const QPointF& point : source) {
        points.append(QPointF(xOffset + point.x() * xScale, point.y()));
    }

    return points;
}

QVector<QPointF> lttbDownsample(const QVector<float>& source, int targetCount)
{
    QVector<QPointF> destination;
    const int sourceSize = source.size();

    if (targetCount <= 0 || sourceSize <= 0) {
        return destination;
    }

    if (targetCount >= sourceSize) {
        return indexedPoints(source);
    }

    if (targetCount == 1) {
        destination.append(QPointF(0.0, source.first()));
        return destination;
    }

    destination.reserve(targetCount);

    const double every =
        static_cast<double>(sourceSize - 2) / static_cast<double>(targetCount - 2);
    int aIndex = 0;

    destination.append(QPointF(0.0, source[0]));

    for (int i = 0; i < targetCount - 2; ++i) {
        int avgRangeStart = static_cast<int>((i + 1) * every) + 1;
        int avgRangeEnd = static_cast<int>((i + 2) * every) + 1;

        avgRangeStart = std::clamp(avgRangeStart, 0, sourceSize);
        avgRangeEnd = std::clamp(avgRangeEnd, 0, sourceSize);
        if (avgRangeEnd <= avgRangeStart) {
            avgRangeEnd = std::min(sourceSize, avgRangeStart + 1);
        }

        double avgX = 0.0;
        double avgY = 0.0;
        const int avgRangeLength = avgRangeEnd - avgRangeStart;

        for (int index = avgRangeStart; index < avgRangeEnd; ++index) {
            avgX += index;
            avgY += source[index];
        }

        avgX /= avgRangeLength;
        avgY /= avgRangeLength;

        int rangeOffs = static_cast<int>(i * every) + 1;
        int rangeTo = static_cast<int>((i + 1) * every) + 1;
        rangeOffs = std::clamp(rangeOffs, 1, sourceSize - 1);
        rangeTo = std::clamp(rangeTo, rangeOffs + 1, sourceSize);

        const double pointAX = aIndex;
        const double pointAY = source[aIndex];

        double maxArea = -1.0;
        int nextAIndex = rangeOffs;

        for (int index = rangeOffs; index < rangeTo; ++index) {
            const double area = std::abs(
                ((pointAX - avgX) * (source[index] - pointAY)) -
                ((pointAX - index) * (avgY - pointAY)));

            if (area > maxArea) {
                maxArea = area;
                nextAIndex = index;
            }
        }

        destination.append(QPointF(nextAIndex, source[nextAIndex]));
        aIndex = nextAIndex;
    }

    destination.append(QPointF(sourceSize - 1, source[sourceSize - 1]));
    return destination;
}

int resolveTargetPointCount(qsizetype sampleCount, int sampleRate)
{
    if (sampleCount <= 0 || sampleRate <= 0) {
        return 0;
    }

    const double durationSeconds =
        static_cast<double>(sampleCount) / static_cast<double>(sampleRate);
    return qMax(
        1,
        qRound(durationSeconds * AdaptiveDownsampling::CURRENT_LEVEL_POINTS_PER_SECOND));
}

}

AdaptiveDownsampling::AdaptiveDownsampling(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new DownsamplingWorker();
    m_worker->moveToThread(m_thread);

    connect(this, &AdaptiveDownsampling::processingStart, m_worker, &DownsamplingWorker::startWork);
    connect(this, &AdaptiveDownsampling::processingStop, m_worker, &DownsamplingWorker::stopWork);
    connect(m_worker, &DownsamplingWorker::resultReady, this, &AdaptiveDownsampling::updateDownsampledData);
    connect(m_worker, &DownsamplingWorker::pointsPerFrameChanged, this, &AdaptiveDownsampling::updatePointsPerFrame);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

AdaptiveDownsampling::~AdaptiveDownsampling()
{
    if (m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
}

/** @brief 对原始波形数据进行单级 LTTB 降采样，生成当前缩放级别对应的显示点序列。
 *  @param source 原始采样数据
 *  @param sampleRate 信号采样率 (Hz)
 *  @returns 降采样后的 (时间秒, 幅值) 点序列
 */
QVector<QPointF> AdaptiveDownsampling::buildCurrentLevelDownsampledPoints(
    const QVector<float>& source,
    int sampleRate)
{
    const int targetCount = resolveTargetPointCount(source.size(), sampleRate);
    if (targetCount <= 0) {
        return {};
    }

    return scalePointX(
        lttbDownsample(source, targetCount),
        1.0 / static_cast<double>(sampleRate));
}

void AdaptiveDownsampling::updatePointsPerFrame(int pointsPerFrame)
{
    if (m_pointsPerFrame == pointsPerFrame) {
        return;
    }

    m_pointsPerFrame = pointsPerFrame;
    emit pointsPerFrameChanged();
}

void AdaptiveDownsampling::startDownsamplingProcessing()
{
    m_downsampledData.clear();
    emit downsampledDataReady();
    emit processingStart();
}

void AdaptiveDownsampling::updateDownsampledData(QVariantList downsampledData)
{
    m_downsampledData = downsampledData;
    emit downsampledDataReady();
}

DownsamplingWorker::DownsamplingWorker(QObject* parent)
    : QObject(parent)
{
}

DownsamplingWorker::~DownsamplingWorker()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

void DownsamplingWorker::startWork()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(AdaptiveDownsampling::PROCESSING_INTERVAL_MS);
        connect(m_timer, &QTimer::timeout, this, &DownsamplingWorker::processing);
    }

    m_elapsedSeconds = 0.0;
    m_timer->start();
}

void DownsamplingWorker::stopWork()
{
    if (m_timer) {
        m_timer->stop();
    }

    m_elapsedSeconds = 0.0;
}

int DownsamplingWorker::calculateTargetPointCount(qsizetype sampleCount, int sampleRate) const
{
    return resolveTargetPointCount(sampleCount, sampleRate);
}

/** @brief 定时器回调：从信号预处理缓存获取最新数据，执行降采样并将结果写入 DataManager。 */
void DownsamplingWorker::processing()
{
    const QVector<float> rawData = SignalPreprocessing::instance()->getDataCache();
    if (rawData.isEmpty()) {
        return;
    }

    const int sampleRate = DataManager::instance()->configuredSampleRate();
    const int targetPointCount = calculateTargetPointCount(rawData.size(), sampleRate);
    if (targetPointCount <= 0) {
        return;
    }

    if (m_pointsPerFrame != targetPointCount) {
        m_pointsPerFrame = targetPointCount;
        emit pointsPerFrameChanged(m_pointsPerFrame);
    }

    const QVector<QPointF> relativeDownsampledData =
        peakDetectingDownsampling(rawData, targetPointCount, sampleRate);
    const double batchStartSeconds = m_elapsedSeconds;
    m_elapsedSeconds += static_cast<double>(rawData.size()) /
                        static_cast<double>(sampleRate);

    const QVector<QPointF> absoluteDownsampledData =
        scalePointX(relativeDownsampledData, 1.0, batchStartSeconds);
    DataManager::instance()->splicDownsampledData(absoluteDownsampledData);
    emit resultReady(DataManager::toVariantPointList(absoluteDownsampledData));
}

QVector<QPointF> DownsamplingWorker::peakDetectingDownsampling(
    const QVector<float>& rawData,
    int targetPointCount,
    int sampleRate) const
{
    return scalePointX(
        lttbDownsample(rawData, targetPointCount),
        1.0 / static_cast<double>(sampleRate));
}
