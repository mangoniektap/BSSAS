#include "DataManager.h"
#include "DatabaseStoragePaths.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QMutexLocker>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

DataManager* DataManager::m_instance = nullptr;

namespace {
constexpr int kRealtimeChannelCount = 8;

int sanitizeSampleRate(int sampleRate)
{
    return sampleRate > 0 ? sampleRate : DataManager::DEFAULT_SAMPLE_RATE;
}

int normalizeConfiguredSampleRate(int sampleRate)
{
    constexpr int kMinSampleRate = 8000;
    constexpr int kMaxSampleRate = 48000;
    constexpr int kSampleRateStep = 1000;

    const int clampedSampleRate = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
    const int roundedSampleRate =
        ((clampedSampleRate + (kSampleRateStep / 2)) / kSampleRateStep) * kSampleRateStep;
    return std::clamp(roundedSampleRate, kMinSampleRate, kMaxSampleRate);
}

bool ensureDirectoryExists(const QString& directoryPath)
{
    if (QDir(directoryPath).exists()) {
        return true;
    }

    if (QDir().mkpath(directoryPath)) {
        return true;
    }

    qWarning() << "DataManager: failed to create database directory:" << directoryPath;
    return false;
}

bool clearDirectoryContents(const QString& directoryPath)
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        return ensureDirectoryExists(directoryPath);
    }

    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);

    bool removedAllEntries = true;
    for (const QFileInfo& entry : entries) {
        bool removed = false;
        if (entry.isDir()) {
            removed = QDir(entry.absoluteFilePath()).removeRecursively();
        } else {
            removed = QFile::remove(entry.absoluteFilePath());
        }

        if (!removed) {
            qWarning() << "DataManager: failed to remove temporary entry:"
                       << entry.absoluteFilePath();
            removedAllEntries = false;
        }
    }

    return removedAllEntries;
}

QString buildTemporaryFileTemplate(const QString& prefix, const QString& suffix)
{
    return QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH).filePath(
        prefix + QStringLiteral("_XXXXXX") + suffix);
}

bool createTemporaryFileInManagedDirectory(
    QString& temporaryFilePath,
    const QString& fileTemplate,
    const char* failureContext)
{
    if (!temporaryFilePath.isEmpty()) {
        return true;
    }

    if (!ensureDirectoryExists(DatabaseStoragePaths::TEMPORARY_FILE_PATH)) {
        return false;
    }

    QTemporaryFile temporaryFile(fileTemplate);
    temporaryFile.setAutoRemove(false);
    if (!temporaryFile.open()) {
        qWarning() << failureContext;
        return false;
    }

    temporaryFilePath = temporaryFile.fileName();
    temporaryFile.close();
    return true;
}

void ensureDatabaseStorageDirectories()
{
    ensureDirectoryExists(DatabaseStoragePaths::ROOT_PATH);
    ensureDirectoryExists(DatabaseStoragePaths::TEMPORARY_FILE_PATH);
    ensureDirectoryExists(DatabaseStoragePaths::REPORTS_PATH);
    ensureDirectoryExists(DatabaseStoragePaths::AUDIO_PATH);
}

constexpr float kImportedStftWindowSeconds = 0.2f;

int computeImportedStftWindowSampleCount(int sampleRate)
{
    const int sanitizedSampleRate = sanitizeSampleRate(sampleRate);
    return std::max(1, static_cast<int>(std::lround(
                           static_cast<double>(sanitizedSampleRate) *
                           static_cast<double>(kImportedStftWindowSeconds))));
}

int computeImportedStftFftSize(int windowSampleCount)
{
    int fftSize = 1;
    while (fftSize < std::max(1, windowSampleCount)) {
        fftSize <<= 1;
    }

    return fftSize;
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
    timeRange.insert(QStringLiteral("fromSeconds"), std::max(0.0, centerSeconds - halfWindowSeconds));
    timeRange.insert(QStringLiteral("toSeconds"), std::max(0.0, centerSeconds + halfWindowSeconds));
    return timeRange;
}

QString buildRealtimeChannelFilePath(const QString& sessionTime, int channelIndex)
{
    return QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH).filePath(
        sessionTime + QStringLiteral("_channel%1.tmp").arg(channelIndex));
}

QVariant mergeVariantValue(const QVariant& currentValue, const QVariant& updateValue)
{
    if (currentValue.typeId() == QMetaType::QVariantMap &&
        updateValue.typeId() == QMetaType::QVariantMap) {
        QVariantMap mergedMap = currentValue.toMap();
        const QVariantMap updateMap = updateValue.toMap();
        for (auto iterator = updateMap.cbegin(); iterator != updateMap.cend(); ++iterator) {
            if (mergedMap.contains(iterator.key())) {
                mergedMap.insert(
                    iterator.key(),
                    mergeVariantValue(mergedMap.value(iterator.key()), iterator.value()));
            } else {
                mergedMap.insert(iterator.key(), iterator.value());
            }
        }
        return mergedMap;
    }

    return updateValue;
}

void mergeVariantMaps(QVariantMap& target, const QVariantMap& update)
{
    for (auto iterator = update.cbegin(); iterator != update.cend(); ++iterator) {
        if (target.contains(iterator.key())) {
            target.insert(
                iterator.key(),
                mergeVariantValue(target.value(iterator.key()), iterator.value()));
        } else {
            target.insert(iterator.key(), iterator.value());
        }
    }
}
}

DatabaseCache::DatabaseCache()
    : m_downsampledRingBuffer(DOWNSAMPLED_RING_BUFFER_SIZE)
    , m_rawSampleRate(DataManager::DEFAULT_SAMPLE_RATE)
    , m_importedRawSampleRate(DataManager::DEFAULT_SAMPLE_RATE)
{
    m_realtimeChannelTemporaryFilePaths.resize(kRealtimeChannelCount);
}

DatabaseCache::~DatabaseCache()
{
    removeRawTemporaryFile();
    removeRealtimeChannelTemporaryFiles();
    removeDownsampledTemporaryFile();
    removeRealtimeSpectrumTemporaryFile();
    removeTemporaryFile(m_importedRawTemporaryFilePath);
    removeTemporaryFile(m_importedDownsampledTemporaryFilePath);
    removeTemporaryFile(m_importedSpectrumTemporaryFilePath);
}

void DatabaseCache::clear()
{
    removeRawTemporaryFile();
    removeRealtimeChannelTemporaryFiles();
    removeDownsampledTemporaryFile();
    removeRealtimeSpectrumTemporaryFile();

    m_realtimeCollectionSessionTime = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    m_collectionCompleted = false;
    m_rawSampleRate = DataManager::instance()->configuredSampleRate();
    ensureDownsampledRingBufferAllocated();
    m_downsampledBufferedPointCount = 0;
}

void DatabaseCache::collectionCompleted()
{
    if (m_collectionCompleted) {
        return;
    }

    if (m_downsampledBufferedPointCount > 0 && ensureDownsampledTemporaryFileCreated()) {
        appendDownsampledPointsToTemporaryFile(
            m_downsampledRingBuffer.constData(),
            m_downsampledBufferedPointCount);
    }

    releaseDownsampledRingBuffer();
    m_collectionCompleted = true;
}

void DatabaseCache::splicTimeDomainData(const QVector<float>& rawData, int sampleRate)
{
    if (rawData.isEmpty() || m_collectionCompleted) {
        return;
    }

    m_rawSampleRate = sanitizeSampleRate(sampleRate);
    if (!ensureRawTemporaryFileCreated()) {
        return;
    }

    appendRawSamplesToTemporaryFile(rawData.constData(), rawData.size());
}

