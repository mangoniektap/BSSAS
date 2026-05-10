#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QCoreApplication> // qApp definition
#include <QMutex>
#include <QObject>
#include <QIODevice>
#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DatabaseCache
{
public:
    static constexpr float MAX_DISPLAY_FREQUENCY = 3000.0f;

    DatabaseCache();
    ~DatabaseCache();

    void clear();
    void collectionCompleted();
    void splicTimeDomainData(const QVector<float>& rawData, int sampleRate);
    void splicRealtimeChannelTimeDomainData(
        int channelIndex,
        const QVector<float>& rawData,
        int sampleRate);
    void splicDownsampledData(const QVector<QPointF>& downsampledData);
    void storeImportedTimeDomainData(int sampleRate, const QVector<float>& importedRawData);
    void storeImportedDownsampledData(const QVector<QPointF>& importedDownsampledData);
    void storeRealtimeSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    void storeImportedSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    void storeImportedStftSpectrumData(
        int sampleRate,
        const QVector<float>& frameCenterTimes,
        const QVector<QVector<float>>& frameMagnitudes);
    void clearImportedData();
    QString rawTemporaryFilePath() const;
    QString downsampledTemporaryFilePath() const;
    QString importedRawTemporaryFilePath() const;
    QString importedDownsampledTemporaryFilePath() const;
    QString realtimeChannelTemporaryFilePath(int channelIndex) const;
    QString temporaryFilePath() const;
    QVariantList realtimeWaveformPoints(double fromSeconds, double toSeconds) const;
    QVariantList realtimeWaveformPointsForChannel(
        int channelIndex,
        double fromSeconds,
        double toSeconds) const;
    QVariantList realtimeDownsampledWaveformPoints(double fromSeconds, double toSeconds) const;
    QVariantList importedWaveformPoints(double fromSeconds, double toSeconds) const;
    QVariantList importedDownsampledWaveformPoints(double fromSeconds, double toSeconds) const;
    QVariantList realtimeSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const;
    QVariantList importedSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const;
    QVariantList importedSpectrumPointsAtTime(
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds) const;
    QVariantMap importedSpectrumTimeRangeAtTime(double centerSeconds) const;
    double realtimeWaveformDuration() const;
    double realtimeWaveformDurationForChannel(int channelIndex) const;
    double importedWaveformDuration() const;
    double realtimeSpectrumBoundary() const;
    double importedSpectrumBoundary() const;
    int realtimeWaveformSampleRate() const;
    int realtimeWaveformSampleRateForChannel(int channelIndex) const;
    int importedWaveformSampleRate() const;

private:
    struct DownsampledPointRecord
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct ImportedStftFrameSelection
    {
        bool frameCacheFormat = false;
        bool valid = false;
        int sampleRate = 0;
        int fftSize = 0;
        int magnitudeCountPerFrame = 0;
        qint64 valuesPerFrame = 0;
        qint64 selectedFrameIndex = 0;
        double selectedCenterSeconds = 0.0;
    };

    bool appendRawSamplesToTemporaryFile(const float* samples, qsizetype sampleCount);
    bool appendRealtimeChannelSamplesToTemporaryFile(
        int channelIndex,
        const float* samples,
        qsizetype sampleCount);
    bool appendDownsampledPointsToTemporaryFile(
        const DownsampledPointRecord* points,
        qsizetype pointCount);
    bool writeImportedRawSamplesToTemporaryFile(const float* samples, qsizetype sampleCount);
    bool writeImportedDownsampledPointsToTemporaryFile(
        const DownsampledPointRecord* points,
        qsizetype pointCount);
    bool writeSpectrumMagnitudesToTemporaryFile(
        QString& temporaryFilePath,
        int sampleRate,
        const float* magnitudes,
        qsizetype magnitudeCount,
        const char* failureContext);
    bool ensureRawTemporaryFileCreated();
    bool ensureRealtimeChannelTemporaryFileCreated(int channelIndex);
    bool ensureDownsampledTemporaryFileCreated();
    bool ensureImportedRawTemporaryFileCreated();
    bool ensureImportedDownsampledTemporaryFileCreated();
    bool ensureRealtimeSpectrumTemporaryFileCreated();
    bool ensureImportedSpectrumTemporaryFileCreated();
    static bool ensureTemporaryFileCreated(QString& temporaryFilePath);
    static bool writeBytesToTemporaryFile(
        const QString& temporaryFilePath,
        const char* bytes,
        qint64 byteCount,
        QIODevice::OpenMode openMode,
        const char* failureContext);
    static bool writeSampleRateHeaderToTemporaryFile(
        const QString& temporaryFilePath,
        int sampleRate,
        QIODevice::OpenMode openMode,
        const char* failureContext);
    static int readSampleRateFromTemporaryFile(const QString& temporaryFilePath);
    static qint64 dataElementCountFromTemporaryFile(const QString& temporaryFilePath);
    static QVector<float> readFloatRangeFromTemporaryFile(
        const QString& temporaryFilePath,
        qsizetype startIndex,
        qsizetype elementCount);
    static QVariantList waveformPointsFromTemporaryFile(
        const QString& temporaryFilePath,
        double fromSeconds,
        double toSeconds);
    static QVariantList downsampledWaveformPointsFromTemporaryFile(
        const QString& temporaryFilePath,
        double fromSeconds,
        double toSeconds);
    static QVariantList spectrumPointsFromTemporaryFile(
        const QString& temporaryFilePath,
        double fromFrequencyHz,
        double toFrequencyHz);
    static QVariantList spectrumPointsFromImportedStftTemporaryFile(
        const QString& temporaryFilePath,
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds);
    static QVariantMap spectrumTimeRangeFromImportedStftTemporaryFile(
        const QString& temporaryFilePath,
        double centerSeconds);
    static ImportedStftFrameSelection selectImportedStftFrameFromTemporaryFile(
        const QString& temporaryFilePath,
        double centerSeconds);
    static double waveformDurationFromTemporaryFile(const QString& temporaryFilePath);
    static double spectrumBoundaryFromTemporaryFile(const QString& temporaryFilePath);
    void ensureDownsampledRingBufferAllocated();
    void releaseDownsampledRingBuffer();
    void removeRawTemporaryFile();
    void removeRealtimeChannelTemporaryFiles();
    void removeDownsampledTemporaryFile();
    void removeRealtimeSpectrumTemporaryFile();
    static bool isValidChannelIndex(int channelIndex);
    static void removeTemporaryFile(QString& temporaryFilePath);

    static constexpr qsizetype DOWNSAMPLED_RING_BUFFER_SIZE = 4800;

    int m_rawSampleRate;
    QString m_rawTemporaryFilePath;
    QString m_realtimeCollectionSessionTime;
    QVector<QString> m_realtimeChannelTemporaryFilePaths;
    QVector<DownsampledPointRecord> m_downsampledRingBuffer;
    qsizetype m_downsampledBufferedPointCount = 0;
    QString m_downsampledTemporaryFilePath;
    int m_importedRawSampleRate;
    QString m_importedRawTemporaryFilePath;
    QString m_importedDownsampledTemporaryFilePath;
    QString m_realtimeSpectrumTemporaryFilePath;
    QString m_importedSpectrumTemporaryFilePath;
    bool m_collectionCompleted = false;
};

