/** @file SignalPreprocessing.h
 *  @brief 信号预处理模块，提供带通滤波、陷波、FIR 滤波、主动降噪、自适应降噪、小波去噪等多级预处理流水线。
 */

#ifndef SIGNALPREPROCESSING_H
#define SIGNALPREPROCESSING_H

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QVector>
#include <array>
#include <memory>

#include "ActiveNoiseCancellation.h"
#include "AdaptiveNoiseReduction.h"
#include "MotionArtifactReduction.h"
#include "WaveletTransform.h"

#include <kfr/dsp.hpp>

class QThread;
class QTimer;

enum NotchFrequencyMode
{
    NotchFrequencyFixed = 0,
    NotchFrequencyAdaptive = 1
};

/** @brief 信号预处理选项，集中管理各类预处理开关与增益配置。 */
struct SignalPreprocessOptions
{
    bool bandpassEnabled = true;                  /**< 带通滤波是否启用 */
    bool notchEnabled = true;                     /**< 陷波滤波是否启用 */
    int notchFrequencyMode = NotchFrequencyFixed; /**< 陷波中心频率模式 */
    bool firFilterEnabled = false;                /**< FIR 滤波是否启用 */
    bool activeNoiseCancellationEnabled = true;   /**< 主动降噪 (ANC) 是否启用 */
    bool adaptiveNoiseReductionEnabled = true;    /**< 自适应降噪 (ANR) 是否启用 */
    AdaptiveNoiseReduction::Parameters adaptiveNoiseReductionParameters;
    bool waveletEnabled = true;                   /**< 小波去噪是否启用 */
    bool transientNoiseSuppressionEnabled = true; /**< 瞬态噪声抑制是否启用 */
    bool motionArtifactReductionEnabled = true;   /**< 运动伪影削减是否启用 */
    double gain = 1.0;                            /**< 增益系数 */
};

/** @brief 预滤波流式状态，分别保存带通、陷波和自适应频率跟踪。 */
struct SignalPreFilterState
{
    kfr::iir_state<double, 5> bandpassState{ kfr::iir_params<double, 5>() };
    std::unique_ptr<kfr::fir_state<double, double>> bandpassFirState;
    int bandpassFirOrder = 0;
    kfr::iir_state<double, 5> notchState{ kfr::iir_params<double, 5>() };
    QVector<float> adaptiveNotchHistory;
    std::array<double, 3> adaptiveNotchFrequencies{};
    std::array<int, 3> adaptiveNotchMissedFrames{};
};

/** @brief 预处理工作线程，负责实时信号的逐帧采集与多级降噪处理。 */
class PreprocessingWorker : public QObject
{
    Q_OBJECT

public:
    explicit PreprocessingWorker(QObject* parent = nullptr);
    ~PreprocessingWorker() override;

public slots:
    /** @brief 更新当前采集通道 @param channel 通道号 */
    void updateChannel(int channel);
    /** @brief 更新处理会话 ID @param session 会话号 */
    void updateProcessingSession(int session);
    /** @brief 定时处理回调，执行一帧预处理 */
    void processing();
    /** @brief 刷新处理管线中残留的数据 */
    void flushProcessing();

signals:
    /** @brief 预处理结果就绪信号 @param preprocessedData 预处理后数据 @param processingSession 处理会话 ID */
    void resultReady(const QVector<float>& preprocessedData, int processingSession);

private:
    /** @brief 重置实时 ANC 状态 */
    void resetRealtimeAncStates();
    /** @brief 重置实时降噪状态 */
    void resetRealtimeDenoiseStates();
    /** @brief 初始化预滤波器 @param sampleRate 采样率 @param options 预处理选项 */
    void initPreFilter(int sampleRate, const SignalPreprocessOptions& options);
    bool m_preFilterBandpassFirFilterEnabled = false;