void DatabaseCache::splicRealtimeChannelTimeDomainData(
    int channelIndex,
    const QVector<float>& rawData,
    int sampleRate)
{
    if (!isValidChannelIndex(channelIndex) || rawData.isEmpty() || m_collectionCompleted) {
        return;
    }

    m_rawSampleRate = sanitizeSampleRate(sampleRate);
    if (!ensureRealtimeChannelTemporaryFileCreated(channelIndex)) {
        return;
    }

    appendRealtimeChannelSamplesToTemporaryFile(
        channelIndex,
        rawData.constData(),
        rawData.size());
}

void DatabaseCache::splicDownsampledData(const QVector<QPointF>& downsampledData)
{
    if (downsampledData.isEmpty() || m_collectionCompleted) {
        return;
    }

    if (!ensureDownsampledTemporaryFileCreated()) {
        return;
    }

    ensureDownsampledRingBufferAllocated();

    qsizetype readOffset = 0;
    while (readOffset < downsampledData.size()) {
        const qsizetype writableCount =
            std::min<qsizetype>(
                DOWNSAMPLED_RING_BUFFER_SIZE - m_downsampledBufferedPointCount,
                downsampledData.size() - readOffset);

        for (qsizetype index = 0; index < writableCount; ++index) {
            const QPointF& point = downsampledData[readOffset + index];
            DownsampledPointRecord& record =
                m_downsampledRingBuffer[m_downsampledBufferedPointCount + index];
            record.x = static_cast<float>(point.x());
            record.y = static_cast<float>(point.y());
        }

        m_downsampledBufferedPointCount += writableCount;
        readOffset += writableCount;

        if (m_downsampledBufferedPointCount == DOWNSAMPLED_RING_BUFFER_SIZE &&
            !appendDownsampledPointsToTemporaryFile(
                m_downsampledRingBuffer.constData(),
                DOWNSAMPLED_RING_BUFFER_SIZE)) {
            return;
        }
    }

    if (m_downsampledBufferedPointCount > 0) {
        appendDownsampledPointsToTemporaryFile(
            m_downsampledRingBuffer.constData(),
            m_downsampledBufferedPointCount);
    }
}

void DatabaseCache::storeImportedTimeDomainData(
    int sampleRate,
    const QVector<float>& importedRawData)
{
    removeTemporaryFile(m_importedRawTemporaryFilePath);
    m_importedRawSampleRate = sanitizeSampleRate(sampleRate);

    if (importedRawData.isEmpty()) {
        return;
    }

    if (!ensureImportedRawTemporaryFileCreated()) {
        return;
    }

    writeImportedRawSamplesToTemporaryFile(importedRawData.constData(), importedRawData.size());
}

void DatabaseCache::storeImportedDownsampledData(const QVector<QPointF>& importedDownsampledData)
{
    removeTemporaryFile(m_importedDownsampledTemporaryFilePath);

    if (importedDownsampledData.isEmpty()) {
        return;
    }

    if (!ensureImportedDownsampledTemporaryFileCreated()) {
        return;
    }

    QVector<DownsampledPointRecord> importedRecords(importedDownsampledData.size());
    for (qsizetype index = 0; index < importedDownsampledData.size(); ++index) {
        const QPointF& point = importedDownsampledData[index];
        importedRecords[index].x = static_cast<float>(point.x());
        importedRecords[index].y = static_cast<float>(point.y());
    }

    writeImportedDownsampledPointsToTemporaryFile(
        importedRecords.constData(),
        importedRecords.size());
}

void DatabaseCache::storeRealtimeSpectrumData(
    int sampleRate,
    const QVector<float>& magnitudes)
{
    if (magnitudes.isEmpty()) {
        removeRealtimeSpectrumTemporaryFile();
        return;
    }

    if (!ensureRealtimeSpectrumTemporaryFileCreated()) {
        return;
    }

    writeSpectrumMagnitudesToTemporaryFile(
        m_realtimeSpectrumTemporaryFilePath,
        sampleRate,
        magnitudes.constData(),
        magnitudes.size(),
        "write realtime spectrum cache data");
}

void DatabaseCache::storeImportedSpectrumData(
    int sampleRate,
    const QVector<float>& magnitudes)
{
    removeTemporaryFile(m_importedSpectrumTemporaryFilePath);

    if (magnitudes.isEmpty()) {
        return;
    }

    if (!ensureImportedSpectrumTemporaryFileCreated()) {
        return;
    }

    writeSpectrumMagnitudesToTemporaryFile(
        m_importedSpectrumTemporaryFilePath,
        sampleRate,
        magnitudes.constData(),
        magnitudes.size(),
        "write imported spectrum cache data");
}

void DatabaseCache::storeImportedStftSpectrumData(
    int sampleRate,
    const QVector<float>& frameCenterTimes,
    const QVector<QVector<float>>& frameMagnitudes)
{
    removeTemporaryFile(m_importedSpectrumTemporaryFilePath);

    if (frameCenterTimes.isEmpty() || frameMagnitudes.isEmpty() ||
        frameCenterTimes.size() != frameMagnitudes.size()) {
        return;
    }

    if (!ensureImportedSpectrumTemporaryFileCreated()) {
        return;
    }

    if (!writeSampleRateHeaderToTemporaryFile(
            m_importedSpectrumTemporaryFilePath,
            sampleRate,
            QIODevice::WriteOnly | QIODevice::Truncate,
            "write imported stft sample rate header")) {
        return;
    }

    for (qsizetype frameIndex = 0; frameIndex < frameMagnitudes.size(); ++frameIndex) {
        const QVector<float>& magnitudes = frameMagnitudes[frameIndex];
        if (magnitudes.isEmpty()) {
            continue;
        }

        const float centerTimeSeconds = frameCenterTimes[frameIndex];
        if (!writeBytesToTemporaryFile(
                m_importedSpectrumTemporaryFilePath,
                reinterpret_cast<const char*>(&centerTimeSeconds),
                sizeof(centerTimeSeconds),
                QIODevice::WriteOnly | QIODevice::Append,
                "append imported stft center time")) {
            return;
        }

        const qint64 magnitudeByteCount =
            static_cast<qint64>(magnitudes.size() * sizeof(float));
        if (!writeBytesToTemporaryFile(
                m_importedSpectrumTemporaryFilePath,
                reinterpret_cast<const char*>(magnitudes.constData()),
                magnitudeByteCount,
                QIODevice::WriteOnly | QIODevice::Append,
                "append imported stft magnitudes")) {
            return;
        }
    }
}

void DatabaseCache::clearImportedData()
{
    removeTemporaryFile(m_importedRawTemporaryFilePath);
    removeTemporaryFile(m_importedDownsampledTemporaryFilePath);
    removeTemporaryFile(m_importedSpectrumTemporaryFilePath);
}

QString DatabaseCache::rawTemporaryFilePath() const
{
    return m_rawTemporaryFilePath;
}

QString DatabaseCache::downsampledTemporaryFilePath() const
{
    return m_downsampledTemporaryFilePath;
}

QString DatabaseCache::importedRawTemporaryFilePath() const
{
    return m_importedRawTemporaryFilePath;
}

QString DatabaseCache::importedDownsampledTemporaryFilePath() const
{
    return m_importedDownsampledTemporaryFilePath;
}