class FeatureCache
{
public:
    FeatureCache();
    ~FeatureCache();

    void clear();
    void storeImportedFeatureValues(const QVariantMap& featureValues);
    QVariantMap importedFeatureValues() const;
    QString importedFeatureTemporaryFilePath() const;
    void setImportedFeatureTemporaryFilePath(const QString& temporaryFilePath);
    void updateImportedAnalysisSummary(const QVariantMap& summary);
    QVariantMap importedAnalysisSummary() const;

private:
    QVariantMap m_importedFeatureValues;
    QString m_importedFeatureTemporaryFilePath;
    QVariantMap m_importedAnalysisSummary;
};

class DataManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        bool highSpeedCollectionMode
        READ highSpeedCollectionMode
        WRITE setHighSpeedCollectionMode
        NOTIFY highSpeedCollectionModeChanged)
    Q_PROPERTY(
        int configuredSampleRate
        READ configuredSampleRate
        WRITE setConfiguredSampleRate
        NOTIFY configuredSampleRateChanged)
    Q_PROPERTY(
        QVariantMap importedAnalysisSummary
        READ importedAnalysisSummary
        NOTIFY importedAnalysisSummaryChanged)
public:
    static constexpr int NORMAL_SAMPLE_RATE = 12000;
    static constexpr int HIGH_SPEED_SAMPLE_RATE = 150000;
    static constexpr int DEFAULT_SAMPLE_RATE = NORMAL_SAMPLE_RATE;

    static DataManager* instance()
    {
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new DataManager(qApp);
            }
        }
        return m_instance;
    }

    bool highSpeedCollectionMode() const;
    void setHighSpeedCollectionMode(bool enabled);
    int configuredSampleRate() const;
    void setConfiguredSampleRate(int sampleRate);

    Q_INVOKABLE void initializeDatabase();
    Q_INVOKABLE void collectionCompleted();
    void splicTimeDomainData(const QVector<float>& rawData, int sampleRate = DEFAULT_SAMPLE_RATE);
    void splicRealtimeChannelTimeDomainData(
        int channelIndex,
        const QVector<float>& rawData,
        int sampleRate = DEFAULT_SAMPLE_RATE);
    void splicDownsampledData(const QVector<QPointF>& downsampledData);
    void storeImportedTimeDomainData(int sampleRate, const QVector<float>& importedRawData);
    void storeImportedDownsampledData(const QVector<QPointF>& importedDownsampledData);
    void storeRealtimeSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    void storeImportedSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    void storeImportedStftSpectrumData(
        int sampleRate,
        const QVector<float>& frameCenterTimes,
        const QVector<QVector<float>>& frameMagnitudes);
    void storeImportedFeatureValues(const QVariantMap& featureValues);
    void setImportedFeatureTemporaryFilePath(const QString& temporaryFilePath);
    void updateImportedAnalysisSummary(const QVariantMap& summary);
    void clearImportedData();
    Q_INVOKABLE QString rawTemporaryFilePath();
    Q_INVOKABLE QString downsampledTemporaryFilePath();
    Q_INVOKABLE QString importedRawTemporaryFilePath();
    Q_INVOKABLE QString importedDownsampledTemporaryFilePath();
    Q_INVOKABLE QString realtimeChannelTemporaryFilePath(int channelIndex);
    Q_INVOKABLE QString importedFeatureTemporaryFilePath();
    Q_INVOKABLE QString temporaryFilePath();
    Q_INVOKABLE QVariantList realtimeWaveformPoints(double fromSeconds, double toSeconds);
    Q_INVOKABLE QVariantList realtimeWaveformPointsForChannel(
        int channelIndex,
        double fromSeconds,
        double toSeconds);
    Q_INVOKABLE QVariantList realtimeDownsampledWaveformPoints(double fromSeconds, double toSeconds);
    Q_INVOKABLE QVariantList importedWaveformPoints(double fromSeconds, double toSeconds);
    Q_INVOKABLE QVariantList importedDownsampledWaveformPoints(double fromSeconds, double toSeconds);
    Q_INVOKABLE QVariantList realtimeSpectrumPoints(double fromFrequencyHz, double toFrequencyHz);
    Q_INVOKABLE QVariantList importedSpectrumPoints(double fromFrequencyHz, double toFrequencyHz);
    Q_INVOKABLE QVariantList importedSpectrumPointsAtTime(
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds);
    Q_INVOKABLE QVariantMap importedSpectrumTimeRangeAtTime(double centerSeconds);
    Q_INVOKABLE QVariantMap importedAnalysisSummary();
    Q_INVOKABLE QVariantMap importedFeatureValues();
    Q_INVOKABLE double realtimeWaveformDuration();
    Q_INVOKABLE double realtimeWaveformDurationForChannel(int channelIndex);
    Q_INVOKABLE double importedWaveformDuration();
    Q_INVOKABLE double realtimeSpectrumBoundary();
    Q_INVOKABLE double importedSpectrumBoundary();
    int realtimeWaveformSampleRate();
    int realtimeWaveformSampleRateForChannel(int channelIndex);
    int importedWaveformSampleRate();

    static QVariantList toVariantPointList(
        const QVector<QPointF>& source,
        double xScale = 1.0,
        double xOffset = 0.0);

signals:
    void highSpeedCollectionModeChanged();
    void configuredSampleRateChanged();
    void importedAnalysisSummaryChanged();

private:
    explicit DataManager(QObject* parent = nullptr);
    ~DataManager() override;
    DatabaseCache* ensureDatabaseCacheCreated();
    FeatureCache* ensureFeatureCacheCreated();

    static DataManager* m_instance;

    mutable QMutex m_runtimeConfigMutex;
    bool m_highSpeedCollectionMode = false;
    int m_normalConfiguredSampleRate = DEFAULT_SAMPLE_RATE;
    int m_configuredSampleRate = DEFAULT_SAMPLE_RATE;
    QMutex m_databaseCacheMutex;
    DatabaseCache* m_databaseCache = nullptr;
    QMutex m_featureCacheMutex;
    FeatureCache* m_featureCache = nullptr;
};

#endif // DATAMANAGER_H
