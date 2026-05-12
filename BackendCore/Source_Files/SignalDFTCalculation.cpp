/**
 * @file SignalDFTCalculation.cpp
 * @brief 信号DFT/STFT计算模块，负责实时流式STFT帧生成、频谱幅值归一化与缩放展示、导入信号的STFT离线缓存，以及时域频谱点的时间轴查询。
 */

#include "SignalDFTCalculation.h"

#include "DataManager.h"
#include "KfrDftUtils.h"
#include "SignalPreprocessing.h"
#include "WAVHandle.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kMaxDisplayFrequency = 3000.0;
constexpr double kNormalizedDisplayMax = 4.0;
constexpr double kImportedStftWindowSeconds = 0.2;

void normalizeSpectrumMagnitudes(QVector<float>& magnitudes, int fftSize)
{
    if (fftSize <= 0) {
        return;
    }

    const float scale = 2.0f / static_cast<float>(fftSize);
    for (float& magnitude : magnitudes) {
        if (!std::isfinite(magnitude) || magnitude <= 0.0f) {
            magnitude = 0.0f;
            continue;
        }
        magnitude *= scale;
    }
}

QVector<float> scaleSpectrumMagnitudesForDisplay(
    const QVector<float>& magnitudes,
    int sampleRate,
    int fftSize)
{
    if (magnitudes.isEmpty() || sampleRate <= 0 || fftSize <= 0) {
        return {};
    }

    const double frequencyResolution = static_cast<double>(sampleRate) / static_cast<double>(fftSize);
    const int displayPoints = qMin(
        magnitudes.size(),
        static_cast<int>(qFloor(kMaxDisplayFrequency / frequencyResolution)) + 1);
    if (displayPoints <= 0) {
        return {};
    }

    float maxMagnitude = 0.0f;
    for (int index = 0; index < displayPoints; ++index) {
        const float magnitude = magnitudes[index];
        if (std::isfinite(magnitude) && magnitude > maxMagnitude) {
            maxMagnitude = magnitude;
        }
    }

    const double magnitudeScale = maxMagnitude > 0.0f
        ? kNormalizedDisplayMax / static_cast<double>(maxMagnitude)
        : 0.0;

    QVector<float> scaledMagnitudes = magnitudes;
    for (float& magnitude : scaledMagnitudes) {
        if (!std::isfinite(magnitude) || magnitude <= 0.0f) {
            magnitude = 0.0f;
            continue;
        }

        const double scaledMagnitude =
            static_cast<double>(magnitude) * magnitudeScale;
        magnitude = std::isfinite(scaledMagnitude)
            ? static_cast<float>(scaledMagnitude)
            : 0.0f;
    }

    return scaledMagnitudes;
}

QVariantList buildSpectrumPoints(
    const QVector<float>& magnitudes,
    int sampleRate,
    int fftSize,
    double fromFrequencyHz,
    double toFrequencyHz)
{
    QVariantList points;
    if (magnitudes.isEmpty() || sampleRate <= 0 || fftSize <= 0) {
        return points;
    }

    const double frequencyResolution = static_cast<double>(sampleRate) / static_cast<double>(fftSize);
    const double clampedFrom = qMax(0.0, fromFrequencyHz);
    const double clampedTo = qMin(kMaxDisplayFrequency, toFrequencyHz);
    if (clampedTo < clampedFrom || frequencyResolution <= 0.0) {
        return points;
    }

    const int startBin = qMax(0, static_cast<int>(qFloor(clampedFrom / frequencyResolution)));
    const int endBin = qMin(
        magnitudes.size() - 1,
        static_cast<int>(qCeil(clampedTo / frequencyResolution)));
    if (endBin < startBin) {
        return points;
    }

    QVector<QPointF> pointBuffer;
    pointBuffer.reserve(endBin - startBin + 1);
    for (int bin = startBin; bin <= endBin; ++bin) {
        pointBuffer.append(QPointF(
            static_cast<double>(bin) * frequencyResolution,
            static_cast<double>(magnitudes[bin])));
    }

    return DataManager::toVariantPointList(pointBuffer);
}