QString DatabaseCache::realtimeChannelTemporaryFilePath(int channelIndex) const
{
    if (!isValidChannelIndex(channelIndex) ||
        channelIndex >= m_realtimeChannelTemporaryFilePaths.size()) {
        return {};
    }

    return m_realtimeChannelTemporaryFilePaths[channelIndex];
}

QString DatabaseCache::temporaryFilePath() const
{
    return rawTemporaryFilePath();
}

QVariantList DatabaseCache::realtimeWaveformPoints(double fromSeconds, double toSeconds) const
{
    return realtimeWaveformPointsForChannel(0, fromSeconds, toSeconds);
}

QVariantList DatabaseCache::realtimeWaveformPointsForChannel(
    int channelIndex,
    double fromSeconds,
    double toSeconds) const
{
    return waveformPointsFromTemporaryFile(
        realtimeChannelTemporaryFilePath(channelIndex),
        fromSeconds,
        toSeconds);
}

QVariantList DatabaseCache::realtimeDownsampledWaveformPoints(
    double fromSeconds,
    double toSeconds) const
{
    return downsampledWaveformPointsFromTemporaryFile(
        m_downsampledTemporaryFilePath,
        fromSeconds,
        toSeconds);
}

QVariantList DatabaseCache::importedWaveformPoints(double fromSeconds, double toSeconds) const
{
    return waveformPointsFromTemporaryFile(m_importedRawTemporaryFilePath, fromSeconds, toSeconds);
}

QVariantList DatabaseCache::importedDownsampledWaveformPoints(
    double fromSeconds,
    double toSeconds) const
{
    return downsampledWaveformPointsFromTemporaryFile(
        m_importedDownsampledTemporaryFilePath,
        fromSeconds,
        toSeconds);
}

QVariantList DatabaseCache::realtimeSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const
{
    return spectrumPointsFromTemporaryFile(
        m_realtimeSpectrumTemporaryFilePath,
        fromFrequencyHz,
        toFrequencyHz);
}

QVariantList DatabaseCache::importedSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const
{
    return spectrumPointsFromImportedStftTemporaryFile(
        m_importedSpectrumTemporaryFilePath,
        fromFrequencyHz,
        toFrequencyHz,
        0.0);
}

QVariantList DatabaseCache::importedSpectrumPointsAtTime(
    double fromFrequencyHz,
    double toFrequencyHz,
    double centerSeconds) const
{
    return spectrumPointsFromImportedStftTemporaryFile(
        m_importedSpectrumTemporaryFilePath,
        fromFrequencyHz,
        toFrequencyHz,
        centerSeconds);
}

QVariantMap DatabaseCache::importedSpectrumTimeRangeAtTime(double centerSeconds) const
{
    return spectrumTimeRangeFromImportedStftTemporaryFile(
        m_importedSpectrumTemporaryFilePath,
        centerSeconds);
}

double DatabaseCache::realtimeWaveformDuration() const
{
    return realtimeWaveformDurationForChannel(0);
}

double DatabaseCache::realtimeWaveformDurationForChannel(int channelIndex) const
{
    return waveformDurationFromTemporaryFile(realtimeChannelTemporaryFilePath(channelIndex));
}

double DatabaseCache::importedWaveformDuration() const
{
    return waveformDurationFromTemporaryFile(m_importedRawTemporaryFilePath);
}

double DatabaseCache::realtimeSpectrumBoundary() const
{
    return spectrumBoundaryFromTemporaryFile(m_realtimeSpectrumTemporaryFilePath);
}

double DatabaseCache::importedSpectrumBoundary() const
{
    return spectrumBoundaryFromTemporaryFile(m_importedSpectrumTemporaryFilePath);
}

int DatabaseCache::realtimeWaveformSampleRate() const
{
    return realtimeWaveformSampleRateForChannel(0);
}

int DatabaseCache::realtimeWaveformSampleRateForChannel(int channelIndex) const
{
    return readSampleRateFromTemporaryFile(realtimeChannelTemporaryFilePath(channelIndex));
}

int DatabaseCache::importedWaveformSampleRate() const
{
    return readSampleRateFromTemporaryFile(m_importedRawTemporaryFilePath);
}

bool DatabaseCache::appendRawSamplesToTemporaryFile(const float* samples, qsizetype sampleCount)
{
    if (samples == nullptr || sampleCount <= 0 || m_rawTemporaryFilePath.isEmpty()) {
        return false;
    }

    const qint64 byteCount = static_cast<qint64>(sampleCount * sizeof(float));
    if (!writeBytesToTemporaryFile(
            m_rawTemporaryFilePath,
            reinterpret_cast<const char*>(samples),
            byteCount,
            QIODevice::WriteOnly | QIODevice::Append,
            "append cache data")) {
        return false;
    }

    return true;
}

bool DatabaseCache::appendRealtimeChannelSamplesToTemporaryFile(
    int channelIndex,
    const float* samples,
    qsizetype sampleCount)
{
    if (!isValidChannelIndex(channelIndex) ||
        samples == nullptr ||
        sampleCount <= 0 ||
        channelIndex >= m_realtimeChannelTemporaryFilePaths.size()) {
        return false;
    }

    const QString& temporaryFilePath = m_realtimeChannelTemporaryFilePaths[channelIndex];
    if (temporaryFilePath.isEmpty()) {
        return false;
    }

    const qint64 byteCount = static_cast<qint64>(sampleCount * sizeof(float));
    return writeBytesToTemporaryFile(
        temporaryFilePath,
        reinterpret_cast<const char*>(samples),
        byteCount,
        QIODevice::WriteOnly | QIODevice::Append,
        "append realtime channel cache data");
}

bool DatabaseCache::appendDownsampledPointsToTemporaryFile(
    const DownsampledPointRecord* points,
    qsizetype pointCount)
{
    if (points == nullptr || pointCount <= 0 || m_downsampledTemporaryFilePath.isEmpty()) {
        return false;
    }

    const qint64 byteCount =
        static_cast<qint64>(pointCount * sizeof(DownsampledPointRecord));
    if (!writeBytesToTemporaryFile(
            m_downsampledTemporaryFilePath,
            reinterpret_cast<const char*>(points),
            byteCount,
            QIODevice::WriteOnly | QIODevice::Append,
            "append downsampled cache data")) {
        return false;
    }

    m_downsampledBufferedPointCount = 0;
    return true;
}

bool DatabaseCache::writeImportedRawSamplesToTemporaryFile(
    const float* samples,
    qsizetype sampleCount)
{
    if (samples == nullptr || sampleCount <= 0 || m_importedRawTemporaryFilePath.isEmpty()) {
        return false;
    }

    if (!writeSampleRateHeaderToTemporaryFile(
            m_importedRawTemporaryFilePath,
            m_importedRawSampleRate,
            QIODevice::WriteOnly | QIODevice::Truncate,
            "write imported raw sample rate header")) {
        return false;
    }

    const qint64 byteCount = static_cast<qint64>(sampleCount * sizeof(float));
    return writeBytesToTemporaryFile(
        m_importedRawTemporaryFilePath,
        reinterpret_cast<const char*>(samples),
        byteCount,
        QIODevice::WriteOnly | QIODevice::Append,
        "write imported raw cache data");
}