    QMutex m_dataMutex;                                                                 /**< 数据互斥锁 */
    int m_currentChannel = 0;                                                           /**< 当前通道号 */
    int m_processingSession = 0;                                                        /**< 处理会话 ID */
    int m_preFilterSampleRate = 0;                                                      /**< 预滤波器采样率 */
    bool m_preFilterBandpassEnabled = true;                                             /**< 预滤波带通开关 */
    bool m_preFilterNotchEnabled = true;                                                /**< 预滤波陷波开关 */
    int m_preFilterNotchFrequencyMode = NotchFrequencyFixed;                            /**< 预滤波陷波频率模式 */
    bool m_hasBandpassFilterState = false;                                              /**< 是否已有带通滤波状态 */
    bool m_hasNotchFilterState = false;                                                 /**< 是否已有陷波滤波状态 */
    std::array<SignalPreFilterState, 8> m_preFilterStates{};                            /**< 8通道预滤波器状态 */
    std::array<AdaptiveNoiseReduction::StreamingState, 8> m_realtimeAdaptiveStates{};   /**< 8通道自适应降噪流式状态 */
    std::array<ActiveNoiseCancellation::StreamingState, 8> m_realtimeAncStates{};       /**< 8通道 ANC 流式状态 */
    int m_realtimeDenoiseSampleRate = 0;                                                /**< 实时降噪采样率 */
    bool m_realtimeAncEnabled = true;                                                   /**< 实时 ANC 启用标志 */
    bool m_realtimeAdaptiveEnabled = true;                                              /**< 实时自适应降噪启用标志 */
    AdaptiveNoiseReduction::Parameters m_realtimeAdaptiveParameters;
};

/** @brief 信号预处理管理器 (单例)，管理导入/实时两套预处理流水线的配置与线程调度。 */
class SignalPreprocessing : public QObject
{
    Q_OBJECT