QVariantList filterSpectrumPointsByRange(
    const QVariantList& points,
    double fromFrequencyHz,
    double toFrequencyHz)
{
    if (points.isEmpty()) {
        return {};
    }

    const double clampedFrom = qMax(0.0, fromFrequencyHz);
    const double clampedTo = qMin(kMaxDisplayFrequency, toFrequencyHz);
    if (clampedTo < clampedFrom) {
        return {};
    }

    QVariantList filtered;
    filtered.reserve(points.size());
    for (const QVariant& pointVariant : points) {
        const QPointF point = pointVariant.toPointF();
        if (point.x() >= clampedFrom && point.x() <= clampedTo) {
            filtered.append(pointVariant);
        }
    }

    return filtered;
}

QVariantMap buildSpectrumTimeRange(double centerSeconds, double windowSeconds)
{
    QVariantMap timeRange;
    if (!std::isfinite(centerSeconds) || !std::isfinite(windowSeconds) || windowSeconds <= 0.0) {
        return timeRange;
    }

    const double halfWindowSeconds = windowSeconds / 2.0;
    timeRange.insert(QStringLiteral("valid"), true);
    timeRange.insert(QStringLiteral("centerSeconds"), centerSeconds);
    timeRange.insert(QStringLiteral("fromSeconds"), qMax(0.0, centerSeconds - halfWindowSeconds));
    timeRange.insert(QStringLiteral("toSeconds"), qMax(0.0, centerSeconds + halfWindowSeconds));
    return timeRange;
}

double elapsedMilliseconds(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

QVariantList computeImportedDftDataAndCache(
    const QVector<float>& timeDomainDataImport,
    int sampleRate)
{
    if (timeDomainDataImport.isEmpty() || sampleRate <= 0) {
        DataManager::instance()->storeImportedStftSpectrumData(sampleRate, {}, {});
        return {};
    }

    const int windowSampleCount = qMax(
        1,
        static_cast<int>(qRound(static_cast<double>(sampleRate) * kImportedStftWindowSeconds)));
    const int hopSampleCount = windowSampleCount;

    if (timeDomainDataImport.size() < windowSampleCount) {
        DataManager::instance()->storeImportedStftSpectrumData(sampleRate, {}, {});
        return {};
    }

    const int frameCount = timeDomainDataImport.size() / hopSampleCount;
    if (frameCount <= 0) {
        DataManager::instance()->storeImportedStftSpectrumData(sampleRate, {}, {});
        return {};
    }

    QVector<float> frameCenterTimes;
    frameCenterTimes.reserve(frameCount);
    QVector<QVector<float>> frameMagnitudes;
    frameMagnitudes.reserve(frameCount);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int startSampleIndex = frameIndex * hopSampleCount;
        if (startSampleIndex + windowSampleCount > timeDomainDataImport.size()) {
            break;
        }

        const QVector<float> frameSignal =
            timeDomainDataImport.mid(startSampleIndex, windowSampleCount);
        const KfrDftUtils::RealDftSpectrumResult spectrum =
            KfrDftUtils::computeRealSpectrumMagnitudes(frameSignal);
        QVector<float> frequencyMagnitudes = spectrum.magnitudes;
        normalizeSpectrumMagnitudes(frequencyMagnitudes, spectrum.fftSize);

        frameMagnitudes.append(scaleSpectrumMagnitudesForDisplay(
            frequencyMagnitudes,
            sampleRate,
            spectrum.fftSize));
        frameCenterTimes.append(static_cast<float>(
            (static_cast<double>(startSampleIndex) +
             static_cast<double>(windowSampleCount) / 2.0) /
            static_cast<double>(sampleRate)));
    }

    DataManager::instance()->storeImportedStftSpectrumData(
        sampleRate,
        frameCenterTimes,
        frameMagnitudes);
    return DataManager::instance()->importedSpectrumPointsAtTime(
        0.0,
        DataManager::instance()->importedSpectrumBoundary(),
        0.0);
}
}