bool DatabaseCache::writeImportedDownsampledPointsToTemporaryFile(
    const DownsampledPointRecord* points,
    qsizetype pointCount)
{
    if (points == nullptr || pointCount <= 0 || m_importedDownsampledTemporaryFilePath.isEmpty()) {
        return false;
    }

    const qint64 byteCount =
        static_cast<qint64>(pointCount * sizeof(DownsampledPointRecord));
    return writeBytesToTemporaryFile(
        m_importedDownsampledTemporaryFilePath,
        reinterpret_cast<const char*>(points),
        byteCount,
        QIODevice::WriteOnly | QIODevice::Truncate,
        "write imported downsampled cache data");
}

bool DatabaseCache::writeSpectrumMagnitudesToTemporaryFile(
    QString& temporaryFilePath,
    int sampleRate,
    const float* magnitudes,
    qsizetype magnitudeCount,
    const char* failureContext)
{
    if (temporaryFilePath.isEmpty() || magnitudes == nullptr || magnitudeCount <= 0) {
        return false;
    }

    if (!writeSampleRateHeaderToTemporaryFile(
            temporaryFilePath,
            sampleRate,
            QIODevice::WriteOnly | QIODevice::Truncate,
            "write spectrum sample rate header")) {
        return false;
    }

    const qint64 byteCount = static_cast<qint64>(magnitudeCount * sizeof(float));
    return writeBytesToTemporaryFile(
        temporaryFilePath,
        reinterpret_cast<const char*>(magnitudes),
        byteCount,
        QIODevice::WriteOnly | QIODevice::Append,
        failureContext);
}

bool DatabaseCache::ensureRawTemporaryFileCreated()
{
    if (!ensureTemporaryFileCreated(m_rawTemporaryFilePath)) {
        return false;
    }

    if (QFile(m_rawTemporaryFilePath).size() > 0) {
        return true;
    }

    return writeSampleRateHeaderToTemporaryFile(
        m_rawTemporaryFilePath,
        m_rawSampleRate,
        QIODevice::WriteOnly | QIODevice::Truncate,
        "write raw sample rate header");
}

bool DatabaseCache::ensureRealtimeChannelTemporaryFileCreated(int channelIndex)
{
    if (!isValidChannelIndex(channelIndex)) {
        return false;
    }

    if (channelIndex >= m_realtimeChannelTemporaryFilePaths.size()) {
        m_realtimeChannelTemporaryFilePaths.resize(kRealtimeChannelCount);
    }

    if (!ensureDirectoryExists(DatabaseStoragePaths::TEMPORARY_FILE_PATH)) {
        return false;
    }

    QString& temporaryFilePath = m_realtimeChannelTemporaryFilePaths[channelIndex];
    if (temporaryFilePath.isEmpty()) {
        if (m_realtimeCollectionSessionTime.isEmpty()) {
            m_realtimeCollectionSessionTime = QDateTime::currentDateTime().toString(
                QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        }
        temporaryFilePath = buildRealtimeChannelFilePath(m_realtimeCollectionSessionTime, channelIndex);
    }

    if (QFile(temporaryFilePath).exists() && QFile(temporaryFilePath).size() > 0) {
        return true;
    }

    return writeSampleRateHeaderToTemporaryFile(
        temporaryFilePath,
        m_rawSampleRate,
        QIODevice::WriteOnly | QIODevice::Truncate,
        "write realtime channel sample rate header");
}

bool DatabaseCache::ensureDownsampledTemporaryFileCreated()
{
    return ensureTemporaryFileCreated(m_downsampledTemporaryFilePath);
}

bool DatabaseCache::ensureImportedRawTemporaryFileCreated()
{
    if (!ensureTemporaryFileCreated(m_importedRawTemporaryFilePath)) {
        return false;
    }

    if (QFile(m_importedRawTemporaryFilePath).size() > 0) {
        return true;
    }

    return writeSampleRateHeaderToTemporaryFile(
        m_importedRawTemporaryFilePath,
        m_importedRawSampleRate,
        QIODevice::WriteOnly | QIODevice::Truncate,
        "write imported raw sample rate header");
}

bool DatabaseCache::ensureImportedDownsampledTemporaryFileCreated()
{
    return ensureTemporaryFileCreated(m_importedDownsampledTemporaryFilePath);
}

bool DatabaseCache::ensureRealtimeSpectrumTemporaryFileCreated()
{
    return ensureTemporaryFileCreated(m_realtimeSpectrumTemporaryFilePath);
}

bool DatabaseCache::ensureImportedSpectrumTemporaryFileCreated()
{
    return ensureTemporaryFileCreated(m_importedSpectrumTemporaryFilePath);
}

bool DatabaseCache::ensureTemporaryFileCreated(QString& temporaryFilePath)
{
    return createTemporaryFileInManagedDirectory(
        temporaryFilePath,
        buildTemporaryFileTemplate(QStringLiteral("cache"), QStringLiteral(".tmp")),
        "DataManager: failed to create temporary cache file");
}

bool DatabaseCache::writeBytesToTemporaryFile(
    const QString& temporaryFilePath,
    const char* bytes,
    qint64 byteCount,
    QIODevice::OpenMode openMode,
    const char* failureContext)
{
    if (temporaryFilePath.isEmpty() || bytes == nullptr || byteCount <= 0) {
        return false;
    }

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(openMode)) {
        qWarning() << "DataManager: failed to" << failureContext << "to temporary file"
                   << temporaryFilePath;
        return false;
    }

    const qint64 writtenBytes = temporaryFile.write(bytes, byteCount);
    temporaryFile.close();

    if (writtenBytes != byteCount) {
        qWarning() << "DataManager: incomplete write while trying to" << failureContext
                   << "to temporary file" << temporaryFilePath;
        return false;
    }

    return true;
}

bool DatabaseCache::writeSampleRateHeaderToTemporaryFile(
    const QString& temporaryFilePath,
    int sampleRate,
    QIODevice::OpenMode openMode,
    const char* failureContext)
{
    const float sampleRateValue = static_cast<float>(sanitizeSampleRate(sampleRate));
    return writeBytesToTemporaryFile(
        temporaryFilePath,
        reinterpret_cast<const char*>(&sampleRateValue),
        sizeof(sampleRateValue),
        openMode,
        failureContext);
}

int DatabaseCache::readSampleRateFromTemporaryFile(const QString& temporaryFilePath)
{
    if (temporaryFilePath.isEmpty()) {
        return 0;
    }

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray headerBytes = temporaryFile.read(sizeof(float));
    temporaryFile.close();

    if (headerBytes.size() != sizeof(float)) {
        return 0;
    }

    float sampleRateValue = 0.0f;
    std::memcpy(&sampleRateValue, headerBytes.constData(), sizeof(float));
    const int sampleRate = static_cast<int>(std::lround(sampleRateValue));
    return sampleRate > 0 ? sampleRate : 0;
}

qint64 DatabaseCache::dataElementCountFromTemporaryFile(const QString& temporaryFilePath)
{
    if (temporaryFilePath.isEmpty()) {
        return 0;
    }

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const qint64 fileSize = temporaryFile.size();
    temporaryFile.close();
    if (fileSize <= static_cast<qint64>(sizeof(float))) {
        return 0;
    }

    return (fileSize / static_cast<qint64>(sizeof(float))) - 1;
}