    /** @brief 当前通道号 */
    Q_PROPERTY(
        int currentChannel
        READ currentChannel
        WRITE setCurrentChannel
        NOTIFY channelChanged)
    /** @brief 导入模式全处理是否全部启用 */
    Q_PROPERTY(
        bool importAllProcessingEnabled
        READ importAllProcessingEnabled
        NOTIFY importProcessingSettingsChanged)
    /** @brief 导入模式带通滤波是否启用 */
    Q_PROPERTY(
        bool importBandpassEnabled
        READ importBandpassEnabled
        WRITE setImportBandpassEnabled
        NOTIFY importBandpassEnabledChanged)
    /** @brief 导入模式陷波滤波是否启用 */
    Q_PROPERTY(
        bool importNotchEnabled
        READ importNotchEnabled
        WRITE setImportNotchEnabled
        NOTIFY importNotchEnabledChanged)
    /** @brief 导入模式陷波中心频率模式 */
    Q_PROPERTY(
        int importNotchFrequencyMode
        READ importNotchFrequencyMode
        WRITE setImportNotchFrequencyMode
        NOTIFY importNotchFrequencyModeChanged)
    /** @brief 导入模式自适应降噪是否启用 */
    Q_PROPERTY(
        bool importAdaptiveNoiseReductionEnabled
        READ importAdaptiveNoiseReductionEnabled
        WRITE setImportAdaptiveNoiseReductionEnabled
        NOTIFY importAdaptiveNoiseReductionEnabledChanged)
    Q_PROPERTY(
        int importAdaptiveNoiseReductionLevel
        READ importAdaptiveNoiseReductionLevel
        WRITE setImportAdaptiveNoiseReductionLevel
        NOTIFY importAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool importAdaptiveNoiseReductionHighPassFilterEnabled
        READ importAdaptiveNoiseReductionHighPassFilterEnabled
        WRITE setImportAdaptiveNoiseReductionHighPassFilterEnabled
        NOTIFY importAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool importAdaptiveNoiseReductionAutomaticGainControlEnabled
        READ importAdaptiveNoiseReductionAutomaticGainControlEnabled
        WRITE setImportAdaptiveNoiseReductionAutomaticGainControlEnabled
        NOTIFY importAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool importAdaptiveNoiseReductionTransientSuppressionEnabled
        READ importAdaptiveNoiseReductionTransientSuppressionEnabled
        WRITE setImportAdaptiveNoiseReductionTransientSuppressionEnabled
        NOTIFY importAdaptiveNoiseReductionParametersChanged)
    /** @brief 导入模式小波去噪是否启用 */
    Q_PROPERTY(
        bool importWaveletDenoisingEnabled
        READ importWaveletDenoisingEnabled
        WRITE setImportWaveletDenoisingEnabled
        NOTIFY importWaveletDenoisingEnabledChanged)
    /** @brief 导入模式瞬态噪声抑制是否启用 */
    Q_PROPERTY(
        bool importTransientNoiseSuppressionEnabled
        READ importTransientNoiseSuppressionEnabled
        WRITE setImportTransientNoiseSuppressionEnabled
        NOTIFY importTransientNoiseSuppressionEnabledChanged)
    /** @brief 导入模式运动伪影削减是否启用 */
    Q_PROPERTY(
        bool importMotionArtifactReductionEnabled
        READ importMotionArtifactReductionEnabled
        WRITE setImportMotionArtifactReductionEnabled
        NOTIFY importMotionArtifactReductionEnabledChanged)
    /** @brief 导入模式 FIR 滤波是否启用 */
    Q_PROPERTY(
        bool importFirFilterEnabled
        READ importFirFilterEnabled
        WRITE setImportFirFilterEnabled
        NOTIFY importFirFilterEnabledChanged)
    /** @brief 实时模式全处理是否全部启用 */
    Q_PROPERTY(
        bool realtimeAllProcessingEnabled
        READ realtimeAllProcessingEnabled
        NOTIFY realtimeProcessingSettingsChanged)
    /** @brief 实时模式带通滤波是否启用 */
    Q_PROPERTY(
        bool realtimeBandpassEnabled
        READ realtimeBandpassEnabled
        WRITE setRealtimeBandpassEnabled
        NOTIFY realtimeBandpassEnabledChanged)
    /** @brief 实时模式陷波滤波是否启用 */
    Q_PROPERTY(
        bool realtimeNotchEnabled
        READ realtimeNotchEnabled
        WRITE setRealtimeNotchEnabled
        NOTIFY realtimeNotchEnabledChanged)
    /** @brief 实时模式陷波中心频率模式 */
    Q_PROPERTY(
        int realtimeNotchFrequencyMode
        READ realtimeNotchFrequencyMode
        WRITE setRealtimeNotchFrequencyMode
        NOTIFY realtimeNotchFrequencyModeChanged)
    /** @brief 实时模式主动降噪是否启用 */
    Q_PROPERTY(
        bool realtimeActiveNoiseCancellationEnabled
        READ realtimeActiveNoiseCancellationEnabled
        WRITE setRealtimeActiveNoiseCancellationEnabled
        NOTIFY realtimeActiveNoiseCancellationEnabledChanged)
    /** @brief 实时模式自适应降噪是否启用 */
    Q_PROPERTY(
        bool realtimeAdaptiveNoiseReductionEnabled
        READ realtimeAdaptiveNoiseReductionEnabled
        WRITE setRealtimeAdaptiveNoiseReductionEnabled
        NOTIFY realtimeAdaptiveNoiseReductionEnabledChanged)
    Q_PROPERTY(
        int realtimeAdaptiveNoiseReductionLevel
        READ realtimeAdaptiveNoiseReductionLevel
        WRITE setRealtimeAdaptiveNoiseReductionLevel
        NOTIFY realtimeAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool realtimeAdaptiveNoiseReductionHighPassFilterEnabled
        READ realtimeAdaptiveNoiseReductionHighPassFilterEnabled
        WRITE setRealtimeAdaptiveNoiseReductionHighPassFilterEnabled
        NOTIFY realtimeAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled
        READ realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled
        WRITE setRealtimeAdaptiveNoiseReductionAutomaticGainControlEnabled
        NOTIFY realtimeAdaptiveNoiseReductionParametersChanged)
    Q_PROPERTY(
        bool realtimeAdaptiveNoiseReductionTransientSuppressionEnabled
        READ realtimeAdaptiveNoiseReductionTransientSuppressionEnabled
        WRITE setRealtimeAdaptiveNoiseReductionTransientSuppressionEnabled
        NOTIFY realtimeAdaptiveNoiseReductionParametersChanged)
    /** @brief 实时模式小波去噪是否启用 */
    Q_PROPERTY(
        bool realtimeWaveletDenoisingEnabled
        READ realtimeWaveletDenoisingEnabled
        WRITE setRealtimeWaveletDenoisingEnabled
        NOTIFY realtimeWaveletDenoisingEnabledChanged)
    /** @brief 实时模式瞬态噪声抑制是否启用 */
    Q_PROPERTY(
        bool realtimeTransientNoiseSuppressionEnabled
        READ realtimeTransientNoiseSuppressionEnabled
        WRITE setRealtimeTransientNoiseSuppressionEnabled
        NOTIFY realtimeTransientNoiseSuppressionEnabledChanged)
    /** @brief 实时模式运动伪影削减是否启用 */
    Q_PROPERTY(
        bool realtimeMotionArtifactReductionEnabled
        READ realtimeMotionArtifactReductionEnabled
        WRITE setRealtimeMotionArtifactReductionEnabled
        NOTIFY realtimeMotionArtifactReductionEnabledChanged)
    /** @brief 实时模式 FIR 滤波是否启用 */
    Q_PROPERTY(
        bool realtimeFirFilterEnabled
        READ realtimeFirFilterEnabled
        WRITE setRealtimeFirFilterEnabled
        NOTIFY realtimeFirFilterEnabledChanged)
    /** @brief 实时模式增益系数 */
    Q_PROPERTY(
        double realtimeGain
        READ realtimeGain
        WRITE setRealtimeGain
        NOTIFY realtimeGainChanged)

public:
    friend class PreprocessingWorker;

