/** @file DataManager.h
 *  @brief 数据管理器 —— 采集数据的临时文件缓存、波形/频谱查询与特征缓存
 *
 *  本模块是 BSSAS 数据处理的中枢，包含三个核心类：
 *  - DatabaseCache：将实时采集或多通道时域/降采样/频谱数据存入临时文件，
 *    并按时间区间或频率区间返回 QML 可绘制的 QVariantList 数据点。
 *  - FeatureCache：缓存最近一次导入信号的特征值和综合分析摘要。
 *  - DataManager：单例门面，协调 DatabaseCache 和 FeatureCache，
 *    对外暴露 Q_PROPERTY 和 Q_INVOKABLE 接口供 QML 调用。
 *
 *  支持普通模式 (12 kHz) 和高速模式 (150 kHz) 两种采集速率。
 */

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QCoreApplication> // qApp 定义
#include <QMutex>
#include <QObject>
#include <QIODevice>
#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

/** @brief 数据库缓存 —— 管理时域/频谱数据的临时文件存储与区间查询
 *
 *  将实时采集或导入的原始波形、降采样波形、实时频谱和 STFT 频谱
 *  分别写入独立的临时文件，并提供按时间/频率区间线性检索的能力。
 *  内部维护降采样环形缓冲区以加速最近数据的访问。
 */
class DatabaseCache
{
public:
    /** @brief QML 频谱显示的最大频率上限 (Hz) */
    static constexpr float MAX_DISPLAY_FREQUENCY = 3000.0f;

    DatabaseCache();
    ~DatabaseCache();

    /** @brief 清空所有缓存数据 */
    void clear();
    /** @brief 标记一次采集已结束，后续可进行完整分析 */
    void collectionCompleted();
    bool realtimeCollectionAvailable() const;

    /** @brief 追加实时时域原始数据到临时文件 */
    void splicTimeDomainData(const QVector<float>& rawData, int sampleRate);
    /** @brief 追加指定通道的实时时域数据到临时文件 */
    void splicRealtimeChannelTimeDomainData(
        int channelIndex,
        const QVector<float>& rawData,
        int sampleRate);
    /** @brief 追加降采样数据点到临时文件 */
    void splicDownsampledData(const QVector<QPointF>& downsampledData);

    /** @brief 将导入的时域数据写入临时文件 */
    void storeImportedTimeDomainData(int sampleRate, const QVector<float>& importedRawData);
    /** @brief 将导入的降采样数据写入临时文件 */
    void storeImportedDownsampledData(const QVector<QPointF>& importedDownsampledData);
    /** @brief 将实时频谱幅度写入临时文件 */
    void storeRealtimeSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    /** @brief 将导入的频谱幅度写入临时文件 */
    void storeImportedSpectrumData(int sampleRate, const QVector<float>& magnitudes);
    /** @brief 将导入的 STFT 时频谱数据写入临时文件
     *  @param sampleRate 采样率
     *  @param frameCenterTimes 各帧中心时间 (秒)
     *  @param frameMagnitudes 每帧的幅度谱向量
     *  @param windowSeconds STFT 分析窗长 (秒)
     */
    void storeImportedStftSpectrumData(
        int sampleRate,
        const QVector<float>& frameCenterTimes,
        const QVector<QVector<float>>& frameMagnitudes,
        double windowSeconds);

    /** @brief 清空所有导入数据缓存 */
    void clearImportedData();

    /** @brief 获取实时原始数据临时文件路径 */
    QString rawTemporaryFilePath() const;
    /** @brief 获取降采样数据临时文件路径 */
    QString downsampledTemporaryFilePath() const;
    /** @brief 获取导入的原始数据临时文件路径 */
    QString importedRawTemporaryFilePath() const;
    /** @brief 获取导入的降采样数据临时文件路径 */
    QString importedDownsampledTemporaryFilePath() const;
    /** @brief 获取指定通道的实时数据临时文件路径 */
    QString realtimeChannelTemporaryFilePath(int channelIndex) const;
    /** @brief 获取通用临时文件目录路径 */
    QString temporaryFilePath() const;