QVector<float> DatabaseCache::readFloatRangeFromTemporaryFile(
    const QString& temporaryFilePath,
    qsizetype startIndex,
    qsizetype elementCount)
{
    QVector<float> values;
    if (temporaryFilePath.isEmpty() || startIndex < 0 || elementCount <= 0) {
        return values;
    }

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return values;
    }

    const qint64 totalElementCount =
        (temporaryFile.size() / static_cast<qint64>(sizeof(float))) - 1;
    if (totalElementCount <= 0 || startIndex >= totalElementCount) {
        temporaryFile.close();
        return values;
    }

    const qsizetype readableElementCount = static_cast<qsizetype>(std::min<qint64>(
        totalElementCount - startIndex,
        elementCount));
    if (readableElementCount <= 0) {
        temporaryFile.close();
        return values;
    }

    temporaryFile.seek(static_cast<qint64>(sizeof(float)) +
                       static_cast<qint64>(startIndex) * static_cast<qint64>(sizeof(float)));
    const QByteArray bytes =
        temporaryFile.read(static_cast<qint64>(readableElementCount) * sizeof(float));
    temporaryFile.close();

    const qsizetype actualElementCount = bytes.size() / static_cast<qsizetype>(sizeof(float));
    if (actualElementCount <= 0) {
        return values;
    }

    values.resize(actualElementCount);
    std::memcpy(values.data(), bytes.constData(), static_cast<size_t>(bytes.size()));
    return values;
}

QVariantList DatabaseCache::waveformPointsFromTemporaryFile(
    const QString& temporaryFilePath,
    double fromSeconds,
    double toSeconds)
{
    if (toSeconds < fromSeconds) {
        return {};
    }

    const int sampleRate = readSampleRateFromTemporaryFile(temporaryFilePath);
    const qint64 sampleCount = dataElementCountFromTemporaryFile(temporaryFilePath);
    if (sampleRate <= 0 || sampleCount <= 0) {
        return {};
    }

    const double duration = static_cast<double>(sampleCount) / static_cast<double>(sampleRate);
    const double clampedFromSeconds = std::clamp(fromSeconds, 0.0, duration);
    const double clampedToSeconds = std::clamp(toSeconds, clampedFromSeconds, duration);

    qsizetype startIndex =
        static_cast<qsizetype>(std::ceil(clampedFromSeconds * static_cast<double>(sampleRate)));
    qsizetype endIndex =
        static_cast<qsizetype>(std::floor(clampedToSeconds * static_cast<double>(sampleRate)));
    startIndex = std::clamp<qsizetype>(startIndex, 0, static_cast<qsizetype>(sampleCount - 1));
    endIndex = std::clamp<qsizetype>(endIndex, 0, static_cast<qsizetype>(sampleCount - 1));

    if (endIndex < startIndex) {
        return {};
    }

    const QVector<float> visibleSamples =
        readFloatRangeFromTemporaryFile(
            temporaryFilePath,
            startIndex,
            endIndex - startIndex + 1);
    QVector<QPointF> points;
    points.reserve(visibleSamples.size());

    for (qsizetype index = 0; index < visibleSamples.size(); ++index) {
        const qsizetype sampleIndex = startIndex + index;
        points.append(
            QPointF(
                static_cast<double>(sampleIndex) / static_cast<double>(sampleRate),
                visibleSamples[index]));
    }

    return DataManager::toVariantPointList(points);
}

QVariantList DatabaseCache::downsampledWaveformPointsFromTemporaryFile(
    const QString& temporaryFilePath,
    double fromSeconds,
    double toSeconds)
{
    if (temporaryFilePath.isEmpty() || toSeconds < fromSeconds) {
        return {};
    }

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return {};
    }

    const qint64 fileSize = temporaryFile.size();
    const qint64 recordSize = static_cast<qint64>(sizeof(DownsampledPointRecord));
    if (fileSize < recordSize || fileSize % recordSize != 0) {
        temporaryFile.close();
        return {};
    }

    const qsizetype totalPointCount =
        static_cast<qsizetype>(fileSize / recordSize);
    bool readFailed = false;

    auto readRecordAt = [&](qsizetype index, DownsampledPointRecord* record) -> bool {
        if (record == nullptr || index < 0 || index >= totalPointCount) {
            return false;
        }

        if (!temporaryFile.seek(static_cast<qint64>(index) * recordSize)) {
            return false;
        }

        const QByteArray bytes = temporaryFile.read(recordSize);
        if (bytes.size() != recordSize) {
            return false;
        }

        std::memcpy(record, bytes.constData(), static_cast<size_t>(recordSize));
        return true;
    };

    auto lowerBoundIndex = [&](double xValue) -> qsizetype {
        qsizetype left = 0;
        qsizetype right = totalPointCount;

        while (left < right) {
            const qsizetype mid = left + (right - left) / 2;
            DownsampledPointRecord record;
            if (!readRecordAt(mid, &record)) {
                readFailed = true;
                return 0;
            }

            if (static_cast<double>(record.x) < xValue) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }

        return left;
    };

    auto upperBoundIndex = [&](double xValue) -> qsizetype {
        qsizetype left = 0;
        qsizetype right = totalPointCount;

        while (left < right) {
            const qsizetype mid = left + (right - left) / 2;
            DownsampledPointRecord record;
            if (!readRecordAt(mid, &record)) {
                readFailed = true;
                return 0;
            }

            if (static_cast<double>(record.x) <= xValue) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }

        return left;
    };

    const qsizetype startIndex = lowerBoundIndex(fromSeconds);
    const qsizetype endIndex = upperBoundIndex(toSeconds);
    if (readFailed || endIndex <= startIndex) {
        temporaryFile.close();
        return {};
    }

    const qsizetype readablePointCount = endIndex - startIndex;
    if (!temporaryFile.seek(static_cast<qint64>(startIndex) * recordSize)) {
        temporaryFile.close();
        return {};
    }

    const QByteArray bytes =
        temporaryFile.read(static_cast<qint64>(readablePointCount) * recordSize);
    temporaryFile.close();

    const qsizetype actualPointCount = bytes.size() / static_cast<qsizetype>(recordSize);
    if (actualPointCount <= 0) {
        return {};
    }

    QVector<DownsampledPointRecord> records(actualPointCount);
    std::memcpy(records.data(), bytes.constData(), static_cast<size_t>(bytes.size()));

    QVector<QPointF> points;
    points.reserve(records.size());
    for (const DownsampledPointRecord& record : records) {
        points.append(QPointF(record.x, record.y));
    }

    return DataManager::toVariantPointList(points);
}

QVariantList DatabaseCache::spectrumPointsFromTemporaryFile(
    const QString& temporaryFilePath,
    double fromFrequencyHz,
    double toFrequencyHz)
{
    if (toFrequencyHz < fromFrequencyHz) {
        return {};
    }

    const int sampleRate = readSampleRateFromTemporaryFile(temporaryFilePath);
    const qint64 magnitudeCount = dataElementCountFromTemporaryFile(temporaryFilePath);
    if (sampleRate <= 0 || magnitudeCount <= 0) {
        return {};
    }

    const double boundaryFrequencyHz = spectrumBoundaryFromTemporaryFile(temporaryFilePath);
    const double clampedFromFrequencyHz = std::clamp(fromFrequencyHz, 0.0, boundaryFrequencyHz);
    const double clampedToFrequencyHz =
        std::clamp(toFrequencyHz, clampedFromFrequencyHz, boundaryFrequencyHz);

    const int fftSize = magnitudeCount > 1 ? static_cast<int>((magnitudeCount - 1) * 2) : 0;
    const double frequencyResolution =
        fftSize > 0
            ? static_cast<double>(sampleRate) / static_cast<double>(fftSize)
            : boundaryFrequencyHz;
    if (frequencyResolution <= 0.0) {
        return {};
    }

    qsizetype startIndex =
        static_cast<qsizetype>(std::ceil(clampedFromFrequencyHz / frequencyResolution));
    qsizetype endIndex =
        static_cast<qsizetype>(std::floor(clampedToFrequencyHz / frequencyResolution));
    startIndex = std::clamp<qsizetype>(startIndex, 0, static_cast<qsizetype>(magnitudeCount - 1));
    endIndex = std::clamp<qsizetype>(endIndex, 0, static_cast<qsizetype>(magnitudeCount - 1));

    if (endIndex < startIndex) {
        return {};
    }

    const QVector<float> visibleMagnitudes =
        readFloatRangeFromTemporaryFile(
            temporaryFilePath,
            startIndex,
            endIndex - startIndex + 1);
    QVector<QPointF> points;
    points.reserve(visibleMagnitudes.size());

    for (qsizetype index = 0; index < visibleMagnitudes.size(); ++index) {
        const qsizetype magnitudeIndex = startIndex + index;
        points.append(QPointF(
            static_cast<double>(magnitudeIndex) * frequencyResolution,
            visibleMagnitudes[index]));
    }

    return DataManager::toVariantPointList(points);
}