    static SignalPreprocessing* instance()
    {
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new SignalPreprocessing(qApp);
            }
        }
        return m_instance;
    }

    /**
     * @brief 对导入数据进行完整预处理流水线。
     * @param samplingRate 采样率 (Hz)
     * @param rawData      原始信号数据
     * @returns 预处理后的信号数据
     */
    QVector<float> filterDataImport(int samplingRate, const QVector<float>& rawData);

    int currentChannel() const { return m_currentChannel; }
    /** @brief 导入模式全处理是否全部启用 @returns 启用状态 */
    bool importAllProcessingEnabled() const;
    bool importBandpassEnabled() const;
    bool importNotchEnabled() const;
    int importNotchFrequencyMode() const;
    bool importAdaptiveNoiseReductionEnabled() const;
    int importAdaptiveNoiseReductionLevel() const;
    bool importAdaptiveNoiseReductionHighPassFilterEnabled() const;
    bool importAdaptiveNoiseReductionAutomaticGainControlEnabled() const;
    bool importAdaptiveNoiseReductionTransientSuppressionEnabled() const;
    bool importWaveletDenoisingEnabled() const;
    bool importTransientNoiseSuppressionEnabled() const;
    bool importMotionArtifactReductionEnabled() const;
    bool importFirFilterEnabled() const;
    /** @brief 实时模式全处理是否全部启用 @returns 启用状态 */
    bool realtimeAllProcessingEnabled() const;
    bool realtimeBandpassEnabled() const;
    bool realtimeNotchEnabled() const;
    int realtimeNotchFrequencyMode() const;
    bool realtimeActiveNoiseCancellationEnabled() const;
    bool realtimeAdaptiveNoiseReductionEnabled() const;
    int realtimeAdaptiveNoiseReductionLevel() const;
    bool realtimeAdaptiveNoiseReductionHighPassFilterEnabled() const;
    bool realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled() const;
    bool realtimeAdaptiveNoiseReductionTransientSuppressionEnabled() const;
    bool realtimeWaveletDenoisingEnabled() const;
    bool realtimeTransientNoiseSuppressionEnabled() const;
    bool realtimeMotionArtifactReductionEnabled() const;
    bool realtimeFirFilterEnabled() const;
    double realtimeGain() const;

    /** @brief 设置当前通道 @param channel 通道号 */
    Q_INVOKABLE void setCurrentChannel(int channel);
    /** @brief 一键设置导入模式全部处理开关 @param enabled 是否启用 */
    Q_INVOKABLE void setAllImportProcessingEnabled(bool enabled);
    /** @brief 一键设置实时模式全部处理开关 @param enabled 是否启用 */
    Q_INVOKABLE void setAllRealtimeProcessingEnabled(bool enabled);
    /** @brief 设置导入模式带通滤波 @param enabled 是否启用 */
    void setImportBandpassEnabled(bool enabled);
    /** @brief 设置导入模式陷波滤波 @param enabled 是否启用 */
    void setImportNotchEnabled(bool enabled);
    /** @brief 设置导入模式陷波中心频率模式 @param mode 频率模式 */
    void setImportNotchFrequencyMode(int mode);
    /** @brief 设置导入模式自适应降噪 @param enabled 是否启用 */
    void setImportAdaptiveNoiseReductionEnabled(bool enabled);
    void setImportAdaptiveNoiseReductionLevel(int level);
    void setImportAdaptiveNoiseReductionHighPassFilterEnabled(bool enabled);
    void setImportAdaptiveNoiseReductionAutomaticGainControlEnabled(bool enabled);
    void setImportAdaptiveNoiseReductionTransientSuppressionEnabled(bool enabled);
    /** @brief 设置导入模式小波去噪 @param enabled 是否启用 */
    void setImportWaveletDenoisingEnabled(bool enabled);
    /** @brief 设置导入模式瞬态噪声抑制 @param enabled 是否启用 */
    void setImportTransientNoiseSuppressionEnabled(bool enabled);
    /** @brief 设置导入模式运动伪影削减 @param enabled 是否启用 */
    void setImportMotionArtifactReductionEnabled(bool enabled);
    /** @brief 设置导入模式 FIR 滤波 @param enabled 是否启用 */
    void setImportFirFilterEnabled(bool enabled);
    /** @brief 设置实时模式带通滤波 @param enabled 是否启用 */
    void setRealtimeBandpassEnabled(bool enabled);
    /** @brief 设置实时模式陷波滤波 @param enabled 是否启用 */
    void setRealtimeNotchEnabled(bool enabled);
    /** @brief 设置实时模式陷波中心频率模式 @param mode 频率模式 */
    void setRealtimeNotchFrequencyMode(int mode);
    /** @brief 设置实时模式主动降噪 @param enabled 是否启用 */
    void setRealtimeActiveNoiseCancellationEnabled(bool enabled);
    /** @brief 设置实时模式自适应降噪 @param enabled 是否启用 */
    void setRealtimeAdaptiveNoiseReductionEnabled(bool enabled);
    void setRealtimeAdaptiveNoiseReductionLevel(int level);
    void setRealtimeAdaptiveNoiseReductionHighPassFilterEnabled(bool enabled);
    void setRealtimeAdaptiveNoiseReductionAutomaticGainControlEnabled(bool enabled);
    void setRealtimeAdaptiveNoiseReductionTransientSuppressionEnabled(bool enabled);
    /** @brief 设置实时模式小波去噪 @param enabled 是否启用 */
    void setRealtimeWaveletDenoisingEnabled(bool enabled);
    /** @brief 设置实时模式瞬态噪声抑制 @param enabled 是否启用 */
    void setRealtimeTransientNoiseSuppressionEnabled(bool enabled);
    /** @brief 设置实时模式运动伪影削减 @param enabled 是否启用 */
    void setRealtimeMotionArtifactReductionEnabled(bool enabled);
    /** @brief 设置实时模式 FIR 滤波 @param enabled 是否启用 */
    void setRealtimeFirFilterEnabled(bool enabled);
    /** @brief 设置实时模式增益系数 @param gain 增益值 */
    void setRealtimeGain(double gain);

    /** @brief 启动实时预处理流水线 */
    Q_INVOKABLE void startPreprocessing();
    /** @brief 停止实时预处理流水线 */
    Q_INVOKABLE void stopPreprocessing();

    /** @brief 更新预处理数据缓存 @param preprocessedData 预处理后数据 @param processingSession 会话 ID */
    void updateDataCache(const QVector<float>& preprocessedData, int processingSession);
    /** @brief 获取预处理数据缓存 @returns 缓存的数据 */
    QVector<float> getDataCache() const;

