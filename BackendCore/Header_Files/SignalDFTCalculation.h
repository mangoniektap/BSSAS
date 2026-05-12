/** @file SignalDFTCalculation.h
 *  @brief 信号 DFT/STFT 计算模块，支持实时与导入数据的短时傅里叶变换分析。
 */

#ifndef SIGNALDFTCALCULATION_H
#define SIGNALDFTCALCULATION_H

#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QThread;
class QTimer;

/** @brief DFT 工作线程，定时采集并处理实时音频数据以生成 STFT 频谱帧。 */
class DFTWorker : public QObject
{
    Q_OBJECT
public:
    explicit DFTWorker(QObject* parent = nullptr);
    ~DFTWorker();

public slots:
    /** @brief 启动实时 DFT 处理 */
    void startWork();
    /** @brief 停止实时 DFT 处理 */
    void stopWork();

private slots:
    /** @brief 定时处理回调，执行 STFT 帧生成 */
    void processing();

signals:
    /** @brief 频谱结果就绪信号 @param frequencies 频谱数据列表 @param centerTimeSeconds 帧中心时间 (秒) */
    void resultReady(const QVariantList& frequencies, double centerTimeSeconds);

private:
    /** @brief 从累积采样数据中生成 STFT 帧 */
    void produceStftFrames();

    QTimer* m_timer = nullptr;
    QVector<float> m_pendingSamples;
    double m_processedSeconds = 0.0;
    int m_stftWindowSampleCount = 0;
    const int COLLECTION_INTERVAL_MS = 100;
    QMutex m_dataMutex;
};

/** @brief 信号 DFT 计算管理器，管理实时和导入模式下的频谱分析线程。 */
class SignalDFTCalculation : public QObject
{
    Q_OBJECT
    /** @brief DFT 频谱数据 */
    Q_PROPERTY(QVariantList dftData READ dftData NOTIFY dftResultReady)
    /** @brief 导入分析是否正在进行 */
    Q_PROPERTY(bool importBusy READ importBusy NOTIFY importBusyChanged)

public:
    explicit SignalDFTCalculation(QObject* parent = nullptr);
    ~SignalDFTCalculation();

    /** @brief 获取当前 DFT 频谱数据 @returns 频谱数据列表 */
    Q_INVOKABLE const QVariantList& dftData() const { return m_dftData; }
    /** @brief 导入分析是否忙碌 @returns 忙碌状态 */
    Q_INVOKABLE bool importBusy() const { return m_importBusy; }
    /** @brief 启动实时 DFT 处理 */
    Q_INVOKABLE void startDFTProcessing();
    /** @brief 停止实时 DFT 处理 */
    Q_INVOKABLE void stopDFTProcessing();
    /** @brief 启动导入数据的 DFT 处理 */
    Q_INVOKABLE void startImportedDftProcessing();
    /**
     * @brief 查询指定时间范围内特定频段的实时 STFT 数据点。
     * @param fromFrequencyHz  起始频率 (Hz)
     * @param toFrequencyHz    终止频率 (Hz)
     * @param centerSeconds    查询中心时间 (秒)
     * @returns 符合条件的 STFT 数据点列表
     */
    Q_INVOKABLE QVariantList realtimeStftPointsAtTime(
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds) const;
    /**
     * @brief 查询指定时间点的实时 STFT 时间范围。
     * @param centerSeconds 查询中心时间 (秒)
     * @returns 包含时间范围信息的 QVariantMap
     */
    Q_INVOKABLE QVariantMap realtimeStftTimeRangeAtTime(double centerSeconds) const;

signals:
    /** @brief DFT 结果就绪信号 */
    void dftResultReady();
    /** @brief 导入忙碌状态变化信号 */
    void importBusyChanged();
    /** @brief 导入 DFT 处理完成信号 */
    void importedDftProcessingFinished();
    /** @brief 实时处理启动信号 */
    void processingStart();
    /** @brief 实时处理停止信号 */
    void processingStop();

private:
    /** @brief 设置导入忙碌状态 @param busy 是否忙碌 */
    void setImportBusy(bool busy);
    /** @brief 更新 DFT 数据缓存 @param dftData 频谱数据 @param centerTimeSeconds 中心时间 (秒) */
    void updateDFTData(QVariantList dftData, double centerTimeSeconds);
    /** @brief 清空实时 STFT 缓存 */
    void clearRealtimeStftCache();

    QVariantList m_dftData;                             /**< DFT 频谱数据缓存 */
    QVector<double> m_realtimeStftCenterTimes;          /**< 实时 STFT 帧中心时间列表 */
    QVector<QVariantList> m_realtimeStftFrames;         /**< 实时 STFT 帧数据列表 */
    static const int MAX_REALTIME_STFT_FRAMES = 300;    /**< 实时 STFT 帧最大缓存数 */
    QThread* m_thread = nullptr;                        /**< 实时处理线程 */
    DFTWorker* m_worker = nullptr;                      /**< 实时 DFT 工作对象 */
    QThread* m_importThread = nullptr;                  /**< 导入处理线程 */
    bool m_importBusy = false;                          /**< 导入处理忙碌标志 */
};

#endif // SIGNALDFTCALCULATION_H