QVariantList DatabaseCache::spectrumPointsFromImportedStftTemporaryFile(
    const QString& temporaryFilePath,
    double fromFrequencyHz,
    double toFrequencyHz,
    double centerSeconds)
{
    if (toFrequencyHz < fromFrequencyHz) {
        return {};
    }

    const ImportedStftFrameSelection selection =
        selectImportedStftFrameFromTemporaryFile(temporaryFilePath, centerSeconds);
    if (!selection.frameCacheFormat) {
        // Backward compatibility: fall back to one-frame spectrum cache format.
        return spectrumPointsFromTemporaryFile(temporaryFilePath, fromFrequencyHz, toFrequencyHz);
    }

    if (!selection.valid) {
        return {};
    }

    const qsizetype selectedStartIndex =
        static_cast<qsizetype>(selection.selectedFrameIndex * selection.valuesPerFrame + 1);
    const QVector<float> selectedMagnitudes = readFloatRangeFromTemporaryFile(
        temporaryFilePath,
        selectedStartIndex,
        selection.magnitudeCountPerFrame);
    if (selectedMagnitudes.size() != selection.magnitudeCountPerFrame) {
        return {};
    }

    const double boundaryFrequencyHz = std::min(
        static_cast<double>(MAX_DISPLAY_FREQUENCY),
        static_cast<double>(selection.sampleRate) / 2.0);
    const double clampedFromFrequencyHz = std::clamp(fromFrequencyHz, 0.0, boundaryFrequencyHz);
    const double clampedToFrequencyHz =
        std::clamp(toFrequencyHz, clampedFromFrequencyHz, boundaryFrequencyHz);
    const double frequencyResolution =
        static_cast<double>(selection.sampleRate) / static_cast<double>(selection.fftSize);
    if (frequencyResolution <= 0.0) {
        return {};
    }

    qsizetype startIndex =
        static_cast<qsizetype>(std::ceil(clampedFromFrequencyHz / frequencyResolution));
    qsizetype endIndex =
        static_cast<qsizetype>(std::floor(clampedToFrequencyHz / frequencyResolution));
    startIndex =
        std::clamp<qsizetype>(startIndex, 0, static_cast<qsizetype>(selection.magnitudeCountPerFrame - 1));
    endIndex =
        std::clamp<qsizetype>(endIndex, 0, static_cast<qsizetype>(selection.magnitudeCountPerFrame - 1));

    if (endIndex < startIndex) {
        return {};
    }

    QVector<QPointF> points;
    points.reserve(endIndex - startIndex + 1);
    for (qsizetype index = startIndex; index <= endIndex; ++index) {
        points.append(QPointF(
            static_cast<double>(index) * frequencyResolution,
            selectedMagnitudes[index]));
    }

    return DataManager::toVariantPointList(points);
}

QVariantMap DatabaseCache::spectrumTimeRangeFromImportedStftTemporaryFile(
    const QString& temporaryFilePath,
    double centerSeconds)
{
    const ImportedStftFrameSelection selection =
        selectImportedStftFrameFromTemporaryFile(temporaryFilePath, centerSeconds);
    if (!selection.frameCacheFormat || !selection.valid) {
        return {};
    }

    return buildSpectrumTimeRange(
        selection.selectedCenterSeconds,
        static_cast<double>(kImportedStftWindowSeconds));
}

DatabaseCache::ImportedStftFrameSelection DatabaseCache::selectImportedStftFrameFromTemporaryFile(
    const QString& temporaryFilePath,
    double centerSeconds)
{
    ImportedStftFrameSelection selection;

    selection.sampleRate = readSampleRateFromTemporaryFile(temporaryFilePath);
    if (selection.sampleRate <= 0) {
        return selection;
    }

    const qint64 totalValueCount = dataElementCountFromTemporaryFile(temporaryFilePath);
    if (totalValueCount <= 0) {
        return selection;
    }

    const int windowSampleCount = computeImportedStftWindowSampleCount(selection.sampleRate);
    selection.fftSize = computeImportedStftFftSize(windowSampleCount);
    selection.magnitudeCountPerFrame = (selection.fftSize / 2) + 1;
    selection.valuesPerFrame = static_cast<qint64>(selection.magnitudeCountPerFrame) + 1;

    if (selection.valuesPerFrame <= 1 ||
        (totalValueCount % selection.valuesPerFrame) != 0) {
        return selection;
    }

    const qint64 frameCount = totalValueCount / selection.valuesPerFrame;
    if (frameCount <= 0) {
        return selection;
    }

    selection.frameCacheFormat = true;

    QFile temporaryFile(temporaryFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return selection;
    }

    const double clampedCenterSeconds = std::max(0.0, centerSeconds);
    double nearestDistance = std::numeric_limits<double>::max();

    for (qint64 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const qint64 centerOffsetBytes =
            static_cast<qint64>(sizeof(float)) +
            frameIndex * selection.valuesPerFrame * static_cast<qint64>(sizeof(float));
        if (!temporaryFile.seek(centerOffsetBytes)) {
            return selection;
        }

        const QByteArray centerBytes = temporaryFile.read(sizeof(float));
        if (centerBytes.size() != sizeof(float)) {
            return selection;
        }

        float frameCenterSeconds = 0.0f;
        std::memcpy(&frameCenterSeconds, centerBytes.constData(), sizeof(float));
        const double frameCenter = static_cast<double>(frameCenterSeconds);
        const double distance = std::abs(frameCenter - clampedCenterSeconds);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            selection.valid = true;
            selection.selectedFrameIndex = frameIndex;
            selection.selectedCenterSeconds = frameCenter;
        }
    }

    return selection;
}

double DatabaseCache::waveformDurationFromTemporaryFile(const QString& temporaryFilePath)
{
    const int sampleRate = readSampleRateFromTemporaryFile(temporaryFilePath);
    const qint64 sampleCount = dataElementCountFromTemporaryFile(temporaryFilePath);
    if (sampleRate <= 0 || sampleCount <= 0) {
        return 0.0;
    }

    return static_cast<double>(sampleCount) / static_cast<double>(sampleRate);
}