    /** @brief 按时间区间获取实时波形数据点（所有通道合并）
     *  @param fromSeconds 起始时间 (秒)
     *  @param toSeconds 结束时间 (秒)
     *  @returns QML 可绑定的 [x, y] 数据点列表
     */
    QVariantList realtimeWaveformPoints(double fromSeconds, double toSeconds) const;
    /** @brief 按时间区间获取指定通道的实时波形数据点
     *  @param channelIndex 通道索引
     *  @param fromSeconds 起始时间 (秒)
     *  @param toSeconds 结束时间 (秒)
     */
    QVariantList realtimeWaveformPointsForChannel(
        int channelIndex,
        double fromSeconds,
        double toSeconds) const;
    /** @brief 按时间区间获取实时降采样波形数据点 */
    QVariantList realtimeDownsampledWaveformPoints(double fromSeconds, double toSeconds) const;
    /** @brief 按时间区间获取导入波形数据点 */
    QVariantList importedWaveformPoints(double fromSeconds, double toSeconds) const;
    /** @brief 按时间区间获取导入降采样波形数据点 */
    QVariantList importedDownsampledWaveformPoints(double fromSeconds, double toSeconds) const;
    /** @brief 按频率区间获取实时频谱数据点 */
    QVariantList realtimeSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const;
    /** @brief 按频率区间获取导入频谱数据点 */
    QVariantList importedSpectrumPoints(double fromFrequencyHz, double toFrequencyHz) const;
    /** @brief 在指定时间中心按频率区间获取导入 STFT 频谱数据点
     *  @param fromFrequencyHz 起始频率 (Hz)
     *  @param toFrequencyHz 结束频率 (Hz)
     *  @param centerSeconds 帧中心时间 (秒)
     */
    QVariantList importedSpectrumPointsAtTime(
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds) const;
    /** @brief 获取导入 STFT 频谱在指定时间附近的可用时间范围
     *  @param centerSeconds 参考中心时间 (秒)
     *  @returns 包含起始/结束时间的 QVariantMap
     */
    QVariantMap importedSpectrumTimeRangeAtTime(double centerSeconds) const;

    /** @brief 获取实时波形总时长 (秒) */
    double realtimeWaveformDuration() const;
    /** @brief 获取指定通道的实时波形总时长 (秒) */
    double realtimeWaveformDurationForChannel(int channelIndex) const;
    /** @brief 获取导入波形总时长 (秒) */
    double importedWaveformDuration() const;
    /** @brief 获取实时频谱频率上界 (Hz) */
    double realtimeSpectrumBoundary() const;
    /** @brief 获取导入频谱频率上界 (Hz) */
    double importedSpectrumBoundary() const;
    /** @brief 获取实时波形采样率 */
    int realtimeWaveformSampleRate() const;
    /** @brief 获取指定通道的实时波形采样率 */
    int realtimeWaveformSampleRateForChannel(int channelIndex) const;
    /** @brief 获取导入波形采样率 */
    int importedWaveformSampleRate() const;

private:
    /** @brief 降采样点记录（内部紧凑存储） */
    struct DownsampledPointRecord
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /** @brief 导入 STFT 帧选择结果 */
    struct ImportedStftFrameSelection
    {
        bool frameCacheFormat = false;
        bool valid = false;
        int sampleRate = 0;
        int fftSize = 0;
        int magnitudeCountPerFrame = 0;
        qint64 valuesPerFrame = 0;
        qint64 frameDataStartIndex = 0;
        qint64 selectedFrameIndex = 0;
        double selectedCenterSeconds = 0.0;
        double windowSeconds = 0.0;
    };

    // --- 写入临时文件 ---
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

    // --- 确保临时文件已创建 ---
    bool ensureRawTemporaryFileCreated();
    bool ensureRealtimeChannelTemporaryFileCreated(int channelIndex);
    bool ensureDownsampledTemporaryFileCreated();
    bool ensureImportedRawTemporaryFileCreated();
    bool ensureImportedDownsampledTemporaryFileCreated();
    bool ensureRealtimeSpectrumTemporaryFileCreated();
    bool ensureImportedSpectrumTemporaryFileCreated();

    // --- 静态工具方法 ---
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

    // --- 降采样环形缓冲区管理 ---
    void ensureDownsampledRingBufferAllocated();
    void releaseDownsampledRingBuffer();