SignalDFTCalculation::SignalDFTCalculation(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new DFTWorker();
    m_worker->moveToThread(m_thread);

    connect(this, &SignalDFTCalculation::processingStart, m_worker, &DFTWorker::startWork);
    connect(this, &SignalDFTCalculation::processingStop, m_worker, &DFTWorker::stopWork);
    connect(m_worker, &DFTWorker::resultReady, this, &SignalDFTCalculation::updateDFTData);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

SignalDFTCalculation::~SignalDFTCalculation()
{
    if (m_importThread != nullptr && m_importThread->isRunning()) {
        m_importThread->wait();
    }

    if (m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
}

/** @brief 启动实时STFT处理，清空缓存并从预处理管道拉取数据。 */
void SignalDFTCalculation::startDFTProcessing()
{
    clearRealtimeStftCache();
    emit processingStart();
}

void SignalDFTCalculation::stopDFTProcessing()
{
    emit processingStop();
    clearRealtimeStftCache();
}

/**
 * @brief 接收新生成的STFT帧数据并存入滑动窗口缓存。
 * @param dftData QVariantList格式的频谱点。
 * @param centerTimeSeconds 该帧的中心时间（秒）。
 */
void SignalDFTCalculation::updateDFTData(QVariantList dftData, double centerTimeSeconds)
{
    m_dftData = dftData;

    if (!dftData.isEmpty()) {
        m_realtimeStftCenterTimes.append(centerTimeSeconds);
        m_realtimeStftFrames.append(dftData);

        while (m_realtimeStftCenterTimes.size() > MAX_REALTIME_STFT_FRAMES) {
            m_realtimeStftCenterTimes.removeFirst();
            m_realtimeStftFrames.removeFirst();
        }
    }

    emit dftResultReady();
}

QVariantList SignalDFTCalculation::realtimeStftPointsAtTime(
    double fromFrequencyHz,
    double toFrequencyHz,
    double centerSeconds) const
{
    if (m_realtimeStftFrames.isEmpty()) {
        return {};
    }

    int nearestIndex = m_realtimeStftFrames.size() - 1;
    double nearestDistance = std::numeric_limits<double>::max();
    if (centerSeconds >= 0.0 && !m_realtimeStftCenterTimes.isEmpty()) {
        for (int index = 0; index < m_realtimeStftCenterTimes.size(); ++index) {
            const double distance = qAbs(m_realtimeStftCenterTimes[index] - centerSeconds);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }
    }

    return filterSpectrumPointsByRange(
        m_realtimeStftFrames[nearestIndex],
        fromFrequencyHz,
        toFrequencyHz);
}

QVariantMap SignalDFTCalculation::realtimeStftTimeRangeAtTime(double centerSeconds) const
{
    if (m_realtimeStftCenterTimes.isEmpty()) {
        return {};
    }

    int nearestIndex = m_realtimeStftCenterTimes.size() - 1;
    double nearestDistance = std::numeric_limits<double>::max();
    if (centerSeconds >= 0.0) {
        for (int index = 0; index < m_realtimeStftCenterTimes.size(); ++index) {
            const double distance = qAbs(m_realtimeStftCenterTimes[index] - centerSeconds);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }
    }

    return buildSpectrumTimeRange(
        m_realtimeStftCenterTimes[nearestIndex],
        kImportedStftWindowSeconds);
}

void SignalDFTCalculation::clearRealtimeStftCache()
{
    m_realtimeStftCenterTimes.clear();
    m_realtimeStftFrames.clear();
    m_dftData.clear();
    emit dftResultReady();
}

/**
 * @brief 在后台线程中对导入数据执行离线STFT计算和缓存。
 */
void SignalDFTCalculation::startImportedDftProcessing()
{
    if (m_importBusy) {
        return;
    }

    const QVector<float> timeDomainDataImport = WAVHandle::instance()->timeDomainDataImport();
    const int sampleRate = WAVHandle::instance()->importSampleRate();

    setImportBusy(true);
    m_importThread = QThread::create([this, timeDomainDataImport, sampleRate]() {
        QElapsedTimer dftTimer;
        dftTimer.start();
        QVariantList importedDftData =
            computeImportedDftDataAndCache(timeDomainDataImport, sampleRate);
        const double dftMs = elapsedMilliseconds(dftTimer);
        QMetaObject::invokeMethod(
            this,
            [this, importedDftData = std::move(importedDftData), dftMs]() mutable {
                DataManager::instance()->updateImportedAnalysisSummary({
                    {QStringLiteral("timings"), QVariantMap{
                         {QStringLiteral("dftMs"), dftMs}
                     }}
                });
                m_dftData = std::move(importedDftData);
                emit dftResultReady();
                emit importedDftProcessingFinished();
                setImportBusy(false);
            },
            Qt::QueuedConnection);
    });

    connect(
        m_importThread,
        &QThread::finished,
        this,
        [this]() {
            m_importThread = nullptr;
        });
    connect(m_importThread, &QThread::finished, m_importThread, &QObject::deleteLater);
    m_importThread->start();
}

void SignalDFTCalculation::setImportBusy(bool busy)
{
    if (m_importBusy == busy) {
        return;
    }

    m_importBusy = busy;
    emit importBusyChanged();
}

DFTWorker::DFTWorker(QObject* parent)
    : QObject(parent)
{
}

DFTWorker::~DFTWorker()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

void DFTWorker::startWork()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(COLLECTION_INTERVAL_MS);
        connect(m_timer, &QTimer::timeout, this, &DFTWorker::processing);
    }

    {
        QMutexLocker locker(&m_dataMutex);
        m_pendingSamples.clear();
        m_processedSeconds = 0.0;
        m_stftWindowSampleCount = 0;
    }

    m_timer->start();
}