double DatabaseCache::spectrumBoundaryFromTemporaryFile(const QString& temporaryFilePath)
{
    const int sampleRate = readSampleRateFromTemporaryFile(temporaryFilePath);
    if (sampleRate <= 0) {
        return 0.0;
    }

    return std::min(
        static_cast<double>(MAX_DISPLAY_FREQUENCY),
        static_cast<double>(sampleRate) / 2.0);
}

void DatabaseCache::ensureDownsampledRingBufferAllocated()
{
    if (m_downsampledRingBuffer.size() != DOWNSAMPLED_RING_BUFFER_SIZE) {
        m_downsampledRingBuffer.resize(DOWNSAMPLED_RING_BUFFER_SIZE);
    }
}

void DatabaseCache::releaseDownsampledRingBuffer()
{
    m_downsampledRingBuffer.clear();
    m_downsampledRingBuffer.squeeze();
    m_downsampledBufferedPointCount = 0;
}

void DatabaseCache::removeRawTemporaryFile()
{
    removeTemporaryFile(m_rawTemporaryFilePath);
}

void DatabaseCache::removeRealtimeChannelTemporaryFiles()
{
    for (QString& temporaryFilePath : m_realtimeChannelTemporaryFilePaths) {
        removeTemporaryFile(temporaryFilePath);
    }
}

void DatabaseCache::removeDownsampledTemporaryFile()
{
    removeTemporaryFile(m_downsampledTemporaryFilePath);
}

void DatabaseCache::removeRealtimeSpectrumTemporaryFile()
{
    removeTemporaryFile(m_realtimeSpectrumTemporaryFilePath);
}

bool DatabaseCache::isValidChannelIndex(int channelIndex)
{
    return channelIndex >= 0 && channelIndex < kRealtimeChannelCount;
}

void DatabaseCache::removeTemporaryFile(QString& temporaryFilePath)
{
    if (!temporaryFilePath.isEmpty()) {
        QFile::remove(temporaryFilePath);
        temporaryFilePath.clear();
    }
}

FeatureCache::FeatureCache() = default;

FeatureCache::~FeatureCache() = default;

void FeatureCache::clear()
{
    m_importedFeatureValues.clear();
    m_importedAnalysisSummary.clear();
    if (!m_importedFeatureTemporaryFilePath.isEmpty()) {
        QFile::remove(m_importedFeatureTemporaryFilePath);
        m_importedFeatureTemporaryFilePath.clear();
    }
}

void FeatureCache::storeImportedFeatureValues(const QVariantMap& featureValues)
{
    if (featureValues.isEmpty()) {
        clear();
        return;
    }

    m_importedFeatureValues = featureValues;
}

QVariantMap FeatureCache::importedFeatureValues() const
{
    return m_importedFeatureValues;
}

QString FeatureCache::importedFeatureTemporaryFilePath() const
{
    return m_importedFeatureTemporaryFilePath;
}

void FeatureCache::setImportedFeatureTemporaryFilePath(const QString& temporaryFilePath)
{
    if (m_importedFeatureTemporaryFilePath == temporaryFilePath) {
        return;
    }

    if (!m_importedFeatureTemporaryFilePath.isEmpty()) {
        QFile::remove(m_importedFeatureTemporaryFilePath);
    }

    m_importedFeatureTemporaryFilePath = temporaryFilePath;
}

void FeatureCache::updateImportedAnalysisSummary(const QVariantMap& summary)
{
    if (summary.isEmpty()) {
        return;
    }

    mergeVariantMaps(m_importedAnalysisSummary, summary);
}

QVariantMap FeatureCache::importedAnalysisSummary() const
{
    return m_importedAnalysisSummary;
}

DataManager::DataManager(QObject* parent)
    : QObject(parent)
{
    ensureDatabaseStorageDirectories();
    if (!clearDirectoryContents(DatabaseStoragePaths::TEMPORARY_FILE_PATH)) {
        qWarning() << "DataManager: startup temporary directory cleanup incomplete:"
                   << DatabaseStoragePaths::TEMPORARY_FILE_PATH;
    }
}

DataManager::~DataManager()
{
    QMutexLocker databaseLocker(&m_databaseCacheMutex);
    QMutexLocker featureLocker(&m_featureCacheMutex);
    delete m_databaseCache;
    m_databaseCache = nullptr;
    delete m_featureCache;
    m_featureCache = nullptr;
}

bool DataManager::highSpeedCollectionMode() const
{
    QMutexLocker locker(&m_runtimeConfigMutex);
    return m_highSpeedCollectionMode;
}

void DataManager::setHighSpeedCollectionMode(bool enabled)
{
    bool modeChanged = false;
    bool sampleRateChanged = false;

    {
        QMutexLocker locker(&m_runtimeConfigMutex);
        const int nextSampleRate = enabled
            ? HIGH_SPEED_SAMPLE_RATE
            : m_normalConfiguredSampleRate;

        modeChanged = m_highSpeedCollectionMode != enabled;
        sampleRateChanged = m_configuredSampleRate != nextSampleRate;
        if (!modeChanged && !sampleRateChanged) {
            return;
        }

        m_highSpeedCollectionMode = enabled;
        m_configuredSampleRate = nextSampleRate;
    }

    if (modeChanged) {
        emit highSpeedCollectionModeChanged();
    }
    if (sampleRateChanged) {
        emit configuredSampleRateChanged();
    }
}

int DataManager::configuredSampleRate() const
{
    QMutexLocker locker(&m_runtimeConfigMutex);
    return m_configuredSampleRate;
}

void DataManager::setConfiguredSampleRate(int sampleRate)
{
    const int normalizedSampleRate = normalizeConfiguredSampleRate(sampleRate);
    bool sampleRateChanged = false;

    {
        QMutexLocker locker(&m_runtimeConfigMutex);
        if (m_normalConfiguredSampleRate == normalizedSampleRate &&
            (m_highSpeedCollectionMode || m_configuredSampleRate == normalizedSampleRate)) {
            return;
        }

        m_normalConfiguredSampleRate = normalizedSampleRate;
        if (!m_highSpeedCollectionMode && m_configuredSampleRate != normalizedSampleRate) {
            m_configuredSampleRate = normalizedSampleRate;
            sampleRateChanged = true;
        }
    }

    if (sampleRateChanged) {
        emit configuredSampleRateChanged();
    }
}

void DataManager::initializeDatabase()
{
    ensureDatabaseStorageDirectories();

    {
        QMutexLocker locker(&m_databaseCacheMutex);
        ensureDatabaseCacheCreated()->clear();
    }

    {
        QMutexLocker locker(&m_featureCacheMutex);
        ensureFeatureCacheCreated()->clear();
    }

    emit importedAnalysisSummaryChanged();
}

void DataManager::collectionCompleted()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return;
    }

    m_databaseCache->collectionCompleted();
}

void DataManager::splicTimeDomainData(const QVector<float>& rawData, int sampleRate)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return;
    }

    m_databaseCache->splicTimeDomainData(rawData, sampleRate);
}

void DataManager::splicRealtimeChannelTimeDomainData(
    int channelIndex,
    const QVector<float>& rawData,
    int sampleRate)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return;
    }

    m_databaseCache->splicRealtimeChannelTimeDomainData(channelIndex, rawData, sampleRate);
}

void DataManager::splicDownsampledData(const QVector<QPointF>& downsampledData)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return;
    }

    m_databaseCache->splicDownsampledData(downsampledData);
}

void DataManager::storeImportedTimeDomainData(
    int sampleRate,
    const QVector<float>& importedRawData)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    ensureDatabaseCacheCreated()->storeImportedTimeDomainData(sampleRate, importedRawData);
}