    // --- 清理临时文件 ---
    void removeRawTemporaryFile();
    void removeRealtimeChannelTemporaryFiles();
    void removeDownsampledTemporaryFile();
    void removeRealtimeSpectrumTemporaryFile();
    static bool isValidChannelIndex(int channelIndex);
    static void removeTemporaryFile(QString& temporaryFilePath);

    /** 降采样环形缓冲区容量 */
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

/** @brief 特征缓存 —— 缓存最近一次导入信号的特征值和综合分析摘要 */
class FeatureCache
{
public:
    FeatureCache();
    ~FeatureCache();

    /** @brief 清空所有缓存的特征值 */
    void clear();
    /** @brief 存储导入信号的特征值 */
    void storeImportedFeatureValues(const QVariantMap& featureValues);
    /** @brief 获取导入信号的特征值 */
    QVariantMap importedFeatureValues() const;
    /** @brief 获取导入特征值的临时文件路径 */
    QString importedFeatureTemporaryFilePath() const;
    /** @brief 设置导入特征值的临时文件路径 */
    void setImportedFeatureTemporaryFilePath(const QString& temporaryFilePath);
    /** @brief 更新导入信号的综合分析摘要 */
    void updateImportedAnalysisSummary(const QVariantMap& summary);
    /** @brief 获取导入信号的综合分析摘要 */
    QVariantMap importedAnalysisSummary() const;

private:
    QVariantMap m_importedFeatureValues;
    QString m_importedFeatureTemporaryFilePath;
    QVariantMap m_importedAnalysisSummary;
};

/** @brief 数据管理器（单例门面）
 *
 *  统一管理采集缓存和特征缓存的生命周期，处理速率模式切换，
 *  并通过 Q_PROPERTY/Q_INVOKABLE 接口将数据查询暴露给 QML 层。
 */
class DataManager : public QObject
{
    Q_OBJECT
    /** @brief 是否启用高速采集模式 (150 kHz) */
    Q_PROPERTY(
        bool highSpeedCollectionMode
        READ highSpeedCollectionMode
        WRITE setHighSpeedCollectionMode
        NOTIFY highSpeedCollectionModeChanged)
    /** @brief 当前配置的采样率 */
    Q_PROPERTY(
        int configuredSampleRate
        READ configuredSampleRate
        WRITE setConfiguredSampleRate
        NOTIFY configuredSampleRateChanged)
    /** @brief 导入信号的综合分析摘要 */
    Q_PROPERTY(
        QVariantMap importedAnalysisSummary
        READ importedAnalysisSummary
        NOTIFY importedAnalysisSummaryChanged)
    Q_PROPERTY(
        bool realtimeCollectionAvailable
        READ realtimeCollectionAvailable
        NOTIFY realtimeCollectionAvailabilityChanged)
public:
    static constexpr int NORMAL_SAMPLE_RATE = 12000;
    static constexpr int HIGH_SPEED_SAMPLE_RATE = 150000;
    static constexpr int DEFAULT_SAMPLE_RATE = NORMAL_SAMPLE_RATE;

    /** @brief 获取全局单例实例（双重检查锁定线程安全） */
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
    Q_INVOKABLE bool realtimeCollectionAvailable();

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
        const QVector<QVector<float>>& frameMagnitudes,
        double windowSeconds);
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

    /** @brief 将 QPointF 序列转换为 QML 可绑定的 QVariantList 数据点格式
     *  @param source 源数据点序列
     *  @param xScale X 轴缩放因子
     *  @param xOffset X 轴偏移
     *  @returns 转换后的 QVariantList
     */
    static QVariantList toVariantPointList(
        const QVector<QPointF>& source,
        double xScale = 1.0,
        double xOffset = 0.0);

signals:
    /** @brief 高速采集模式状态发生变化时发出 */
    void highSpeedCollectionModeChanged();
    /** @brief 配置采样率发生变化时发出 */
    void configuredSampleRateChanged();
    /** @brief 导入信号分析摘要发生变化时发出 */
    void importedAnalysisSummaryChanged();
    void realtimeCollectionAvailabilityChanged();

private:
    explicit DataManager(QObject* parent = nullptr);
    ~DataManager() override;
    /** @brief 延迟创建 DatabaseCache 实例 */
    DatabaseCache* ensureDatabaseCacheCreated();
    /** @brief 延迟创建 FeatureCache 实例 */
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