private:
    explicit SignalPreprocessing(QObject* parent = nullptr);
    ~SignalPreprocessing() override;

    /** @brief 获取导入模式的预处理选项 @returns 导入预处理选项结构 */
    SignalPreprocessOptions importPreprocessOptions() const;
    /** @brief 获取实时模式的预处理选项 @returns 实时预处理选项结构 */
    SignalPreprocessOptions realtimePreprocessOptions() const;

    static SignalPreprocessing* m_instance;                 /**< 单例指针 */
    static constexpr int COLLECTION_INTERVAL_MS = 100;      /**< 实时采集间隔 (ms) */

    QThread* m_thread = nullptr;                            /**< 预处理工作线程 */
    PreprocessingWorker* m_worker = nullptr;                /**< 工作对象 */
    QTimer* m_timer = nullptr;                              /**< 定时器 */
    int m_currentChannel = 0;                               /**< 当前通道号 */
    int m_processingSession = 0;                            /**< 处理会话 ID */
    bool m_processingActive = false;                        /**< 处理是否活跃 */
    QVector<float> m_preprocessedDataCache;                  /**< 预处理数据缓存 */
    mutable QMutex m_dataCacheMutex;                         /**< 数据缓存互斥锁 */
    mutable QMutex m_importSettingsMutex;                    /**< 导入设置互斥锁 */
    mutable QMutex m_realtimeSettingsMutex;                  /**< 实时设置互斥锁 */
    bool m_importBandpassEnabled = true;                     /**< 导入: 带通滤波开关 */
    bool m_importNotchEnabled = true;                        /**< 导入: 陷波滤波开关 */
    int m_importNotchFrequencyMode = NotchFrequencyFixed;    /**< 导入: 陷波中心频率模式 */
    bool m_importAdaptiveNoiseReductionEnabled = true;       /**< 导入: 自适应降噪开关 */
    AdaptiveNoiseReduction::Parameters m_importAdaptiveNoiseReductionParameters;
    bool m_importWaveletDenoisingEnabled = true;             /**< 导入: 小波去噪开关 */
    bool m_importTransientNoiseSuppressionEnabled = true;    /**< 导入: 瞬态噪声抑制开关 */
    bool m_importMotionArtifactReductionEnabled = true;      /**< 导入: 运动伪影削减开关 */
    bool m_importFirFilterEnabled = false;                   /**< 导入: FIR 滤波开关 */
    bool m_realtimeBandpassEnabled = true;                   /**< 实时: 带通滤波开关 */
    bool m_realtimeNotchEnabled = true;                      /**< 实时: 陷波滤波开关 */
    int m_realtimeNotchFrequencyMode = NotchFrequencyFixed;  /**< 实时: 陷波中心频率模式 */
    bool m_realtimeActiveNoiseCancellationEnabled = true;    /**< 实时: 主动降噪开关 */
    bool m_realtimeAdaptiveNoiseReductionEnabled = true;     /**< 实时: 自适应降噪开关 */
    AdaptiveNoiseReduction::Parameters m_realtimeAdaptiveNoiseReductionParameters;
    bool m_realtimeWaveletDenoisingEnabled = true;           /**< 实时: 小波去噪开关 */
    bool m_realtimeTransientNoiseSuppressionEnabled = true;  /**< 实时: 瞬态噪声抑制开关 */
    bool m_realtimeMotionArtifactReductionEnabled = true;    /**< 实时: 运动伪影削减开关 */
    bool m_realtimeFirFilterEnabled = false;                 /**< 实时: FIR 滤波开关 */
    double m_realtimeGain = 1.0;                             /**< 实时: 增益系数 */