void DataManager::storeImportedDownsampledData(const QVector<QPointF>& importedDownsampledData)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    ensureDatabaseCacheCreated()->storeImportedDownsampledData(importedDownsampledData);
}

void DataManager::storeRealtimeSpectrumData(
    int sampleRate,
    const QVector<float>& magnitudes)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    ensureDatabaseCacheCreated()->storeRealtimeSpectrumData(sampleRate, magnitudes);
}

void DataManager::storeImportedSpectrumData(
    int sampleRate,
    const QVector<float>& magnitudes)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    ensureDatabaseCacheCreated()->storeImportedSpectrumData(sampleRate, magnitudes);
}

void DataManager::storeImportedStftSpectrumData(
    int sampleRate,
    const QVector<float>& frameCenterTimes,
    const QVector<QVector<float>>& frameMagnitudes)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    ensureDatabaseCacheCreated()->storeImportedStftSpectrumData(
        sampleRate,
        frameCenterTimes,
        frameMagnitudes);
}

void DataManager::storeImportedFeatureValues(const QVariantMap& featureValues)
{
    QMutexLocker locker(&m_featureCacheMutex);

    ensureFeatureCacheCreated()->storeImportedFeatureValues(featureValues);
}

void DataManager::setImportedFeatureTemporaryFilePath(const QString& temporaryFilePath)
{
    QMutexLocker locker(&m_featureCacheMutex);

    ensureFeatureCacheCreated()->setImportedFeatureTemporaryFilePath(temporaryFilePath);
}

void DataManager::updateImportedAnalysisSummary(const QVariantMap& summary)
{
    if (summary.isEmpty()) {
        return;
    }

    {
        QMutexLocker locker(&m_featureCacheMutex);
        ensureFeatureCacheCreated()->updateImportedAnalysisSummary(summary);
    }

    emit importedAnalysisSummaryChanged();
}

void DataManager::clearImportedData()
{
    {
        QMutexLocker locker(&m_databaseCacheMutex);
        if (m_databaseCache != nullptr) {
            m_databaseCache->clearImportedData();
        }
    }

    {
        QMutexLocker locker(&m_featureCacheMutex);
        if (m_featureCache != nullptr) {
            m_featureCache->clear();
        }
    }

    emit importedAnalysisSummaryChanged();
}

QString DataManager::rawTemporaryFilePath()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->rawTemporaryFilePath();
}

QString DataManager::downsampledTemporaryFilePath()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->downsampledTemporaryFilePath();
}

QString DataManager::importedRawTemporaryFilePath()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedRawTemporaryFilePath();
}

QString DataManager::importedDownsampledTemporaryFilePath()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedDownsampledTemporaryFilePath();
}

QString DataManager::realtimeChannelTemporaryFilePath(int channelIndex)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->realtimeChannelTemporaryFilePath(channelIndex);
}

QString DataManager::temporaryFilePath()
{
    return rawTemporaryFilePath();
}

QString DataManager::importedFeatureTemporaryFilePath()
{
    QMutexLocker locker(&m_featureCacheMutex);

    if (m_featureCache == nullptr) {
        return {};
    }

    return m_featureCache->importedFeatureTemporaryFilePath();
}

QVariantList DataManager::realtimeWaveformPoints(double fromSeconds, double toSeconds)
{
    return realtimeWaveformPointsForChannel(0, fromSeconds, toSeconds);
}

QVariantList DataManager::realtimeWaveformPointsForChannel(
    int channelIndex,
    double fromSeconds,
    double toSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->realtimeWaveformPointsForChannel(
        channelIndex,
        fromSeconds,
        toSeconds);
}

QVariantList DataManager::realtimeDownsampledWaveformPoints(
    double fromSeconds,
    double toSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->realtimeDownsampledWaveformPoints(fromSeconds, toSeconds);
}

QVariantList DataManager::importedWaveformPoints(double fromSeconds, double toSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedWaveformPoints(fromSeconds, toSeconds);
}

QVariantList DataManager::importedDownsampledWaveformPoints(
    double fromSeconds,
    double toSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedDownsampledWaveformPoints(fromSeconds, toSeconds);
}

QVariantList DataManager::realtimeSpectrumPoints(double fromFrequencyHz, double toFrequencyHz)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->realtimeSpectrumPoints(fromFrequencyHz, toFrequencyHz);
}

QVariantList DataManager::importedSpectrumPoints(double fromFrequencyHz, double toFrequencyHz)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedSpectrumPoints(fromFrequencyHz, toFrequencyHz);
}

QVariantList DataManager::importedSpectrumPointsAtTime(
    double fromFrequencyHz,
    double toFrequencyHz,
    double centerSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedSpectrumPointsAtTime(
        fromFrequencyHz,
        toFrequencyHz,
        centerSeconds);
}

QVariantMap DataManager::importedSpectrumTimeRangeAtTime(double centerSeconds)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return {};
    }

    return m_databaseCache->importedSpectrumTimeRangeAtTime(centerSeconds);
}

QVariantMap DataManager::importedFeatureValues()
{
    QMutexLocker locker(&m_featureCacheMutex);

    if (m_featureCache == nullptr) {
        return {};
    }

    return m_featureCache->importedFeatureValues();
}

QVariantMap DataManager::importedAnalysisSummary()
{
    QMutexLocker locker(&m_featureCacheMutex);

    if (m_featureCache == nullptr) {
        return {};
    }

    return m_featureCache->importedAnalysisSummary();
}

double DataManager::realtimeWaveformDuration()
{
    return realtimeWaveformDurationForChannel(0);
}

double DataManager::realtimeWaveformDurationForChannel(int channelIndex)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0.0;
    }

    return m_databaseCache->realtimeWaveformDurationForChannel(channelIndex);
}

double DataManager::importedWaveformDuration()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0.0;
    }

    return m_databaseCache->importedWaveformDuration();
}

double DataManager::realtimeSpectrumBoundary()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0.0;
    }

    return m_databaseCache->realtimeSpectrumBoundary();
}

double DataManager::importedSpectrumBoundary()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0.0;
    }

    return m_databaseCache->importedSpectrumBoundary();
}

int DataManager::realtimeWaveformSampleRate()
{
    return realtimeWaveformSampleRateForChannel(0);
}

int DataManager::realtimeWaveformSampleRateForChannel(int channelIndex)
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0;
    }

    return m_databaseCache->realtimeWaveformSampleRateForChannel(channelIndex);
}

int DataManager::importedWaveformSampleRate()
{
    QMutexLocker locker(&m_databaseCacheMutex);

    if (m_databaseCache == nullptr) {
        return 0;
    }

    return m_databaseCache->importedWaveformSampleRate();
}

QVariantList DataManager::toVariantPointList(
    const QVector<QPointF>& source,
    double xScale,
    double xOffset)
{
    QVariantList points;
    points.reserve(source.size());

    for (const QPointF& point : source) {
        points.append(QVariant::fromValue(QPointF(xOffset + point.x() * xScale, point.y())));
    }

    return points;
}

DatabaseCache* DataManager::ensureDatabaseCacheCreated()
{
    if (m_databaseCache == nullptr) {
        m_databaseCache = new DatabaseCache();
    }

    return m_databaseCache;
}

FeatureCache* DataManager::ensureFeatureCacheCreated()
{
    if (m_featureCache == nullptr) {
        m_featureCache = new FeatureCache();
    }

    return m_featureCache;
}