void DFTWorker::stopWork()
{
    if (m_timer) {
        m_timer->stop();
    }

    {
        QMutexLocker locker(&m_dataMutex);
        m_pendingSamples.clear();
        m_processedSeconds = 0.0;
        m_stftWindowSampleCount = 0;
    }
}

/**
 * @brief 定时从预处理缓冲区拉取数据，构建STFT帧并同步输出频谱图数据。
 */
void DFTWorker::processing()
{
    const QVector<float> rawData = SignalPreprocessing::instance()->getDataCache();
    if (rawData.isEmpty()) {
        return;
    }

    const int sampleRate = DataManager::instance()->configuredSampleRate();
    if (sampleRate <= 0) {
        return;
    }

    {
        QMutexLocker locker(&m_dataMutex);
        const int nextWindowSampleCount = qMax(
            1,
            static_cast<int>(qRound(
                static_cast<double>(sampleRate) * kImportedStftWindowSeconds)));
        if (m_stftWindowSampleCount != nextWindowSampleCount) {
            m_pendingSamples.clear();
            m_processedSeconds = 0.0;
            m_stftWindowSampleCount = nextWindowSampleCount;
        }

        m_pendingSamples.append(rawData);
    }

    produceStftFrames();
}

void DFTWorker::produceStftFrames()
{
    while (true) {
        QVector<float> frameSignal;
        int sampleRate = DataManager::instance()->configuredSampleRate();
        double centerTimeSeconds = 0.0;

        {
            QMutexLocker locker(&m_dataMutex);
            if (m_stftWindowSampleCount <= 0 || m_pendingSamples.size() < m_stftWindowSampleCount) {
                return;
            }

            frameSignal = m_pendingSamples.mid(0, m_stftWindowSampleCount);
            m_pendingSamples.remove(0, m_stftWindowSampleCount);

            const double frameStartSeconds = m_processedSeconds;
            m_processedSeconds +=
                static_cast<double>(m_stftWindowSampleCount) / static_cast<double>(sampleRate);
            centerTimeSeconds =
                frameStartSeconds +
                static_cast<double>(m_stftWindowSampleCount) /
                    (2.0 * static_cast<double>(sampleRate));
        }

        if (frameSignal.isEmpty()) {
            continue;
        }

        const KfrDftUtils::RealDftSpectrumResult spectrum =
            KfrDftUtils::computeRealSpectrumMagnitudes(frameSignal);
        if (spectrum.magnitudes.isEmpty()) {
            continue;
        }

        QVector<float> frequencyMagnitudes = spectrum.magnitudes;
        normalizeSpectrumMagnitudes(frequencyMagnitudes, spectrum.fftSize);
        const QVector<float> scaledMagnitudes = scaleSpectrumMagnitudesForDisplay(
            frequencyMagnitudes,
            sampleRate,
            spectrum.fftSize);

        emit resultReady(
            buildSpectrumPoints(
                scaledMagnitudes,
                sampleRate,
                spectrum.fftSize,
                0.0,
                kMaxDisplayFrequency),
            centerTimeSeconds);
    }
}