signals:
    /** @brief 通道变化信号 @param channel 新通道号 */
    void channelChanged(int channel);
    /** @brief 导入处理设置变化信号 */
    void importProcessingSettingsChanged();
    /** @brief 导入带通滤波开关变化信号 */
    void importBandpassEnabledChanged();
    /** @brief 导入陷波滤波开关变化信号 */
    void importNotchEnabledChanged();
    /** @brief 导入陷波中心频率模式变化信号 */
    void importNotchFrequencyModeChanged();
    /** @brief 导入自适应降噪开关变化信号 */
    void importAdaptiveNoiseReductionEnabledChanged();
    void importAdaptiveNoiseReductionParametersChanged();
    /** @brief 导入小波去噪开关变化信号 */
    void importWaveletDenoisingEnabledChanged();
    /** @brief 导入瞬态噪声抑制开关变化信号 */
    void importTransientNoiseSuppressionEnabledChanged();
    /** @brief 导入运动伪影削减开关变化信号 */
    void importMotionArtifactReductionEnabledChanged();
    /** @brief 导入 FIR 滤波开关变化信号 */
    void importFirFilterEnabledChanged();
    /** @brief 实时处理设置变化信号 */
    void realtimeProcessingSettingsChanged();
    /** @brief 实时带通滤波开关变化信号 */
    void realtimeBandpassEnabledChanged();
    /** @brief 实时陷波滤波开关变化信号 */
    void realtimeNotchEnabledChanged();
    /** @brief 实时陷波中心频率模式变化信号 */
    void realtimeNotchFrequencyModeChanged();
    /** @brief 实时主动降噪开关变化信号 */
    void realtimeActiveNoiseCancellationEnabledChanged();
    /** @brief 实时自适应降噪开关变化信号 */
    void realtimeAdaptiveNoiseReductionEnabledChanged();
    void realtimeAdaptiveNoiseReductionParametersChanged();
    /** @brief 实时小波去噪开关变化信号 */
    void realtimeWaveletDenoisingEnabledChanged();
    /** @brief 实时瞬态噪声抑制开关变化信号 */
    void realtimeTransientNoiseSuppressionEnabledChanged();
    /** @brief 实时运动伪影削减开关变化信号 */
    void realtimeMotionArtifactReductionEnabledChanged();
    /** @brief 实时 FIR 滤波开关变化信号 */
    void realtimeFirFilterEnabledChanged();
    /** @brief 实时增益变化信号 */
    void realtimeGainChanged();
};

#endif // SIGNALPREPROCESSING_H
